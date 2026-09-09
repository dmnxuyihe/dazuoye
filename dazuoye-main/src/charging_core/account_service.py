from __future__ import annotations

import hmac
import base64
import binascii
import struct
from datetime import UTC, datetime, timedelta
from decimal import ROUND_HALF_UP, Decimal
from typing import Any
from uuid import UUID, uuid4

import asyncpg

from .config import Settings
from .errors import AuthenticationError, ConflictError, ForbiddenError, NotFoundError
from .events import emit_event
from .security import issue_token, new_otp_code, otp_digest, verify_password
from .queries import order_snapshot

CENT = Decimal("0.01")


class AccountService:
    def __init__(self, pool: asyncpg.Pool, settings: Settings) -> None:
        self.pool = pool
        self.settings = settings

    async def request_otp(self, phone: str) -> str | None:
        code = (
            self.settings.dev_otp_code
            if self.settings.is_development
            else new_otp_code()
        )
        digest = otp_digest(phone, code, self.settings.jwt_secret)
        async with self.pool.acquire() as connection, connection.transaction():
            previous = await connection.fetchrow(
                "SELECT created_at FROM otp_challenge WHERE phone = $1 FOR UPDATE",
                phone,
            )
            if previous and previous["created_at"] > datetime.now(UTC) - timedelta(
                seconds=60
            ):
                raise ConflictError("验证码请求过于频繁，请一分钟后重试")
            await connection.execute(
                """
                INSERT INTO otp_challenge (phone, code_digest, expires_at, attempts, created_at)
                VALUES ($1, $2, now() + interval '5 minutes', 0, now())
                ON CONFLICT (phone) DO UPDATE
                SET code_digest = EXCLUDED.code_digest,
                    expires_at = EXCLUDED.expires_at,
                    attempts = 0,
                    created_at = now()
                """,
                phone,
                digest,
            )
        return code if self.settings.is_development else None

    async def verify_otp(self, phone: str, code: str) -> dict[str, Any]:
        failure: AuthenticationError | ForbiddenError | None = None
        user: asyncpg.Record | None = None
        async with self.pool.acquire() as connection, connection.transaction():
            challenge = await connection.fetchrow(
                "SELECT * FROM otp_challenge WHERE phone = $1 FOR UPDATE", phone
            )
            if challenge is None or challenge["expires_at"] <= datetime.now(UTC):
                if challenge is not None:
                    await connection.execute(
                        "DELETE FROM otp_challenge WHERE phone = $1", phone
                    )
                failure = AuthenticationError("验证码不存在或已过期")
            elif challenge["attempts"] >= 5:
                failure = AuthenticationError("验证码尝试次数已用尽")
            elif not hmac.compare_digest(
                otp_digest(phone, code, self.settings.jwt_secret),
                challenge["code_digest"],
            ):
                await connection.execute(
                    "UPDATE otp_challenge SET attempts = attempts + 1 WHERE phone = $1",
                    phone,
                )
                failure = AuthenticationError("验证码错误")
            else:
                user = await connection.fetchrow(
                    "SELECT * FROM app_user WHERE phone = $1", phone
                )
                if user is None:
                    user = await connection.fetchrow(
                        """
                        INSERT INTO app_user (id, phone, nickname)
                        VALUES ($1, $2, $3)
                        RETURNING *
                        """,
                        uuid4(),
                        phone,
                        f"用户{phone[-4:]}",
                    )
                if user["status"] != "active":
                    failure = ForbiddenError("账号已被冻结")
                else:
                    await connection.execute(
                        "DELETE FROM otp_challenge WHERE phone = $1", phone
                    )
        if failure is not None:
            raise failure
        assert user is not None
        return {
            "access_token": issue_token(user["id"], "user", self.settings),
            "token_type": "bearer",
            "user": dict(user),
        }

    async def admin_login(self, username: str, password: str) -> dict[str, str]:
        failed = False
        locked = False
        remaining = 0
        admin: asyncpg.Record | None = None
        async with self.pool.acquire() as connection, connection.transaction():
            admin = await connection.fetchrow(
                "SELECT * FROM admin_account WHERE username = $1 FOR UPDATE", username
            )
            now = datetime.now(UTC)
            if admin is None or not admin["active"]:
                failed = True
            elif admin["locked_until"] is not None and admin["locked_until"] > now:
                failed = True
                locked = True
                remaining = max(1, int((admin["locked_until"] - now).total_seconds()) + 1)
            elif not verify_password(password, admin["password_hash"]):
                attempts = 1 if admin["locked_until"] is not None else min(admin["failed_attempts"] + 1, 5)
                await connection.execute(
                    """
                    UPDATE admin_account
                    SET failed_attempts = $2::smallint,
                        locked_until = CASE WHEN $2::smallint >= 5 THEN now() + interval '30 seconds'
                                            ELSE NULL END
                    WHERE id = $1
                    """,
                    admin["id"],
                    attempts,
                )
                failed = True
                locked = attempts >= 5
                remaining = 30 if locked else 0
            else:
                await connection.execute(
                    """
                    UPDATE admin_account SET failed_attempts = 0, locked_until = NULL
                    WHERE id = $1
                    """,
                    admin["id"],
                )
        if failed:
            if locked:
                raise AuthenticationError(f"连续输错5次，账号暂时锁定，请{remaining}秒后重试")
            raise AuthenticationError("账号或密码错误")
        assert admin is not None
        return {
            "access_token": issue_token(admin["id"], "admin", self.settings),
            "token_type": "bearer",
        }

    async def recharge(
        self, user_id: UUID, amount: Decimal, key: UUID
    ) -> dict[str, Any]:
        async with self.pool.acquire() as connection, connection.transaction():
            existing = await connection.fetchrow(
                """
                SELECT id, amount, balance_after FROM wallet_entry
                WHERE user_id = $1 AND idempotency_key = $2
                """,
                user_id,
                key,
            )
            if existing is not None:
                return dict(existing)
            user = await connection.fetchrow(
                "SELECT * FROM app_user WHERE id = $1 FOR UPDATE", user_id
            )
            if user is None:
                raise NotFoundError("用户不存在")
            if user["status"] != "active":
                raise ForbiddenError("账号已被冻结")
            existing = await connection.fetchrow("SELECT id,amount,balance_after FROM wallet_entry WHERE user_id=$1 AND idempotency_key=$2",user_id,key)
            if existing is not None: return dict(existing)
            balance = (user["balance"] + amount).quantize(CENT, rounding=ROUND_HALF_UP)
            await connection.execute(
                "UPDATE app_user SET balance = $2 WHERE id = $1", user_id, balance
            )
            entry = await connection.fetchrow(
                """
                INSERT INTO wallet_entry
                    (id, user_id, idempotency_key, entry_type, amount, balance_after)
                VALUES ($1, $2, $3, 'recharge', $4, $5)
                RETURNING id, amount, balance_after
                """,
                uuid4(),
                user_id,
                key,
                amount,
                balance,
            )
            await emit_event(
                connection,
                "wallet.recharged",
                {"balance": balance},
                audience_user_id=user_id,
                audience_role="admin",
            )
            return dict(entry)

    async def profile(self, user_id: UUID) -> dict[str, Any]:
        row = await self.pool.fetchrow(
            """
            SELECT *, balance-held_balance AS available_balance FROM app_user WHERE id = $1
            """,
            user_id,
        )
        if row is None:
            raise NotFoundError("用户不存在")
        return dict(row)

    async def withdraw(self, user_id: UUID, amount: Decimal, key: UUID, destination: str = "演示钱包") -> dict[str, Any]:
        async with self.pool.acquire() as c, c.transaction():
            user = await c.fetchrow("SELECT * FROM app_user WHERE id=$1 FOR UPDATE",user_id)
            if user is None or user["status"] != "active":
                raise ForbiddenError("用户不存在或已被冻结")
            old = await c.fetchrow("SELECT * FROM withdrawal_request WHERE user_id=$1 AND request_key=$2",user_id,key)
            if old:
                if old["amount"] != amount or old["destination"] != destination:
                    raise ConflictError("请求编号已用于不同提现")
                return dict(old)
            if await c.fetchval("SELECT EXISTS(SELECT 1 FROM charging_order WHERE user_id=$1 AND status IN ('reserved','charging','pending_payment'))",user_id):
                raise ConflictError("请先结束充电并支付账单，再申请提现")
            if amount <= 0 or amount > user["balance"] - user["held_balance"]:
                raise ConflictError("可用余额不足，不能提现")
            await c.execute("UPDATE app_user SET held_balance=held_balance+$2 WHERE id=$1",user_id,amount)
            request = await c.fetchrow(
                "INSERT INTO withdrawal_request(id,user_id,request_key,amount,destination) VALUES($1,$2,$3,$4,$5) RETURNING *",
                uuid4(),user_id,key,amount,destination)
            await emit_event(c,"wallet.withdrawal_requested",{"request_id":request["id"],"amount":amount},audience_user_id=user_id,audience_role="admin")
            return dict(request)

    async def withdrawals(self, user_id: UUID):
        rows=await self.pool.fetch("SELECT id,amount,status,destination,requested_at,decided_at,review_note FROM withdrawal_request WHERE user_id=$1 ORDER BY requested_at DESC",user_id)
        return [dict(row) for row in rows]

    async def save_vehicle(self, user_id: UUID, data: dict):
        if not data['vehicle_name'].strip() or not data['vehicle_plate'].strip():
            raise ConflictError('车型和车牌不能为空')
        async with self.pool.acquire() as c, c.transaction():
            user=await c.fetchrow("SELECT status FROM app_user WHERE id=$1 FOR UPDATE",user_id)
            if user is None or user["status"] != "active":
                raise ForbiddenError("用户不存在或已被冻结")
            if await c.fetchval("SELECT EXISTS(SELECT 1 FROM charging_order WHERE user_id=$1 AND status IN ('reserved','charging','pending_payment'))",user_id):
                raise ConflictError("请结束订单并支付后再修改车辆资料")
            try:
                await c.execute("""UPDATE app_user SET vehicle_name=$2,vehicle_plate=$3,battery_kwh=$4,vehicle_soc=$5,charge_limit=$6 WHERE id=$1""",
                    user_id,data["vehicle_name"].strip(),data["vehicle_plate"].strip().upper(),data["battery_kwh"],data["vehicle_soc"],data["charge_limit"])
            except asyncpg.UniqueViolationError:
                raise ConflictError("该车牌已绑定其他账户")
            await emit_event(c,"user.vehicle_updated",{"user_id":user_id},audience_user_id=user_id)
        return await self.profile(user_id)

    async def save_avatar(self, user_id: UUID, encoded: str):
        try:
            blob=base64.b64decode(encoded,validate=True)
            if len(blob)>200000 or blob[:8] != b'\x89PNG\r\n\x1a\n' or blob[12:16] != b'IHDR':
                raise ValueError()
            width,height=struct.unpack(">II",blob[16:24])
            if not (0<width<=512 and 0<height<=512):
                raise ValueError()
        except (ValueError,binascii.Error,struct.error):
            raise ConflictError("头像须为512×512以内的PNG图片")
        async with self.pool.acquire() as c, c.transaction():
            row=await c.fetchrow("UPDATE app_user SET avatar_data=$2 WHERE id=$1 AND status='active' RETURNING id",user_id,encoded)
            if not row: raise ForbiddenError("用户不存在或已被冻结")
            await emit_event(c,"user.avatar_updated",{"user_id":user_id},audience_user_id=user_id)
        return await self.profile(user_id)

    async def statistics(self, user_id: UUID, period: str):
        async with self.pool.acquire() as c:
            rows=await c.fetch("""SELECT (coalesce(paid_at,ended_at) AT TIME ZONE 'Asia/Shanghai')::date AS day,
                sum(energy_kwh) AS energy_kwh,sum(amount) AS amount,count(*) AS orders
                FROM charging_order WHERE user_id=$1 AND status='completed'
                AND ($2='all' OR coalesce(paid_at,ended_at) >= date_trunc('month',now() AT TIME ZONE 'Asia/Shanghai') AT TIME ZONE 'Asia/Shanghai')
                GROUP BY day ORDER BY day""",user_id,period)
            stations=await c.fetch("""SELECT s.name AS station_name,sum(o.amount) AS amount FROM charging_order o
                JOIN charger c ON c.id=o.charger_id JOIN station s ON s.id=c.station_id
                WHERE o.user_id=$1 AND o.status='completed'
                AND ($2='all' OR coalesce(o.paid_at,o.ended_at) >= date_trunc('month',now() AT TIME ZONE 'Asia/Shanghai') AT TIME ZONE 'Asia/Shanghai')
                GROUP BY s.id ORDER BY amount DESC""",user_id,period)
        return {"daily":[dict(row) for row in rows],"stations":[dict(row) for row in stations],"period":period,"timezone":"Asia/Shanghai"}

    async def update_profile(
        self, user_id: UUID, nickname: str | None, avatar_path: str | None
    ) -> dict[str, Any]:
        async with self.pool.acquire() as connection, connection.transaction():
            row = await connection.fetchrow(
                """
                UPDATE app_user
                SET nickname = COALESCE($2, nickname),
                    avatar_path = COALESCE($3, avatar_path)
                WHERE id = $1 AND status = 'active'
                RETURNING id, phone, nickname, avatar_path, balance, status, created_at
                """,
                user_id,
                nickname,
                avatar_path,
            )
            if row is None:
                raise ForbiddenError("用户不存在或已被冻结")
            await emit_event(
                connection,
                "user.profile_updated",
                {"user_id": user_id},
                audience_user_id=user_id,
            )
            return dict(row)

    async def order_history(
        self, user_id: UUID, *, limit: int, offset: int
    ) -> list[dict[str, Any]]:
        rows = await self.pool.fetch(
            """
            SELECT o.id, o.status, o.reserved_at, o.started_at, o.ended_at,
                   o.energy_kwh, o.amount, o.stop_reason,
                   c.code AS charger_code, s.name AS station_name
            FROM charging_order o
            JOIN charger c ON c.id = o.charger_id
            JOIN station s ON s.id = c.station_id
            WHERE o.user_id = $1
            ORDER BY o.created_at DESC LIMIT $2 OFFSET $3
            """,
            user_id,
            limit,
            offset,
        )
        # Active and completed orders share the same authoritative snapshot contract.
        async with self.pool.acquire() as connection:
            return [await order_snapshot(connection, row["id"], user_id) for row in rows]

    async def wallet_history(
        self, user_id: UUID, *, limit: int, offset: int
    ) -> list[dict[str, Any]]:
        rows = await self.pool.fetch(
            """
            SELECT id, entry_type, amount, balance_after, order_id, created_at
            FROM wallet_entry WHERE user_id = $1
            ORDER BY created_at DESC LIMIT $2 OFFSET $3
            """,
            user_id,
            limit,
            offset,
        )
        return [dict(row) for row in rows]
