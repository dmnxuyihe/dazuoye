from __future__ import annotations

from datetime import UTC, datetime, timedelta
from decimal import ROUND_HALF_UP, Decimal
from typing import Any
from uuid import UUID, uuid4

import asyncpg

from .config import Settings
from .errors import ConflictError, ForbiddenError, NotFoundError
from .events import emit_event
from .queries import order_snapshot

CENT = Decimal("0.01")
KWH = Decimal("0.000001")


def _money(value: Decimal) -> Decimal:
    return value.quantize(CENT, rounding=ROUND_HALF_UP)


def _energy(value: Decimal) -> Decimal:
    return value.quantize(KWH, rounding=ROUND_HALF_UP)


class ChargingService:
    def __init__(self, pool: asyncpg.Pool, settings: Settings) -> None:
        self.pool = pool
        self.settings = settings

    async def reserve(
        self, user_id: UUID, charger_id: UUID, key: UUID
    ) -> dict[str, Any]:
        async with self.pool.acquire() as connection, connection.transaction():
            existing = await connection.fetchrow(
                "SELECT id FROM charging_order WHERE user_id = $1 AND request_key = $2",
                user_id,
                key,
            )
            if existing is not None:
                return await order_snapshot(connection, existing["id"], user_id)

            user = await connection.fetchrow(
                "SELECT * FROM app_user WHERE id = $1 FOR UPDATE", user_id
            )
            if user is None:
                raise NotFoundError("用户不存在")
            if user["status"] != "active":
                raise ForbiddenError("账号已被冻结")
            if user["balance"] < Decimal(str(self.settings.minimum_start_balance)):
                raise ConflictError("余额不足，请先充值")

            active = await connection.fetchrow(
                """
                SELECT id, status, reserved_until, charger_id
                FROM charging_order
                WHERE user_id = $1 AND status IN ('reserved', 'charging')
                FOR UPDATE
                """,
                user_id,
            )
            if (
                active
                and active["status"] == "reserved"
                and active["reserved_until"] <= datetime.now(UTC)
            ):
                await self._cancel_reserved(connection, active, "reservation_expired")
                active = None
            if active is not None:
                raise ConflictError("您已有未完成订单")

            charger = await connection.fetchrow(
                """
                SELECT c.*, s.unit_price
                FROM charger c JOIN station s ON s.id = c.station_id
                WHERE c.id = $1 FOR UPDATE OF c
                """,
                charger_id,
            )
            if charger is None:
                raise NotFoundError("电桩不存在")
            if charger["status"] != "available":
                raise ConflictError("电桩当前不可预约")

            order_id = uuid4()
            await connection.execute(
                """
                INSERT INTO charging_order
                    (id, user_id, charger_id, request_key, status, unit_price,
                     time_scale, reserved_until)
                VALUES
                    ($1, $2, $3, $4, 'reserved', $5, $6,
                     now() + ($7::int * interval '1 minute'))
                """,
                order_id,
                user_id,
                charger_id,
                key,
                charger["unit_price"],
                self.settings.time_scale,
                self.settings.reservation_minutes,
            )
            await connection.execute(
                "UPDATE charger SET status = 'reserved' WHERE id = $1", charger_id
            )
            await emit_event(
                connection,
                "order.reserved",
                {"order_id": order_id, "charger_id": charger_id},
                audience_user_id=user_id,
                audience_role="admin",
            )
            return await order_snapshot(connection, order_id, user_id)

    async def start(self, user_id: UUID, order_id: UUID) -> dict[str, Any]:
        expired = False
        result: dict[str, Any] | None = None
        async with self.pool.acquire() as connection, connection.transaction():
            user = await connection.fetchrow(
                "SELECT status FROM app_user WHERE id = $1 FOR UPDATE", user_id
            )
            if user is None:
                raise NotFoundError("用户不存在")
            if user["status"] != "active":
                raise ForbiddenError("账号已被冻结")
            order = await connection.fetchrow(
                "SELECT * FROM charging_order WHERE id = $1 FOR UPDATE", order_id
            )
            if order is None or order["user_id"] != user_id:
                raise NotFoundError("订单不存在")
            if order["status"] == "charging":
                return await order_snapshot(connection, order_id, user_id)
            if order["status"] != "reserved":
                raise ConflictError("订单当前状态不能开始充电")
            if order["reserved_until"] <= datetime.now(UTC):
                await self._cancel_reserved(connection, order, "reservation_expired")
                expired = True
            else:
                await connection.execute(
                    """
                    UPDATE charging_order
                    SET status = 'charging', started_at = now(), updated_at = now()
                    WHERE id = $1
                    """,
                    order_id,
                )
                await connection.execute(
                    "UPDATE charger SET status = 'charging' WHERE id = $1",
                    order["charger_id"],
                )
                await emit_event(
                    connection,
                    "order.started",
                    {"order_id": order_id},
                    audience_user_id=user_id,
                    audience_role="admin",
                )
                result = await order_snapshot(connection, order_id, user_id)
        if expired:
            raise ConflictError("预约已过期")
        assert result is not None
        return result

    async def cancel(self, user_id: UUID, order_id: UUID) -> dict[str, Any]:
        async with self.pool.acquire() as connection, connection.transaction():
            order = await connection.fetchrow(
                "SELECT * FROM charging_order WHERE id = $1 FOR UPDATE", order_id
            )
            if order is None or order["user_id"] != user_id:
                raise NotFoundError("订单不存在")
            if order["status"] == "cancelled":
                return await order_snapshot(connection, order_id, user_id)
            if order["status"] != "reserved":
                raise ConflictError("只有预约中的订单可以取消")
            await self._cancel_reserved(connection, order, "user_cancelled")
            return await order_snapshot(connection, order_id, user_id)

    async def get_order(self, user_id: UUID, order_id: UUID) -> dict[str, Any]:
        async with self.pool.acquire() as connection:
            return await order_snapshot(connection, order_id, user_id)

    async def stop(self, user_id: UUID, order_id: UUID) -> dict[str, Any]:
        return await self._settle(
            order_id, expected_user_id=user_id, reason="user_stopped"
        )

    async def _settle(
        self,
        order_id: UUID,
        *,
        expected_user_id: UUID | None,
        reason: str,
    ) -> dict[str, Any]:
        async with self.pool.acquire() as connection, connection.transaction():
            identity = await connection.fetchrow(
                "SELECT user_id FROM charging_order WHERE id = $1", order_id
            )
            if identity is None or (
                expected_user_id is not None and identity["user_id"] != expected_user_id
            ):
                raise NotFoundError("订单不存在")
            user = await connection.fetchrow(
                "SELECT * FROM app_user WHERE id = $1 FOR UPDATE", identity["user_id"]
            )
            order = await connection.fetchrow(
                """
                SELECT o.*, c.power_kw, now() AS db_now
                FROM charging_order o JOIN charger c ON c.id = o.charger_id
                WHERE o.id = $1 FOR UPDATE OF o, c
                """,
                order_id,
            )
            if order["status"] == "completed":
                return await order_snapshot(connection, order_id, identity["user_id"])
            if order["status"] != "charging":
                raise ConflictError("只有充电中的订单可以结算")

            ended_at = order["db_now"]
            elapsed_seconds = Decimal(
                str((ended_at - order["started_at"]).total_seconds())
            )
            raw_energy = (
                order["power_kw"]
                * elapsed_seconds
                * order["time_scale"]
                / Decimal(3600)
            )
            due = _money(raw_energy * order["unit_price"])
            if due >= user["balance"]:
                reason = "balance_exhausted"
                affordable_energy = user["balance"] / order["unit_price"]
                affordable_seconds = (
                    affordable_energy
                    * Decimal(3600)
                    / order["power_kw"]
                    / order["time_scale"]
                )
                ended_at = min(
                    ended_at,
                    order["started_at"] + timedelta(seconds=float(affordable_seconds)),
                )
                energy = _energy(affordable_energy)
                amount = user["balance"]
            else:
                energy = _energy(raw_energy)
                amount = due
            balance_after = _money(user["balance"] - amount)

            await connection.execute(
                """
                UPDATE charging_order
                SET status = 'completed', ended_at = $2, energy_kwh = $3,
                    amount = $4, stop_reason = $5, updated_at = now()
                WHERE id = $1
                """,
                order_id,
                ended_at,
                energy,
                amount,
                reason,
            )
            await connection.execute(
                """
                UPDATE charger
                SET status = 'available', total_sessions = total_sessions + 1
                WHERE id = $1
                """,
                order["charger_id"],
            )
            if amount > 0:
                await connection.execute(
                    "UPDATE app_user SET balance = $2 WHERE id = $1",
                    identity["user_id"],
                    balance_after,
                )
                await connection.execute(
                    """
                    INSERT INTO wallet_entry
                        (id, user_id, idempotency_key, entry_type, amount,
                         balance_after, order_id)
                    VALUES ($1, $2, $3, 'charge', $4, $5, $3)
                    ON CONFLICT (user_id, idempotency_key) DO NOTHING
                    """,
                    uuid4(),
                    identity["user_id"],
                    order_id,
                    -amount,
                    balance_after,
                )
            await emit_event(
                connection,
                "order.completed",
                {"order_id": order_id, "amount": amount, "reason": reason},
                audience_user_id=identity["user_id"],
                audience_role="admin",
            )
            return await order_snapshot(connection, order_id, identity["user_id"])

    async def _cancel_reserved(
        self, connection: asyncpg.Connection, order: asyncpg.Record, reason: str
    ) -> None:
        await connection.execute(
            """
            UPDATE charging_order
            SET status = 'cancelled', ended_at = now(), stop_reason = $2, updated_at = now()
            WHERE id = $1 AND status = 'reserved'
            """,
            order["id"],
            reason,
        )
        await connection.execute(
            "UPDATE charger SET status = 'available' WHERE id = $1 AND status = 'reserved'",
            order["charger_id"],
        )
        await emit_event(
            connection,
            "order.cancelled",
            {"order_id": order["id"], "reason": reason},
            audience_user_id=order["user_id"],
            audience_role="admin",
        )

    async def reconcile_once(self) -> dict[str, int]:
        expired = await self.pool.fetch(
            """
            SELECT id FROM charging_order
            WHERE status = 'reserved' AND reserved_until <= now()
            ORDER BY reserved_until LIMIT 50
            """
        )
        cancelled = 0
        for item in expired:
            async with self.pool.acquire() as connection, connection.transaction():
                order = await connection.fetchrow(
                    """
                    SELECT * FROM charging_order
                    WHERE id = $1 AND status = 'reserved' FOR UPDATE SKIP LOCKED
                    """,
                    item["id"],
                )
                if order is not None and order["reserved_until"] <= datetime.now(UTC):
                    await self._cancel_reserved(
                        connection, order, "reservation_expired"
                    )
                    cancelled += 1

        exhausted = await self.pool.fetch(
            """
            SELECT o.id
            FROM charging_order o
            JOIN charger c ON c.id = o.charger_id
            JOIN app_user u ON u.id = o.user_id
            WHERE o.status = 'charging'
              AND round((c.power_kw * extract(epoch FROM (now() - o.started_at))
                         * o.time_scale / 3600 * o.unit_price)::numeric, 2) >= u.balance
            ORDER BY o.started_at LIMIT 50
            """
        )
        settled = 0
        for item in exhausted:
            try:
                await self._settle(
                    item["id"], expected_user_id=None, reason="balance_exhausted"
                )
                settled += 1
            except ConflictError:
                pass
        return {"cancelled": cancelled, "settled": settled}
