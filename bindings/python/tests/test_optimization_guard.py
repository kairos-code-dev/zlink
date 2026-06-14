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

PUBLIC_CONTRACT_FORBIDDEN_TOKENS = (
    "from zlink._",
    "import zlink._",
    "zlink._native",
    "zlink._runtime",
    "_native_bridge",
    "_native_active_latency_ns",
    "_recv_parts_via_native_bridge",
    "_recv_owner_via_native_bridge",
    "_subscribe_parts_owner",
    "_last_part_data",
    "_socket_handle",
    "ctypes",
    "cffi",
)


def _source_files() -> list[Path]:
    return sorted(
        path
        for path in SRC.rglob("*.py")
        if "__pycache__" not in path.parts and ".egg-info" not in path.parts
    )


def _all_source_text() -> str:
    return "\n".join(path.read_text(encoding="utf-8") for path in _source_files())


def _perf_source_files() -> list[Path]:
    perf_root = ROOT / "perf"
    return sorted(
        path
        for path in perf_root.rglob("*.py")
        if "__pycache__" not in path.parts and ".egg-info" not in path.parts
    )


def _public_contract_violations(files: list[Path]) -> list[str]:
    violations = []
    for path in files:
        body = path.read_text(encoding="utf-8")
        for token in PUBLIC_CONTRACT_FORBIDDEN_TOKENS:
            if token in body:
                violations.append(f"{path.relative_to(ROOT)}:{token}")
    return violations


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


def test_python_hot_paths_prefer_private_native_bridge():
    socket_base = SRC / "_runtime" / "sockets" / "socket_base.py"
    bridge = SRC / "_native" / "bridge.py"
    extension = SRC / "_native" / "_zlink_native.c"
    setup = ROOT / "setup.py"

    socket_text = socket_base.read_text(encoding="utf-8")
    assert "from ..._native import bridge as _native_bridge" in socket_text
    assert "_send_payload_via_native_bridge" in socket_text
    assert "_send_routed_payload_via_native_bridge" in socket_text
    assert "_publish_payload_via_native_bridge" in socket_text
    assert "_recv_parts_via_native_bridge" in socket_text
    assert "_subscribe_parts_via_native_bridge" in socket_text

    socket_impl = SRC / "_runtime" / "sockets" / "socket_base_impl.py"
    socket_impl_text = socket_impl.read_text(encoding="utf-8")
    assert "from ..._native import bridge as _native_bridge" in socket_impl_text
    assert "_recv_parts_via_native_bridge" in socket_impl_text
    assert "router_recv_parts(" in socket_impl_text

    bridge_text = bridge.read_text(encoding="utf-8")
    assert "from . import _zlink_native" in bridge_text
    assert "def send_parts(" in bridge_text
    assert "def send_parts_rid(" in bridge_text
    assert "def publish_parts(" in bridge_text
    assert "def spot_publish_submit_parts(" in bridge_text
    assert "def spot_send_channel_parts(" in bridge_text
    assert "def spot_send_spot_parts(" in bridge_text
    assert "def recv_parts(" in bridge_text
    assert "def subscribe_parts(" in bridge_text
    assert "def router_recv_parts(" in bridge_text
    assert "def spot_subscribe_parts(" in bridge_text
    assert "def spot_recv_parts(" in bridge_text
    assert "def single_socket_one_way(" in bridge_text
    assert "def multi_spot_sendsend_roundtrip(" in bridge_text
    assert "def multi_spot_reqrep_roundtrip(" in bridge_text
    assert "def stream_echo_install(" in bridge_text
    assert "def stream_echo_drain(" in bridge_text

    extension_text = extension.read_text(encoding="utf-8")
    assert "Py_BEGIN_ALLOW_THREADS" in extension_text
    assert "zlink_send_part" in extension_text
    assert "zlink_send_part_rid" in extension_text
    assert "zlink_publish_part" in extension_text
    assert "zlink_recv_part" in extension_text
    assert "zlink_subscribe_part" in extension_text
    assert "zlink_router_recv_part" in extension_text
    assert "zlink_spot_subscribe_part" in extension_text
    assert "zlink_spot_recv_part" in extension_text
    assert "zlink_spot_publish_part" in extension_text
    assert "zlink_spot_send_channel_part" in extension_text
    assert "zlink_spot_send_spot_part" in extension_text
    assert "py_multi_spot_sendsend_roundtrip" in extension_text
    assert "py_multi_spot_reqrep_roundtrip" in extension_text
    assert "py_single_socket_one_way" in extension_text
    assert "zlink_stream_packet_handler" in extension_text
    assert "stream_echo_packet_handler" in extension_text
    assert "for (Py_ssize_t j = i; j < prepared.count; ++j)" in extension_text
    assert "for (Py_ssize_t j = i + 1; j < prepared.count; ++j)" not in extension_text

    spot_receive = SRC / "_runtime" / "service" / "spot" / "spot_receive.py"
    spot_receive_text = spot_receive.read_text(encoding="utf-8")
    assert "from ...._native import bridge as _native_bridge" in spot_receive_text
    assert "spot_subscribe_parts(" in spot_receive_text
    assert "spot_recv_parts(" in spot_receive_text

    setup_text = setup.read_text(encoding="utf-8")
    assert "Extension(" in setup_text
    assert '"zlink._native._zlink_native"' in setup_text

