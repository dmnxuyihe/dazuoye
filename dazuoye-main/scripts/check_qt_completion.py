"""Completion regression: disposable schema, real transactions, no live-data writes."""
import asyncio
import base64
from datetime import datetime, timedelta, timezone
from decimal import Decimal as D
from uuid import uuid4
import asyncpg
from charging_core.config import Settings
from charging_core.db import Database, initialize_schema
from charging_core.service import ChargingService
from charging_core.account_service import AccountService
from charging_core.admin_service import AdminService
from charging_core.console_service import ConsoleService
from charging_core.billing import meter
from charging_core.errors import ConflictError
from charging_core.security import hash_password
from charging_core.admin_routes import TariffUpdate

def billing_checks():
    tz=timezone(timedelta(hours=8))
    start=datetime(2026,9,8,6,30,tzinfo=tz)
    bands=[dict(start_hour=0,end_hour=7,electricity_price="1",service_price="0.2"),
           dict(start_hour=7,end_hour=24,electricity_price="2",service_price="0.2")]
    order=dict(started_at=start,power_kw=D(10),time_scale=1,target_energy_kwh=None,tariff_snapshot=bands,unit_price=D(1))
    bill=meter(order,start+timedelta(hours=1),D(100))
    assert bill['energy_kwh']==10 and bill['amount']==17 and len(bill['billing_detail'])==2,bill
    capped=meter(order,start+timedelta(hours=1),D(10))
    assert capped['amount']==10 and capped['cap_reason']=='balance_exhausted'
    target=meter({**order,'target_energy_kwh':D(5)},start+timedelta(hours=1),D(100))
    assert target['amount']==6 and target['energy_kwh']==5 and target['cap_reason']=='target_reached'
    midnight=datetime(2026,9,8,23,30,tzinfo=tz)
    bill=meter({**order,'started_at':midnight},midnight+timedelta(hours=1),D(100))
    assert bill['amount']==17 and len(bill['billing_detail'])==2
    for invalid in [[dict(start_hour=1,end_hour=24,electricity_price=1,service_price=0)],
                    [dict(start_hour=0,end_hour=24,electricity_price=0,service_price=0)]]:
        try:TariffUpdate(periods=invalid)
        except ValueError:pass
        else:raise AssertionError('Invalid tariff accepted')
    print('PASS tariff boundary, midnight, electricity/service fees, target and balance caps')

