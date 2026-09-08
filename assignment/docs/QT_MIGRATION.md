# Qt 桌面端重构交付 · 2026-09-07

本次交付对应需求图中的“充电用户端”和“PC 管理端”。两个程序使用 **C++17 + Qt 6.5.3 Widgets**，不是浏览器窗口，也没有 QWebEngine、QML 或 Python GUI 包装。Qt Charts 只链接管理端。业务继续由现有 FastAPI/PostgreSQL 服务处理。

## 对照需求

| 要求 | 本次对应实现 |
| --- | --- |
| Qt Widgets 用户端 | `electra-user`：首页、地图、站点接口、预约与充电、统计、历史时段、账户、订单与钱包 |
| Qt PC 管理端 | `electra-admin`：运营总览、站点导航、充电订单、操作审计、负荷预测及管理对话框 |
| C++17 | `desktop/src`，CMake 明确指定标准；不通过 JS 执行业务操作 |
| Qt Charts 仅管理端 | 原生环图、预测折线与误差带；用户端图形由 QPainter 绘制 |
| REST API + WebSocket | QNetworkAccessManager 异步请求；JWT 内存会话；QWebSocket 事件游标与有上限的重连退避 |
| SQLite 本地缓存 | QSQLITE 保存配置、带时间戳的只读快照；按服务地址、角色、用户区分 |
| 服务端权威 | 客户端不直接连接 PostgreSQL；预约、结算、退款、权限与约束全部由原 API 确认 |
| Ubuntu 22.04 | 已编译两个原生 Linux x86_64 程序，并提供 Qt 运行依赖 |
| Windows | 提供 Qt 6.5.3 MSVC SDK / CMake 构建和 windeployqt 脚本；尚未在 Windows 编译运行 |
| Web 大屏 | 原有公网页面保留；本次只迁移 Qt 对应客户端。现有大屏仍是原生 JS，并未迁移 Vue3/Pinia/ECharts |
| Python 智能预测 | 保留已训练岭回归及周期基线；并未新增 PyTorch、MLflow 或 ONNX Runtime |
| 工程依赖 | CMake + 固定 Qt SDK；本次没有声称已落地 Conan/vcpkg 或远程 CI/CD |

## 使用

交付包中的 `linux/start-admin.sh` 和 `linux/start-user.sh` 默认连接现有公网服务：

```bash
./linux/start-admin.sh
./linux/start-user.sh
# 或连接自己启动的统一服务
./linux/start-admin.sh --api http://127.0.0.1:4173
```

这两个程序需要图形桌面（X11；Wayland 桌面可经 XWayland）。服务器无显示设备时仍可继续访问 Web 端；Funnel 暴露的是 API/Web 服务，不是把桌面窗口变成网页。Qt 模拟管理员自动登录仍由后端开发开关控制；用户真实操作仍需验证码登录。

在当前 assignment 工作区中：

```bash
bash scripts/run_qt.sh admin
bash scripts/run_qt.sh user
bash scripts/run_qt.sh admin --api https://lv-l40s-liuzihang.taild6df1c.ts.net:8443
```

Linux 源码构建：Qt 6.5.3 gcc_64 SDK，包含 qtcharts、qtwebsockets（基础包含 Widgets/SQL/SVG）；CMake >=3.21、Ninja、C++17 编译器，系统 OpenGL 开发头文件。指定 `QT_ROOT` 后运行 `desktop/scripts/build-linux.sh`。现有工作区的 Qt SDK 和额外 Ubuntu 依赖均位于 `.runtime`，未修改系统安装。

Windows：在 VS2022 x64 开发环境中安装 Qt 6.5.3 `msvc2019_64`（及 Charts/WebSockets），运行：

```powershell
powershell -ExecutionPolicy Bypass -File desktop/scripts/build-windows.ps1 -QtRoot C:\Qt\6.5.3\msvc2019_64
```

命令行支持 `--api`、`--cache`、`--page`；`--screenshots` 为开发截图入口，不承载产品业务。无 `--cache` 时采用 Qt 标准用户数据目录；工作区启动脚本将缓存放在 assignment/.runtime。

## 操作与视觉

沿用现有紫黑配色、粉紫渐变、透视车辆、能量光环、本地中文字体和深圳道路资产。Qt 自绘车辆/地图/目标环/流带，Qt Charts 展示管理分析图。布局采用 QWidget/QLayout，可调整桌面窗口大小；较长页面滚动查看。原 Web 版本及公网链接保留。

管理对话框提供用户新增/编辑/冻结/删除、站点与电桩维护、故障恢复/重启、代预约/启停/取消、调账、账本、退款及目标设置。服务器保留不可删除历史、金额限制、幂等与占用约束。列表每页最多200条，管理界面提供翻页；用户历史当前显示最近200条。

订单、资金请求不在客户端离线排队。无网络时 GET 可回退到当前身份快照并显示缓存日期，写操作拒绝执行；JWT 不写 SQLite。再次登录必须在线，避免把本地缓存当作认证依据。QWebSocket 游标仅在当前会话内保留，重新连接后刷新 REST 快照。

地图展示真实站点位置与道路背景，但不计算驾驶路线；车辆百分比、续航、分组流带仍明确属于展示口径。用户电量和费用以服务器订单返回值为准。预测数据截止2023-02-28，非当前实时负荷，详见 LOAD_FORECAST.md。

## 本次验证状态

按用户要求，**跳过后续完整验收，以编译和交付为主**。Ubuntu 已完成 C++ 编译，早期客户端缓存/网络单测通过，已有原生窗口截图。中途交互验收发现测试账目类型误写（实际为 charge），以及测试程序跨窗口销毁后定时器事件崩溃；已修正账目断言，并在 ApiClient 析构开始显式停止连接和重连定时器，最终版本未重新执行完整交互验收。Windows 运行、跨平台视觉一致性和完整端到端行为不声称已验收通过。

截图位于交付包 `screenshots`，对应原生 Qt 窗口；它们是布局参考，不替代交互验收。旧 Web 版验收结果不自动算作 Qt 版验收结果。
