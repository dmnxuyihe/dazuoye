"""Create a review ZIP and a private complete snapshot; never serve these archives."""
from pathlib import Path
import hashlib,json,os,stat,tarfile,zipfile
root=Path(__file__).resolve().parents[1];out=root/'deliverables'
# Refuse recursively packaged deliveries or private environment files in installer sdists.
for source in (out/'installer').glob('*.tar.gz'):
 with tarfile.open(source,'r:gz') as archive:
  for member in archive.getmembers():
   parts=Path(member.name).parts
   assert not any(x in parts for x in ('deliverables','.runtime','.venv','.env','.env.local')), member.name
archives={'assignment-review.zip','assignment-full-private.tar.gz','SHA256SUMS','archive-validation.txt','full-file-manifest.json'}
def excluded(path):return path.parent==out and path.name in archives
# Materialize names first to avoid including the archive itself while it grows.
files=[];skipped=[]
for directory,dirs,names in os.walk(root,followlinks=False):
 for name in names:
  p=Path(directory)/name
  if excluded(p):continue
  mode=p.lstat().st_mode
  if stat.S_ISREG(mode) or stat.S_ISLNK(mode):files.append(p)
  else:skipped.append(str(p.relative_to(root)))
 for name in dirs:
  p=Path(directory)/name
  if p.is_symlink():files.append(p)
manifest={'scope':'all regular files and symlinks inside assignment, excluding delivery archives and their manifests','live_database':'raw online database files are non-authoritative; use verified logical dump','skipped_special_files':skipped,'files':[{'path':str(p.relative_to(root)),'size':p.lstat().st_size} for p in sorted(files)]}
(out/'full-file-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2))
# Portable review excludes local secrets, interpreter/runtime caches and old distributions.
review=[]
for p in files:
 rel=p.relative_to(root);parts=rel.parts
 if parts[0] in ('.runtime','.venv','logs','dist'):continue
 if p.name in ('.env','.env.local') or '__pycache__' in parts or p.suffix=='.pyc':continue
 review.append((p,'assignment/'+str(rel)))
for p in (root/'.runtime/urbanev-full').rglob('*'):
 if p.is_file() and (p.is_relative_to(root/'.runtime/urbanev-full/data') or 'manifest' in p.name.lower()):review.append((p,'assignment/data/UrbanEV/'+str(p.relative_to(root/'.runtime/urbanev-full'))))
with zipfile.ZipFile(out/'assignment-review.zip','w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
 for p,name in review:z.write(p,name)
with tarfile.open(out/'assignment-full-private.tar.gz','w:gz',compresslevel=6,dereference=False) as t:
 for p in sorted(files):t.add(p,arcname='assignment/'+str(p.relative_to(root)),recursive=False)
 t.add(out/'full-file-manifest.json',arcname='assignment/deliverables/full-file-manifest.json')
with zipfile.ZipFile(out/'assignment-review.zip') as z:
 assert z.testzip() is None
 assert not any(n.endswith('/.env') or n.endswith('/.env.local') for n in z.namelist())
 assert sum(n.startswith('assignment/data/UrbanEV/data/') and not n.endswith('/') for n in z.namelist())==23
 zip_count=len(z.namelist())
with tarfile.open(out/'assignment-full-private.tar.gz','r:gz') as t:
 members=t.getmembers();assert len(members)==len(files)+1
 for member in members:
  if member.isfile():
   stream=t.extractfile(member)
   while stream.read(1024*1024):pass
lines=[]
for name in ['assignment-review.zip','assignment-full-private.tar.gz']:
 h=hashlib.sha256()
 with (out/name).open('rb') as f:
  for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
 lines.append(h.hexdigest()+'  '+name)
(out/'SHA256SUMS').write_text('\n'.join(lines)+'\n')
(out/'archive-validation.txt').write_text(f'PASS review ZIP CRC: {zip_count} entries; no .env/.env.local; all 23 UrbanEV files\nPASS private TAR/GZIP read all contents: {len(members)} entries\nPASS SHA256 generated for both archives\nSpecial files excluded: {len(skipped)}\n')
print((out/'archive-validation.txt').read_text())
