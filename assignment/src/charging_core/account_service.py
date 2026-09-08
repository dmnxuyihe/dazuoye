from __future__ import annotations

import hmac
from datetime import UTC, datetime, timedelta
from decimal import ROUND_HALF_UP, Decimal
from typing import Any
from uuid import UUID, uuid4

import asyncpg

from .config import Settings
from .errors import AuthenticationError, ConflictError, ForbiddenError, NotFoundError
from .events import emit_event
from .security import issue_token, new_otp_code, otp_digest, verify_password

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
            elif not verify_password(password, admin["password_hash"]):
                attempts = min(admin["failed_attempts"] + 1, 5)
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
                raise AuthenticationError("账号暂时锁定，请稍后重试")
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
            SELECT id, phone, nickname, avatar_path, balance, status, created_at
            FROM app_user WHERE id = $1
            """,
            user_id,
        )
        if row is None:
            raise NotFoundError("用户不存在")
        return dict(row)

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
        return [dict(row) for row in rows]

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
