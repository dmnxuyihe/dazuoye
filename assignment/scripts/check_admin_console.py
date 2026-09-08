"""End-to-end administrator acceptance; run only against isolated charging_console_test on 4175."""

import asyncio, concurrent.futures, json, urllib.request, urllib.error, uuid
from decimal import Decimal
import asyncpg
from charging_core.config import Settings
from charging_core.console_service import ConsoleService

BASE = "http://127.0.0.1:4175"


def call(path, method="GET", body=None, token=None):
    headers = {"Content-Type": "application/json"}
    if token:
        headers["Authorization"] = "Bearer " + token
    try:
        with urllib.request.urlopen(
            urllib.request.Request(
                BASE + path,
                method=method,
                headers=headers,
                data=json.dumps(body).encode() if body is not None else None,
            ),
            timeout=15,
        ) as r:
            raw = r.read()
            return r.status, json.loads(raw) if raw else None
    except urllib.error.HTTPError as e:
        return e.code, json.load(e)


def ok(*args, **kwargs):
    code, data = call(*args, **kwargs)
    assert 200 <= code < 300, (code, data)
    return data


def key():
    return str(uuid.uuid4())


async def main():
    settings = Settings()
    assert settings.database_schema == "charging_console_test"
    pool = await asyncpg.create_pool(
        settings.database_url, server_settings={"search_path": settings.database_schema}
    )
    try:
        token = ok("/auth/console", "POST")["access_token"]
        demo = ok("/auth/demo", "POST")["access_token"]
        assert call("/admin/users", token=demo)[0] == 403
        assert call("/admin/console/settings")[0] == 401
        assert (
            call(
                "/admin/users",
                "POST",
                {"phone": "13800000001", "nickname": "拒绝"},
                demo,
            )[0]
            == 403
        )
        print(
            "PASS full admin auto-session; anonymous and read-only demo denied management"
        )
        users = [
            ok(
                "/admin/users",
                "POST",
                {
                    "phone": "1" + str(uuid.uuid4().int)[-10:],
                    "nickname": "验收用户" + str(i),
                },
                token,
            )
            for i in range(3)
        ]
        user = users[0]
        uid = user["id"]
        other = users[1]["id"]
        assert (
            ok(
                "/admin/users/" + uid,
                "PATCH",
                {"phone": user["phone"], "nickname": "已修改"},
                token,
            )["nickname"]
            == "已修改"
        )
        ok("/admin/users/" + users[2]["id"], "DELETE", token=token)
        money = {"amount": "100", "reason": "测试准备", "idempotency_key": key()}
        endpoint = "/admin/users/" + uid + "/wallet-adjustments"
        with concurrent.futures.ThreadPoolExecutor(2) as ex:
            results = list(
                ex.map(lambda _: ok(endpoint, "POST", money, token), range(2))
            )
        assert results[0]["id"] == results[1]["id"]
        assert len(ok("/admin/users/" + uid + "/wallet", token=token)) == 1
        assert call(endpoint, "POST", {**money, "amount": "99"}, token)[0] == 409
        assert (
            call(
                endpoint,
                "POST",
                {"amount": "0", "reason": "zero", "idempotency_key": key()},
                token,
            )[0]
            == 422
        )
        assert (
            call(
                endpoint,
                "POST",
                {"amount": "-101", "reason": "negative", "idempotency_key": key()},
                token,
            )[0]
            == 409
        )
        print(
            "PASS create/edit/delete user; concurrent adjustment idempotency, payload mismatch, zero and negative-balance rejection"
        )
        ok(
            "/admin/users/" + other + "/wallet-adjustments",
            "POST",
            {"amount": "100", "reason": "准备", "idempotency_key": key()},
            token,
        )
        station = ok(
            "/admin/stations",
            "POST",
            {
                "name": "后台验收站",
                "address": "深圳",
                "longitude": "114.05",
                "latitude": "22.54",
                "unit_price": "1.20",
            },
            token,
        )
        sid = station["id"]
        ok("/admin/stations/" + sid, "PATCH", {"name": "后台修改站"}, token)
        chargers = [
            ok(
                "/admin/chargers",
                "POST",
                {
                    "station_id": sid,
                    "code": "ADMIN-" + key()[:8],
                    "kind": "fast",
                    "power_kw": "60",
                },
                token,
            )
            for _ in range(2)
        ]
        cid = chargers[0]["id"]
        edit = {"code": chargers[0]["code"], "kind": "slow", "power_kw": "40"}
        ok("/admin/chargers/" + cid, "PATCH", edit, token)
        ok("/admin/chargers/" + cid + "/status", "PATCH", {"status": "faulted"}, token)
        assert (
            ok("/admin/chargers/" + cid + "/restart", "POST", token=token)["status"]
            == "available"
        )
        print("PASS station create/edit; charger create/edit/fault/restart")
        order = ok(
            "/admin/orders",
            "POST",
            {"user_id": uid, "charger_id": cid, "idempotency_key": key()},
            token,
        )
        oid = order["id"]
        assert (
            call(
                "/admin/orders",
                "POST",
                {"user_id": other, "charger_id": cid, "idempotency_key": key()},
                token,
            )[0]
            == 409
        )
        assert call("/admin/chargers/" + cid, "PATCH", edit, token)[0] == 409
        assert (
            call(
                endpoint,
                "POST",
                {"amount": "-1", "reason": "活动订单禁止", "idempotency_key": key()},
                token,
            )[0]
            == 409
        )
        ok("/admin/orders/" + oid + "/start", "POST", token=token)
        await pool.execute(
            "UPDATE charging_order SET started_at=now()-interval '1 second' WHERE id=$1",
            uuid.UUID(oid),
        )
        ended = ok("/admin/orders/" + oid + "/stop", "POST", token=token)
        assert ended["status"] == "completed"
        assert Decimal(ended["amount"]) > 0
        assert (
            ok("/admin/orders/" + oid + "/stop", "POST", token=token)["amount"]
            == ended["amount"]
        )
        refund = {
            "amount": ended["amount"],
            "reason": "验收退款",
            "idempotency_key": key(),
        }
        with concurrent.futures.ThreadPoolExecutor(2) as ex:
            results = list(
                ex.map(
                    lambda _: ok(
                        "/admin/orders/" + oid + "/refund", "POST", refund, token
                    ),
                    range(2),
                )
            )
        assert results[0]["id"] == results[1]["id"]
        assert (
            call(
                "/admin/orders/" + oid + "/refund",
                "POST",
                {**refund, "idempotency_key": key()},
                token,
            )[0]
            == 409
        )
        assert (
            call(
                "/admin/orders/" + oid + "/refund",
                "POST",
                {**refund, "amount": "-1", "idempotency_key": key()},
                token,
            )[0]
            == 409
        )
        assert call("/admin/users/" + uid, "DELETE", token=token)[0] == 409
        assert call("/admin/chargers/" + cid, "DELETE", token=token)[0] == 409
        print(
            "PASS admin reserve/start/stop; duplicate stop; concurrent refund idempotency; refund cap; occupied charger and history preservation"
        )
        next_order = ok(
            "/admin/orders",
            "POST",
            {
                "user_id": other,
                "charger_id": chargers[1]["id"],
                "idempotency_key": key(),
            },
            token,
        )
        ok("/admin/orders/" + next_order["id"] + "/cancel", "POST", token=token)
        ok("/admin/users/" + other + "/status", "PATCH", {"status": "frozen"}, token)
        assert (
            call(
                "/admin/orders",
                "POST",
                {
                    "user_id": other,
                    "charger_id": chargers[1]["id"],
                    "idempotency_key": key(),
                },
                token,
            )[0]
            == 403
        )
        ok("/admin/users/" + other + "/status", "PATCH", {"status": "active"}, token)
        before = await pool.fetchval("SELECT count(*) FROM charging_order")
        try:
            await ConsoleService(pool, settings).order_command(
                uuid.uuid4(),
                "reserve",
                user_id=uuid.UUID(other),
                charger_id=uuid.UUID(chargers[1]["id"]),
                key=uuid.uuid4(),
            )
        except asyncpg.ForeignKeyViolationError:
            pass
        else:
            raise AssertionError("Missing audit FK error")
        assert await pool.fetchval("SELECT count(*) FROM charging_order") == before
        assert (
            await pool.fetchval(
                "SELECT status FROM charger WHERE id=$1", uuid.UUID(chargers[1]["id"])
            )
            == "available"
        )
        print(
            "PASS cancel/free charger; frozen-user policy; audit failure rolls back order and charger in same transaction"
        )
        prefs = {
            "daily_goal": 64,
            "weekly_goal": 25,
            "charge_limit": 80,
            "vehicle": "BYD Seal",
        }
        assert ok("/admin/console/settings", "PUT", prefs, token) == prefs
        assert ok("/admin/console/settings", token=token) == prefs
        assert len(ok("/admin/ops-logs?limit=200", token=token)) >= 15
        clean = ok(
            "/admin/stations",
            "POST",
            {
                "name": "空站",
                "address": "测试",
                "longitude": "114",
                "latitude": "22",
                "unit_price": "1",
            },
            token,
        )
        free = ok(
            "/admin/chargers",
            "POST",
            {
                "station_id": clean["id"],
                "code": "DEL-" + key()[:8],
                "kind": "fast",
                "power_kw": "60",
            },
            token,
        )
        ok("/admin/chargers/" + free["id"], "DELETE", token=token)
        ok("/admin/stations/" + clean["id"], "DELETE", token=token)
        print(
            "PASS settings persist; audit logs present; unused charger/station deletion"
        )
    finally:
        await pool.close()


asyncio.run(main())
