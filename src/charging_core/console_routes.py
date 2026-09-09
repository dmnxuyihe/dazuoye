import json
from typing import Annotated, Literal
from decimal import Decimal
from uuid import UUID
from fastapi import APIRouter, Depends, Response, HTTPException, Query
from .forecast import load_forecast
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
        _: Admin, scope: str = Query(default="all", pattern=r"^(all|[0-9]{1,6})$")
    ):
        artifact = load_forecast()
        if artifact is None:
            return {"ready": False, "message": "尚未生成历史预测，请运行训练脚本。"}
        result = artifact["results"].get(scope)
        if result is None:
            raise HTTPException(status_code=404, detail="区域不存在")
        return {**artifact["metadata"], "scope": scope, **result}

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
