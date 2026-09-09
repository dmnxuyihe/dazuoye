import json
from typing import Any
from uuid import UUID

import asyncpg


async def emit_event(
    connection: asyncpg.Connection,
    event_type: str,
    payload: dict[str, Any],
    *,
    audience_user_id: UUID | None = None,
    audience_role: str | None = None,
) -> None:
    await connection.execute(
        """
        INSERT INTO domain_event
            (audience_user_id, audience_role, event_type, payload)
        VALUES ($1, $2, $3, $4::jsonb)
        """,
        audience_user_id,
        audience_role,
        event_type,
        json.dumps(payload, default=str, ensure_ascii=False),
    )


async def events_after(
    pool: asyncpg.Pool,
    *,
    cursor: int,
    user_id: UUID | None,
    role: str,
) -> list[dict[str, Any]]:
    rows = await pool.fetch(
        """
        SELECT id, event_type, payload, created_at
        FROM domain_event
        WHERE id > $1
          AND (
              (audience_user_id = $2)
              OR audience_role = $3
              OR (audience_user_id IS NULL AND audience_role IS NULL)
          )
        ORDER BY id LIMIT 100
        """,
        cursor,
        user_id,
        role,
    )
    return [dict(row) for row in rows]
