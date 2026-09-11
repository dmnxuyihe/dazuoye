# 两分支整合与 Linux 运行

2026-09-10。来源：main `2646438`、admin-local `88fa23b`。工作树以 main 为基础，接入 admin-local 的管理端前端；保留 main 的用户端、摄像头/头像、地图与底部抽屉逻辑。admin-local 的 `dazuoye-main/` 外层目录不复制到项目中。

## 修复与数据边界

- 管理端保留同期预测、站点竖向车辆展示、记录时间格式和管理详情布局。
- 同期预测的站点名称、来源编号及 `zone_id` 从业务数据库读取；同一区域只累计一次。区域观测不等于单站实际电表数据。
- 数据缺小时/缺天、NULL、非有限数或负电量不能拼成连续历史。只使用目标日前连续完整天数；前一天不完整时显示空态与导入提示，不伪造日期或填零。
- 日期按北京时间映射到 2022-09-01 至 2023-02-28。3–8 月及闰日没有对应数据，显示不可用；不是当前实时预测。少于 21 天使用可用的历史基线；没有验证样本时显示“暂无验证数据”，不冒充零误差或测试覆盖率。
- NumPy 成为默认运行依赖，因为同期接口需要它；训练计算在线程池执行。原打包实验通过 `mode=artifact` 保留，Web 预测页显式使用这一模式。
- 管理表格原本预设 720×620，通用弹窗策略却缩成 320×345；现在保留预设大小并扣除窗口边框、限制在屏幕/所属窗口范围内。长详情滚动，标题关闭按钮保持可见。
- 嵌套 JSON 原本经 `QVariant::toString()` 变空；现在保留修改前后值和嵌套数组。业务时间固定显示北京时间，避免 UTC Linux 上少 8 小时。

两个远端分支的 `schema.sql` 相同，本次没有重建业务表或添加新迁移。验证了已有 schema 再次初始化后保留业务记录。后续已在本地 assignment 的现有数据库上验证预测接口，并完成原地升级，见下节。assignment 中的 `测试问题.zip` 是 9 月 8 日的问题清单，三个视频对应用户充电金额、统计刷新和返回导航；没有发现本次管理端负荷预测的专门录屏。

## 本地 assignment 已原地升级

2026-09-10，将 `/home/liuzihang/ljw-gf/assignment` 从 `29d0042` 快进到整合版本 `00382ca`，没有另建旧 assignment 副本。沿用该目录的 `.env`、`.env.local`、PostgreSQL 和运行端口；没有重新执行业务 seed 或重建现有数据库。

- 使用 assignment 自己的 `.venv` 安装依赖、`.runtime/qt-build` 编译两端，补齐 Qt Multimedia SDK。
- 在 assignment 中重跑 CTest：5/5 组通过（摄像头用例因无设备跳过）；实际 API 与独立 schema 的原生集成测试 5 项通过；同期预测单元、数据库完整性、资金与并发回归均通过。
- `scripts/package_qt_delivery.py --linux-only` 更新实际预览使用的 `deliverables/qt/linux`，包含 FFmpeg 多媒体插件；此选项跳过旧演示文档和压缩包生成。
- 重启 `assignment-public:web`、`assignment-qt-public:admin/user`，网关和路由继续沿用原进程。通过 noVNC 连接确认两端窗口正常，实际查看了新预测页。运行中两端二进制 SHA256 与新构建相同。
- 更新前后业务行数相同：17 个站点、13 个用户、53 个订单；历史观测 1,194,600 条。回归测试使用独立 schema 并在完成后删除。

本地本轮日志与窗口截图位于 `.runtime/assignment-integration/`，包括 `build.log`、`ctest.log`、`integration.log`、`forecast-api.log`、`preview-forecast.png` 和 `preview-user.png`。程序继续在原 API 4173 和 Qt 预览 6082 端口运行。

## Linux 准备与启动

需要 Python 3.12、C++17 编译器、CMake、Ninja、PostgreSQL 14，以及 Qt 6.5.3 的 Widgets/Network/WebSockets/Sql/Svg/Charts/WebEngineWidgets/WebChannel/Multimedia/MultimediaWidgets/Test 模块。OpenCV 为可选依赖；没有它时人脸处理不可用，普通头像仍可用。

以 Ubuntu 22.04 为例，系统依赖可由管理员安装：

```bash
sudo apt-get install build-essential cmake ninja-build libgl-dev libegl-dev \
  libopengl-dev libxkbcommon-x11-0 libxcb-cursor0 libxcb-icccm4 \
  libxcb-keysyms1 libxcb-image0 libxcb-render-util0 libxcb-xinerama0 \
  libnss3 libnspr4 libasound2 libxcomposite1 libxdamage1 libxrandr2 libxtst6
# 可选人脸处理：sudo apt-get install libopencv-dev
python3.12 -m venv .venv
.venv/bin/pip install -e . aqtinstall
.venv/bin/aqt install-qt linux desktop 6.5.3 gcc_64 -O .runtime/qt \
  -m qtcharts qtwebsockets qtwebchannel qtwebengine qtpositioning qtmultimedia
cp .env.example .env
# 编辑 .env，设置独立数据库地址及随机 JWT 密钥。
bash scripts/local_postgres14.sh init
.venv/bin/charging-core set-admin-password --username admin
BUILD_TESTING=ON bash desktop/scripts/build-linux.sh
.venv/bin/charging-core serve --host 127.0.0.1 --port 4173
```

在有桌面的终端分别启动：

```bash
bash scripts/run_qt.sh user --api http://127.0.0.1:4173
bash scripts/run_qt.sh admin --api http://127.0.0.1:4173
```

仅演示环境可显式启用 `.env` 中的 `CHARGING_ADMIN_CONSOLE_ENABLED=true` 获得自动管理会话。初次初始化只导入业务样本，不包含完整历史观测；预测空态时执行：