def test_public_socket_contracts_keep_builder_only_send_publish_surface():
    contract_files = [
        SRC / "contracts" / "sockets" / "message_socket_contracts.py",
        SRC / "contracts" / "sockets" / "routed_socket_contracts.py",
        SRC / "contracts" / "sockets" / "pubsub_socket_contracts.py",
        SRC / "_runtime" / "sockets" / "socket_base_impl.py",
    ]
    text = "\n".join(path.read_text(encoding="utf-8") for path in contract_files)

    assert "def send(self, payload" not in text
    assert "def send(self, routing_id, payload" not in text
    assert "def publish(self, topic, payload" not in text
    assert "def send(self):" in text
    assert "def send(self, routing_id):" in text
    assert "def publish(self, topic):" in text


def test_python_perf_uses_public_binding_contract():
    violations = _public_contract_violations(_perf_source_files())
    assert violations == []


def test_python_samples_and_examples_use_public_binding_contract():
    files = sorted(
        path
        for root in (ROOT / "samples", ROOT / "examples")
        for path in root.rglob("*.py")
        if "__pycache__" not in path.parts
    )

    violations = _public_contract_violations(files)
    assert violations == []


def test_perf_scripts_do_not_bypass_public_python_api_active_loops():
    forbidden_calls = (
        "single_socket_one_way(",
        "multi_send_one_way(",
        "multi_echo_roundtrip(",
        "multi_spot_sendsend_roundtrip(",
        "multi_spot_reqrep_roundtrip(",
        "subscribe_count_active(",
        "publish_active(",
        "spot_subscribe_count_active(",
        "spot_publish_active(",
        "recv_count_active(",
        "router_echo_server_loop(",
        "stream_echo_install(",
        "stream_echo_drain(",
        "spot_routed_echo_install(",
        "spot_count_install(",
        "spot_count_start(",
        "spot_count_stats(",
    )

    violations: list[str] = []
    for path in _perf_source_files():
        if path.name == "run_benchmarks.py":
            continue
        body = path.read_text(encoding="utf-8")
        for call in forbidden_calls:
            if call in body:
                violations.append(f"{path.relative_to(ROOT)}:{call}")

    assert violations == []


def test_public_helpers_remain_canonical_facade_objects():
    import zlink

    assert hasattr(zlink, "Received")
    assert hasattr(zlink, "TopicMessage")
    assert not hasattr(zlink, "SpotSubscribedPart")
    assert hasattr(zlink, "RoutingId")
    assert hasattr(zlink, "SendFlags")
    assert hasattr(zlink, "RecvFlags")
    assert hasattr(zlink.Received, "single_part_or_throw")
    assert hasattr(zlink.TopicMessage, "single_part_or_throw")
    assert not hasattr(zlink.Spot, "subscribe_part")
    assert not hasattr(zlink.Spot, "subscribe_part_into")
    assert not hasattr(zlink.PairSocket, "try_send")
    assert not hasattr(zlink.PairSocket, "try_recv")


def test_root_facade_does_not_expose_runtime_wiring_aliases():
    import zlink

    forbidden = (
        "core_runtime",
        "runtime_open_socket_monitor",
        "messaging_runtime",
        "socket_options_runtime",
        "socket_runtime",
        "discovery_runtime",
        "spot_runtime",
        "contracts_projection",
        "messaging_contracts_projection",
        "message_contract_module",
    )

    violations = [name for name in forbidden if hasattr(zlink, name)]
    assert violations == []
