#!/usr/bin/env python3
"""Repeatable acceptance smoke test against a running development server."""

import argparse
import asyncio
import json
import os
import time
import urllib.error
import urllib.request
import uuid

import websockets


class Client:
    def __init__(self, base_url: str) -> None:
        self.base_url = base_url.rstrip("/")

    def call(
        self,
        method: str,
        path: str,
        body: dict | None = None,
        token: str | None = None,
        expected: tuple[int, ...] = (200, 201, 204),
    ) -> dict | None:
        headers = {"Content-Type": "application/json"}
        if token:
            headers["Authorization"] = f"Bearer {token}"
        request = urllib.request.Request(
            self.base_url + path,
            data=json.dumps(body).encode() if body is not None else None,
            headers=headers,
            method=method,
        )
        try:
            with urllib.request.urlopen(request, timeout=10) as response:
                payload = json.load(response) if response.status != 204 else None
                if response.status not in expected:
                    raise AssertionError((response.status, payload))
                return payload
        except urllib.error.HTTPError as exc:
            payload = json.load(exc)
            if exc.code not in expected:
                raise AssertionError((exc.code, payload)) from exc
            return payload


def run(base_url: str) -> None:
    client = Client(base_url)
    assert client.call("GET", "/health") == {"status": "ok"}
    admin = client.call(
        "POST",
        "/auth/admin/login",
        {
            "username": os.getenv("SMOKE_ADMIN_USER", "admin"),
            "password": os.getenv("SMOKE_ADMIN_PASSWORD", "123456"),
        },
    )["access_token"]

    phone = "1" + str(time.time_ns())[-10:]
    client.call("POST", "/auth/otp/request", {"phone": phone})
    user = client.call(
        "POST",
        "/auth/otp/verify",
        {"phone": phone, "code": os.getenv("CHARGING_DEV_OTP_CODE", "246810")},
    )["access_token"]
    profile = client.call("PATCH", "/me", {"nickname": "验收用户"}, user)
    assert profile["nickname"] == "验收用户"

    recharge_key = str(uuid.uuid4())
    first = client.call(
        "POST",
        "/wallet/recharges",
        {"amount": "100.00", "idempotency_key": recharge_key},
        user,
    )
    second = client.call(
        "POST",
        "/wallet/recharges",
        {"amount": "100.00", "idempotency_key": recharge_key},
        user,
    )
    assert first["id"] == second["id"]

    suffix = uuid.uuid4().hex[:8].upper()
    station = client.call(
        "POST",
        "/admin/stations",
        {
            "name": f"验收临时站-{suffix}",
            "address": "验收临时地址",
            "longitude": "120.1",
            "latitude": "30.2",
            "unit_price": "1.00",
        },
        admin,
    )
    charger = client.call(
        "POST",
        "/admin/chargers",
        {
            "station_id": station["id"],
            "code": f"SMOKE-{suffix}",
            "kind": "fast",
            "power_kw": "60",
        },
        admin,
    )
    client.call(
        "PATCH",
        f"/admin/chargers/{charger['id']}/status",
        {"status": "faulted"},
        admin,
    )
    client.call("POST", f"/admin/chargers/{charger['id']}/restart", token=admin)
    client.call("DELETE", f"/admin/chargers/{charger['id']}", token=admin)
    client.call("DELETE", f"/admin/stations/{station['id']}", token=admin)

    stations = client.call("GET", "/stations", token=user)
    chargers = client.call("GET", f"/stations/{stations[0]['id']}/chargers", token=user)
    available = next(item for item in chargers if item["status"] == "available")
    order = client.call(
        "POST",
        "/orders",
        {"charger_id": available["id"], "idempotency_key": str(uuid.uuid4())},
        user,
    )
    client.call("POST", f"/orders/{order['id']}/start", token=user)
    completed = client.call("POST", f"/orders/{order['id']}/stop", token=user)
    assert completed["status"] == "completed" and completed["balance"] >= "0.00"
    assert client.call("GET", "/me/orders", token=user)
    assert (
        client.call("GET", "/admin/stats/summary", token=admin)["completed_orders"] >= 1
    )
    assert len(client.call("GET", "/admin/stats/revenue?days=7", token=admin)) == 7

    async def check_admin_events() -> None:
        uri = base_url.replace("http://", "ws://").replace("https://", "wss://")
        async with websockets.connect(f"{uri}/ws?token={admin}&after=0") as socket:
            for _ in range(100):
                event = json.loads(await asyncio.wait_for(socket.recv(), timeout=5))
                if event["event_type"] == "order.completed":
                    return
        raise AssertionError("管理员未收到订单完成事件")

    asyncio.run(check_admin_events())
    print("smoke acceptance: ok")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", default="http://127.0.0.1:8000")
    run(parser.parse_args().base_url)
