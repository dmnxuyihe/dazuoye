import asyncio
import time
import logging
import json
from collections.abc import AsyncIterator
from contextlib import asynccontextmanager, suppress
from dataclasses import dataclass
from decimal import Decimal
from pathlib import Path
from typing import Annotated
from uuid import UUID, uuid4

import jwt
from fastapi import Depends, FastAPI, Query, WebSocket, WebSocketDisconnect
from fastapi.encoders import jsonable_encoder
from fastapi.responses import JSONResponse, RedirectResponse
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer
from fastapi.staticfiles import StaticFiles
from fastapi.middleware.cors import CORSMiddleware
from .maps import MapService
from pydantic import BaseModel, Field, model_validator

from .account_service import AccountService
from .admin_routes import build_admin_router
from .admin_service import AdminService
from .console_service import ConsoleService
from .console_routes import build_console_router
from .config import Settings, get_settings
from .db import Database
from .errors import DomainError, ForbiddenError
from .events import events_after
from .queries import charger_list, station_list, urbanev_overview
from .security import decode_token, issue_token
from .service import ChargingService

logger = logging.getLogger(__name__)


class OtpRequest(BaseModel):
    phone: str = Field(pattern=r"^1[0-9]{10}$")


class OtpVerify(OtpRequest):
    code: str = Field(pattern=r"^[0-9]{6}$")


class AdminLogin(BaseModel):
    username: str = Field(min_length=1, max_length=64)
    password: str = Field(min_length=1, max_length=200)


class RechargeRequest(BaseModel):
    amount: Decimal = Field(gt=0, le=10_000, decimal_places=2)
    idempotency_key: UUID


class ReserveRequest(BaseModel):
    charger_id: UUID
    idempotency_key: UUID
    initial_soc: Decimal | None = Field(default=None, ge=0, lt=100)
    target_soc: Decimal | None = Field(default=None, gt=0, le=100)
    battery_kwh: Decimal | None = Field(default=None, gt=0, le=300)

    @model_validator(mode="after")
    def validate_target(self):
        values = (self.initial_soc, self.target_soc, self.battery_kwh)
        if any(v is not None for v in values):
            if any(v is None for v in values) or self.target_soc <= self.initial_soc:
                raise ValueError("请填写电池容量，充电上限必须高于当前电量")
        return self


class PreviewLocation(BaseModel):
    latitude: float = Field(ge=-85,le=85)
    longitude: float = Field(ge=-180,le=180)


class VehicleUpdate(BaseModel):
    vehicle_name: str = Field(min_length=1,max_length=80)
    vehicle_plate: str = Field(min_length=1,max_length=20)
    battery_kwh: Decimal = Field(gt=0,le=300,decimal_places=2)
    vehicle_soc: Decimal = Field(ge=0,le=100,decimal_places=2)
    charge_limit: Decimal = Field(gt=0,le=100,decimal_places=2)


class AvatarUpdate(BaseModel):
    png_base64: str = Field(min_length=32,max_length=270000)


class WithdrawalRequest(RechargeRequest):
    destination: str = Field(default="演示钱包",min_length=1,max_length=100)


class ProfileUpdate(BaseModel):
    nickname: str | None = Field(default=None, min_length=1, max_length=20)
    avatar_path: str | None = Field(default=None, min_length=1, max_length=300)


@dataclass(frozen=True)
class Principal:
    id: UUID
    role: str


bearer = HTTPBearer(auto_error=False)