async def main():
    billing_checks()
    settings=Settings(database_schema="qt_completion_"+uuid4().hex[:12])
    db=Database(settings)
    try:
        await initialize_schema(settings)
        await initialize_schema(settings)
        await db.connect()
        p=db.require_pool()
        accounts=AccountService(p,settings); service=ChargingService(p,settings)
        admin=AdminService(p); console=ConsoleService(p,settings)
        aid,uid,other=uuid4(),uuid4(),uuid4()
        await p.execute("INSERT INTO admin_account(id,username,password_hash) VALUES($1,'completion',$2)",aid,hash_password("completion-test"))
        await p.execute("INSERT INTO app_user(id,phone,nickname,balance) VALUES($1,'13900000001','回归用户',100),($2,'13900000002','另一个用户',100)",uid,other)
        station=await admin.create_station(aid,name="回归站",address="测试地址",longitude=D("114.05"),latitude=D("22.55"),unit_price=D("1.20"),initial_chargers=2)
        await admin.update_station(aid,station['id'],name=None,address="核验地址",longitude=None,latitude=None,unit_price=None,address_verified=True)
        assert (await admin.list_stations())[0]['address_verified']
        vehicle=dict(vehicle_name="测试车辆",vehicle_plate="粤B12345",battery_kwh=D(60),vehicle_soc=D(40),charge_limit=D(70))
        assert (await accounts.save_vehicle(uid,vehicle))['vehicle_plate']=="粤B12345"
        try:await accounts.save_vehicle(other,vehicle)
        except ConflictError:pass
        else:raise AssertionError('Duplicate plate accepted')
        avatar="iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aZ1kAAAAASUVORK5CYII="
        assert (await accounts.save_avatar(uid,avatar))['avatar_data']==avatar
        assert (await AccountService(p,settings).profile(uid))['vehicle_name']=="测试车辆"
        print('PASS repeatable migration, verified station address, persistent vehicle/avatar, unique plate')
        bands=[dict(start_hour=0,end_hour=24,electricity_price="1.00",service_price="0.20")]
        await admin.set_tariff(aid,station['id'],bands)
        charger=(await admin.list_chargers(station['id'],None))[0]['id']
        key=uuid4()
        first,second=await asyncio.gather(service.reserve(uid,charger,key),service.reserve(uid,charger,key))
        assert first['id']==second['id']
        oid=first['id']
        await admin.set_tariff(aid,station['id'],[dict(start_hour=0,end_hour=24,electricity_price="9",service_price="1")])
        await service.start(uid,oid)
        await p.execute("UPDATE charging_order SET started_at=now()-interval '1 hour' WHERE id=$1",oid)
        await service.reconcile_once()
        stopped=await service.get_order(uid,oid)
        assert stopped['status']=='pending_payment' and stopped['amount']==D("21.60") and stopped['energy_kwh']==18,stopped
        assert stopped['billing_detail'][0]['service_price']=="0.20"
        profile=await accounts.profile(uid)
        assert profile['balance']==100 and profile['held_balance']==D("21.60") and profile['vehicle_soc']==70
        assert await p.fetchval("SELECT status FROM charger WHERE id=$1",charger)=='available'
        assert await p.fetchval("SELECT count(*) FROM wallet_entry WHERE order_id=$1",oid)==0
        assert (await accounts.statistics(uid,"all"))['daily']==[]
        try:await accounts.withdraw(uid,D(1),uuid4())
        except ConflictError:pass
        else:raise AssertionError('Withdrawal with unpaid order accepted')
        await asyncio.gather(service.pay(uid,oid),service.pay(uid,oid))
        assert (await accounts.profile(uid))['balance']==D("78.40")
        assert await p.fetchval("SELECT count(*) FROM wallet_entry WHERE order_id=$1",oid)==1
        assert (await accounts.statistics(uid,"month"))['daily'][0]['amount']==D("21.60")
        await p.execute("UPDATE charging_order SET paid_at=now()-interval '40 days' WHERE id=$1",oid)
        assert (await accounts.statistics(uid,"month"))['daily']==[]
        assert (await accounts.statistics(uid,"all"))['daily'][0]['amount']==D("21.60")
        print('PASS concurrent reservation/payment, immutable invoice, no debit before payment, released charger, personal periods')
        key=uuid4()
        a,b=await asyncio.gather(accounts.withdraw(uid,D(10),key),accounts.withdraw(uid,D(10),key))
        assert a['id']==b['id']
        assert (await accounts.profile(uid))['held_balance']==10
        await console.review_withdrawal(aid,a['id'],False,"演示驳回")
        assert (await accounts.profile(uid))['held_balance']==0
        req=await accounts.withdraw(uid,D(10),uuid4(),"演示账户")
        await asyncio.gather(console.review_withdrawal(aid,req['id'],True,"演示到账"),console.review_withdrawal(aid,req['id'],True,"重复审核"))
        assert (await accounts.profile(uid))['balance']==D("68.40")
        assert await p.fetchval("SELECT count(*) FROM wallet_entry WHERE entry_type='withdrawal'")==1
        assert len(await accounts.withdrawals(uid))==2
        print('PASS withdrawal application, rejection release, simulated payout, concurrent review and single ledger entry')
        # Pending withdrawal reduces charge budget; payment does not consume its hold.
        pending=await accounts.withdraw(uid,D(60),uuid4())
        order=await service.reserve(uid,charger,uuid4(),target_soc=D(100))
        await service.start(uid,order['id'])
        await p.execute("UPDATE charging_order SET started_at=now()-interval '1 hour' WHERE id=$1",order['id'])
        await service.reconcile_once()
        ended=await service.get_order(uid,order['id'])
        assert ended['amount']==D("8.40") and ended['stop_reason']=='balance_exhausted'
        await service.pay(uid,order['id'])
        profile=await accounts.profile(uid)
        assert profile['balance']==60 and profile['held_balance']==60
        await console.review_withdrawal(aid,pending['id'],True,"完成演示")
        assert (await accounts.profile(uid))['balance']==0
        print('PASS charge budget respects withdrawal hold; all balances and holds reconcile')
    finally:
        await db.close()
        c=await asyncpg.connect(settings.database_url)
        try:await c.execute(f'DROP SCHEMA IF EXISTS "{settings.database_schema}" CASCADE')
        finally:await c.close()
        print('Disposable schema removed; live data untouched.')

if __name__=="__main__":asyncio.run(main())
