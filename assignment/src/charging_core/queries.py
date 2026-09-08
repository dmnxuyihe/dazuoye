import json
from decimal import Decimal
from typing import Any
from uuid import UUID

import asyncpg

from .errors import NotFoundError


async def station_list(
    pool: asyncpg.Pool,
    latitude: Decimal | None = None,
    longitude: Decimal | None = None,
) -> list[dict[str, Any]]:
    rows = await pool.fetch(
        """
        SELECT s.id, s.name, s.address, s.longitude, s.latitude, s.unit_price,
               s.source_dataset, s.source_station_id, s.zone_id,
               count(c.id)::int AS charger_count,
               count(c.id) FILTER (WHERE c.status = 'available')::int AS available_count,
               max(c.power_kw) AS max_power_kw,
               CASE WHEN $1::numeric IS NULL THEN NULL ELSE
                 round((6371 * acos(least(1, greatest(-1,
                   cos(radians($1::double precision)) * cos(radians(s.latitude::double precision))
                   * cos(radians(s.longitude::double precision) - radians($2::double precision))
                   + sin(radians($1::double precision)) * sin(radians(s.latitude::double precision))
                 ))))::numeric, 1)
               END AS distance_km
        FROM station s LEFT JOIN charger c ON c.station_id = s.id
        GROUP BY s.id ORDER BY distance_km NULLS LAST, s.name
        """,
        latitude,
        longitude,
    )
    return [dict(row) for row in rows]


async def urbanev_overview(pool: asyncpg.Pool) -> dict[str, Any]:
    metadata = await pool.fetchval(
        "SELECT metadata FROM dataset_metadata WHERE name = 'UrbanEV'"
    )
    rows = await pool.fetch(
        """
        SELECT hour, occupancy_rate, energy_kwh, electricity_price, service_price
        FROM urbanev_hourly_profile ORDER BY hour
        """
    )
    if isinstance(metadata, str):
        metadata = json.loads(metadata)
    return {"metadata": metadata or {}, "hourly_profile": [dict(row) for row in rows]}


async def charger_list(pool: asyncpg.Pool, station_id: UUID) -> list[dict[str, Any]]:
    rows = await pool.fetch(
        """
        SELECT id, station_id, code, kind, power_kw, status, total_sessions
        FROM charger WHERE station_id = $1 ORDER BY code
        """,
        station_id,
    )
    return [dict(row) for row in rows]


async def order_snapshot(
    connection: asyncpg.Connection, order_id: UUID, user_id: UUID
) -> dict[str, Any]:
    row = await connection.fetchrow(
        """
        SELECT o.id, o.user_id, o.charger_id, o.status, o.unit_price,
               o.reserved_at, o.reserved_until, o.started_at, o.ended_at,
               CASE WHEN o.status = 'charging' THEN
                   round((c.power_kw * extract(epoch FROM (now() - o.started_at))
                          * o.time_scale / 3600)::numeric, 6)
                   ELSE o.energy_kwh END AS energy_kwh,
               CASE WHEN o.status = 'charging' THEN
                   round((c.power_kw * extract(epoch FROM (now() - o.started_at))
                          * o.time_scale / 3600 * o.unit_price)::numeric, 2)
                   ELSE o.amount END AS amount,
               o.stop_reason, c.code AS charger_code, c.power_kw,
               s.name AS station_name, u.balance
        FROM charging_order o
        JOIN charger c ON c.id = o.charger_id
        JOIN station s ON s.id = c.station_id
        JOIN app_user u ON u.id = o.user_id
        WHERE o.id = $1 AND o.user_id = $2
        """,
        order_id,
        user_id,
    )
    if row is None:
        raise NotFoundError("订单不存在")
    return dict(row)
