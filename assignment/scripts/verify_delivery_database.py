"""Restore the delivery dump into a disposable database and compare table counts."""
import asyncio, json, os, subprocess, uuid
from pathlib import Path
from urllib.parse import urlparse, unquote
import asyncpg
from charging_core.config import Settings
async def main():
 root=Path(__file__).resolve().parents[1]; settings=Settings(); p=urlparse(settings.database_url)
 name='assignment_restore_'+uuid.uuid4().hex[:12]
 connection=await asyncpg.connect(settings.database_url)
 await connection.execute('CREATE DATABASE '+name)
 env=os.environ.copy(); bin=root/'.runtime/postgresql14/root/usr/lib/postgresql/14/bin'
 env.update(PGHOST=p.hostname,PGPORT=str(p.port or 5432),PGUSER=unquote(p.username or 'postgres'),PGPASSWORD=unquote(p.password or ''),PGDATABASE=name,LD_LIBRARY_PATH=str(root/'.runtime/postgresql14/root/usr/lib/x86_64-linux-gnu')+':'+str(bin.parent/'lib'))
 try:
  subprocess.run([str(bin/'pg_restore'),'--exit-on-error','--no-owner','--no-privileges','--dbname='+name,str(root/'deliverables/database/assignment-database.dump')],env=env,check=True)
  restored=await asyncpg.connect(host=p.hostname,port=p.port or 5432,user=unquote(p.username or 'postgres'),password=unquote(p.password or ''),database=name)
  try:
   expected=json.loads((root/'deliverables/evidence/database-counts.json').read_text())
   for table in ['app_user','station','charger','charging_order','wallet_entry','ops_log','urbanev_station','urbanev_observation','urbanev_source_file','admin_account','console_settings']:
    count=await restored.fetchval('SELECT count(*) FROM charging_core.'+table)
    assert count==expected[table],(table,count,expected[table]); print('PASS restore',table,count)
  finally: await restored.close()
 finally:
  await connection.execute('DROP DATABASE '+name); await connection.close()
asyncio.run(main())
