# 全权限模拟管理员后台

> 状态：已实现并接入模拟部署。包含 Dashboard、Station、My Trips、History 四个页面，以及用户、站点、电桩、资金、订单、设置与审计管理抽屉。

## 验收目标与视觉

独立入口 `/ui/admin.html`。依据用户指定的 [Dribbble 27064652](https://dribbble.com/shots/27064652-EV-Charging-Dashboard-UI-UX-EV-Charging-Station-App-UI-Design) 主图和站点导航图实现：黑紫背景、顶部 ELECTRA 导航、三栏车辆/半圆目标/路线地图、下方堆叠流图与站点清单；站点页用左侧竖向车辆透视和右侧大地图。管理操作通过抽屉与表单打开，避免改变主图构图。图表与透视车辆中的展示型数值标注模拟，业务清单直接来自数据库。

## 管理员能力与接口设计

所有管理接口要求 `admin` JWT。自动登录还要求 `CHARGING_ENVIRONMENT=development`；默认配置关闭，当前已授权的模拟部署显式开启。独立开发开关 `CHARGING_ADMIN_CONSOLE_ENABLED` 开启时，`POST /auth/console` 为该模拟部署签发全权限管理员令牌；正式管理员登录仍可使用。旧 `demo_admin` 保持只读。自动会话使用独立管理员账号，不把密码写进前端。

| 操作 | 接口 | 行为 |
|---|---|---|
| 总览/营收 | GET /admin/stats/summary、/admin/stats/revenue | 现有真实业务统计 |
| 历史分析/目标 | GET /admin/console/analytics；GET/PUT /admin/console/settings | UrbanEV 聚合、运营目标与界面设置 |
| 站点维护 | GET/POST /admin/stations；PATCH/DELETE /admin/stations/{id} | 名称、地址、坐标、价格；有电桩不可直接删除 |
| 电桩维护 | GET/POST /admin/chargers；PATCH/DELETE /admin/chargers/{id} | 编号、类型、功率；活动订单设备不得改计费参数 |
| 故障/恢复/重启 | PATCH /admin/chargers/{id}/status；POST /admin/chargers/{id}/restart | 先处理活动订单，再维护设备 |
| 用户管理 | GET/POST /admin/users；PATCH/DELETE /admin/users/{id}；PATCH .../status | 资料修改、冻结/解冻；有资金/历史记录的账号冻结保留 |
| 余额调整 | POST /admin/users/{id}/wallet-adjustments | 正负金额、原因、幂等键；不允许负余额，负向调整先结束活动充电 |
| 钱包流水 | GET /admin/users/{id}/wallet | 全部收支可审计 |
| 订单详情/创建 | GET/POST /admin/orders；GET /admin/orders/{id} | 管理员代选用户和充电枪预约，仍检查余额与占用 |
| 启动/取消/停止 | POST /admin/orders/{id}/start、/cancel、/stop | 复用现有充电状态机，停止走统一结算 |
| 退款 | POST /admin/orders/{id}/refund | 仅已完成订单，支持部分退款，累计退款不得超过结算金额；幂等 |
| 操作日志 | GET /admin/ops-logs | 所有变更与业务操作同事务写日志 |
| 导出 | 前端导出当前管理表 CSV | 所见筛选结果导出，防止表格公式注入 |

## 业务约束

数据库仍是唯一事实源。金额操作使用 Decimal、行锁、钱包账本和幂等键；禁止直接删除已有资金/订单的历史记录。管理员权限覆盖全部业务操作，但不破坏一个用户/充电枪最多一个活动订单、余额非负和订单终态不可回退等约束。管理员订单命令的日志与订单状态变化同事务提交。

新设置表只保存运营目标/偏好，不参与计费。已新增 wallet_entry 类型为 adjustment/refund，保持原充值和扣款行为兼容。当前工作区没有 Git 元数据，修改前备份在 `.runtime/admin-console/before/`，以文件 diff 验收。

## 视觉与数据

主界面按参考图的三上二下卡片、导航、青色透视车辆、半圆进度、粉色路线与紫金粉流带复刻；Station 页面为竖向车辆与大地图。车辆图为 AI 生成素材，不是原站设计资产。路线图是独立 SVG 示意，不是实际道路导航；车辆、目标与默认分组流图为模拟。UrbanEV 切换通过 `/admin/console/analytics` 读取真实历史汇总，以月能量相对峰值绘图，分组色带为展示拆分。站点、电桩、用户和订单直接读取数据库。

所有菜单和工具按钮可操作；Book Now 打开管理员代预约表单。用户需有至少 5 元余额，电桩必须空闲。用户、订单、审计当前 UI 显示最近 200 条，接口支持分页。CSV 导出当前筛选结果并转义公式起始字符。

## 事务复用与本机兼容

ConsoleService 使用绑定当前连接的 TransactionPool 复用 ChargingService，内层事务作为 savepoint；管理日志与预约/启停在同一外层事务提交。验收通过注入不存在的管理员 ID 触发日志外键失败，确认订单与电桩更新均回滚。资金调整及退款先锁用户，再锁订单，幂等键复用但金额/订单不同则拒绝。

本机旧 PostgreSQL 库编码为 SQL_ASCII。审计与事件 JSON 使用原始 Unicode 序列化，避免 JSONB 转换 Unicode 转义时失败；新建本地数据库明确使用 UTF8。已保留现有数据，不转换运行库。

## 验证

- `scripts/check_admin_console.py`：隔离 schema charging_console_test / 4175，验证所有管理写操作、幂等、退款上限、历史保留、回滚与日志。
- `scripts/check_admin_browser.cjs`：四页面、四种视口；CHECK_MUTATIONS=1 仅允许 localhost:4175，执行真实 UI 用户编辑、调账、预约启停、退款、导出、设置持久化。
- 默认生产开关关闭；自动登录仅用于用户明确授权的全权限模拟站。真实支付和设备接入仍未实现。

## My Trips / History 视觉迭代

先生成并保存风格概念图 `docs/design/admin-trips-history-concept.png`，再将其中路线卡、状态环图、粉紫能量曲线、带连接线的订单条目、彩色时间线、热力图和事件详情卡实现为 HTML/CSS/SVG。概念图不作为交互页面背景。`admin-activity.js/css` 只读取现有订单、设备与审计数据；所有命令仍复用原按钮与 API。

订单支持状态/关键词筛选；日志支持类型/关键词筛选、事件选中联动与完整记录。能量按预约日期聚合，时间窗口截至最新记录日期；没有记录的日期显示零。日志按 UTC 分组，热力图按 UTC 星期与两小时时段汇总。原始审计标识保留，未增加假审计记录或虚构订单填充图表。

新验收脚本 `scripts/check_admin_activity.cjs` 检查四种视口、筛选、详情与时间线交互；原 `check_admin_browser.cjs` 继续覆盖真实隔离库中的预约启停、退款及导出。

## 中文界面

管理员四页面现为“运营总览 / 站点导航 / 充电订单 / 操作审计”。导航、图表、筛选、状态、管理抽屉、表单、提示和 CSV 表头统一中文；详情字段及事件名称在展示层翻译。接口路径、状态枚举、数据字段和车型持久化标识保持原值，品牌、设备编号与 kWh 等标准单位保留。中文视口与导出验收见 `scripts/check_admin_chinese.cjs`。


### 历史负荷预测

管理员新增“负荷预测”入口 `/ui/admin.html#forecast`。已训练岭回归并与昨日、上周基线做时间留出评估，支持全网与275个区域。数据截止2023-02-28，非当前实时预测。复现、指标和限制见 [负荷预测说明](LOAD_FORECAST.md)。
