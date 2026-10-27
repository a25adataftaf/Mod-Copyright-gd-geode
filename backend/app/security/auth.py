"""Constant-time authentication helpers for administrative and review endpoints."""

from __future__ import annotations

import secrets
from typing import Optional

from fastapi import Header, HTTPException, Request, status


def verify_admin_credentials(
    request: Request,
    authorization: Optional[str] = Header(default=None),
    x_mrc_admin_key: Optional[str] = Header(default=None),
) -> str:
    """
    Validate reviewer/admin API key using constant-time comparison.
    Never logs raw credentials or returns secret details in error responses.
    """
    configured_key: str = getattr(request.app.state, "settings").admin_api_key
    if not configured_key:
        raise HTTPException(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            detail="Administrative review endpoints are disabled until MRC_ADMIN_API_KEY is configured.",
        )

    candidate = ""
    if authorization and authorization.lower().startswith("bearer "):
        candidate = authorization[7:].strip()
    elif x_mrc_admin_key:
        candidate = x_mrc_admin_key.strip()

    if not candidate or not secrets.compare_digest(
        candidate.encode("utf-8"), configured_key.encode("utf-8")
    ):
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid or missing administrative credentials.",
            headers={"WWW-Authenticate": "Bearer"},
        )

    return "admin:authenticated"
