import json
from decimal import Decimal
from typing import Any
from uuid import UUID

import asyncpg

from .errors import NotFoundError
from .billing import meter, tariff_rows


async def station_list(
    pool: asyncpg.Pool,
    latitude: Decimal | None = None,
    longitude: Decimal | None = None,
) -> list[dict[str, Any]]:
    rows = await pool.fetch(
        """
        SELECT s.id, s.name, s.address, s.longitude, s.latitude, s.unit_price,
               s.source_dataset, s.source_station_id, s.zone_id, s.tariff, s.address_verified,
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
    result = []
    for row in rows:
        station = dict(row)
        # FastAPI may serialize Decimal as a JSON string. Distances are display/sort
        # values, so expose them as JSON numbers for every client.
        if station["distance_km"] is not None:
            station["distance_km"] = float(station["distance_km"])
        station["tariff"] = tariff_rows(station["tariff"], station["unit_price"])
        result.append(station)
    return result


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
        SELECT o.*, c.code AS charger_code,c.power_kw,s.name AS station_name,
               s.id AS station_id,u.balance,u.held_balance,now() AS db_now
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
    result = dict(row)
    result["available_balance"] = row["balance"] - row["held_balance"]
    result["tariff_snapshot"] = tariff_rows(row["tariff_snapshot"],row["unit_price"])
    result["billing_detail"] = json.loads(row["billing_detail"]) if isinstance(row["billing_detail"],str) else row["billing_detail"]
    wallet = await connection.fetchrow(
        "SELECT amount,balance_after FROM wallet_entry WHERE order_id=$1 ORDER BY created_at DESC LIMIT 1",
        order_id,
    )
    if wallet:
        result["balance_before"] = wallet["balance_after"] - wallet["amount"]
        result["balance_after"] = wallet["balance_after"]
    if row["status"] == "charging":
        result.update(meter(row,row["db_now"],result["available_balance"]))
        result.pop("ended_at",None)
    if row["battery_kwh"]:
        result["vehicle_soc"] = min(100,row["initial_soc"] + result["energy_kwh"] / row["battery_kwh"] * 100)
    result.pop("balance", None)
    result.pop("held_balance", None)
    result.pop("available_balance", None)
    result.pop("db_now",None)
    return result
