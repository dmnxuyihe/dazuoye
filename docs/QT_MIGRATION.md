# Qt 当前实现与验收基线

更新日期：2026-09-09。本文件是 Qt 范围、数据映射和验收状态的当前权威说明。其他带日期的 QT_* 文档保留历史记录，其中“跳过验收”、演示固定值、旧地图实现等描述不代表当前版本。

## 范围与架构

交付用户端 `electra-user` 和管理端 `electra-admin`。HTML/CSS 提供已通过的视觉先验；业务页面使用 C++17、Qt 6.5.3 Widgets、QPainter 与 Qt Charts。仅地图允许 Qt WebEngine + Leaflet + OpenStreetMap。运营三屏不在此次迁移范围。

Qt → QNetworkAccessManager REST / QWebSocket → FastAPI → PostgreSQL。PostgreSQL 是业务事实来源；Qt 的 SQLite 仅存身份隔离的带时间戳只读快照及偏好，不保存登录令牌，也不接受离线交易。沿用现有后端与数据库结构。

指标选择顺序：优先合理映射已有数据；无法映射且删除将破坏布局时补齐业务；原业务难以补齐时改用容易实现的新指标。本轮所需指标均可由既有接口支持，无需新增表或构造演示订单填充图表。

## 视觉组件与真实数据

| 页面 / 组件 | 当前数据与行为 |
| --- | --- |
| 用户首页车辆 / 电池 / 两个读数卡 | `/me` 的车型、容量、充电上限；活动订单的初始 SOC、电量及容量推导电量。无数据显示“—”，不按固定系数虚构续航。车辆插画为装饰，SOC 沿用服务端订单模拟。 |
| 用户站点地图与详情 | `/public/stations`、站点接口与分时价格；实际坐标、搜索、模式筛选、充电上限和预约。未登录的电量与金额预估显示登录提示。 |
| 充电能量环 / 金额 / 倒计时 | `/orders/{id}` 的预约、充电、待支付及终态；启停、付款均经真实 API。金额为零时仍显示零。 |
| 个人统计 | `/me/statistics?period=month/all` 全量已支付个人订单。业务事件或定时刷新重新读取数据，原图表保持，不因后台刷新切回页面。 |
| 分时用电 | 所选站点当前 tariff；24 小时价格图、全天/最低价/最高价筛选及电费/服务费明细。不是 UrbanEV 历史电价。 |
| 钱包 / 资料 / 历史 | 现有账户、钱包流水、车辆/头像、提现、订单接口；用户历史列表仍限最近 200 条，统计不受该页限制。 |
| 管理首页透视车辆卡 | 已预约和充电设备占全部设备比例，来自 `/admin/chargers`；下方显示全站点、设备及占用数量。 |
| 管理首页双弧仪表 | 外弧为设备可用率，内弧为充电率，分母为全部设备；没有设备时显示空态。 |
| 管理首页营收流带 | `/admin/stats/summary` 的今日/月/总营收；`/admin/stats/revenue?days=7/30` 按日期连续分为三个区间并求和绘图，显示准确日期、金额及已支付订单数。不制造分组占比或最低柱高。 |
| 管理站点 | 当前站点空闲接口比例及站点/电桩维护；地图来自真实坐标，可用“定位 / 路线”工具解析地址、定位和规划路线。 |
| 管理订单 | 当前页订单电量、金额、状态及启停操作；原虚构行程示意改为该订单的真实状态流程。 |
| 管理审计 | 真实操作日志时间线、类别统计和热力图；日期/小时统一北京时间，明确当前页统计范围。 |
| 负荷预测 | 保留已有模型产物和接口；历史实验数据，不代表当前实时负荷。 |

首页恢复车辆插画、双弧仪表、地图、营收流带与站点列表的两排结构。固定图形区域的合理高度，避免伸展为超高空卡；窄管理窗口允许纵向滚动。用户端保留电池、裁切车辆、紫色能量环、分段选择和底部导航；最小宽度 360 px。地图采用紫色底图样式，紧凑卡片隐藏地址工具，完整地图保留可展开的定位/路线工具及地图署名。

## 同步规则

- WebSocket 事件触发有界延迟刷新，连续事件不会不断重置计时器导致刷新饥饿；两端增加 5 秒快照轮询补充事件同步，用户活动订单仍每 3 秒读取。
- 首页账户字段直接更新控件；个人统计重新读取已支付数据并更新原图表；管理首页刷新保留图表周期和站点筛选。
- 管理员其他页仅在相关快照变化时重建；预测页不被轮询反复重置。会话失效或退出时清理身份数据及未完成刷新上下文。
- 网络失败不能当作新业务成功。缓存显示时间戳，只读；部分快照失败有提示。

## 弹窗适配（2026-09-09 补充）

两端共同使用 `desktop/src/dialogs.cpp` 的应用级弹窗策略：根据所属主窗口与屏幕可用范围约束尺寸和位置；带统一标题栏的业务弹窗将内容放入滚动区，标题关闭按钮及末尾保存/关闭按钮固定保留。窄窗口中的表单改为标签与输入框上下排列，宽表格通过自身横向滚动查看完整字段。

文件选择使用 Qt 文件对话框，窄窗口隐藏可选侧栏/工具按钮并将打开、保存、取消按钮放到下方，保留文件模型、选择验证和覆盖确认。深色调色板及列表、表格角落、横向滚动条样式统一，避免白底浅字。

该类问题的回归入口是 `test-dialogs`（CTest `dialog-fit`），覆盖提现记录、文件选择、360 像素宽长表单、标准确认/输入框、文件打开与导出路径以及主题对比度。补充证据位于 `.runtime/qt-dialog-fit-20260909/`，包括修改前失败记录、460×900 X11 显示环境截图、构建及回归结果；先前页面与数据库证据保留在下节目录中。

