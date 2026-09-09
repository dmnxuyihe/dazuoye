import json
from decimal import Decimal
from pathlib import Path
from uuid import NAMESPACE_URL, UUID, uuid5

import asyncpg

from .security import hash_password


async def seed_demo(pool: asyncpg.Pool, *, cities_only: bool = False) -> None:
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
    for city, lon, lat in [("北京", "116.397000", "39.908000"),
                           ("广州", "113.264000", "23.129000"),
                           ("杭州", "120.155000", "30.274000")]:
        stations.append((uuid5(NAMESPACE_URL, "electra:demo-city:" + city),
                         city + "演示充电站", city + " · 模拟站点（非真实营业地址）",
                         Decimal(lon), Decimal(lat), Decimal("1.20")))
    # Give every demo region several nearby choices. Stable UUIDs make this
    # safe to apply to both new and existing databases.
    for key, name, address, lon, lat, price in [
        ("shanghai-hongqiao", "\u4e0a\u6d77\u8679\u6865\u5145\u7535\u7ad9", "\u4e0a\u6d77\u5e02\u95f5\u884c\u533a\u8679\u6865\u5546\u52a1\u533a", "121.327000", "31.200000", "1.28"),
        ("shanghai-pudong", "\u4e0a\u6d77\u6d66\u4e1c\u5145\u7535\u7ad9", "\u4e0a\u6d77\u5e02\u6d66\u4e1c\u65b0\u533a\u4e16\u7eaa\u5927\u9053", "121.544000", "31.221000", "1.32"),
        ("beijing-guomao", "\u5317\u4eac\u56fd\u8d38\u5145\u7535\u7ad9", "\u5317\u4eac\u5e02\u671d\u9633\u533a\u56fd\u8d38\u5546\u5708", "116.458000", "39.908000", "1.32"),
        ("beijing-haidian", "\u5317\u4eac\u6d77\u6dc0\u5145\u7535\u7ad9", "\u5317\u4eac\u5e02\u6d77\u6dc0\u533a\u4e2d\u5173\u6751", "116.316000", "39.983000", "1.24"),
        ("guangzhou-tianhe", "\u5e7f\u5dde\u5929\u6cb3\u5145\u7535\u7ad9", "\u5e7f\u5dde\u5e02\u5929\u6cb3\u533a\u73e0\u6c5f\u65b0\u57ce", "113.324000", "23.119000", "1.32"),
        ("guangzhou-panyu", "\u5e7f\u5dde\u756a\u79ba\u5145\u7535\u7ad9", "\u5e7f\u5dde\u5e02\u756a\u79ba\u533a\u6c49\u6eaa\u5927\u9053", "113.330000", "22.992000", "1.24"),
        ("hangzhou-binjiang", "\u676d\u5dde\u6ee8\u6c5f\u5145\u7535\u7ad9", "\u676d\u5dde\u5e02\u6ee8\u6c5f\u533a\u6c5f\u5357\u5927\u9053", "120.205000", "30.208000", "1.28"),
        ("hangzhou-yuhang", "\u676d\u5dde\u4f59\u676d\u5145\u7535\u7ad9", "\u676d\u5dde\u5e02\u4f59\u676d\u533a\u672a\u6765\u79d1\u6280\u57ce", "120.005000", "30.280000", "1.20"),
    ]:
        stations.append((uuid5(NAMESPACE_URL, "electra:demo-region:" + key),
                         name, address, Decimal(lon), Decimal(lat), Decimal(price)))

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
    for station in stations[2:]:
        for index, kind, power in [(1, "fast", 120), (2, "slow", 7)]:
            chargers.append((uuid5(NAMESPACE_URL, f"electra:demo-charger:{station[0]}:{index}"),
                             station[0], f"DEMO-{str(station[0])[:8]}-{index:02d}", kind, Decimal(power)))
    if cities_only:
        stations, chargers = stations[2:], chargers[3:]
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
        if cities_only:
            return
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
