# 在线地图接入（2026-09-07）

按用户最新要求，地图区域改为 Qt WebEngine 嵌入网页地图，其余 UI、认证、订单、钱包和缓存继续使用原有 C++ / Qt Widgets 代码。此说明取代早期“客户端完全不使用 WebEngine”的描述。

用户提供的 https://www.openstreetmap.net.cn/ 实际是 OSM 爱好者介绍站，未提供交互地图。本实现将本地 Leaflet 页面嵌入 QWebEngineView，加载 https://tile.openstreetmap.org/{z}/{x}/{y}.png 的 OSM 标准在线瓦片。完整道路、建筑、水域和地名由底图提供，按视野/缩放加载，不再使用旧版局部 GeoJSON 自绘路网。

Qt 向地图传递经过筛选的站点 ID、名称和经纬度。Qt WebChannel 仅公开站点选择回调，C++ 校验 ID 后发出原有 stationSelected 信号，打开现有站点详情；不向网页传递 token、钱包或订单写接口。地图使用独立缓存 profile，网络资源仅允许 OSM 瓦片，禁用网页弹窗。

支持拖动、缩放、站点气泡、当前站点区域/全部站点、比例尺、重新加载和瓦片失败提示。当前区域按照所选站点周围 100km 适配，属于视野分组，不是行政区边界。OSM 原始标准配色保留，站点及按钮采用紫色主题。订单页的“充电行程”仍是已标注的示意图，不是轨迹或导航。

保留 OSM 署名；使用正常 HTTP 磁盘缓存及可识别客户端 User-Agent，不批量预取或下载离线瓦片。在线底图依赖网络和第三方服务可用性，公开演示不代表第三方地图服务无限流量承诺。政策：https://operations.osmfoundation.org/policies/tiles/ 。

构建新增 Qt WebEngineWidgets / WebChannel 模块。Linux 发布包附 QtWebEngineProcess、resources、locales 和相关动态库。公网仍在 assignment-qt-public 独立 tmux 会话中运行。当前功能由 test-map 检查加载、站点回调、无效 ID 过滤、缩放、跨城市适配；设置 ELECTRA_TEST_ONLINE_MAP=1 时额外检查实际瓦片加载。生产交易完整验收和 Windows 运行未执行。