## 电桩选择与头像编辑（2026-09-09 补充）

用户端选桩页采用车辆信息、全部电桩卡片、费用预估和预约操作的顺序。电桩卡片直接读取 `/public/stations/{id}/chargers` 的编号、类型、功率、状态和 `total_sessions`（充电停止结算时累计一次，取消预约不计数），每 3 秒刷新。空闲、充电中、预约、故障分别使用绿、橙、蓝、红边框；忙碌/故障及离线缓存不可预约。已选桩变为不可用时清空选择，需重新选桩，服务端仍最终校验并发预约。车辆电量为账户/服务端模拟电量；未接入车辆里程数据，车辆卡保留电池容量，不编造续航。

头像编辑为原生 Qt：导入 PNG/JPEG 并应用 EXIF 方向、拖动方形裁剪、缩放、旋转、水平翻转、亮度和重置；圆形虚线提示实际头像显示范围。三张示例复用已有车辆/充电素材。保存的是处理后的 256×256 PNG，通过 `/me/avatar` 写入 PostgreSQL，取消不保存。`test-editing` 覆盖图像像素变化、状态变更后选桩失效、离线禁用和窄屏布局；真实集成测试另验证编辑→保存→API 读回的像素一致性。

本次 Linux 构建、5 组 CTest、X11 编辑测试（5 项，含初始化/清理）及隔离数据库集成测试（5 项，含初始化/清理）通过。可随源码查看 [Qt 回归日志](qt-current/ctest.log)、[X11 编辑日志](qt-current/editing-x11.log)、[数据库集成日志](qt-current/integration.log)、[选桩页面](qt-current/user-station.png)、[四种状态卡片](qt-current/charger-cards.png) 和 [头像编辑器](qt-current/avatar-editor.png)。详细本地证据位于 `.runtime/qt-cards-avatar/`。Windows 未构建运行。

仓库沿用原远端提交历史，将源码提升至根目录；旧交付二进制、压缩包、数据库导出和演示材料保留在历史提交中。当前 `.env`、运行环境、数据库和本地交付包不纳入 Git。源码空白检查通过；第三方 Leaflet CSS 与许可证保留上游 CRLF，不做格式改写。

## 前次整体改造验证

证据目录：`.runtime/qt-rebuild-20260909T031834Z/`。源码基线为 `source-before.tar.gz` 和 `sha256-before.json`，变更审阅为 `changes.patch`。该工作区无 Git 元数据。

- Linux 两端和测试目标编译；`build.log`。
- Qt 原生页面、布局边界、首页账户更新、统计事件刷新、弹窗及导航：`test-visuals`，9 项通过（含初始化/清理）。
- 实际 API 与隔离 PostgreSQL schema：`scripts/check_qt_integration.py`，覆盖 UI 登录充值、车辆跨端更新、预约→开始→停止→待支付→付款、管理端自动感知设备占用/释放、管理用户新增/冻结、另一客户端付款更新已打开的统计页，以及新客户端读取持久化账单。
- `test-client`：5 项通过，含缓存身份隔离、中文错误、离线只读。
- `test-map`：3 项通过，包含实际 WebEngine 选点/缩放；本轮另开启在线检查，确认真实 OSM 瓦片加载。
- `scripts/check_qt_completion.py`：五组真实数据库/计费回归，覆盖分时边界、车辆头像持久化、并发预约与付款、待支付资金冻结、提现审核及余额约束。
- X11/Xvfb 实际 Qt 窗口截图：`screenshots/user`、`screenshots/admin`；隔离业务流程截图：`integration/`。布局与可见性检查不等同于逐像素一致性证明。

真实业务验收创建并删除独立 schema，不重置或写入现有演示账户、订单、余额。在线展示截图连接既有服务只读。Windows 未构建/运行，未宣称跨平台视觉一致。

## 构建、运行与复现

```bash
bash scripts/run_qt.sh user
bash scripts/run_qt.sh admin
# 两端均可指定 API
bash scripts/run_qt.sh user --api http://127.0.0.1:4173

# 当前工作区已配置 BUILD_TESTING=ON
cmake --build .runtime/qt-build --parallel 4
export LD_LIBRARY_PATH="$PWD/.runtime/qt/6.5.3/gcc_64/lib:$PWD/.runtime/qt-deps/usr/lib/x86_64-linux-gnu"
export QT_PLUGIN_PATH="$PWD/.runtime/qt/6.5.3/gcc_64/plugins"
export QTWEBENGINE_CHROMIUM_FLAGS='--disable-gpu --no-sandbox'
ctest --test-dir .runtime/qt-build --output-on-failure
.venv/bin/python scripts/check_qt_integration.py
.venv/bin/python scripts/check_qt_completion.py
```

集成测试默认 offscreen；设置 `DISPLAY` 和 `ELECTRA_TEST_PLATFORM=xcb` 可在 X11 显示环境运行。在线地图测试另设 `ELECTRA_TEST_ONLINE_MAP=1`。离线或网络受限时不要把在线检查跳过写成通过。

新环境构建见 `desktop/scripts/build-linux.sh` 与 `build-windows.ps1`，需要 Qt Widgets/Network/WebSockets/SQL/SVG/Charts/WebEngineWidgets/WebChannel/Test 模块。

工作区启动脚本使用 `.runtime/qt-build`；公开 Qt 预览使用 `deliverables/qt/linux/bin`。noVNC 仅传输 Qt 窗口与鼠标键盘，不承载 Web 业务页面。后台 `assignment-qt-public` 的 user/admin 两个进程与 gateway/funnel 分离，更新客户端无需重置网关或路由。公开两端是共享会话。
