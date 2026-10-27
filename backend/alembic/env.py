"""Alembic migration environment supporting both SQLite and PostgreSQL."""

from __future__ import annotations

import os
from logging.config import fileConfig
from pathlib import Path

from alembic import context
from sqlalchemy import engine_from_config, pool

from backend.app.db.models import Base

config = context.config
if config.config_file_name is not None:
    fileConfig(config.config_file_name)

target_metadata = Base.metadata


def get_sync_url() -> str:
    env_url = os.getenv("MRC_SYNC_DATABASE_URL") or os.getenv("MRC_DATABASE_URL")
    if env_url:
        sync_url = env_url.replace("sqlite+aiosqlite://", "sqlite://").replace(
            "postgresql+asyncpg://", "postgresql://"
        )
    else:
        sync_url = config.get_main_option("sqlalchemy.url", "sqlite:///./data/mrc.db")

    if sync_url.startswith("sqlite:///"):
        raw_path = sync_url[len("sqlite:///") :].split("?", 1)[0]
        if raw_path and raw_path != ":memory:":
            Path(raw_path).parent.mkdir(parents=True, exist_ok=True)
    return sync_url


def run_migrations_offline() -> None:
    url = get_sync_url()
    context.configure(
        url=url,
        target_metadata=target_metadata,
        literal_binds=True,
        dialect_opts={"paramstyle": "named"},
        render_as_batch=url.startswith("sqlite"),
    )
    with context.begin_transaction():
        context.run_migrations()


def run_migrations_online() -> None:
    url = get_sync_url()
    configuration = config.get_section(config.config_ini_section, {})
    configuration["sqlalchemy.url"] = url

    connectable = engine_from_config(
        configuration,
        prefix="sqlalchemy.",
        poolclass=pool.NullPool,
    )

    with connectable.connect() as connection:
        context.configure(
            connection=connection,
            target_metadata=target_metadata,
            render_as_batch=url.startswith("sqlite"),
        )
        with context.begin_transaction():
            context.run_migrations()


if context.is_offline_mode():
    run_migrations_offline()
else:
    run_migrations_online()
