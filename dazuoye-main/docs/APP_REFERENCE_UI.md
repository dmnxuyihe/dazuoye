# APP 参考图复刻（2026-09-06）

仅修改 assignment 的移动端。以用户提供的两张 Electra 截图为视觉依据，保留后端业务接口和三张大屏。

- 首页：车辆名称、立体电池、电量、里程/续航卡、右侧裁切车头、附近站点地图与底栏。
- 站点页：紫黑道路地图、充电桩图钉、底部站点卡、路线和充电枪入口。
- 充电设置：俯视车辆、Eco/Fast 模式筛选、演示充电上限、充电枪折叠选择、费用预估和预约按钮。
- 充电页：紫色电光环、百分比、订单指标及操作按钮。车辆电量/续航为明确标注的演示数据；真实订单电量和金额只读取后端，零值不替换为虚构增长。
- 统计与分时用电：读取现有 `/auth/demo` 和 `/demo/analytics` 的 UrbanEV 历史观测，不冒充个人订单。月度为末 31 天，年度入口显示现有全部 181 天；高/中/低时段按历史平均综合电价范围三等分。个人订单入口保留。

主要文件：`mobile.html`、`mobile.js`、`mobile-reference.css`、`mobile-reference.js`。原 `mobile.css` 和共享 `styles.css` 未修改。界面修改前备份保存在 `.runtime/app-reference/before/`。

## 素材

- `assets/ev-photo.png`、`ev-top-photo.png`、`charging-plasma.png`：通过 imagegen 为本项目生成的透明背景素材，不包含参考截图中的 UI。
- `assets/shenzhen-roads.geojson`：2026-09-06 从 OpenStreetMap Overpass API 下载深圳范围内 motorway/trunk/primary/secondary/tertiary 道路，合并成分类型 MultiLineString，并以约 13 米容差简化，约 1.5 MB。覆盖框：纬度 22.43–22.83，经度 113.78–114.62。保留道路位置用于视觉路网，完整路线仍打开 OSM 导航。
- 数据许可与署名：© [OpenStreetMap contributors](https://www.openstreetmap.org/copyright)，[ODbL](https://opendatacommons.org/licenses/odbl/)。地图内显示署名。获取脚本在 `.runtime/app-reference/fetch-roads.py`；项目保存已处理数据。读取失败时回退到现有 OSM 瓦片。

## 验收

`.runtime/app-reference/acceptance.cjs` 使用 Playwright 验证六个页面、390×844 / 360×740 / 430×932 / 1280×900、地图搜索、充电枪筛选与选择、上限费用预估、未登录预约入口、统计周期和分时筛选。

真实预约接口不在自动 UI 验收中写入数据。单独浏览器上下文用接口 fixture 验证预约→开始→轮询→结束→订单页，并检查后端 0 电量/0 金额的显示，避免制造真实订单。

截图在 `.runtime/app-reference/final-*.png`。公网入口沿用 Tailscale Funnel 的 `/ui/mobile.html`。

最终验证结果：本地及公网 Funnel（使用 Cloudflare 解析得到的地址绑定原域名，保留 HTTPS 校验）均通过上述完整验收。验收时系统 DNS / Google DNS 对原域名返回 NXDOMAIN，Cloudflare 返回正常 A 记录；这是当前公网域名解析的限制，不能把指定地址通过等同于所有 DNS 已恢复。Funnel 原端口 8443 配置已重确认，未修改其他公开服务。
