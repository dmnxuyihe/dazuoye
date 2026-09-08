from __future__ import annotations

import argparse
import asyncio
import getpass
import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path

import asyncpg
import uvicorn

from .config import get_settings
from .db import initialize_schema
from .security import hash_password
from .seed import seed_demo


def configure_logging(log_dir: str) -> None:
    directory = Path(log_dir)
    directory.mkdir(parents=True, exist_ok=True)
    handler = RotatingFileHandler(
        directory / "charging-core.log",
        maxBytes=5 * 1024 * 1024,
        backupCount=3,
        encoding="utf-8",
    )
    handler.setFormatter(
        logging.Formatter("%(asctime)s %(levelname)s %(name)s %(message)s")
    )
    logging.basicConfig(level=logging.INFO, handlers=[handler, logging.StreamHandler()])


async def init_db() -> None:
    settings = get_settings()
    await initialize_schema(settings)
    pool = await asyncpg.create_pool(
        settings.database_url,
        min_size=1,
        max_size=2,
        server_settings={"search_path": f"{settings.database_schema},public"},
    )
    try:
        await seed_demo(pool)
    finally:
        await pool.close()
    print(f"initialized schema: {settings.database_schema}")


async def set_admin_password(username: str) -> None:
    settings = get_settings()
    password = getpass.getpass("New administrator password: ")
    confirmation = getpass.getpass("Repeat password: ")
    if len(password) < 10:
        raise SystemExit("password must contain at least 10 characters")
    if password != confirmation:
        raise SystemExit("passwords do not match")
    connection = await asyncpg.connect(
        settings.database_url,
        server_settings={"search_path": f"{settings.database_schema},public"},
    )
    try:
        result = await connection.execute(
            """
            UPDATE admin_account
            SET password_hash = $2, failed_attempts = 0, locked_until = NULL
            WHERE username = $1
            """,
            username,
            hash_password(password),
        )
    finally:
        await connection.close()
    if result == "UPDATE 0":
        raise SystemExit(f"administrator not found: {username}")
    print(f"updated administrator password: {username}")


def main() -> None:
    parser = argparse.ArgumentParser(prog="charging-core")
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser(
        "init-db", help="create schema and insert idempotent demo data"
    )
    password = subparsers.add_parser(
        "set-admin-password", help="securely replace an administrator password"
    )
    password.add_argument("--username", default="admin")
    serve = subparsers.add_parser("serve", help="run the API server")
    serve.add_argument("--host", default="127.0.0.1")
    serve.add_argument("--port", default=8000, type=int)
    args = parser.parse_args()

    try:
        if args.command == "init-db":
            asyncio.run(init_db())
        elif args.command == "set-admin-password":
            asyncio.run(set_admin_password(args.username))
        elif args.command == "serve":
            configure_logging(get_settings().log_dir)
            uvicorn.run(
                "charging_core.api:create_app",
                factory=True,
                host=args.host,
                port=args.port,
            )
    except (TimeoutError, OSError, asyncpg.PostgresError) as exc:
        detail = str(exc) or type(exc).__name__
        parser.exit(2, f"database operation failed: {detail}\n")


if __name__ == "__main__":
    main()
