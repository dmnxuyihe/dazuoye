import urllib.request,urllib.parse,json
from pathlib import Path
query='[out:json][timeout:40];way["highway"~"^(motorway|trunk|primary|secondary|tertiary)$"](31.15,121.38,31.30,121.58);out geom;'
request=urllib.request.Request('https://overpass-api.de/api/interpreter',data=urllib.parse.urlencode({'data':query}).encode(),headers={'User-Agent':'Electra-assignment/1.0 offline map asset'})
with urllib.request.urlopen(request,timeout=55) as r: source=json.load(r)
features=[]
for way in source['elements']:
 if len(way.get('geometry',[]))<2:continue
 features.append({'type':'Feature','properties':{'highway':way['tags']['highway']},'geometry':{'type':'LineString','coordinates':[[round(p['lon'],6),round(p['lat'],6)] for p in way['geometry']]}})
Path('desktop/assets/shanghai-roads.geojson').write_text(json.dumps({'type':'FeatureCollection','features':features},separators=(',',':')))
print(len(features),'road segments')
