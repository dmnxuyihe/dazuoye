> 主前端为 C++17 + Qt 6.5.3 Widgets 用户端与管理端，HTML 保留为视觉先验。当前实现、数据映射和实际验收见 [Qt 基线](docs/QT_MIGRATION.md)。

# Charging Core

这是充电桩平台的最小后端核心。架构取舍与不变量见 `ARCHITECTURE.md`，发布部署流程见 `DEPLOYMENT.md`。

## 环境

```bash
python3 -m venv .venv
.venv/bin/python -m pip install -e .
cp .env.example .env
```

本项目使用本地 PostgreSQL 14 模拟生产数据库。无需系统级安装，脚本会把运行时和数据隔离到被忽略的 `.runtime/`：

```bash
scripts/local_postgres14.sh init
.venv/bin/charging-core set-admin-password --username admin
.venv/bin/charging-core serve --port 4173
```

演示服务地址为 `http://127.0.0.1:4173`，交互文档位于 `/docs`，健康检查位于 `/health`。

正式展示使用 Qt 用户端和管理端：`bash scripts/run_qt.sh user`、`bash scripts/run_qt.sh admin`。构建依赖、数据映射和验证命令见 [Qt 基线](docs/QT_MIGRATION.md)。两端从 API 读取 PostgreSQL 数据，登录、充值、预约、开始、停止和付款均走业务服务；充电设备、电量及资金渠道属于作业模拟，没有接入实体硬件或第三方支付。

浏览器根页面和 `/ui/mobile.html` 保留为视觉先验和历史实现，不作为正式客户端。Qt 地图使用 OpenStreetMap 瓦片、数据库中的站点坐标与电桩状态。

站点样本与典型日曲线由 UrbanEV 官方公开数据派生。项目保留 12 个可交互站点，并对完整数据的占用量、充电量及电价文件做 24 小时聚合，以控制大作业仓库体积；原始坐标从 GCJ-02 转为 OpenStreetMap 使用的 WGS84。派生元数据、原文件校验值及许可证信息存于 `src/charging_core/data/urbanev_sample.json`。

服务启动后可在另一个终端执行完整冒烟验收：

```bash
.venv/bin/python scripts/smoke.py
```

验收脚本会创建独立用户，验证登录、幂等充值、用户资料、管理员站点/电桩运维、完整充电结算、统计及管理员事件流。临时站点和电桩会通过公开 API 删除；验收用户与订单作为审计数据保留。

开发环境调用 `POST /auth/otp/request` 会在响应中返回模拟验证码；生产环境不会回传验证码，且必须接入实际短信发送器。

管理员初始账号仅供首次演示。正式部署后应更换默认密码，并把 `.env` 权限限制为当前服务用户可读。服务日志默认轮转写入 `logs/charging-core.log`。

## 前端界面

正式客户端源码在 `desktop/`，使用 C++17、Qt 6.5.3 Widgets。用户端选桩卡片展示全部电桩的真实状态和累计次数；头像编辑支持裁剪、缩放、旋转、翻转、亮度调整及示例图片，处理结果通过 API 保存。仅地图使用 Qt WebEngine 与 Leaflet。

`src/charging_core/static/` 中的 HTML/CSS/JavaScript 为保留的视觉参考。源码仓库不收录 `.runtime/`、本地数据库、`.env`、`deliverables/` 和生成压缩包；旧交付目录可在 Git 历史中查看。

设计参考与此次验证记录见 `docs/UI_REFACTOR.md`。

大屏已接入完整 UrbanEV GitHub 历史数据目录，提供三屏各 15 块图表、球面区域聚焦和充电负荷日 K。导入、默认演示管理员会话与验收说明见 [UrbanEV 大屏](docs/URBANEV_DASHBOARD.md)。

APP 已按 2026-09-06 提供的参考图更新为车辆电量首页、紫色道路地图、充电设置/光环、统计和分时用电界面；素材与验收说明见 [APP 参考图复刻](docs/APP_REFERENCE_UI.md)。

## Electra 管理员 Dashboard

入口 `/ui/admin.html`，复刻参考图的深紫色桌面布局，提供 Dashboard、Station、My Trips、History 与全权限管理抽屉。已实现用户、站点、电桩维护、代预约启停、钱包调整、退款、设置、审计和 CSV 导出。业务约束及验证方法见 [ADMIN_CONSOLE.md](docs/ADMIN_CONSOLE.md)。

开发模拟环境可显式设置 `CHARGING_ADMIN_CONSOLE_ENABLED=true` 自动获得管理员会话；默认关闭。运行 `charging-core init-db` 应用 migration 4 后重启服务。已有业务数据保留，新增 console_settings 与账本 adjustment/refund 类型。真实设备与支付仍为模拟。


### 历史负荷预测

管理员新增“负荷预测”入口 `/ui/admin.html#forecast`。已训练岭回归并与昨日、上周基线做时间留出评估，支持全网与275个区域。数据截止2023-02-28，非当前实时预测。复现、指标和限制见 [负荷预测说明](docs/LOAD_FORECAST.md)。
