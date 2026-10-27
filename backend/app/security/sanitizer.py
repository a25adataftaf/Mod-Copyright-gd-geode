"""Security controls for untrusted external content, SSRF prevention, and prompt-injection defense."""

from __future__ import annotations

import html
import ipaddress
import re
import socket
from html.parser import HTMLParser
from typing import Optional
from urllib.parse import urlparse, urlunparse

# Zero-width, bidi-override, and non-printable control characters stripped from untrusted inputs
_DANGEROUS_UNICODE_RE = re.compile(
    r"[\x00-\x08\x0b\x0c\x0e-\x1f\x7f\u200b-\u200f\u202a-\u202e\u2060-\u2069\ufeff]"
)

# Patterns indicative of prompt injection or instruction hijacking embedded in song metadata/pages
_INJECTION_PATTERNS: tuple[tuple[str, re.Pattern[str]], ...] = (
    (
        "instruction_override",
        re.compile(
            r"\b(ignore|disregard|forget|override)\s+(all\s+|any\s+)?(previous|prior|above|system|developer)\s+"
            r"(instructions?|prompts?|rules?|policies|guidelines)\b",
            re.IGNORECASE,
        ),
    ),
    (
        "role_impersonation",
        re.compile(
            r"(<\|im_start\|>|<\|system\|>|\[INST\]|###\s*(system|instruction)\s*:|"
            r"\byou\s+are\s+now\s+in\s+developer\s+mode\b)",
            re.IGNORECASE,
        ),
    ),
    (
        "rights_verdict_manipulation",
        re.compile(
            r"\b(classify|mark|set|return|output)\s+(this\s+song|overall_status|monetization|commercial_use)"
            r"\s+(as|to|=)\s*['\"]?(permitted|safe|allowed|public_domain)\b",
            re.IGNORECASE,
        ),
    ),
    (
        "tool_invocation_spoof",
        re.compile(
            r"\b(execute_command|tool_call|function_call|curl\s+http|DROP\s+TABLE)\b",
            re.IGNORECASE,
        ),
    ),
)

_BLOCKED_HOSTNAMES = frozenset(
    {
        "localhost",
        "localhost.localdomain",
        "metadata.google.internal",
        "metadata",
        "ip6-localhost",
        "ip6-loopback",
    }
)


class _SafeVisibleHTMLParser(HTMLParser):
    """Extracts visible text from HTML while discarding script, style, hidden nodes, and comments."""

    _SKIP_TAGS = frozenset({"script", "style", "noscript", "iframe", "object", "embed", "svg", "template"})

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self._skip_depth = 0
        self._hidden_depth = 0
        self.chunks: list[str] = []
        self.links: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, Optional[str]]]) -> None:
        lower_tag = tag.lower()
        attr_map = {k.lower(): (v or "") for k, v in attrs}
        if lower_tag in self._SKIP_TAGS:
            self._skip_depth += 1
            return
        style_val = attr_map.get("style", "").lower().replace(" ", "")
        if (
            "hidden" in attr_map
            or attr_map.get("aria-hidden", "").lower() == "true"
            or "display:none" in style_val
            or "visibility:hidden" in style_val
            or "font-size:0" in style_val
        ):
            self._hidden_depth += 1
            return
        if lower_tag == "a" and "href" in attr_map and self._skip_depth == 0 and self._hidden_depth == 0:
            href = attr_map["href"].strip()
            if href.startswith(("http://", "https://")):
                self.links.append(href)
        if lower_tag in {"br", "p", "div", "li", "tr", "h1", "h2", "h3", "h4"}:
            self.chunks.append("\n")

    def handle_endtag(self, tag: str) -> None:
        lower_tag = tag.lower()
        if lower_tag in self._SKIP_TAGS and self._skip_depth > 0:
            self._skip_depth -= 1
            return
        if self._hidden_depth > 0 and lower_tag in {"div", "span", "p", "section", "aside"}:
            self._hidden_depth -= 1

    def handle_data(self, data: str) -> None:
        if self._skip_depth == 0 and self._hidden_depth == 0:
            self.chunks.append(data)

    def handle_comment(self, _data: str) -> None:
        # Never expose HTML comments to downstream parsers or AI models
        return


