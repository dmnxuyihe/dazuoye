"""Explicit geocoding and routing with bounded requests, caching and OSM attribution."""
import asyncio
import json
import time
import urllib.parse
import urllib.request

from .errors import ConflictError


class MapService:
    def __init__(self,settings):
        self.settings=settings
        self.lock=asyncio.Lock()
        self.last_request=0.
        self.cache={}
        self.preview_location=None

    async def fetch(self,url):
        if url in self.cache and time.monotonic()-self.cache[url][0]<86400:
            return self.cache[url][1]
        async with self.lock:
            if url in self.cache and time.monotonic()-self.cache[url][0]<86400:
                return self.cache[url][1]
            wait=1.1-(time.monotonic()-self.last_request)
            if wait>0: await asyncio.sleep(wait)
            def request():
                req=urllib.request.Request(url,headers={'User-Agent':'ElectraAssignment/1.1 (+https://lv-l40s-liuzihang.taild6df1c.ts.net/qt/)','Accept-Language':'zh-CN,zh;q=0.9,en;q=0.5'})
                with urllib.request.urlopen(req,timeout=6) as response:
                    return json.loads(response.read(2_000_000))
            try:
                result=await asyncio.to_thread(request)
            except Exception:
                raise ConflictError('地图服务暂不可用，请选择城市或在地图手动定位后重试')
            finally:
                self.last_request=time.monotonic()
            if len(self.cache)>512: self.cache.clear()
            self.cache[url]=(time.monotonic(),result)
            return result

    async def geocode(self,address):
        query=urllib.parse.urlencode({'q':address,'format':'jsonv2','limit':5,'accept-language':'zh-CN','addressdetails':1})
        result=await self.fetch(self.settings.geocoding_url.rstrip('/')+'/search?'+query)
        if not result: raise ConflictError('地址未找到，请补充城市，或选择城市/地图定位')
        return [dict(latitude=float(r['lat']),longitude=float(r['lon']),address=r['display_name']) for r in result]

    async def reverse(self,latitude,longitude):
        query=urllib.parse.urlencode({'lat':latitude,'lon':longitude,'format':'jsonv2','accept-language':'zh-CN','zoom':18})
        result=await self.fetch(self.settings.geocoding_url.rstrip('/')+'/reverse?'+query)
        if not result.get('display_name'): raise ConflictError('该坐标暂无可解析地址，请手动填写')
        return {'address':result['display_name'],'source':'OpenStreetMap · 坐标附近地址，需人工核验'}

    async def route(self,latitude,longitude,to_latitude,to_longitude):
        coords=f'{longitude},{latitude};{to_longitude},{to_latitude}'
        result=await self.fetch(self.settings.routing_url.rstrip('/')+'/route/v1/driving/'+coords+'?overview=full&geometries=geojson&steps=true')
        if result.get('code')!='Ok' or not result.get('routes'): raise ConflictError('未找到可通行道路路线，请更换起点')
        route=result['routes'][0]
        steps=[]
        for leg in route.get('legs',[]):
            for step in leg.get('steps',[]):
                steps.append(dict(name=step.get('name') or '未命名道路',distance=step['distance'],maneuver=step['maneuver'].get('type',''),modifier=step['maneuver'].get('modifier','')))
        return {'geometry':route['geometry'],'distance_m':route['distance'],'duration_s':route['duration'],'steps':steps,'attribution':'© OpenStreetMap contributors · OSRM'}
