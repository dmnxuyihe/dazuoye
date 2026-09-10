"""Run native Qt integration tests against a fresh, isolated schema. Never reset live data."""
import asyncio
import os
from pathlib import Path
import subprocess
import time
import urllib.request
import uuid
import asyncpg
from charging_core.config import Settings

ROOT=Path(__file__).resolve().parents[1]
async def main():
    settings=Settings()
    schema='charging_qt_'+uuid.uuid4().hex[:12]
    env={**os.environ,'CHARGING_DATABASE_SCHEMA':schema,'CHARGING_ADMIN_CONSOLE_ENABLED':'true','CHARGING_DEMO_ADMIN_ENABLED':'true','CHARGING_ENVIRONMENT':'development','CHARGING_DEV_OTP_CODE':'246810','CHARGING_LOG_DIR':str(ROOT/'.runtime/qt-migration/test-logs')}
    db=await asyncpg.connect(settings.database_url)
    server=None
    (ROOT/'.runtime/qt-migration').mkdir(parents=True, exist_ok=True)
    with (ROOT/'.runtime/qt-migration/test-server.log').open('w') as log:
        try:
            subprocess.run([str(ROOT/'.venv/bin/charging-core'),'init-db'],cwd=ROOT,env=env,check=True,stdout=log,stderr=log)
            # Bind a dedicated port; fail on collision instead of reaching another service.
            import socket
            with socket.socket() as probe:
                probe.bind(('127.0.0.1',0)); port=probe.getsockname()[1]
            server=subprocess.Popen([str(ROOT/'.venv/bin/charging-core'),'serve','--host','127.0.0.1','--port',str(port)],cwd=ROOT,env=env,stdout=log,stderr=log)
            base=f'http://127.0.0.1:{port}'
            for _ in range(100):
                if server.poll() is not None:raise RuntimeError('Isolated server exited')
                try:
                    with urllib.request.urlopen(base+'/health',timeout=1) as r:
                        if r.status==200:break
                except OSError:time.sleep(.1)
            else:raise RuntimeError('Isolated server did not become healthy')
            platform=os.environ.get('ELECTRA_TEST_PLATFORM','offscreen')
            command=[str(ROOT/'.runtime/qt-build/test-desktop'),'-platform',platform]
            if os.environ.get('ELECTRA_DEBUG_CRASH'):
                command=[str(ROOT/'.runtime/qt-deps/usr/bin/gdb'),'-batch','-ex','run','-ex','bt','--args',*command]
            subprocess.run(command,cwd=ROOT,env={**env,'LD_LIBRARY_PATH':str(ROOT/'.runtime/qt/6.5.3/gcc_64/lib')+':'+str(ROOT/'.runtime/qt-deps/usr/lib/x86_64-linux-gnu')+':'+str(ROOT/'.runtime/qt-public/root/usr/lib/x86_64-linux-gnu'),'QT_PLUGIN_PATH':str(ROOT/'.runtime/qt/6.5.3/gcc_64/plugins'),'QTWEBENGINE_CHROMIUM_FLAGS':'--disable-gpu --no-sandbox','QT_QPA_PLATFORM':platform,'ELECTRA_TEST_API':base},check=True,timeout=150)
        finally:
            if server:
                server.terminate()
                try:server.wait(timeout=10)
                except subprocess.TimeoutExpired:server.kill();server.wait()
            await db.execute(f'DROP SCHEMA IF EXISTS "{schema}" CASCADE')
            await db.close()
            print('Isolated Qt test schema removed; live business data untouched.')

if __name__=='__main__':asyncio.run(main())