def sanitize_untrusted_text(raw: str, max_length: int = 4000) -> str:
    """Normalize and strip dangerous control/invisible characters from untrusted text."""
    if not raw:
        return ""
    unescaped = html.unescape(raw)
    cleaned = _DANGEROUS_UNICODE_RE.sub("", unescaped)
    # Collapse excessive blank lines and horizontal whitespace while preserving line breaks
    lines = [" ".join(line.split()) for line in cleaned.splitlines()]
    compact = "\n".join(line for line in lines if line)
    return compact[:max_length].strip()


def detect_prompt_injection(raw: str) -> list[str]:
    """Return security flag identifiers if untrusted input contains prompt-injection patterns."""
    if not raw:
        return []
    normalized = _DANGEROUS_UNICODE_RE.sub("", html.unescape(raw))
    flags: list[str] = []
    for name, pattern in _INJECTION_PATTERNS:
        if pattern.search(normalized):
            flags.append(f"untrusted_input:{name}")
    return flags


def extract_visible_html(raw_html: str, max_text_length: int = 12_000) -> tuple[str, list[str], list[str]]:
    """
    Parse untrusted HTML into (sanitized_visible_text, outbound_http_links, security_flags).
    """
    security_flags = detect_prompt_injection(raw_html)
    parser = _SafeVisibleHTMLParser()
    try:
        parser.feed(raw_html[:262_144])
        parser.close()
    except Exception:
        security_flags.append("untrusted_input:malformed_html")
    visible_text = sanitize_untrusted_text("".join(parser.chunks), max_length=max_text_length)
    for flag in detect_prompt_injection(visible_text):
        if flag not in security_flags:
            security_flags.append(flag)
    unique_links: list[str] = []
    seen = set()
    for link in parser.links:
        if link not in seen and len(unique_links) < 50:
            seen.add(link)
            unique_links.append(link)
    return visible_text, unique_links, security_flags


def _is_public_ip(ip_str: str) -> bool:
    try:
        addr = ipaddress.ip_address(ip_str)
    except ValueError:
        return False
    return (
        addr.is_global
        and not addr.is_private
        and not addr.is_loopback
        and not addr.is_link_local
        and not addr.is_multicast
        and not addr.is_reserved
        and not addr.is_unspecified
    )


def validate_public_http_url(url: Optional[str], resolve_dns: bool = False) -> Optional[str]:
    """
    Validate that a URL is a safe public HTTP(S) URL (mitigating SSRF and scheme injection).
    Returns normalized URL if valid, or None if unsafe/invalid.
    """
    if not url:
        return None
    candidate = url.strip()
    if not candidate or len(candidate) > 1024 or _DANGEROUS_UNICODE_RE.search(candidate):
        return None
    try:
        parsed = urlparse(candidate)
    except ValueError:
        return None

    if parsed.scheme.lower() not in {"http", "https"}:
        return None
    if parsed.username or parsed.password:
        return None

    hostname = (parsed.hostname or "").strip().lower().rstrip(".")
    if not hostname or hostname in _BLOCKED_HOSTNAMES:
        return None
    if hostname.endswith((".local", ".internal", ".localhost", ".arpa")):
        return None

    try:
        port = parsed.port
    except ValueError:
        return None
    if port is not None and port not in {80, 443}:
        return None

    # Check direct IP literals
    try:
        ipaddress.ip_address(hostname)
        if not _is_public_ip(hostname):
            return None
    except ValueError:
        # Hostname: optionally verify all DNS A/AAAA records resolve strictly to global public IPs
        if resolve_dns:
            try:
                infos = socket.getaddrinfo(hostname, port or (443 if parsed.scheme == "https" else 80))
            except socket.gaierror:
                return None
            if not infos:
                return None
            for info in infos:
                sockaddr = info[4]
                if not sockaddr or not _is_public_ip(str(sockaddr[0])):
                    return None

    normalized = urlunparse(
        (
            parsed.scheme.lower(),
            parsed.netloc,
            parsed.path or "/",
            parsed.params,
            parsed.query,
            "",  # Strip client fragments
        )
    )
    return normalized
