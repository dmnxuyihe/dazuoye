"""Concurrency/settlement acceptance on the isolated charging_defense_test schema only."""
import asyncio, concurrent.futures, json, os, urllib.request, urllib.error, uuid
from decimal import Decimal
import asyncpg
from charging_core.config import Settings
from charging_core.service import ChargingService
BASE='http://127.0.0.1:4175'
def call(path,method='GET',body=None,token=None):
 headers={'Content-Type':'application/json'}
 if token:headers['Authorization']='Bearer '+token
 request=urllib.request.Request(BASE+path,method=method,headers=headers,data=json.dumps(body).encode() if body is not None else None)
 try:
  with urllib.request.urlopen(request,timeout=15) as response:return response.status,json.load(response)
 except urllib.error.HTTPError as error:return error.code,json.load(error)
def ok(*args,**kwargs):
 status,data=call(*args,**kwargs);assert status in (200,201), (status,data);return data
async def main():
 settings=Settings();assert settings.database_schema=='charging_defense_test','Refusing to alter non-test schema'
 pool=await asyncpg.create_pool(settings.database_url,server_settings={'search_path':settings.database_schema})
 admin=ok('/auth/admin/login','POST',{'username':'admin','password':'123456'})['access_token']
 users=[]
 for i in range(2):
  phone='1'+str(uuid.uuid4().int)[-10:]
  ok('/auth/otp/request','POST',{'phone':phone})
  token=ok('/auth/otp/verify','POST',{'phone':phone,'code':settings.dev_otp_code})['access_token']
  ok('/wallet/recharges','POST',{'amount':'5.00','idempotency_key':str(uuid.uuid4())},token)
  users.append(token)
 station=ok('/admin/stations','POST',{'name':'并发验收站','address':'隔离测试','longitude':'114.05','latitude':'22.54','unit_price':'1.00'},admin)
 chargers=[ok('/admin/chargers','POST',{'station_id':station['id'],'code':'DEF-'+uuid.uuid4().hex[:12],'kind':'fast','power_kw':'60'},admin) for _ in range(2)]
 def reserve(token,charger):return call('/orders','POST',{'charger_id':charger['id'],'idempotency_key':str(uuid.uuid4())},token)
 with concurrent.futures.ThreadPoolExecutor(2) as executor:
  results=list(executor.map(lambda token:reserve(token,chargers[0]),users))
 assert sorted(code for code,_ in results)==[200,409],results
 winner=next(i for i,r in enumerate(results) if r[0]==200);order=results[winner][1]
 print('PASS two users / one charger: exactly one reservation succeeds, one conflict')
 assert reserve(users[winner],chargers[1])[0]==409
 print('PASS one user cannot hold a second active order')
 ok('/orders/'+order['id']+'/start','POST',token=users[winner])
 await pool.execute("UPDATE charging_order SET started_at=now()-interval '1 hour' WHERE id=$1",uuid.UUID(order['id']))
 await ChargingService(pool,settings).reconcile_once()
 ended=ok('/orders/'+order['id'],token=users[winner]);assert ended['status']=='completed' and Decimal(ended['balance'])==0 and Decimal(ended['amount'])==5
 repeated=ok('/orders/'+order['id']+'/stop','POST',token=users[winner]);assert repeated['amount']==ended['amount']
 assert await pool.fetchval('SELECT count(*) FROM wallet_entry WHERE order_id=$1',uuid.UUID(order['id']))==1
 print('PASS balance exhaustion auto-settles at 5.00, balance remains zero; repeated stop creates no second debit')
 remaining=users[1-winner];_,reserved=reserve(remaining,chargers[1]);await pool.execute("UPDATE charging_order SET reserved_until=now()-interval '1 second' WHERE id=$1",uuid.UUID(reserved['id']))
 await ChargingService(pool,settings).reconcile_once()
 expired=ok('/orders/'+reserved['id'],token=remaining);assert expired['status']=='cancelled'
 assert await pool.fetchval('SELECT status FROM charger WHERE id=$1',uuid.UUID(chargers[1]['id']))=='available'
 print('PASS expired reservation cancels and releases charger')
 await pool.close()
asyncio.run(main())
