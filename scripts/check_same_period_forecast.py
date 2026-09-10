"""Real PostgreSQL regression; creates and removes only its own temporary schema."""
import asyncio
from datetime import date, datetime, timedelta
from uuid import uuid4

import asyncpg
from charging_core.config import Settings
from charging_core.db import Database, initialize_schema
from charging_core.seed import seed_demo
from charging_core.forecast import same_period_snapshot


async def main():
    settings = Settings(database_schema='forecast_test_' + uuid4().hex[:12])
    db = Database(settings)
    try:
        await initialize_schema(settings)
        await db.connect()
        pool = db.require_pool()
        await seed_demo(pool)
        # Re-running migrations must preserve existing business records.
        before = await pool.fetchval('SELECT count(*) FROM station')
        await initialize_schema(settings)
        assert await pool.fetchval('SELECT count(*) FROM station') == before
        today = date(2026, 9, 10)
        empty = await same_period_snapshot(pool, 'business', today)
        assert empty['ready'] and not empty['available'] and '导入' in empty['message']
        assert await same_period_snapshot(pool, 'station-999999', today) is None
        # Change the mapping as an imported/restructured database may do.
        await pool.execute("UPDATE station SET zone_id=42 WHERE source_station_id IS NOT NULL")
        rows = [(42, datetime(2022,9,1) + timedelta(hours=i), float(10 + i % 24)) for i in range(9*24)]
        async with pool.acquire() as conn:
            await conn.copy_records_to_table('urbanev_observation', records=rows,
                                            columns=['zone_id','observed_at','energy_kwh'])
        result = await same_period_snapshot(pool, 'business', today)
        assert result['available'] and len(result['prediction']) == 24
        # Several business stations sharing a zone must not multiply its energy.
        station = await same_period_snapshot(pool, 'station-1075', today)
        assert result['prediction'] == station['prediction']
        assert result['history'][-1] == 33
        await pool.execute("UPDATE station SET name='改名测试' WHERE source_station_id='1075'")
        station = await same_period_snapshot(pool, 'station-1075', today)
        assert station['label'] == '改名测试'
        # One missing region-hour must not silently undercount the business total.
        await pool.execute("UPDATE station SET zone_id=43 WHERE source_station_id='1075'")
        partial = await same_period_snapshot(pool, 'business', today)
        assert not partial['available']
        await pool.execute("UPDATE station SET zone_id=42 WHERE source_station_id='1075'")
        await pool.execute('UPDATE urbanev_observation SET energy_kwh=NULL WHERE observed_at=$1',rows[-1][1])
        assert not (await same_period_snapshot(pool, 'business', today))['available']
        assert not (await same_period_snapshot(pool, 'business', date(2026,6,1)))['available']
        print('PASS migrations preserve stations; empty database; dynamic station mapping/name; deduplicated regions; missing region/hour and NULL energy; unsupported date; unknown scope.')
    finally:
        await db.close()
        connection = await asyncpg.connect(settings.database_url)
        try:
            await connection.execute(f'DROP SCHEMA IF EXISTS "{settings.database_schema}" CASCADE')
        finally:
            await connection.close()


if __name__ == '__main__':
    asyncio.run(main())
