# SPDX-License-Identifier: MPL-2.0

from typing import Protocol, runtime_checkable

from .codes import MonitorEventMask

_socket_monitor_factory = None


def register_socket_monitor_factory(factory):
    global _socket_monitor_factory
    _socket_monitor_factory = factory


class MonitorStatus:
    def __init__(
        self,
        *,
        source_kind,
        state_flags,
        detail_flags,
        snd_pending_msgs,
        rcv_pending_msgs,
        auto_hwm_enabled=None,
        auto_hwm_profile=None,
        auto_hwm_role=None,
        auto_hwm_policy_class=None,
        auto_hwm_unit_budget_bytes=None,
        auto_hwm_size_cap=None,
        auto_hwm_socket_message_slots=None,
        auto_hwm_effective_message_bytes=None,
        auto_hwm_applied_sndhwm=None,
        auto_hwm_applied_rcvhwm=None,
        auto_hwm_effective_sndbuf=None,
        auto_hwm_effective_rcvbuf=None,
        auto_hwm_last_recalc_ms=None,
        auto_hwm_last_recalc_reason=None,
        auto_hwm_send_blocked_ratio_ppm=None,
        auto_hwm_deferred_sndhwm=None,
        auto_hwm_deferred_rcvhwm=None,
    ):
        self.source_kind = source_kind
        self.state_flags = state_flags
        self.detail_flags = detail_flags
        self.snd_pending_msgs = snd_pending_msgs
        self.rcv_pending_msgs = rcv_pending_msgs
        self.auto_hwm_enabled = auto_hwm_enabled
        self.auto_hwm_profile = auto_hwm_profile
        self.auto_hwm_role = auto_hwm_role
        self.auto_hwm_policy_class = auto_hwm_policy_class
        self.auto_hwm_unit_budget_bytes = auto_hwm_unit_budget_bytes
        self.auto_hwm_size_cap = auto_hwm_size_cap
        self.auto_hwm_socket_message_slots = auto_hwm_socket_message_slots
        self.auto_hwm_effective_message_bytes = auto_hwm_effective_message_bytes
        self.auto_hwm_applied_sndhwm = auto_hwm_applied_sndhwm
        self.auto_hwm_applied_rcvhwm = auto_hwm_applied_rcvhwm
        self.auto_hwm_effective_sndbuf = auto_hwm_effective_sndbuf
        self.auto_hwm_effective_rcvbuf = auto_hwm_effective_rcvbuf
        self.auto_hwm_last_recalc_ms = auto_hwm_last_recalc_ms
        self.auto_hwm_last_recalc_reason = auto_hwm_last_recalc_reason
        self.auto_hwm_send_blocked_ratio_ppm = auto_hwm_send_blocked_ratio_ppm
        self.auto_hwm_deferred_sndhwm = auto_hwm_deferred_sndhwm
        self.auto_hwm_deferred_rcvhwm = auto_hwm_deferred_rcvhwm

    def is_ready(self):
        return bool(self.state_flags & (1 << 0))


class MonitorEvent:
    def __init__(self, *, event, value, routing_id, local_addr, remote_addr):
        self.event = event
        self.value = value
        self.routing_id = routing_id
        self.local_addr = local_addr
        self.remote_addr = remote_addr

@runtime_checkable
class MonitorSocket(Protocol):
    ignore_handler = staticmethod(lambda event: None)

    def status(self): ...

    def close(self): ...

    def recv(self, *, flags=0): ...

    def on_event(self, handler): ...

    def __enter__(self): ...

    def __exit__(self, exc_type, exc, tb): ...

    async def __aenter__(self): ...

    async def __aexit__(self, exc_type, exc, tb): ...


def open_socket_monitor(socket, events=MonitorEventMask.ALL):
    if _socket_monitor_factory is None:
        raise RuntimeError("zlink monitor runtime is not registered")
    return _socket_monitor_factory(socket, events=events)
