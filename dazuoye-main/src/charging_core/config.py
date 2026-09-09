from __future__ import annotations

import re
from functools import lru_cache

from pydantic import Field, field_validator
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_file=(".env", ".env.local"),
        env_prefix="CHARGING_",
        extra="ignore",
    )

    database_url: str
    database_schema: str = "charging_core"
    jwt_secret: str = Field(min_length=32)
    environment: str = "development"
    demo_admin_enabled: bool = False
    admin_console_enabled: bool = False
    dev_otp_code: str = Field(default="246810", pattern=r"^[0-9]{6}$")
    access_token_minutes: int = Field(default=60, ge=5, le=1440)
    time_scale: int = Field(default=60, ge=1, le=3600)
    reservation_minutes: int = Field(default=15, ge=1, le=120)
    minimum_start_balance: float = Field(default=5.0, gt=0)
    coordinator_interval_seconds: float = Field(default=1.0, ge=0.2, le=30)
    log_dir: str = "logs"
    geocoding_url: str = "https://nominatim.openstreetmap.org"
    routing_url: str = "https://router.project-osrm.org"

    @field_validator("database_schema")
    @classmethod
    def validate_schema(cls, value: str) -> str:
        if not re.fullmatch(r"[a-z_][a-z0-9_]{0,62}", value):
            raise ValueError("database_schema must be a safe PostgreSQL identifier")
        return value

    @property
    def is_development(self) -> bool:
        return self.environment.lower() == "development"


@lru_cache
def get_settings() -> Settings:
    return Settings()  # type: ignore[call-arg]