def create_app(settings: Settings | None = None) -> FastAPI:
    actual_settings = settings or get_settings()
    database = Database(actual_settings)
    maps = MapService(actual_settings)

    async def coordinator(service: ChargingService) -> None:
        while True:
            try:
                await service.reconcile_once()
            except asyncio.CancelledError:
                raise
            except Exception:
                logger.exception("charging coordinator pass failed")
            await asyncio.sleep(actual_settings.coordinator_interval_seconds)

    @asynccontextmanager
    async def lifespan(app: FastAPI) -> AsyncIterator[None]:
        await database.connect()
        pool = database.require_pool()
        service = ChargingService(pool, actual_settings)
        app.state.account_service = AccountService(pool, actual_settings)
        app.state.admin_service = AdminService(pool)
        app.state.console_service = ConsoleService(pool, actual_settings)
        app.state.service = service
        task = asyncio.create_task(coordinator(service), name="charging-coordinator")
        try:
            yield
        finally:
            task.cancel()
            with suppress(asyncio.CancelledError):
                await task
            await database.close()

    app = FastAPI(
        title="Charging Core API",
        version="0.1.0",
        description="Server-authoritative EV charging business core",
        lifespan=lifespan,
    )

    app.add_middleware(CORSMiddleware,allow_origins=["https://lv-l40s-liuzihang.taild6df1c.ts.net"],allow_methods=["GET","POST"],allow_headers=["Content-Type"])

    @app.get("/public/map/geocode")
    async def geocode(address: str = Query(min_length=2,max_length=200)):
        return await maps.geocode(address)

    @app.get("/public/map/reverse")
    async def reverse(latitude: float = Query(ge=-85,le=85),longitude: float = Query(ge=-180,le=180)):
        return await maps.reverse(latitude,longitude)

    @app.get("/public/map/route")
    async def route(latitude: float = Query(ge=-85,le=85),longitude: float = Query(ge=-180,le=180),
                    to_latitude: float = Query(ge=-85,le=85),to_longitude: float = Query(ge=-180,le=180)):
        return await maps.route(latitude,longitude,to_latitude,to_longitude)

    @app.post("/public/map/preview-location")
    async def set_preview_location(body: PreviewLocation):
        maps.preview_location={**body.model_dump(),"updated_at":time.time()}
        return {"status":"ok"}

    @app.get("/public/map/preview-location")
    async def preview_location():
        return maps.preview_location if maps.preview_location and time.time()-maps.preview_location["updated_at"]<60 else {}

    @app.exception_handler(DomainError)
    async def domain_error_handler(_, exc: DomainError) -> JSONResponse:
        return JSONResponse(
            status_code=exc.status_code,
            content={"error": {"code": exc.code, "message": exc.message}},
        )

    def get_service() -> ChargingService:
        return app.state.service

    def get_account_service() -> AccountService:
        return app.state.account_service

    def get_admin_service() -> AdminService:
        return app.state.admin_service

    def get_principal(
        credentials: Annotated[HTTPAuthorizationCredentials | None, Depends(bearer)],
    ) -> Principal:
        if credentials is None or credentials.scheme.lower() != "bearer":
            from .errors import AuthenticationError

            raise AuthenticationError("缺少访问令牌")
        try:
            payload = decode_token(credentials.credentials, actual_settings)
            return Principal(UUID(payload["sub"]), payload["role"])
        except (jwt.InvalidTokenError, KeyError, TypeError, ValueError):
            from .errors import AuthenticationError

            raise AuthenticationError("访问令牌无效或已过期")

    def require_user(
        principal: Annotated[Principal, Depends(get_principal)],
    ) -> Principal:
        if principal.role != "user":
            raise ForbiddenError("该接口仅允许用户访问")
        return principal

    def require_admin(
        principal: Annotated[Principal, Depends(get_principal)],
    ) -> Principal:
        if principal.role != "admin":
            raise ForbiddenError("该接口仅允许管理员访问")
        return principal

    def get_console_service() -> ConsoleService:
        return app.state.console_service

    @app.post("/auth/console", tags=["admin-console"])
    async def console_login() -> dict:
        if not actual_settings.admin_console_enabled or not actual_settings.is_development:
            raise ForbiddenError("模拟管理员自动登录未启用")
        account = await database.require_pool().fetchrow(
            """
            INSERT INTO admin_account (id, username, password_hash)
            VALUES ($1, 'console-demo', '!')
            ON CONFLICT (username) DO UPDATE SET username = excluded.username
            RETURNING id, active
            """,
            uuid4(),
        )
        if not account["active"]:
            raise ForbiddenError("模拟管理员账号已停用")
        return {
            "access_token": issue_token(account["id"], "admin", actual_settings),
            "role": "admin",
            "name": "Console Administrator",
            "read_only": False,
        }

    @app.post("/auth/demo", tags=["demo"])
    async def demo_login() -> dict:
        if not actual_settings.demo_admin_enabled:
            raise ForbiddenError("演示管理员未启用")
        return {"access_token": issue_token(UUID(int=0), "demo_admin", actual_settings),
                "role": "demo_admin", "name": "演示管理员", "read_only": True}

    @app.get("/demo/analytics", tags=["demo"])
    async def demo_analytics(principal: Annotated[Principal, Depends(get_principal)]) -> dict:
        if not actual_settings.demo_admin_enabled or principal.role not in {"demo_admin", "admin"}:
            raise ForbiddenError("该接口仅允许演示管理员访问")
        data = await database.require_pool().fetchval("SELECT metadata FROM dataset_metadata WHERE name='UrbanEV-full'")
        if not data:
            return {"ready": False}
        return {"ready": True, **json.loads(data)}

    @app.get("/health", tags=["system"])
    async def health() -> dict[str, str]:
        await database.require_pool().fetchval("SELECT 1")
        return {"status": "ok"}

    @app.get("/public/stations", tags=["public"])
    async def public_stations(
        latitude: Annotated[Decimal | None, Query(ge=-90, le=90)] = None,
        longitude: Annotated[Decimal | None, Query(ge=-180, le=180)] = None,
    ) -> list[dict]:
        if (latitude is None) != (longitude is None):
            from .errors import DomainError

            raise DomainError("经纬度必须同时提供")
        return await station_list(database.require_pool(), latitude, longitude)

    @app.get("/public/stats/summary", tags=["public"])
    async def public_summary(
        service: Annotated[AdminService, Depends(get_admin_service)],
    ) -> dict:
        return await service.summary()

    @app.get("/public/stats/revenue", tags=["public"])
    async def public_revenue(
        service: Annotated[AdminService, Depends(get_admin_service)],
        days: int = Query(7, ge=1, le=365),
    ) -> list[dict]:
        return await service.revenue(days)

    @app.get("/public/analytics/urbanev", tags=["public"])
    async def public_urbanev() -> dict:
        return await urbanev_overview(database.require_pool())

    @app.get("/public/stations/{station_id}/chargers", tags=["public"])
    async def public_chargers(station_id: UUID) -> list[dict]:
        return await charger_list(database.require_pool(), station_id)

    @app.post("/auth/otp/request", tags=["auth"])
    async def request_otp(
        body: OtpRequest,
        service: Annotated[AccountService, Depends(get_account_service)],
    ) -> dict[str, str]:
        code = await service.request_otp(body.phone)
        response = {"status": "sent"}
        if code is not None:
            response["development_code"] = code
        return response

    @app.post("/auth/otp/verify", tags=["auth"])
    async def verify_otp(
        body: OtpVerify,
        service: Annotated[AccountService, Depends(get_account_service)],
    ) -> dict:
        return await service.verify_otp(body.phone, body.code)

    @app.post("/auth/admin/login", tags=["auth"])
    async def admin_login(
        body: AdminLogin,
        service: Annotated[AccountService, Depends(get_account_service)],
    ) -> dict[str, str]:
        return await service.admin_login(body.username, body.password)

    @app.post("/wallet/recharges", tags=["wallet"])
    async def recharge(
        body: RechargeRequest,
        principal: Annotated[Principal, Depends(require_user)],
        service: Annotated[AccountService, Depends(get_account_service)],
    ) -> dict:
        return await service.recharge(principal.id, body.amount, body.idempotency_key)

    @app.get("/me", tags=["account"])
    async def profile(
        principal: Annotated[Principal, Depends(require_user)],
        service: Annotated[AccountService, Depends(get_account_service)],
    ) -> dict:
        return await service.profile(principal.id)

    @app.patch("/me", tags=["account"])
    async def update_profile(
        body: ProfileUpdate,
        principal: Annotated[Principal, Depends(require_user)],
        service: Annotated[AccountService, Depends(get_account_service)],
    ) -> dict:
        if not body.model_fields_set:
            from .errors import DomainError

            raise DomainError("至少提供一个待修改字段")
        return await service.update_profile(
            principal.id, body.nickname, body.avatar_path
        )

    @app.put("/me/vehicle", tags=["account"])
    async def vehicle(body: VehicleUpdate, principal: Annotated[Principal,Depends(require_user)], service: Annotated[AccountService,Depends(get_account_service)]) -> dict:
        return await service.save_vehicle(principal.id,body.model_dump())

    @app.put("/me/avatar", tags=["account"])
    async def avatar(body: AvatarUpdate, principal: Annotated[Principal,Depends(require_user)], service: Annotated[AccountService,Depends(get_account_service)]) -> dict:
        return await service.save_avatar(principal.id,body.png_base64)

    @app.get("/me/statistics", tags=["account"])
    async def statistics(principal: Annotated[Principal,Depends(require_user)], service: Annotated[AccountService,Depends(get_account_service)],
                         period: str = Query("month",pattern="^(month|all)$")) -> dict:
        return await service.statistics(principal.id,period)

    @app.get("/me/withdrawals", tags=["account"])
    async def withdrawals(principal: Annotated[Principal,Depends(require_user)], service: Annotated[AccountService,Depends(get_account_service)]) -> list[dict]:
        return await service.withdrawals(principal.id)

    @app.get("/me/orders", tags=["account"])
    async def order_history(
        principal: Annotated[Principal, Depends(require_user)],
        service: Annotated[AccountService, Depends(get_account_service)],
        limit: int = Query(50, ge=1, le=200),
        offset: int = Query(0, ge=0),
    ) -> list[dict]:
        return await service.order_history(principal.id, limit=limit, offset=offset)

    @app.get("/me/wallet-entries", tags=["account"])
    async def wallet_history(
        principal: Annotated[Principal, Depends(require_user)],
        service: Annotated[AccountService, Depends(get_account_service)],
        limit: int = Query(50, ge=1, le=200),
        offset: int = Query(0, ge=0),
    ) -> list[dict]:
        return await service.wallet_history(principal.id, limit=limit, offset=offset)

    @app.get("/stations", tags=["stations"])
    async def stations(
        _: Annotated[Principal, Depends(require_user)],
        service: Annotated[ChargingService, Depends(get_service)],
        latitude: Annotated[Decimal | None, Query(ge=-90, le=90)] = None,
        longitude: Annotated[Decimal | None, Query(ge=-180, le=180)] = None,
    ) -> list[dict]:
        if (latitude is None) != (longitude is None):
            from .errors import DomainError

            raise DomainError("经纬度必须同时提供")
        return await station_list(service.pool, latitude, longitude)

    @app.get("/stations/{station_id}/chargers", tags=["stations"])
    async def chargers(
        station_id: UUID,
        _: Annotated[Principal, Depends(require_user)],
        service: Annotated[ChargingService, Depends(get_service)],
    ) -> list[dict]:
        return await charger_list(service.pool, station_id)

    @app.post("/orders", tags=["charging"])
    async def reserve(
        body: ReserveRequest,
        principal: Annotated[Principal, Depends(require_user)],
        service: Annotated[ChargingService, Depends(get_service)],
    ) -> dict:
        return await service.reserve(
            principal.id, body.charger_id, body.idempotency_key,
            initial_soc=body.initial_soc, target_soc=body.target_soc, battery_kwh=body.battery_kwh,
        )

    @app.post("/wallet/withdrawals", tags=["wallet"])
    async def withdraw(
        body: WithdrawalRequest,
        principal: Annotated[Principal, Depends(require_user)],
        service: Annotated[AccountService, Depends(get_account_service)],
    ) -> dict:
        return await service.withdraw(principal.id, body.amount, body.idempotency_key, body.destination)

    @app.post("/orders/{order_id}/start", tags=["charging"])
    async def start(
        order_id: UUID,
        principal: Annotated[Principal, Depends(require_user)],
        service: Annotated[ChargingService, Depends(get_service)],
    ) -> dict:
        return await service.start(principal.id, order_id)

    @app.post("/orders/{order_id}/cancel", tags=["charging"])
    async def cancel(
        order_id: UUID,
        principal: Annotated[Principal, Depends(require_user)],
        service: Annotated[ChargingService, Depends(get_service)],
    ) -> dict:
        return await service.cancel(principal.id, order_id)

    @app.get("/orders/{order_id}", tags=["charging"])
    async def order(
        order_id: UUID,
        principal: Annotated[Principal, Depends(require_user)],
        service: Annotated[ChargingService, Depends(get_service)],
    ) -> dict:
        return await service.get_order(principal.id, order_id)

    @app.post("/orders/{order_id}/stop", tags=["charging"])
    async def stop(
        order_id: UUID,
        principal: Annotated[Principal, Depends(require_user)],
        service: Annotated[ChargingService, Depends(get_service)],
    ) -> dict:
        return await service.stop(principal.id, order_id)

    @app.post("/orders/{order_id}/pay", tags=["charging"])
    async def pay(order_id: UUID, principal: Annotated[Principal,Depends(require_user)], service: Annotated[ChargingService,Depends(get_service)]) -> dict:
        return await service.pay(principal.id,order_id)

    @app.websocket("/ws")
    async def websocket_events(
        websocket: WebSocket,
        token: Annotated[str, Query()],
        after: Annotated[int, Query(ge=0)] = 0,
    ) -> None:
        try:
            payload = decode_token(token, actual_settings)
            role = payload["role"]
            user_id = UUID(payload["sub"]) if role == "user" else None
        except (jwt.InvalidTokenError, KeyError, TypeError, ValueError):
            await websocket.close(code=4401, reason="invalid token")
            return
        await websocket.accept()
        cursor = after
        service: ChargingService = app.state.service
        try:
            while True:
                events = await events_after(
                    service.pool, cursor=cursor, user_id=user_id, role=role
                )
                for event in events:
                    cursor = event["id"]
                    await websocket.send_json(jsonable_encoder(event))
                try:
                    message = await asyncio.wait_for(websocket.receive(), timeout=1.0)
                    if message["type"] == "websocket.disconnect":
                        return
                except TimeoutError:
                    pass
        except (WebSocketDisconnect, RuntimeError):
            return

    app.include_router(build_admin_router(get_admin_service, require_admin))
    app.include_router(build_console_router(get_console_service, require_admin))

    static_dir = Path(__file__).with_name("static")
    if static_dir.is_dir():

        @app.get("/", include_in_schema=False)
        async def frontend() -> RedirectResponse:
            return RedirectResponse("/ui/dashboard.html")

        app.mount("/ui", StaticFiles(directory=static_dir, html=True), name="ui")

    return app
