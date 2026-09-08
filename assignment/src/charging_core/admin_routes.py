from collections.abc import Callable
from decimal import Decimal
from typing import Annotated, Any, Literal
from uuid import UUID

from fastapi import APIRouter, Depends, Query, Response, status
from pydantic import BaseModel, Field, model_validator

from .admin_service import AdminService


class UserStatusUpdate(BaseModel):
    status: Literal["active", "frozen"]


class StationCreate(BaseModel):
    name: str = Field(min_length=1, max_length=100)
    address: str = Field(min_length=1, max_length=200)
    longitude: Decimal = Field(ge=-180, le=180, decimal_places=6)
    latitude: Decimal = Field(ge=-90, le=90, decimal_places=6)
    unit_price: Decimal = Field(gt=0, le=9999, decimal_places=2)


class StationUpdate(BaseModel):
    name: str | None = Field(default=None, min_length=1, max_length=100)
    address: str | None = Field(default=None, min_length=1, max_length=200)
    longitude: Decimal | None = Field(default=None, ge=-180, le=180, decimal_places=6)
    latitude: Decimal | None = Field(default=None, ge=-90, le=90, decimal_places=6)
    unit_price: Decimal | None = Field(default=None, gt=0, le=9999, decimal_places=2)

    @model_validator(mode="after")
    def at_least_one_field(self) -> "StationUpdate":
        if not self.model_fields_set:
            raise ValueError("至少提供一个待修改字段")
        return self


class ChargerCreate(BaseModel):
    station_id: UUID
    code: str = Field(min_length=1, max_length=40)
    kind: Literal["fast", "slow"]
    power_kw: Decimal = Field(gt=0, le=1000, decimal_places=2)


class ChargerStatusUpdate(BaseModel):
    status: Literal["available", "faulted"]


def build_admin_router(
    get_service: Callable[[], AdminService],
    require_admin: Callable[..., Any],
) -> APIRouter:
    router = APIRouter(prefix="/admin", tags=["admin"])

    @router.get("/users")
    async def users(
        _: Annotated[Any, Depends(require_admin)],
        service: Annotated[AdminService, Depends(get_service)],
        search: str | None = None,
        user_status: Literal["active", "frozen"] | None = Query(None, alias="status"),
        limit: int = Query(50, ge=1, le=200),
        offset: int = Query(0, ge=0),
    ) -> list[dict]:
        return await service.list_users(
            search=search, status=user_status, limit=limit, offset=offset
        )

    @router.patch("/users/{user_id}/status")
    async def set_user_status(
        user_id: UUID,
        body: UserStatusUpdate,
        principal: Annotated[Any, Depends(require_admin)],
        service: Annotated[AdminService, Depends(get_service)],
    ) -> dict:
        return await service.set_user_status(principal.id, user_id, body.status)

    @router.get("/orders")
    async def orders(
        _: Annotated[Any, Depends(require_admin)],
        service: Annotated[AdminService, Depends(get_service)],
        order_status: Literal["reserved", "charging", "completed", "cancelled"]
        | None = Query(None, alias="status"),
        limit: int = Query(50, ge=1, le=200),
        offset: int = Query(0, ge=0),
    ) -> list[dict]:
        return await service.list_orders(
            status=order_status, limit=limit, offset=offset
        )

    @router.get("/stations")
    async def stations(
        _: Annotated[Any, Depends(require_admin)],
        service: Annotated[AdminService, Depends(get_service)],
    ) -> list[dict]:
        return await service.list_stations()

    @router.post("/stations", status_code=status.HTTP_201_CREATED)
    async def create_station(
        body: StationCreate,
        principal: Annotated[Any, Depends(require_admin)],
        service: Annotated[AdminService, Depends(get_service)],
    ) -> dict:
        return await service.create_station(principal.id, **body.model_dump())

    @router.patch("/stations/{station_id}")
    async def update_station(
        station_id: UUID,
        body: StationUpdate,
        principal: Annotated[Any, Depends(require_admin)],
        service: Annotated[AdminService, Depends(get_service)],
    ) -> dict:
        return await service.update_station(
            principal.id, station_id, **body.model_dump()
        )

    @router.delete("/stations/{station_id}", status_code=status.HTTP_204_NO_CONTENT)
    async def delete_station(
        station_id: UUID,
        principal: Annotated[Any, Depends(require_admin)],
        service: Annotated[AdminService, Depends(get_service)],
    ) -> Response:
        await service.delete_station(principal.id, station_id)
        return Response(status_code=status.HTTP_204_NO_CONTENT)

    @router.get("/chargers")
    async def chargers(
        _: Annotated[Any, Depends(require_admin)],
        service: Annotated[AdminService, Depends(get_service)],
        station_id: UUID | None = None,
        charger_status: Literal["available", "reserved", "charging", "faulted"]
        | None = Query(None, alias="status"),
    ) -> list[dict]:
        return await service.list_chargers(station_id, charger_status)

    @router.post("/chargers", status_code=status.HTTP_201_CREATED)
    async def create_charger(
        body: ChargerCreate,
        principal: Annotated[Any, Depends(require_admin)],
        service: Annotated[AdminService, Depends(get_service)],
    ) -> dict:
        return await service.create_charger(principal.id, **body.model_dump())

    @router.patch("/chargers/{charger_id}/status")
    async def set_charger_status(
        charger_id: UUID,
        body: ChargerStatusUpdate,
        principal: Annotated[Any, Depends(require_admin)],
        service: Annotated[AdminService, Depends(get_service)],
    ) -> dict:
        return await service.set_charger_status(principal.id, charger_id, body.status)

    @router.post("/chargers/{charger_id}/restart")
    async def restart_charger(
        charger_id: UUID,
        principal: Annotated[Any, Depends(require_admin)],
        service: Annotated[AdminService, Depends(get_service)],
    ) -> dict:
        return await service.restart_charger(principal.id, charger_id)

    @router.delete("/chargers/{charger_id}", status_code=status.HTTP_204_NO_CONTENT)
    async def delete_charger(
        charger_id: UUID,
        principal: Annotated[Any, Depends(require_admin)],
        service: Annotated[AdminService, Depends(get_service)],
    ) -> Response:
        await service.delete_charger(principal.id, charger_id)
        return Response(status_code=status.HTTP_204_NO_CONTENT)

    @router.get("/stats/summary")
    async def summary(
        _: Annotated[Any, Depends(require_admin)],
        service: Annotated[AdminService, Depends(get_service)],
    ) -> dict:
        return await service.summary()

    @router.get("/stats/revenue")
    async def revenue(
        _: Annotated[Any, Depends(require_admin)],
        service: Annotated[AdminService, Depends(get_service)],
        days: int = Query(7, ge=1, le=365),
    ) -> list[dict]:
        return await service.revenue(days)

    @router.get("/ops-logs")
    async def ops_logs(
        _: Annotated[Any, Depends(require_admin)],
        service: Annotated[AdminService, Depends(get_service)],
        limit: int = Query(50, ge=1, le=200),
        offset: int = Query(0, ge=0),
    ) -> list[dict]:
        return await service.ops_logs(limit, offset)

    return router
