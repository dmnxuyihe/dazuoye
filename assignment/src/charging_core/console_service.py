"""Administrator commands; all command effects and audit records share a transaction."""

import json
from contextlib import asynccontextmanager
from decimal import Decimal
from uuid import uuid4
import asyncpg
from .admin_service import AdminService
from .errors import ConflictError, NotFoundError
from .events import emit_event
from .service import ChargingService


class TransactionPool:
    """Bind existing charging commands to an outer transaction (inner transactions are savepoints)."""

    def __init__(self, connection):
        self.connection = connection

    @asynccontextmanager
    async def acquire(self):
        yield self.connection


class ConsoleService(AdminService):
    def __init__(self, pool, settings):
        super().__init__(pool)
        self.settings = settings

    async def settings_read(self):
        value = await self.pool.fetchval(
            "SELECT value FROM console_settings WHERE id=1"
        )
        return (
            json.loads(value)
            if value
            else {
                "daily_goal": 72,
                "weekly_goal": 18,
                "charge_limit": 70,
                "vehicle": "Tesla M-3",
            }
        )

    async def settings_write(self, admin, value):
        async with self.pool.acquire() as c, c.transaction():
            await c.execute(
                "INSERT INTO console_settings(id,value) VALUES(1,$1::jsonb) ON CONFLICT(id) DO UPDATE SET value=excluded.value",
                json.dumps(value, ensure_ascii=False),
            )
            await self._log(c, admin, "console.settings", "settings", None, value)
        return value

    async def user_save(self, admin, user_id, data):
        async with self.pool.acquire() as c, c.transaction():
            try:
                if user_id:
                    row = await c.fetchrow(
                        "UPDATE app_user SET phone=$2,nickname=$3 WHERE id=$1 RETURNING *",
                        user_id,
                        data["phone"],
                        data["nickname"],
                    )
                else:
                    row = await c.fetchrow(
                        "INSERT INTO app_user(id,phone,nickname) VALUES($1,$2,$3) RETURNING *",
                        uuid4(),
                        data["phone"],
                        data["nickname"],
                    )
            except asyncpg.UniqueViolationError:
                raise ConflictError("手机号已存在")
            if not row:
                raise NotFoundError("用户不存在")
            await self._log(
                c,
                admin,
                "user.updated" if user_id else "user.created",
                "user",
                row["id"],
                data,
            )
            return dict(row)

    async def user_delete(self, admin, user_id):
        async with self.pool.acquire() as c, c.transaction():
            user = await c.fetchrow(
                "SELECT * FROM app_user WHERE id=$1 FOR UPDATE", user_id
            )
            if not user:
                raise NotFoundError("用户不存在")
            used = await c.fetchval(
                "SELECT EXISTS(SELECT 1 FROM charging_order WHERE user_id=$1) OR EXISTS(SELECT 1 FROM wallet_entry WHERE user_id=$1)",
                user_id,
            )
            if used or user["balance"] != 0:
                raise ConflictError("有资金或历史记录，请冻结账号并保留审计数据")
            await c.execute("DELETE FROM app_user WHERE id=$1", user_id)
            await self._log(c, admin, "user.deleted", "user", user_id)

    async def wallet(self, user_id):
        if not await self.pool.fetchval("SELECT 1 FROM app_user WHERE id=$1", user_id):
            raise NotFoundError("用户不存在")
        return [
            dict(r)
            for r in await self.pool.fetch(
                "SELECT * FROM wallet_entry WHERE user_id=$1 ORDER BY created_at DESC",
                user_id,
            )
        ]

    async def money(self, admin, user_id, amount, key, reason, order_id=None):
        async with self.pool.acquire() as c, c.transaction():
            user = await c.fetchrow(
                "SELECT * FROM app_user WHERE id=$1 FOR UPDATE", user_id
            )
            if not user:
                raise NotFoundError("用户不存在")
            kind = "refund" if order_id else "adjustment"
            old = await c.fetchrow(
                "SELECT * FROM wallet_entry WHERE user_id=$1 AND idempotency_key=$2",
                user_id,
                key,
            )
            if old:
                if (
                    old["entry_type"] != kind
                    or old["amount"] != amount
                    or old["order_id"] != order_id
                ):
                    raise ConflictError("幂等键已用于不同资金操作")
                return dict(old)
            if order_id:
                order = await c.fetchrow(
                    "SELECT * FROM charging_order WHERE id=$1 FOR UPDATE", order_id
                )
                if not order or order["user_id"] != user_id:
                    raise NotFoundError("订单不存在")
                if order["status"] != "completed":
                    raise ConflictError("仅已完成订单可以退款")
                total = await c.fetchval(
                    "SELECT coalesce(sum(amount),0) FROM wallet_entry WHERE order_id=$1 AND entry_type='refund'",
                    order_id,
                )
                if amount <= 0 or total + amount > order["amount"]:
                    raise ConflictError("累计退款不可超过订单结算金额")
            if amount < 0 and await c.fetchval(
                "SELECT EXISTS(SELECT 1 FROM charging_order WHERE user_id=$1 AND status IN ('reserved','charging'))",
                user_id,
            ):
                raise ConflictError("请先结束活动订单，再进行负向调整")
            balance = user["balance"] + amount
            if balance < 0:
                raise ConflictError("调整后余额不能为负")
            if balance > Decimal("9999999999.99"):
                raise ConflictError("余额超出上限")
            await c.execute(
                "UPDATE app_user SET balance=$2 WHERE id=$1", user_id, balance
            )
            row = await c.fetchrow(
                "INSERT INTO wallet_entry(id,user_id,idempotency_key,entry_type,amount,balance_after,order_id) VALUES($1,$2,$3,$4,$5,$6,$7) RETURNING *",
                uuid4(),
                user_id,
                key,
                kind,
                amount,
                balance,
                order_id,
            )
            await self._log(
                c,
                admin,
                "wallet." + kind,
                "user",
                user_id,
                {
                    "amount": str(amount),
                    "reason": reason,
                    "order_id": str(order_id) if order_id else None,
                    "entry_id": str(row["id"]),
                },
            )
            await emit_event(
                c,
                "wallet.changed",
                {"user_id": user_id, "balance": balance},
                audience_user_id=user_id,
                audience_role="admin",
            )
            return dict(row)

    async def charger_edit(self, admin, charger_id, data):
        async with self.pool.acquire() as c, c.transaction():
            row = await c.fetchrow(
                "SELECT * FROM charger WHERE id=$1 FOR UPDATE", charger_id
            )
            if not row:
                raise NotFoundError("电桩不存在")
            if row["status"] in ("reserved", "charging"):
                raise ConflictError("活动订单电桩不可修改参数")
            try:
                row = await c.fetchrow(
                    "UPDATE charger SET code=$2,kind=$3,power_kw=$4 WHERE id=$1 RETURNING *",
                    charger_id,
                    data["code"],
                    data["kind"],
                    data["power_kw"],
                )
            except asyncpg.UniqueViolationError:
                raise ConflictError("电桩编号已存在")
            await self._log(c, admin, "charger.updated", "charger", charger_id, data)
            return dict(row)

    async def order_user(self, order_id):
        value = await self.pool.fetchval(
            "SELECT user_id FROM charging_order WHERE id=$1", order_id
        )
        if not value:
            raise NotFoundError("订单不存在")
        return value

    async def order_command(
        self, admin, action, *, order_id=None, user_id=None, charger_id=None, key=None
    ):
        async with self.pool.acquire() as c, c.transaction():
            service = ChargingService(TransactionPool(c), self.settings)
            if action == "reserve":
                result = await service.reserve(user_id, charger_id, key)
            else:
                user_id = await c.fetchval(
                    "SELECT user_id FROM charging_order WHERE id=$1", order_id
                )
                if not user_id:
                    raise NotFoundError("订单不存在")
                result = await getattr(service, action)(user_id, order_id)
            await self._log(
                c,
                admin,
                "order." + action,
                "order",
                order_id or result["id"],
                {"user_id": str(user_id)},
            )
            return result