```bash
.venv/bin/python scripts/download_urbanev.py
.venv/bin/python scripts/import_urbanev.py .runtime/urbanev-full
```

下载器按固定上游提交校验文件，导入器不改业务账户、订单或余额。无需重训旧打包实验。自装 Qt 可设 `QT_ROOT`；自定义构建目录可设 `ELECTRA_BUILD_DIR`，构建与启动脚本均支持。`CHARGING_LOCAL_PG_PORT` 必须与 `.env` 的数据库端口一致；测试机器因默认端口已占用，使用隔离端口 55439，没有停止原服务。

## 验证与限制

本次实际使用 Ubuntu 22.04 x86_64、Python 3.12、Qt 6.5.3、PostgreSQL 14，在无 sudo 环境把依赖解包到本项目 `.runtime/`；X11 测试使用 Xvfb。完整 UrbanEV 导入 23 个校验文件、275 区域、1,194,600 条小时观测。

复现命令（Qt 库与插件路径需对应自己的 SDK）：

```bash
export LD_LIBRARY_PATH="$PWD/.runtime/qt/6.5.3/gcc_64/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$PWD/.runtime/qt/6.5.3/gcc_64/plugins"
ctest --test-dir .runtime/qt-build --output-on-failure
.venv/bin/python -m unittest discover -s tests -v
.venv/bin/python scripts/check_same_period_forecast.py
.venv/bin/python scripts/check_qt_completion.py
.venv/bin/python scripts/check_qt_integration.py
# 需已启动演示 API：
CHARGING_TEST_API=http://127.0.0.1:4173 .venv/bin/python scripts/check_load_forecast.py
```

设置 `DISPLAY` 和 `ELECTRA_TEST_PLATFORM=xcb` 可把集成测试放到 X11。三个数据库回归脚本创建并删除各自独立 schema，不使用用户业务数据。Web 预测检查见 `scripts/check_forecast_browser.cjs`，支持 `PLAYWRIGHT_MODULE` 和 `CHARGING_TEST_API`。

本次结果已在本地运行时目录完成验证；交付文档只保留配置和复现命令，不再附带历史截图与逐次日志。

| 检查 | 实际结果 |
| --- | --- |
| Linux 两端与测试目标编译 | 通过 |
| CTest | 5/5 组通过；内部摄像头用例跳过 1 项 |
| X11 弹窗回归 | 11 项通过（含初始化/清理） |
| X11 + 实际 API + 隔离数据库 | 5 项通过（含初始化/清理），覆盖充值、预约、启停、付款、管理操作、跨客户端统计刷新 |
| 同期预测单元回归 | 6 项通过 |
| 数据库映射、缺失观测及迁移回归 | 通过 |
| 计费、并发预约/付款、提现与余额约束 | 通过 |
| 预测 API 权限及打包实验检查 | 通过 |
| Web 预测页面 | 14 个范围、切换、390/768/1440/1920 宽度、导航及无 JS 错误检查通过 |
| Qt X11 在线地图 | 交互和真实瓦片加载通过 |

预测页和窗口验证可按上面的启动、复现命令重新生成。

摄像头测试因机器没有摄像头跳过；未安装 OpenCV，没有验证真实人脸处理。Windows 未运行，Wayland 未验证。Qt 地图交互与在线瓦片加载是不同的检查，不以离线通过代替在线通过。


## 用户要求的改动审计与再次回归（2026-09-10）

复测时间：2026-09-10T13:07:14.344366+00:00。源码为 `997062b`；本轮没有修改业务代码。

影响范围不能简单表述为“只改了管理端”：

1. 相对 GitHub main `2646438`，本次生产逻辑改动集中在管理窗口、共用 `ui/dialogs`、预测服务与路由、Web 预测脚本；用户页面、地图、头像、计费和订单服务没有额外改动。
2. 相对旧 assignment `29d0042`，原地升级还带入了上游 main 已有的用户端重构、地图/抽屉、摄像头等更新；这些不是此次新编写的修复，但同样需要回归验证。
3. 预测接口默认模式改为同期模拟，旧打包实验通过 `mode=artifact` 保留。项目内 Web 页面已显式指定模式；未在仓库中的第三方调用者没有被验证。
4. 共用弹窗尺寸、详情 JSON 展示和北京时间格式会影响使用它们的两端页面，不能承诺其他界面零风险。

再次实际执行的结果：

| 检查 | 结果 |
| --- | --- |
| 构建与 CTest | 5/5 组通过；摄像头用例因无设备跳过 1 项 |
| 原生客户端 + 实际 API + 独立数据库 schema | 5 项通过（含初始化/清理），覆盖登录、头像保存、充值、预约、启停、付款、管理维护和跨端统计同步 |
| 计费、并发资金与提现回归 | 通过 |
| 同期预测单元 | 6 项通过；缺失日、无效电量、因果边界、短历史指标等 |
| 预测数据库映射与完整性 | 通过；改名/区域变更、去重、缺观测、NULL 与迁移保留 |
| 预测权限与产物 | 通过 |
| 全范围接口追加检查 | 两模式各 14 个范围，共 28 次请求通过；校验日期先后、数组长度、有限非负值和上下界 |
| Web 页面 | 14 范围目录、切换、四种宽度、返回导航和无 JS 错误检查通过 |

测试使用独立 schema，完成后删除。测试通过说明覆盖范围内未发现回归，不等于证明整个项目没有剩余缺陷。Windows、Wayland、真实摄像头及 OpenCV 人脸处理仍未验证；本轮没有逐项验收旧问题清单中的所有功能。因此不能把“可复现的预测/弹窗缺陷已修复”扩展成“所有历史问题全部完成”。
