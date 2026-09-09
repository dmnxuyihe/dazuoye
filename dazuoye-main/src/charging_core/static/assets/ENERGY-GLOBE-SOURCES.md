# 地球视觉素材来源

`energy-globe.svg` 是本项目生成的本地矢量背景，包含球面投影、经纬线、陆地点阵、中国省界、光晕与轨道碎片。

地理数据输入：

- 世界边界：Natural Earth，https://github.com/nvkelso/natural-earth-vector/blob/master/geojson/ne_110m_admin_0_countries.geojson
- 中国省界：阿里云 DataV，https://geo.datav.aliyun.com/areas_v3/bound/100000_full.json

为了匹配参考图的视觉重心，中国省界经球面投影后做了局部放大，不用于测绘或地理距离计算。UI 构图依据用户提供的 Dribbble 参考截图；未将整张参考截图用作页面背景。

`shenzhen-region.svg` 的行政区划来源：https://geo.datav.aliyun.com/areas_v3/bound/440300_full.json。地图与站点采用相同经纬度到视图坐标映射，区名位置取数据中的 centroid/center。
