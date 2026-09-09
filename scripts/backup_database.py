"""Create a consistent logical backup of the assignment schema without logging credentials."""
import json, os, subprocess
from pathlib import Path
from urllib.parse import urlparse, unquote
from charging_core.config import Settings
settings=Settings()
root=Path(__file__).resolve().parents[1]
out=root/'deliverables/database';out.mkdir(parents=True,exist_ok=True)
parsed=urlparse(settings.database_url)
env=os.environ.copy()
for name,value in {'PGHOST':parsed.hostname,'PGPORT':str(parsed.port or 5432),'PGUSER':unquote(parsed.username or 'postgres'),'PGPASSWORD':unquote(parsed.password or ''),'PGDATABASE':unquote(parsed.path.lstrip('/'))}.items():env[name]=value or ''
bin_dir=root/'.runtime/postgresql14/root/usr/lib/postgresql/14/bin'
env['LD_LIBRARY_PATH']=str(root/'.runtime/postgresql14/root/usr/lib/x86_64-linux-gnu')+':'+str(bin_dir.parent/'lib')
subprocess.run([str(bin_dir/'pg_dump'),'--format=custom','--no-owner','--no-privileges','--schema='+settings.database_schema,'--file='+str(out/'assignment-database.dump')],env=env,check=True)
with (out/'database-contents.txt').open('w') as log:subprocess.run([str(bin_dir/'pg_restore'),'--list',str(out/'assignment-database.dump')],env=env,stdout=log,check=True)
print('Database backup complete:',out/'assignment-database.dump')
