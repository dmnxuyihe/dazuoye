from __future__ import annotations

import json
from decimal import Decimal
from typing import Any
from uuid import UUID, uuid4

import asyncpg

from .errors import ConflictError, NotFoundError
from .events import emit_event
from .queries import order_snapshot


class AdminService:
    def __init__(self, pool: asyncpg.Pool) -> None:
        self.pool = pool

    async def _log(
        self,
        connection: asyncpg.Connection,
        admin_id: UUID,
        action: str,
        target_type: str,
        target_id: UUID | None,
        detail: dict[str, Any] | None = None,
    ) -> None:
        await connection.execute(
            """
            INSERT INTO ops_log (admin_id, action, target_type, target_id, detail)
            VALUES ($1, $2, $3, $4, $5::jsonb)
            """,
            admin_id,
            action,
            target_type,
            target_id,
            json.dumps(detail or {}, default=str, ensure_ascii=False),
        )

    async def list_users(
        self,
        *,
        search: str | None,
        status: str | None,
        limit: int,
        offset: int,
    ) -> list[dict[str, Any]]:
        rows = await self.pool.fetch(
            """
            SELECT id, phone, nickname, avatar_path, balance, status, created_at
            FROM app_user
            WHERE ($1::text IS NULL OR phone LIKE '%' || $1 || '%')
              AND ($2::text IS NULL OR status = $2)
            ORDER BY created_at DESC LIMIT $3 OFFSET $4
            """,
            search,
            status,
            limit,
            offset,
        )
        return [dict(row) for row in rows]

    async def list_stations(self) -> list[dict[str, Any]]:
        rows = await self.pool.fetch(
            """
            SELECT s.*, count(c.id)::int AS charger_count,
                   count(c.id) FILTER (WHERE c.status <> 'faulted')::int AS online_count,
                   round(100.0 * count(c.id) FILTER (WHERE c.status <> 'faulted') / nullif(count(c.id),0), 1) AS online_rate
            FROM station s LEFT JOIN charger c ON c.station_id = s.id
            GROUP BY s.id ORDER BY s.name
            """
        )
        return [dict(row) for row in rows]

    async def list_chargers(
        self, station_id: UUID | None, status: str | None
    ) -> list[dict[str, Any]]:
        rows = await self.pool.fetch(
            """
            SELECT c.*, s.name AS station_name,
                COALESCE((SELECT round(sum(extract(epoch FROM (o.ended_at - o.started_at)) * o.time_scale / 60), 1)
                    FROM charging_order o WHERE o.charger_id=c.id AND o.status IN ('completed','pending_payment')),0) AS total_minutes
            FROM charger c JOIN station s ON s.id = c.station_id
            WHERE ($1::uuid IS NULL OR c.station_id = $1)
              AND ($2::text IS NULL OR c.status = $2)
            ORDER BY s.name, c.code
            """,
            station_id,
            status,
        )
        return [dict(row) for row in rows]

    async def set_user_status(
        self, admin_id: UUID, user_id: UUID, status: str
    ) -> dict[str, Any]:
        async with self.pool.acquire() as connection, connection.transaction():
            row = await connection.fetchrow(
                """
                UPDATE app_user SET status = $2 WHERE id = $1
                RETURNING id, phone, nickname, balance, status, created_at
                """,
                user_id,
                status,
            )
            if row is None:
                raise NotFoundError("用户不存在")
            await self._log(connection, admin_id, f"user.{status}", "user", user_id)
            await emit_event(
                connection,
                "user.status_changed",
                {"user_id": user_id, "status": status},
                audience_user_id=user_id,
            )
            return dict(row)

    async def list_orders(
        self, *, status: str | None, limit: int, offset: int
    ) -> list[dict[str, Any]]:
        rows = await self.pool.fetch(
            """
            SELECT o.id, o.user_id, o.status, o.reserved_at, o.started_at, o.ended_at,
                   o.energy_kwh, o.amount, o.stop_reason, u.phone,
                   c.code AS charger_code, s.name AS station_name
            FROM charging_order o
            JOIN app_user u ON u.id = o.user_id
            JOIN charger c ON c.id = o.charger_id
            JOIN station s ON s.id = c.station_id
            WHERE ($1::text IS NULL OR o.status = $1)
            ORDER BY o.created_at DESC LIMIT $2 OFFSET $3
            """,
            status,
            limit,
            offset,
        )
        async with self.pool.acquire() as connection:
            return [{**await order_snapshot(connection, row["id"], row["user_id"]), "phone": row["phone"]} for row in rows]

    async def set_tariff(self,admin_id,station_id,periods):
        async with self.pool.acquire() as c,c.transaction():
            row=await c.fetchrow("UPDATE station SET tariff=$2::jsonb WHERE id=$1 RETURNING id,tariff",station_id,json.dumps(periods))
            if not row: raise NotFoundError("站点不存在")
            await self._log(c,admin_id,"station.tariff_updated","station",station_id,{"periods":periods})
            await emit_event(c,"station.changed",{"station_id":station_id})
            return {"id":row["id"],"tariff":periods}

    async def create_station(
        self,
        admin_id: UUID,
        *,
        name: str,
        address: str,
        longitude: Decimal,
        latitude: Decimal,
        unit_price: Decimal,
        initial_chargers: int = 0,
        address_verified: bool = False,
    ) -> dict[str, Any]:
        station_id = uuid4()
        async with self.pool.acquire() as connection, connection.transaction():
            row = await connection.fetchrow(
                """
                INSERT INTO station (id, name, address, longitude, latitude, unit_price, address_verified)
                VALUES ($1, $2, $3, $4, $5, $6, $7) RETURNING *
                """,
                station_id,
                name,
                address,
                longitude,
                latitude,
                unit_price,
                address_verified,
            )
            for index in range(initial_chargers):
                await connection.execute(
                    "INSERT INTO charger(id,station_id,code,kind,power_kw) VALUES($1,$2,$3,'fast',120)",
                    uuid4(), station_id, f"{name[:6]}-{station_id.hex[:8]}-{index + 1:03d}",
                )
            await self._log(connection, admin_id, "station.created", "station", station_id,
                            {"initial_chargers": initial_chargers})
            await emit_event(
                connection,
                "station.changed",
                {"station_id": station_id, "action": "created"},
            )
            return dict(row)

    async def update_station(
        self,
        admin_id: UUID,
        station_id: UUID,
        *,
        name: str | None,
        address: str | None,
        longitude: Decimal | None,
        latitude: Decimal | None,
        unit_price: Decimal | None,
        address_verified: bool | None = None,
    ) -> dict[str, Any]:
        async with self.pool.acquire() as connection, connection.transaction():
            row = await connection.fetchrow(
                """
                UPDATE station SET
                    name = COALESCE($2, name), address = COALESCE($3, address),
                    longitude = COALESCE($4, longitude), latitude = COALESCE($5, latitude),
                    unit_price = COALESCE($6, unit_price),
                    address_verified = COALESCE($7, CASE WHEN $3::text IS NOT NULL OR $4::numeric IS NOT NULL OR $5::numeric IS NOT NULL THEN false ELSE address_verified END)
                WHERE id = $1 RETURNING *
                """,
                station_id,
                name,
                address,
                longitude,
                latitude,
                unit_price,
                address_verified,
            )
            if row is None:
                raise NotFoundError("充电站不存在")
            await self._log(
                connection, admin_id, "station.updated", "station", station_id
            )
            await emit_event(
                connection,
                "station.changed",
                {"station_id": station_id, "action": "updated"},
            )
            return dict(row)

    async def delete_station(self, admin_id: UUID, station_id: UUID) -> None:
        async with self.pool.acquire() as connection, connection.transaction():
            station = await connection.fetchrow(
                "SELECT id FROM station WHERE id = $1 FOR UPDATE", station_id
            )
            if station is None:
                raise NotFoundError("充电站不存在")
            if await connection.fetchval(
                "SELECT EXISTS(SELECT 1 FROM charger WHERE station_id = $1)", station_id
            ):
                raise ConflictError("充电站下仍有电桩，不能删除")
            await connection.execute("DELETE FROM station WHERE id = $1", station_id)
            await self._log(
                connection, admin_id, "station.deleted", "station", station_id
            )

    async def create_charger(
        self,
        admin_id: UUID,
        *,
        station_id: UUID,
        code: str,
        kind: str,
        power_kw: Decimal,
    ) -> dict[str, Any]:
        charger_id = uuid4()
        async with self.pool.acquire() as connection, connection.transaction():
            station = await connection.fetchrow("SELECT name FROM station WHERE id=$1 FOR UPDATE", station_id)
            if station is None:
                raise NotFoundError("充电站不存在")
            if not code.strip():
                code = f"{station['name'][:6]}-{station_id.hex[:8]}-{charger_id.hex[:8]}"
            if not await connection.fetchval(
                "SELECT EXISTS(SELECT 1 FROM station WHERE id = $1)", station_id
            ):
                raise NotFoundError("充电站不存在")
            try:
                row = await connection.fetchrow(
                    """
                    INSERT INTO charger (id, station_id, code, kind, power_kw)
                    VALUES ($1, $2, $3, $4, $5) RETURNING *
                    """,
                    charger_id,
                    station_id,
                    code,
                    kind,
                    power_kw,
                )
            except asyncpg.UniqueViolationError as exc:
                raise ConflictError("电桩编号已存在") from exc
            await self._log(
                connection, admin_id, "charger.created", "charger", charger_id
            )
            await emit_event(
                connection,
                "charger.changed",
                {"charger_id": charger_id, "status": "available"},
            )
            return dict(row)

    async def set_charger_status(
        self, admin_id: UUID, charger_id: UUID, status: str
    ) -> dict[str, Any]:
        async with self.pool.acquire() as connection, connection.transaction():
            charger = await connection.fetchrow(
                "SELECT * FROM charger WHERE id = $1 FOR UPDATE", charger_id
            )
            if charger is None:
                raise NotFoundError("电桩不存在")
            if charger["status"] in {"reserved", "charging"}:
                raise ConflictError("活动订单占用中的电桩不能人工改状态")
            if status == "available" and charger["status"] != "faulted":
                raise ConflictError("只有故障电桩可以恢复为空闲")
            row = await connection.fetchrow(
                "UPDATE charger SET status = $2 WHERE id = $1 RETURNING *",
                charger_id,
                status,
            )
            await self._log(
                connection,
                admin_id,
                "charger.status_changed",
                "charger",
                charger_id,
                {"from": charger["status"], "to": status},
            )
            await emit_event(
                connection,
                "charger.changed",
                {"charger_id": charger_id, "status": status},
            )
            return dict(row)

    async def restart_charger(self, admin_id: UUID, charger_id: UUID) -> dict[str, Any]:
        async with self.pool.acquire() as connection, connection.transaction():
            charger = await connection.fetchrow(
                "SELECT * FROM charger WHERE id = $1 FOR UPDATE", charger_id
            )
            if charger is None:
                raise NotFoundError("电桩不存在")
            if charger["status"] != "faulted":
                raise ConflictError("仅故障电桩需要模拟重启")
            row = await connection.fetchrow(
                "UPDATE charger SET status = 'available' WHERE id = $1 RETURNING *",
                charger_id,
            )
            await self._log(
                connection, admin_id, "charger.restarted", "charger", charger_id
            )
            await emit_event(
                connection,
                "charger.changed",
                {
                    "charger_id": charger_id,
                    "status": "available",
                    "action": "restarted",
                },
            )
            return dict(row)

    async def delete_charger(self, admin_id: UUID, charger_id: UUID) -> None:
        async with self.pool.acquire() as connection, connection.transaction():
            charger = await connection.fetchrow(
                "SELECT * FROM charger WHERE id = $1 FOR UPDATE", charger_id
            )
            if charger is None:
                raise NotFoundError("电桩不存在")
            if charger["status"] in {"reserved", "charging"}:
                raise ConflictError("使用中的电桩不能删除")
            if await connection.fetchval(
                "SELECT EXISTS(SELECT 1 FROM charging_order WHERE charger_id = $1)",
                charger_id,
            ):
                raise ConflictError("存在历史订单的电桩不能删除，请保留审计记录")
            await connection.execute("DELETE FROM charger WHERE id = $1", charger_id)
            await self._log(
                connection, admin_id, "charger.deleted", "charger", charger_id
            )

    async def summary(self) -> dict[str, Any]:
        row = await self.pool.fetchrow(
            """
            SELECT
              (SELECT count(*) FROM app_user) AS users,
              (SELECT count(*) FROM station) AS stations,
              (SELECT count(*) FROM charger) AS chargers,
              (SELECT count(*) FROM charger WHERE status = 'available') AS available,
              (SELECT count(*) FROM charger WHERE status = 'charging') AS charging,
              (SELECT count(*) FROM charger WHERE status = 'faulted') AS faulted,
              (SELECT count(*) FROM charging_order WHERE status = 'completed') AS completed_orders,
              (SELECT COALESCE(sum(amount), 0) FROM charging_order WHERE status = 'completed') AS total_revenue,
              (SELECT COALESCE(sum(amount), 0) FROM charging_order
                 WHERE status = 'completed' AND (coalesce(paid_at,ended_at) AT TIME ZONE 'Asia/Shanghai')::date = (now() AT TIME ZONE 'Asia/Shanghai')::date) AS today_revenue,
              (SELECT count(*) FROM charging_order
                 WHERE status = 'completed' AND ended_at::date = CURRENT_DATE) AS today_orders,
              (SELECT COALESCE(sum(amount), 0) FROM charging_order
                 WHERE status = 'completed' AND date_trunc('month', coalesce(paid_at,ended_at) AT TIME ZONE 'Asia/Shanghai') = date_trunc('month', now() AT TIME ZONE 'Asia/Shanghai')) AS month_revenue
            """
        )
        return dict(row)

    async def revenue(self, days: int) -> list[dict[str, Any]]:
        rows = await self.pool.fetch(
            """
            SELECT day::date AS date, COALESCE(sum(o.amount), 0) AS revenue,
                   count(o.id)::int AS orders
            FROM generate_series((now() AT TIME ZONE 'Asia/Shanghai')::date - ($1::int - 1), (now() AT TIME ZONE 'Asia/Shanghai')::date, interval '1 day') day
            LEFT JOIN charging_order o ON o.status = 'completed' AND (coalesce(o.paid_at,o.ended_at) AT TIME ZONE 'Asia/Shanghai')::date = day::date
            GROUP BY day ORDER BY day
            """,
            days,
        )
        return [dict(row) for row in rows]

    async def ops_logs(self, limit: int, offset: int) -> list[dict[str, Any]]:
        rows = await self.pool.fetch(
            """
            SELECT l.id, a.username, l.action, l.target_type, l.target_id,
                   l.detail, l.created_at
            FROM ops_log l JOIN admin_account a ON a.id = l.admin_id
            ORDER BY l.created_at DESC LIMIT $1 OFFSET $2
            """,
            limit,
            offset,
        )
        return [dict(row) for row in rows]
