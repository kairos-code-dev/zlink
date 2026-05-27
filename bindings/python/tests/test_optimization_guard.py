"""Source-level guards for binding helper and hot-path invariants."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src" / "zlink"

AGGREGATE_SYMBOLS = (
    "zlink_send",
    "zlink_recv",
    "zlink_publish",
    "zlink_subscribe",
    "zlink_router_recv",
    "zlink_dealer_request",
    "zlink_router_request",
    "zlink_router_reply",
    "zlink_spot_send_channel",
    "zlink_spot_request_channel",
    "zlink_spot_request_spot",
    "zlink_spot_request_router",
    "zlink_spot_publish",
    "zlink_spot_subscribe",
    "zlink_spot_send_spot",
    "zlink_spot_reply_spot",
    "zlink_spot_reply_router",
    "zlink_spot_recv",
)

REQUIRED_PART_SYMBOLS = (
    "zlink_send_part",
    "zlink_recv_part",
    "zlink_publish_part",
    "zlink_subscribe_part",
    "zlink_router_recv_part",
    "zlink_dealer_request_part",
    "zlink_router_request_part",
    "zlink_router_reply_part",
    "zlink_spot_publish_part",
    "zlink_spot_subscribe_part",
    "zlink_spot_request_channel_part",
    "zlink_spot_request_spot_part",
    "zlink_spot_reply_router_part",
)


def _source_files() -> list[Path]:
    return sorted(
        path
        for path in SRC.rglob("*.py")
        if "__pycache__" not in path.parts and ".egg-info" not in path.parts
    )


def _all_source_text() -> str:
    return "\n".join(path.read_text(encoding="utf-8") for path in _source_files())


def test_hot_paths_use_part_substrate_not_aggregate_calls():
    text = _all_source_text()

    missing = [symbol for symbol in REQUIRED_PART_SYMBOLS if symbol not in text]
    assert missing == []

    violations: list[str] = []
    for path in _source_files():
        body = path.read_text(encoding="utf-8")
        for symbol in AGGREGATE_SYMBOLS:
            pattern = re.compile(rf"\b{re.escape(symbol)}\s*\(")
            for match in pattern.finditer(body):
                if body[match.start() :].startswith(symbol + "_part"):
                    continue
                violations.append(f"{path.relative_to(ROOT)}:{symbol}")

    assert violations == []


def test_runtime_source_does_not_depend_on_reflective_or_dynamic_ffi_helpers():
    text = _all_source_text()

    assert "cffi" not in text
    assert "ffi-napi" not in text
    assert "getattr(lib()" not in text
    assert "setattr(lib()" not in text


def test_public_helpers_remain_canonical_facade_objects():
    import zlink

    assert hasattr(zlink, "Received")
    assert hasattr(zlink, "TopicMessage")
    assert hasattr(zlink, "SpotSubscribedPart")
    assert hasattr(zlink, "RoutingId")
    assert hasattr(zlink, "SendFlags")
    assert hasattr(zlink, "RecvFlags")
    assert hasattr(zlink.Received, "single_part_or_throw")
    assert hasattr(zlink.TopicMessage, "single_part_or_throw")
    assert hasattr(zlink.Spot, "subscribe_part")
    assert hasattr(zlink.Spot, "subscribe_part_into")
    assert not hasattr(zlink.PairSocket, "try_send")
    assert not hasattr(zlink.PairSocket, "try_recv")
