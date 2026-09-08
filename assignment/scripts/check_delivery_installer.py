"""Check source archive boundaries and packaged administrator frontend."""
from pathlib import Path
import tarfile,zipfile
base=Path(__file__).resolve().parents[1]/'deliverables/installer'
with tarfile.open(base/'charging_core-0.1.0.tar.gz') as archive:
 names=archive.getnames()
 for name in names:
  assert not set(Path(name).parts)&{'deliverables','.runtime','.venv','.env','.env.local'},name
 assert any(n.endswith('src/charging_core/static/admin.html') for n in names)
 print('PASS sdist source boundaries: no nested deliverables, private env or runtime')
with zipfile.ZipFile(base/'charging_core-0.1.0-py3-none-any.whl') as archive:
 assert archive.testzip() is None
 for name in ['static/admin.html','static/admin.css','static/admin.js','static/admin-activity.js','static/admin-activity.css','static/assets/admin-xray-car.png','console_routes.py','console_service.py']:
  assert 'charging_core/'+name in archive.namelist(),name
 assert b'new Option' in archive.read('charging_core/static/admin.js')
 print('PASS wheel CRC; administrator pages/assets/endpoints and final custom setting fix included')
