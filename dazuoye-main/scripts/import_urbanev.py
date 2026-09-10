"""Import the pinned UrbanEV GitHub data archive and aligned hourly observations.
Usage: .venv/bin/python scripts/import_urbanev.py .runtime/urbanev-full
Existing operational stations, orders and accounts are never modified.
"""
import asyncio, csv, hashlib, json, sys
from collections import defaultdict, Counter
from datetime import datetime
from pathlib import Path
import asyncpg
from charging_core.config import Settings
from charging_core.db import initialize_schema

PINNED_COMMIT = '44f2aa0c8d89f192bce00bafb0def74a21b39c68'


def load_or_create_manifest(root):
    manifest_path = root / 'manifest.json'
    if manifest_path.exists():
        return json.loads(manifest_path.read_text())
    # GitHub's Download ZIP does not include the downloader-generated manifest.
    # Build an equivalent local integrity list so manual downloads can be imported.
    files = []
    for path in sorted((root / 'data').rglob('*')):
        if path.is_file():
            content = path.read_bytes()
            files.append({
                'path': path.relative_to(root).as_posix(),
                'bytes': len(content),
                'sha256': hashlib.sha256(content).hexdigest(),
            })
    if not files:
        raise FileNotFoundError(f'UrbanEV data files not found under {root / "data"}')
    manifest = {'commit': PINNED_COMMIT, 'files': files, 'source': 'manual-github-zip'}
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print('Created local manifest for', len(files), 'manually downloaded files')
    return manifest


async def main(root):
    settings = Settings()
    await initialize_schema(settings)
    db = await asyncpg.connect(settings.database_url, server_settings={'search_path': settings.database_schema})
    manifest = load_or_create_manifest(root)
    try:
        existing = await db.fetchval("SELECT metadata FROM dataset_metadata WHERE name='UrbanEV-full'")
        if existing and json.loads(existing)['commit'] == manifest['commit']:
            print('Already imported commit', manifest['commit']); return
        stations = list(csv.DictReader((root/'data/inf.csv').open()))
        capacities = Counter()
        for s in stations: capacities[int(s['TAZID'])] += int(s['charge_count'])
        names = ['occupancy','duration','volume','volume-11kW','e_price','s_price']
        handles = [(root/f'data/{n}.csv').open() for n in names]
        readers = [csv.reader(f) for f in handles]
        headers = [next(r) for r in readers]
        assert all(h == headers[0] for h in headers), 'Metric zone headers differ'
        zones = list(map(int, headers[0][1:]))
        daily = defaultdict(list); hourly = defaultdict(list); zone_energy = Counter(); observations = 0; batch=[]
        async with db.transaction():
            await db.execute('TRUNCATE urbanev_observation, urbanev_source_file, urbanev_station')
            for item in manifest['files']:
                content = (root/item['path']).read_bytes()
                assert len(content)==item['bytes'] and hashlib.sha256(content).hexdigest()==item['sha256']
                await db.execute('INSERT INTO urbanev_source_file VALUES ($1,$2,$3,$4,$5)',item['path'],manifest['commit'],item['sha256'],len(content),content)
            await db.copy_records_to_table('urbanev_station',records=[(int(s['station_id']),int(s['TAZID']),float(s['longitude']),float(s['latitude']),int(s['charge_count'])) for s in stations])
            import itertools, math
            for aligned in itertools.zip_longest(*readers):
                assert all(aligned), 'Metric row counts differ'
                stamp=aligned[0][0]; assert all(r[0]==stamp and len(r)==len(headers[0]) for r in aligned)
                dt=datetime.fromisoformat(stamp)
                values=[[float(v) if v and math.isfinite(float(v)) else None for v in r[1:]] for r in aligned]
                energy=sum(v or 0 for v in values[2]); occupied=sum(v or 0 for v in values[0])
                sample=[energy,occupied,sum(v or 0 for v in values[4])/len(zones),sum(v or 0 for v in values[5])/len(zones)]
                daily[stamp[:10]].append(sample); hourly[dt.hour].append(sample)
                for i,z in enumerate(zones):
                    batch.append((z,dt,*[v[i] for v in values])); zone_energy[z]+=values[2][i] or 0
                observations += len(zones)
                if len(batch)>=50000:
                    await db.copy_records_to_table('urbanev_observation',records=batch); batch=[]
            if batch: await db.copy_records_to_table('urbanev_observation',records=batch)
            for f in handles: f.close()
            days=[dict(date=d,open=v[0][0],close=v[-1][0],high=max(x[0] for x in v),low=min(x[0] for x in v),energy=sum(x[0] for x in v),occupied=sum(x[1] for x in v)/len(v)) for d,v in daily.items()]
            profile=[dict(hour=h,energy=sum(x[0] for x in v)/len(v),occupancy=sum(x[1] for x in v)/len(v)/sum(capacities.values())*100,price=sum(x[2]+x[3] for x in v)/len(v)) for h,v in sorted(hourly.items())]
            poi=Counter(r['primary_types'] for r in csv.DictReader((root/'data/poi.csv').open()))
            weather=[dict(time=r['time'],temperature=float(r['T'])) for r in csv.DictReader((root/'data/weather_central.csv').open()) if r['T']][-168:]
            result=dict(commit=manifest['commit'],files=len(manifest['files']),bytes=sum(x['bytes'] for x in manifest['files']),stations=len(stations),chargers=sum(capacities.values()),zones=len(zones),observations=observations,start=days[0]['date'],end=days[-1]['date'],daily=days,hourly=profile,ranking=[dict(zone=z,energy=e,capacity=capacities[z]) for z,e in zone_energy.most_common()],poi=poi,weather=weather)
            await db.execute("INSERT INTO dataset_metadata(name,metadata) VALUES('UrbanEV-full',$1::jsonb) ON CONFLICT(name) DO UPDATE SET metadata=EXCLUDED.metadata,imported_at=now()",json.dumps(result,allow_nan=False))
        print(json.dumps({k:v for k,v in result.items() if k not in ['daily','hourly','ranking','poi','weather']}))
    finally: await db.close()

asyncio.run(main(Path(sys.argv[1])))
