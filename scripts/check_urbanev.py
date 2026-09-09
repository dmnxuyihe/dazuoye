"""Read-only import integrity and demo authorization acceptance checks."""
import asyncio, hashlib, json, sys, urllib.request, urllib.error
import asyncpg
from charging_core.config import Settings
base=sys.argv[1] if len(sys.argv)>1 else 'http://127.0.0.1:4174'
def request(path,method='GET',token=None,body=None):
    headers={'Content-Type':'application/json'}
    if token:headers['Authorization']='Bearer '+token
    req=urllib.request.Request(base+path,method=method,headers=headers,data=json.dumps(body).encode() if body is not None else None)
    try:
        with urllib.request.urlopen(req) as response:return response.status,json.load(response)
    except urllib.error.HTTPError as error:return error.code,json.load(error)
async def check():
    settings=Settings();db=await asyncpg.connect(settings.database_url,server_settings={'search_path':settings.database_schema})
    try:
        rows=await db.fetch('SELECT path,sha256,byte_count,content FROM urbanev_source_file')
        assert len(rows)==23
        for row in rows:assert len(row['content'])==row['byte_count'] and hashlib.sha256(row['content']).hexdigest()==row['sha256']
        assert await db.fetchval('SELECT count(*) FROM urbanev_station')==1362
        assert await db.fetchval('SELECT sum(capacity) FROM urbanev_station')==17532
        assert await db.fetchval('SELECT count(*) FROM urbanev_observation')==1194600
        assert await db.fetchval('SELECT count(DISTINCT observed_at) FROM urbanev_observation')==4344
        assert await db.fetchval('SELECT count(DISTINCT zone_id) FROM urbanev_observation')==275
        status,session=request('/auth/demo','POST');assert status==200 and session['role']=='demo_admin' and session['read_only']
        token=session['access_token'];status,data=request('/demo/analytics',token=token)
        assert status==200 and data['ready'] and len(data['daily'])==181 and len(data['hourly'])==24
        total=await db.fetchval('SELECT sum(energy_kwh) FROM urbanev_observation')
        assert abs(total-sum(d['energy'] for d in data['daily']))<1
        assert request('/demo/analytics')[0]==401
        assert request('/admin/users',token=token)[0]==403
        assert request('/admin/orders',token=token)[0]==403
        assert request('/admin/users/00000000-0000-0000-0000-000000000001/status','PATCH',token,{'status':'frozen'})[0]==403
        print('PASS: 23 complete source archives and hashes; 1,194,600 rows / 4,344 hours / 275 zones; station capacities; aggregate energy; automatic read-only demo session; protected admin reads and writes denied.')
    finally:await db.close()
asyncio.run(check())
