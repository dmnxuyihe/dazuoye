"""Fetch all data/ files at the audited UrbanEV commit, preserving a manifest."""
import concurrent.futures
import hashlib
import json
import sys
import urllib.request
from pathlib import Path

COMMIT = '44f2aa0c8d89f192bce00bafb0def74a21b39c68'
REPOSITORY = 'IntelligentSystemsLab/UrbanEV'
root = Path(sys.argv[1] if len(sys.argv)>1 else '.runtime/urbanev-full')
root.mkdir(parents=True, exist_ok=True)
with urllib.request.urlopen(f'https://api.github.com/repos/{REPOSITORY}/git/trees/{COMMIT}?recursive=1', timeout=60) as response:
    tree = json.load(response)
assert not tree.get('truncated'), 'Incomplete GitHub tree'
(root/'tree.json').write_text(json.dumps(tree))
files = [x for x in tree['tree'] if x['path'].startswith('data/') and x['type']=='blob']
def download(item):
    path = root/item['path']
    path.parent.mkdir(parents=True,exist_ok=True)
    if not path.exists() or path.stat().st_size != item['size']:
        with urllib.request.urlopen(f'https://raw.githubusercontent.com/{REPOSITORY}/{COMMIT}/{item["path"]}', timeout=90) as response:
            content=response.read()
        assert len(content)==item['size'], f'Incomplete download: {path}'
        path.write_bytes(content)
    content=path.read_bytes()
    # Git blob hash validates cached files too, against the pinned upstream tree.
    digest=hashlib.sha1(f'blob {len(content)}\0'.encode()+content).hexdigest()
    assert digest==item['sha'], f'Git blob mismatch: {path}; remove this cached file and retry'
    return dict(path=item['path'],bytes=len(content),sha256=hashlib.sha256(content).hexdigest())
with concurrent.futures.ThreadPoolExecutor(max_workers=5) as executor:
    manifest=list(executor.map(download,files))
(root/'manifest.json').write_text(json.dumps(dict(commit=COMMIT,files=manifest),indent=2))
print('Verified',len(manifest),'files;',sum(x['bytes'] for x in manifest),'bytes')
