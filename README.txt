Electra Qt 桌面端交付 · 2026-09-07

1. Ubuntu 22.04 x86_64 图形桌面：运行 linux/start-admin.sh 或 linux/start-user.sh。
   已附 Qt 6.5.3 和额外 XCB 运行依赖；系统仍需桌面环境、glibc、OpenGL 驱动与常规 Ubuntu 基础库。
   默认连接现有公网 API；可传 --api http://127.0.0.1:4173 连接本地服务。
2. 源码：assignment-qt-source.zip。包含 Qt C++ 客户端、现有 FastAPI/Web 源码、预测产物、构建脚本与文档。
3. Windows：源码内 desktop/scripts/build-windows.ps1；本次没有 Windows 可执行文件或运行验收结论。
4. 答辩：Qt 重构版 PPTX / PDF / DOCX。旧压缩包为之前版本，本次以 assignment-qt-delivery.zip 为准。
5. 按用户要求跳过后续完整验收。已有截图与编译不等同于完整交互通过；修复后的销毁/定时器处理未重跑完整验收。
6. Web 公网入口保留：https://lv-l40s-liuzihang.taild6df1c.ts.net:8443/ui/admin.html
7. 恢复本地业务库可使用 database/assignment-database.dump（既有交付快照；不会自动恢复）。恢复说明见源码 DEPLOYMENT.md。

更多范围及限制见 使用与架构说明.md。Qt 端不是 WebEngine 套壳，Qt Charts 仅用于管理员图表。
