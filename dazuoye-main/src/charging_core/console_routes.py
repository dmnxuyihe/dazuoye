import json
from datetime import date, datetime, time
from typing import Annotated, Literal
from decimal import Decimal
from uuid import UUID
from fastapi import APIRouter, Depends, Response, HTTPException, Query
from .forecast import (
    BUSINESS_STATIONS,
    console_scopes,
    historical_same_period_forecast,
    map_current_date_to_dataset_date,
)
from pydantic import BaseModel, Field, field_validator
from .service import ChargingService


class UserEdit(BaseModel):
    phone: str = Field(pattern=r"^1[0-9]{10}$")
    nickname: str = Field(min_length=1, max_length=20)


class MoneyEdit(BaseModel):
    amount: Decimal = Field(ge=-10000, le=10000, decimal_places=2)
    idempotency_key: UUID
    reason: str = Field(min_length=1, max_length=200)

    @field_validator("amount")
    @classmethod
    def nonzero(cls, value):
        if value == 0:
            raise ValueError("金额不能为零")
        return value


class ChargerEdit(BaseModel):
    code: str = Field(min_length=1, max_length=40)
    kind: Literal["fast", "slow"]
    power_kw: Decimal = Field(gt=0, le=1000, decimal_places=2)


class OrderCreate(BaseModel):
    user_id: UUID
    charger_id: UUID
    idempotency_key: UUID


class WithdrawalReview(BaseModel):
    approve: bool
    note: str = Field(min_length=1,max_length=200)


class Preferences(BaseModel):
    daily_goal: int = Field(ge=0, le=100)
    weekly_goal: int = Field(ge=0, le=100)
    charge_limit: int = Field(ge=40, le=100)
    vehicle: str = Field(min_length=1, max_length=40)


def build_console_router(get_service, require_admin):
    router = APIRouter(prefix="/admin", tags=["admin-console"])
    Admin = Annotated[object, Depends(require_admin)]
    Service = Annotated[object, Depends(get_service)]

    @router.get("/console/analytics")
    async def analytics(_: Admin, s: Service):
        data = await s.pool.fetchval(
            "SELECT metadata FROM dataset_metadata WHERE name='UrbanEV-full'"
        )
        return {"ready": True, **json.loads(data)} if data else {"ready": False}

    @router.get("/console/forecast")
    async def forecast(
        _: Admin,
        s: Service,
        scope: str = Query(
            default="business", pattern=r"^(all|business|station-[0-9]{1,6})$"
        ),
    ):
        current_date = date.today()
        mapped_date = map_current_date_to_dataset_date(current_date)
        scopes = console_scopes()
        selected_scope = next((item for item in scopes if item["id"] == scope), None)
        if selected_scope is None:
            raise HTTPException(status_code=404, detail="区域不存在")
        metadata = {
            "source": "UrbanEV",
            "timezone": "Asia/Shanghai",
            "scopes": scopes,
        }
        if mapped_date is None:
            result = historical_same_period_forecast([], current_date, selected_scope["label"])
            return {**metadata, "scope": scope, **result}

        target_start = datetime.combine(mapped_date, time.min)
        query = """
            SELECT observed_at, SUM(energy_kwh)::double precision AS energy
            FROM urbanev_observation
            WHERE observed_at < $1
        """
        arguments = [target_start]
        if scope == "business":
            query += " AND zone_id = ANY($2::integer[])"
            arguments.append([int(item[2]) for item in BUSINESS_STATIONS])
        elif scope.startswith("station-"):
            station_id = scope.removeprefix("station-")
            station = next((item for item in BUSINESS_STATIONS if item[0] == station_id), None)
            if station is None:
                raise HTTPException(status_code=404, detail="站点不存在")
            query += " AND zone_id = $2"
            arguments.append(int(station[2]))
        query += " GROUP BY observed_at ORDER BY observed_at"
        rows = await s.pool.fetch(query, *arguments)
        result = historical_same_period_forecast(
            [(row["observed_at"], row["energy"]) for row in rows],
            current_date,
            selected_scope["label"],
        )
        if not rows:
            result["message"] = "UrbanEV 历史观测数据尚未导入，无法生成同期预测"
        return {**metadata, "scope": scope, **result}

    @router.get("/console/settings")
    async def settings(_: Admin, s: Service):
        return await s.settings_read()

    @router.put("/console/settings")
    async def save_settings(body: Preferences, a: Admin, s: Service):
        return await s.settings_write(a.id, body.model_dump())

    @router.get("/withdrawals")
    async def withdrawals(_: Admin,s: Service,status: Literal["pending","paid","rejected"] | None = None):
        return await s.withdrawals(status)

    @router.post("/withdrawals/{request_id}/review")
    async def review_withdrawal(request_id: UUID,body: WithdrawalReview,a: Admin,s: Service):
        return await s.review_withdrawal(a.id,request_id,body.approve,body.note)

    @router.post("/users", status_code=201)
    async def create_user(body: UserEdit, a: Admin, s: Service):
        return await s.user_save(a.id, None, body.model_dump())

    @router.patch("/users/{user_id}")
    async def edit_user(user_id: UUID, body: UserEdit, a: Admin, s: Service):
        return await s.user_save(a.id, user_id, body.model_dump())

    @router.delete("/users/{user_id}", status_code=204)
    async def delete_user(user_id: UUID, a: Admin, s: Service):
        await s.user_delete(a.id, user_id)
        return Response(status_code=204)

    @router.get("/users/{user_id}/wallet")
    async def wallet(user_id: UUID, _: Admin, s: Service):
        return await s.wallet(user_id)

    @router.post("/users/{user_id}/wallet-adjustments")
    async def adjustment(user_id: UUID, body: MoneyEdit, a: Admin, s: Service):
        return await s.money(
            a.id, user_id, body.amount, body.idempotency_key, body.reason
        )

    @router.patch("/chargers/{charger_id}")
    async def edit_charger(charger_id: UUID, body: ChargerEdit, a: Admin, s: Service):
        return await s.charger_edit(a.id, charger_id, body.model_dump())

    @router.post("/orders")
    async def create_order(body: OrderCreate, a: Admin, s: Service):
        return await s.order_command(
            a.id,
            "reserve",
            user_id=body.user_id,
            charger_id=body.charger_id,
            key=body.idempotency_key,
        )

    @router.get("/orders/{order_id}")
    async def order(order_id: UUID, _: Admin, s: Service):
        return await ChargingService(s.pool, s.settings).get_order(
            await s.order_user(order_id), order_id
        )

    @router.post("/orders/{order_id}/refund")
    async def refund(order_id: UUID, body: MoneyEdit, a: Admin, s: Service):
        return await s.money(
            a.id,
            await s.order_user(order_id),
            body.amount,
            body.idempotency_key,
            body.reason,
            order_id,
        )

    @router.post("/orders/{order_id}/{action}")
    async def command(
        order_id: UUID, action: Literal["start", "cancel", "stop", "pay"], a: Admin, s: Service
    ):
        return await s.order_command(a.id, action, order_id=order_id)

    return router
