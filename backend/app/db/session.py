"""Async SQLAlchemy engine, session factory, and SQLite pragma setup."""

from __future__ import annotations

from pathlib import Path
from typing import AsyncIterator

from sqlalchemy import event
from sqlalchemy.ext.asyncio import (
    AsyncEngine,
    AsyncSession,
    async_sessionmaker,
    create_async_engine,
)

from backend.app.db.models import Base


def _ensure_sqlite_parent_dir(database_url: str) -> None:
    prefixes = ("sqlite+aiosqlite:///", "sqlite:///")
    for prefix in prefixes:
        if database_url.startswith(prefix):
            raw_path = database_url[len(prefix) :]
            if raw_path and raw_path != ":memory:" and not raw_path.startswith("file::memory:"):
                db_path = Path(raw_path.split("?", 1)[0])
                db_path.parent.mkdir(parents=True, exist_ok=True)
            break


def build_async_engine(database_url: str) -> AsyncEngine:
    _ensure_sqlite_parent_dir(database_url)
    is_sqlite = database_url.startswith("sqlite")
    connect_args = {"check_same_thread": False, "timeout": 15} if is_sqlite else {}
    engine = create_async_engine(
        database_url,
        future=True,
        pool_pre_ping=not is_sqlite,
        connect_args=connect_args,
    )
    if is_sqlite:
        @event.listens_for(engine.sync_engine, "connect")
        def _set_sqlite_pragma(dbapi_connection, _connection_record) -> None:  # type: ignore[no-untyped-def]
            cursor = dbapi_connection.cursor()
            cursor.execute("PRAGMA foreign_keys=ON;")
            if ":memory:" not in database_url:
                cursor.execute("PRAGMA journal_mode=WAL;")
            cursor.execute("PRAGMA busy_timeout=5000;")
            cursor.close()

    return engine


def build_session_factory(engine: AsyncEngine) -> async_sessionmaker[AsyncSession]:
    return async_sessionmaker(engine, expire_on_commit=False, class_=AsyncSession)


async def init_schema_if_needed(engine: AsyncEngine) -> None:
    """Create tables if not already migrated (safe idempotent fallback for test/ephemeral DBs)."""
    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)


async def yield_session(
    session_factory: async_sessionmaker[AsyncSession],
) -> AsyncIterator[AsyncSession]:
    async with session_factory() as session:
        yield session
