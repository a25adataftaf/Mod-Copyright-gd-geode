"""Bounded sliding-window rate limiter protecting public and AI-backed endpoints."""

from __future__ import annotations

import time
from collections import deque
from dataclasses import dataclass
from threading import Lock


@dataclass(frozen=True)
class RateLimitDecision:
    allowed: bool
    remaining: int
    retry_after_seconds: int


class SlidingWindowRateLimiter:
    """Thread-safe in-memory sliding-window rate limiter with bounded bucket eviction."""

    def __init__(self, max_requests: int, window_seconds: int = 60, max_keys: int = 10_000) -> None:
        self._max_requests = max(1, max_requests)
        self._window_seconds = max(1, window_seconds)
        self._max_keys = max(100, max_keys)
        self._buckets: dict[str, deque[float]] = {}
        self._lock = Lock()
        self._last_prune = 0.0
        self.rejected_count = 0

    def check(self, key: str) -> RateLimitDecision:
        now = time.monotonic()
        cutoff = now - self._window_seconds
        with self._lock:
            if len(self._buckets) > self._max_keys and (now - self._last_prune) > 15.0:
                self._prune_locked(cutoff)
                self._last_prune = now

            bucket = self._buckets.get(key)
            if bucket is None:
                bucket = deque()
                self._buckets[key] = bucket

            while bucket and bucket[0] <= cutoff:
                bucket.popleft()

            if len(bucket) >= self._max_requests:
                self.rejected_count += 1
                oldest = bucket[0]
                retry_after = max(1, int(self._window_seconds - (now - oldest)) + 1)
                return RateLimitDecision(allowed=False, remaining=0, retry_after_seconds=retry_after)

            bucket.append(now)
            remaining = max(0, self._max_requests - len(bucket))
            return RateLimitDecision(allowed=True, remaining=remaining, retry_after_seconds=0)

    def _prune_locked(self, cutoff: float) -> None:
        stale_keys = [
            k for k, dq in self._buckets.items() if not dq or dq[-1] <= cutoff
        ]
        for k in stale_keys:
            self._buckets.pop(k, None)
        if len(self._buckets) > self._max_keys:
            # Drop oldest half if still above cap under distributed abuse
            overflow = len(self._buckets) - (self._max_keys // 2)
            for k in list(self._buckets.keys())[:overflow]:
                self._buckets.pop(k, None)
