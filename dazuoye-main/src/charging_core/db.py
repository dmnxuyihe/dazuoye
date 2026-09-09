from __future__ import annotations

from pathlib import Path
from typing import Any

import asyncpg

from .config import Settings


class Database:
    def __init__(self, settings: Settings) -> None:
        self.settings = settings
        self.pool: asyncpg.Pool | None = None

    async def connect(self) -> None:
        self.pool = await asyncpg.create_pool(
            self.settings.database_url,
            min_size=1,
            max_size=10,
            command_timeout=10,
            server_settings={
                "application_name": "charging-core",
                "search_path": f"{self.settings.database_schema},public",
                "timezone": "UTC",
            },
        )

    async def close(self) -> None:
        if self.pool is not None:
            await self.pool.close()
            self.pool = None

    def require_pool(self) -> asyncpg.Pool:
        if self.pool is None:
            raise RuntimeError("database is not connected")
        return self.pool


async def initialize_schema(settings: Settings) -> None:
    template = Path(__file__).with_name("schema.sql").read_text(encoding="utf-8")
    sql = template.format(schema=settings.database_schema)
    connection = await asyncpg.connect(settings.database_url, timeout=10)
    try:
        await connection.execute(sql)
    finally:
        await connection.close()


def record_to_dict(record: asyncpg.Record | None) -> dict[str, Any] | None:
    return dict(record) if record is not None else None
