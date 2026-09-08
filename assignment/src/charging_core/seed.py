import json
from decimal import Decimal
from pathlib import Path
from uuid import NAMESPACE_URL, UUID, uuid5

import asyncpg

from .security import hash_password


async def seed_demo(pool: asyncpg.Pool) -> None:
    stations = [
        (
            UUID("10000000-0000-0000-0000-000000000001"),
            "高新园充电站",
            "高新园区创新路 1 号",
            Decimal("121.500000"),
            Decimal("31.230000"),
            Decimal("1.20"),
        ),
        (
            UUID("10000000-0000-0000-0000-000000000002"),
            "中心广场充电站",
            "中心广场南侧",
            Decimal("121.470000"),
            Decimal("31.220000"),
            Decimal("1.35"),
        ),
    ]
    chargers = [
        (
            UUID("20000000-0000-0000-0000-000000000001"),
            stations[0][0],
            "GX-01",
            "fast",
            Decimal(60),
        ),
        (
            UUID("20000000-0000-0000-0000-000000000002"),
            stations[0][0],
            "GX-02",
            "slow",
            Decimal(7),
        ),
        (
            UUID("20000000-0000-0000-0000-000000000003"),
            stations[1][0],
            "ZX-01",
            "fast",
            Decimal(120),
        ),
    ]
    async with pool.acquire() as connection, connection.transaction():
        await connection.executemany(
            """
            INSERT INTO station (id, name, address, longitude, latitude, unit_price)
            VALUES ($1, $2, $3, $4, $5, $6) ON CONFLICT (id) DO NOTHING
            """,
            stations,
        )
        await connection.executemany(
            """
            INSERT INTO charger (id, station_id, code, kind, power_kw)
            VALUES ($1, $2, $3, $4, $5) ON CONFLICT (id) DO NOTHING
            """,
            chargers,
        )
        await connection.execute(
            """
            INSERT INTO admin_account (id, username, password_hash)
            VALUES ($1, 'admin', $2) ON CONFLICT (username) DO NOTHING
            """,
            UUID("30000000-0000-0000-0000-000000000001"),
            hash_password("123456"),
        )

        sample_path = Path(__file__).with_name("data") / "urbanev_sample.json"
        sample = json.loads(sample_path.read_text(encoding="utf-8"))
        await connection.execute(
            """
            INSERT INTO dataset_metadata (name, metadata)
            VALUES ('UrbanEV', $1::jsonb)
            ON CONFLICT (name) DO UPDATE
            SET metadata = EXCLUDED.metadata, imported_at = now()
            """,
            json.dumps(sample["dataset"], ensure_ascii=False),
        )
        for index, item in enumerate(sample["stations"]):
            station_id = uuid5(
                NAMESPACE_URL, f"urbanev:station:{item['source_station_id']}"
            )
            unit_price = Decimal(str(1.2 + (index % 5) * 0.08))
            await connection.execute(
                """
                INSERT INTO station
                    (id, name, address, longitude, latitude, unit_price,
                     source_dataset, source_station_id, zone_id)
                VALUES ($1, $2, $3, $4, $5, $6, 'UrbanEV', $7, $8)
                ON CONFLICT (id) DO UPDATE SET
                    name = EXCLUDED.name, address = EXCLUDED.address,
                    longitude = EXCLUDED.longitude, latitude = EXCLUDED.latitude,
                    unit_price = EXCLUDED.unit_price, source_dataset = 'UrbanEV',
                    source_station_id = EXCLUDED.source_station_id,
                    zone_id = EXCLUDED.zone_id
                """,
                station_id,
                item["name"],
                item["address"],
                Decimal(str(item["longitude"])),
                Decimal(str(item["latitude"])),
                unit_price,
                item["source_station_id"],
                item["zone_id"],
            )
            for number in range(1, item["charger_count"] + 1):
                charger_id = uuid5(
                    NAMESPACE_URL,
                    f"urbanev:charger:{item['source_station_id']}:{number}",
                )
                kind = "fast" if number % 4 else "slow"
                power = Decimal("120") if number % 3 else Decimal("60")
                if kind == "slow":
                    power = Decimal("7")
                await connection.execute(
                    """
                    INSERT INTO charger (id, station_id, code, kind, power_kw)
                    VALUES ($1, $2, $3, $4, $5)
                    ON CONFLICT (id) DO NOTHING
                    """,
                    charger_id,
                    station_id,
                    f"UEV-{item['source_station_id']}-{number:02d}",
                    kind,
                    power,
                )
        await connection.executemany(
            """
            INSERT INTO urbanev_hourly_profile
                (hour, occupancy_rate, energy_kwh, electricity_price, service_price)
            VALUES ($1, $2, $3, $4, $5)
            ON CONFLICT (hour) DO UPDATE SET
                occupancy_rate = EXCLUDED.occupancy_rate,
                energy_kwh = EXCLUDED.energy_kwh,
                electricity_price = EXCLUDED.electricity_price,
                service_price = EXCLUDED.service_price
            """,
            [
                (
                    row["hour"],
                    Decimal(str(row["occupancy_rate"])),
                    Decimal(str(row["energy_kwh"])),
                    Decimal(str(row["electricity_price"])),
                    Decimal(str(row["service_price"])),
                )
                for row in sample["hourly_profile"]
            ],
        )
