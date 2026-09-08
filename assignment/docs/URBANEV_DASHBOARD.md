# UrbanEV 历史数据大屏

本次只修改 assignment。业务订单、账户余额和现有站点保持原数据。大屏的历史研究统计与平台实时明细明确分开；右侧每屏 15 个图表，站点页提供球面区域聚焦，订单页提供充电量日 K、MA5/MA20、日累计电量柱、范围切换和十字线。站点搜索与实时订单入口保留在“平台实时明细”弹窗。

## 数据来源与范围

来源：<https://github.com/IntelligentSystemsLab/UrbanEV>，CC0，固定提交 `44f2aa0c8d89f192bce00bafb0def74a21b39c68`。

完整导入该提交 `data/` 下 23 个文件，共 121,149,765 字节；包括原始/预处理站点表、6 份时序矩阵、邻接和距离矩阵、POI、天气、GIS 与项目文件。仓库链接到的外部 Dryad/Google Drive 原始压缩包不属于本次 GitHub 数据目录。

- 预处理站点：1,362；充电桩容量：17,532；区域：275。
- 2022-09-01 至 2023-02-28，4,344 小时，1,194,600 条区域小时观测。
- `urbanev_source_file` 保存全部原始字节、SHA-256 和提交号；下载时额外核对 Git blob SHA-1。
- `urbanev_station` 保存源站点、区域、GCJ-02 坐标和容量。
- `urbanev_observation` 规范化保存占用数量、时长、两种充电量、电费和服务费。时间为源数据当地墙上时间，不擅自转换成 UTC。
- `dataset_metadata` 中 `UrbanEV-full` 缓存日线、小时均值、排名、POI 分类和天气。无需在每次刷新时扫描百万行。
- 占用率由源占用数量除以总容量计算。日 K 的开/收分别为当日首/末小时全区域充电量，高/低为当日小时极值；下方柱为日累计电量。这些不是实际订单营收。

## 复现

在 assignment 根目录执行：

```bash
.venv/bin/python scripts/download_urbanev.py .runtime/urbanev-full
.venv/bin/python scripts/import_urbanev.py .runtime/urbanev-full
.venv/bin/python scripts/check_urbanev.py http://127.0.0.1:4173
```

下载可恢复，导入在单个事务内执行；相同提交已导入时跳过。文件校验、CSV 区域标题、时间戳和行数必须一致，否则回滚。不会修改业务订单/账户/钱包表。

## 默认会话

本部署 `.env.local` 设置 `CHARGING_DEMO_ADMIN_ENABLED=true`。打开大屏自动通过 `/auth/demo` 获取 `demo_admin` 会话，界面显示“演示管理员 · 已登录”，无需账号密码。`/demo/analytics` 返回公共研究数据。真实管理员账户和写操作仍要求 `admin` 角色；演示令牌访问真实用户、订单和用户状态修改返回 403。其他部署默认关闭这个入口。

## 验证

`scripts/check_urbanev.py` 检查全部源文件字节与哈希、站点容量、观测行数/小时数/区域数、日线与数据库电量总和以及演示会话权限边界。

Playwright 验收脚本和截图位于 `.runtime/ui-refactor-v4/`，覆盖三屏 45 个图表、1920×1080 / 1440×900 / 1366×768、地图聚焦、站点点击与搜索、30/181 日日 K 切换、十字线、刷新后历史数据保留和浏览器错误。此目录也保存修改前备份；工作区没有 Git 元数据，使用文件差异审查。
