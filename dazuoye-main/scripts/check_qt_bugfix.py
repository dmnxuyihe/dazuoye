"""Regression checks in a disposable schema; never mutate the live schema."""
import asyncio
from decimal import Decimal as D
from uuid import uuid4

import asyncpg
from charging_core.config import Settings
from charging_core.db import Database, initialize_schema
from charging_core.service import ChargingService
from charging_core.account_service import AccountService
from charging_core.admin_service import AdminService
from charging_core.console_service import ConsoleService
from charging_core.errors import AuthenticationError, ConflictError
from charging_core.security import hash_password
from charging_core.api import ReserveRequest


async def main():
    settings = Settings(database_schema="qt_bugfix_" + uuid4().hex[:12])
    assert settings.database_schema.startswith("qt_bugfix_")
    db = Database(settings)
    try:
        await initialize_schema(settings)
        await db.connect()
        pool = db.require_pool()
        service, accounts, admin = ChargingService(pool, settings), AccountService(pool, settings), AdminService(pool)
        aid, uid = uuid4(), uuid4()
        await pool.execute("INSERT INTO admin_account(id,username,password_hash) VALUES($1,'regression',$2)", aid, hash_password("correct-password"))
        await pool.execute("INSERT INTO app_user(id,phone,nickname,balance) VALUES($1,'13900000001','回归用户',100)", uid)
        station = await admin.create_station(aid, name="回归站", address="隔离测试", longitude=D('114.05'), latitude=D('22.55'), unit_price=D('1.20'), initial_chargers=2)
        chargers = await admin.list_chargers(station['id'], None)
        assert len(chargers) == 2 and len({c['code'] for c in chargers}) == 2
        extra = await admin.create_charger(aid, station_id=station['id'], code='', kind='slow', power_kw=D('7'))
        assert extra['code'].startswith('回归站-')
        print('PASS initial chargers and generated station-based identifiers')
        body = ReserveRequest(charger_id=chargers[0]['id'], idempotency_key=uuid4(), initial_soc=40, target_soc=70, battery_kwh=60)
        order = await service.reserve(uid, body.charger_id, body.idempotency_key, initial_soc=body.initial_soc, target_soc=body.target_soc, battery_kwh=body.battery_kwh)
        assert order['target_energy_kwh'] == 18
        await service.start(uid, order['id'])
        await pool.execute("UPDATE charging_order SET started_at=now()-interval '2 seconds' WHERE id=$1", order['id'])
        history = await accounts.order_history(uid, limit=200, offset=0)
        assert history[0]['amount'] > 0 and history[0]['energy_kwh'] > 0
        assert history[0]['reserved_until'] and history[0]['power_kw']
        try:
            await accounts.withdraw(uid, D('1'), uuid4())
            raise AssertionError('withdrawal allowed during charging')
        except ConflictError:
            pass
        await pool.execute("UPDATE charging_order SET started_at=now()-interval '1 hour' WHERE id=$1", order['id'])
        snapshot = await service.get_order(uid, order['id'])
        assert snapshot['energy_kwh'] == 18 and snapshot['amount'] == D('21.60')
        await service.reconcile_once()
        ended = await service.get_order(uid, order['id'])
        assert ended['status'] == 'pending_payment' and ended['stop_reason'] == 'target_reached'
        assert ended['energy_kwh'] == 18 and ended['amount'] == D('21.60')
        await service.stop(uid, order['id'])
        await service.pay(uid, order['id'])
        assert await pool.fetchval('SELECT count(*) FROM wallet_entry WHERE order_id=$1', order['id']) == 1
        print('PASS live order history, target cap, autonomous stop, and idempotent settlement')
        key = uuid4()
        first, second = await asyncio.gather(accounts.withdraw(uid,D('1'),key), accounts.withdraw(uid,D('1'),key))
        assert first['id'] == second['id']
        await ConsoleService(pool,settings).review_withdrawal(aid,first['id'],True,'回归模拟到账')
        assert (await accounts.profile(uid))['balance'] == D('77.40')
        print('PASS withdrawal active-order guard and concurrent idempotency')
        await pool.execute('UPDATE app_user SET balance=5 WHERE id=$1',uid)
        order = await service.reserve(uid, chargers[0]['id'], uuid4(), initial_soc=D(40), target_soc=D(100), battery_kwh=D(60))
        await service.start(uid,order['id'])
        await pool.execute("UPDATE charging_order SET started_at=now()-interval '1 hour' WHERE id=$1",order['id'])
        snapshot = await service.get_order(uid,order['id'])
        assert snapshot['amount'] == 5
        await service.reconcile_once()
        ended = await service.get_order(uid,order['id'])
        assert ended['stop_reason'] == 'balance_exhausted' and ended['status'] == 'pending_payment' and ended['amount'] == 5
        assert (await service.pay(uid,order['id']))['balance'] == 0
        print('PASS balance cap and automatic exhaustion settlement')
        for i in range(5):
            try:
                await accounts.admin_login('regression','wrong')
                raise AssertionError('bad login accepted')
            except AuthenticationError as error:
                assert ('30秒' in str(error)) == (i == 4), str(error)
        await pool.execute("UPDATE admin_account SET locked_until=now()-interval '1 second' WHERE id=$1",aid)
        try:
            await accounts.admin_login('regression','wrong')
        except AuthenticationError as error:
            assert '锁定' not in str(error)
        assert await pool.fetchval('SELECT failed_attempts FROM admin_account WHERE id=$1',aid) == 1
        await accounts.admin_login('regression','correct-password')
        print('PASS five-attempt lock, expiry reset and successful login')
        assert (await admin.list_chargers(station['id'],None))[0]['total_minutes'] >= 0
        assert (await admin.list_stations())[0]['online_rate'] == 100
        assert (await admin.summary())['total_revenue'] == D('26.60')
        print('PASS station availability, cumulative time and actual revenue')
    finally:
        await db.close()
        connection = await asyncpg.connect(settings.database_url)
        try:
            await connection.execute(f'DROP SCHEMA IF EXISTS "{settings.database_schema}" CASCADE')
        finally:
            await connection.close()
        print('Disposable regression schema removed; live data untouched.')


if __name__ == '__main__':
    asyncio.run(main())
