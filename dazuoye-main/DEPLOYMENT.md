# 后端部署说明

## 发布物

`dist/charging_core-0.1.0-py3-none-any.whl` 是可安装发布包，包含服务代码和数据库 schema。目标机需要 Python 3.12 与可访问的 PostgreSQL 14+，不需要复制源码目录。

## 安装

```bash
python3.12 -m venv /opt/charging-core/venv
/opt/charging-core/venv/bin/pip install charging_core-0.1.0-py3-none-any.whl
install -m 600 .env.example /opt/charging-core/.env
```

在 `.env` 中设置实际数据库 URL、独立 schema 和至少 32 字符的随机 JWT 密钥。生产环境必须把 `CHARGING_ENVIRONMENT` 改为 `production`；此时服务不会在验证码响应中泄露验证码，但仍需接入真实短信发送适配器后才能登录。

## 初始化与启动

```bash
cd /opt/charging-core
venv/bin/charging-core init-db
venv/bin/charging-core set-admin-password --username admin
venv/bin/charging-core serve --host 127.0.0.1 --port 8000
```

`init-db` 可重复执行，不清除业务数据。服务进程以普通专用用户运行；由 systemd 等进程管理器负责自动重启。公网入口应使用 Nginx/Caddy 终止 TLS，并把 `/ws` 的 Upgrade 请求转发给同一服务。

## 验收与观测

- `GET /health` 验证服务和数据库连通性；
- `/docs` 查看 OpenAPI 并手工演示；
- 开发或验收环境运行 `scripts/smoke.py`；
- 日志默认写入 `logs/charging-core.log`，5 MiB 轮转并保留三份；
- 数据库中的 `ops_log` 保存管理员业务操作，`wallet_entry` 保存资金流水，二者不能用日志文件代替。

当前协调器适合单服务进程的课程部署。未来多实例扩容时，应把预约过期和自动结算循环拆到独立 worker；数据库行锁与终态检查已保证重复扫描不会重复结算。

