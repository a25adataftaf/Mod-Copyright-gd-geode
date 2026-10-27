"""FastAPI application factory for the centralized Music Rights Checker backend."""

from __future__ import annotations

import logging
from contextlib import asynccontextmanager
from typing import AsyncIterator, Optional

from fastapi import FastAPI, Request, status
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse

from backend.app.api.v1.routes import router as v1_router
from backend.app.config import Settings
from backend.app.db.session import (
    build_async_engine,
    build_session_factory,
    init_schema_if_needed,
)
from backend.app.research.ai_providers import (
    AIResearchAdapter,
    ConfigurableHttpAIAdapter,
    DailyBudgetTracker,
)
from backend.app.security.rate_limit import SlidingWindowRateLimiter
from backend.app.services.rights_service import RightsService

logger = logging.getLogger("mrc.backend")


def create_app(
    settings: Optional[Settings] = None,
    ai_adapter_override: Optional[AIResearchAdapter] = None,
) -> FastAPI:
    cfg = settings or Settings.from_env()

    @asynccontextmanager
    async def lifespan(app: FastAPI) -> AsyncIterator[None]:
        engine = build_async_engine(cfg.database_url)
        await init_schema_if_needed(engine)
        session_factory = build_session_factory(engine)
        budget = DailyBudgetTracker(cfg.daily_ai_call_budget)
        adapter = ai_adapter_override or ConfigurableHttpAIAdapter(cfg, budget)

        app.state.settings = cfg
        app.state.engine = engine
        app.state.session_factory = session_factory
        app.state.general_limiter = SlidingWindowRateLimiter(
            max_requests=cfg.rate_limit_requests_per_minute, window_seconds=60
        )
        app.state.report_limiter = SlidingWindowRateLimiter(
            max_requests=cfg.report_rate_limit_per_minute, window_seconds=60
        )
        app.state.rights_service = RightsService(
            session_factory=session_factory,
            settings=cfg,
            ai_adapter=adapter,
            budget_tracker=budget,
        )
        try:
            yield
        finally:
            await app.state.rights_service.wait_for_background_tasks(timeout=5.0)
            await engine.dispose()

    app = FastAPI(
        title=cfg.app_name,
        version=cfg.app_version,
        description=(
            "Centralized API and evidence-based music rights research service for the "
            "Geometry Dash Music Rights Checker Geode mod."
        ),
        lifespan=lifespan,
    )

    app.add_middleware(
        CORSMiddleware,
        allow_origins=["*"],
        allow_credentials=False,
        allow_methods=["GET", "POST", "PATCH", "OPTIONS"],
        allow_headers=["Authorization", "Content-Type", "X-MRC-Admin-Key", "X-MRC-Client"],
    )

    @app.exception_handler(Exception)
    async def _unhandled_exception_handler(_request: Request, exc: Exception) -> JSONResponse:
        # Log exception class internally without leaking sensitive environment/SQL details externally
        logger.error("Unhandled server error (%s)", type(exc).__name__)
        return JSONResponse(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            content={"detail": "Internal server error. Please retry later."},
        )

    @app.get("/", tags=["meta"])
    async def root() -> dict[str, str]:
        return {
            "service": cfg.app_name,
            "version": cfg.app_version,
            "api_v1": f"{cfg.api_prefix}/health",
            "docs": "/docs",
        }

    app.include_router(v1_router)
    return app


app = create_app()
