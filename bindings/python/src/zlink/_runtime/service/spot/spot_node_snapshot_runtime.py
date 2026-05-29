# SPDX-License-Identifier: MPL-2.0

import ctypes

from ...._native.ffi import (
    ZlinkSpotNodeActorEntry,
    ZlinkSpotNodePeerEntry,
    ZlinkSpotNodePeerFilter,
    ZlinkSpotNodeSocketEntry,
    ZlinkSpotNodeSocketFilter,
    ZlinkSpotNodeSpotEntry,
    ZlinkSpotNodeStatus,
    ZlinkSpotNodeSubjectEntry,
    ZlinkSpotNodeSubjectFilter,
    lib,
)
from ....contracts.service.codes import (
    SpotKind,
    SpotNodeState,
    SpotNodeSocketOwner,
    SpotPeerKind,
    SpotPeerSource,
    SpotPeerState,
    SpotRole,
    SubjectKind,
)
from ....contracts.sockets.codes import SocketType
from ...eventing.monitor import _monitor_status_from_native
from ...handles.native_support import ConfigError, ConfigResult, _raise_result_error, _routing_id_bytes
from .spot import _decode_fixed, _fixed_buffer_value
from .spot_models_runtime import (
    SpotNodeActorEntry,
    SpotNodePeerEntry,
    SpotNodeSocketEntry,
    SpotNodeSpotEntry,
    SpotNodeStatus,
    SpotNodeSubjectEntry,
    _actor_ref_from_native,
)


class SpotNodeSnapshotMixin:
    def status(self):
        native = ZlinkSpotNodeStatus()
        rc = lib().zlink_spot_node_status(self._handle, ctypes.byref(native))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return SpotNodeStatus(
            channel_name=_decode_fixed(native.channel_name),
            local_endpoint=_decode_fixed(native.local_endpoint),
            node_routing_id=_routing_id_bytes(native.node_routing_id),
            state=SpotNodeState(int(native.state)),
            configured_peer_count=int(native.configured_peer_count),
            active_peer_count=int(native.active_peer_count),
            connected_peer_count=int(native.connected_peer_count),
            subject_count=int(native.subject_count),
            ready_subject_count=int(native.ready_subject_count),
            disconnected_sub_target_count=int(native.disconnected_sub_target_count),
            disconnected_routed_target_count=int(native.disconnected_routed_target_count),
            last_error=int(native.last_error),
            last_changed_ms=int(native.last_changed_ms),
        )

    def peers(self):
        return self.peers_query(None)

    def peers_query(self, filter_=None):
        count = ctypes.c_size_t()
        filter_ptr = None
        filter_native = None
        if filter_ is not None:
            filter_native = ZlinkSpotNodePeerFilter()
            filter_native.peer_endpoint = _fixed_buffer_value(filter_.peer_endpoint, 256)
            filter_native.source = 0 if filter_.source is None else int(filter_.source)
            filter_native.state = 0 if filter_.state is None else int(filter_.state)
            filter_ptr = ctypes.byref(filter_native)
        rc = lib().zlink_spot_node_peers(
            self._handle, filter_ptr, None, ctypes.byref(count)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        if count.value == 0:
            return []
        entries = (ZlinkSpotNodePeerEntry * int(count.value))()
        rc = lib().zlink_spot_node_peers(
            self._handle, filter_ptr, entries, ctypes.byref(count)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return [
            SpotNodePeerEntry(
                channel_name=_decode_fixed(entry.channel_name),
                local_endpoint=_decode_fixed(entry.local_endpoint),
                peer_endpoint=_decode_fixed(entry.peer_endpoint),
                source=SpotPeerSource(int(entry.source)),
                kind=SpotPeerKind(int(entry.kind)),
                state=SpotPeerState(int(entry.state)),
                weight=int(entry.weight),
                connected_since_ms=int(entry.connected_since_ms),
                last_changed_ms=int(entry.last_changed_ms),
            )
            for entry in entries[: int(count.value)]
        ]

    def spots(self):
        count = ctypes.c_size_t()
        rc = lib().zlink_spot_node_spots(self._handle, None, ctypes.byref(count))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        if count.value == 0:
            return []
        entries = (ZlinkSpotNodeSpotEntry * int(count.value))()
        rc = lib().zlink_spot_node_spots(
            self._handle, entries, ctypes.byref(count)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return [
            SpotNodeSpotEntry(
                spot_rid=_routing_id_bytes(entry.spot_rid),
                spot_kind=SpotKind(int(entry.spot_kind)),
                dispatch_handler_attached=bool(entry.dispatch_handler_attached),
                joined_actor_count=int(entry.joined_actor_count),
                pending_actor_join_count=int(entry.pending_actor_join_count),
                route_synced=bool(entry.route_synced),
                last_changed_ms=int(entry.last_changed_ms),
            )
            for entry in entries[: int(count.value)]
        ]

    def subjects(self, filter_=None):
        count = ctypes.c_size_t()
        filter_ptr = None
        filter_native = None
        if filter_ is not None:
            filter_native = ZlinkSpotNodeSubjectFilter()
            filter_native.role = 0 if filter_.role is None else int(filter_.role)
            filter_native.subject = _fixed_buffer_value(filter_.subject, 256)
            filter_native.subject_kind = (
                0 if filter_.subject_kind is None else int(filter_.subject_kind)
            )
            filter_ptr = ctypes.byref(filter_native)
        rc = lib().zlink_spot_node_subjects(
            self._handle, filter_ptr, None, ctypes.byref(count)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        if count.value == 0:
            return []
        entries = (ZlinkSpotNodeSubjectEntry * int(count.value))()
        rc = lib().zlink_spot_node_subjects(
            self._handle, filter_ptr, entries, ctypes.byref(count)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return [
            SpotNodeSubjectEntry(
                role=SpotRole(int(entry.role)),
                subject=_decode_fixed(entry.subject),
                subject_kind=SubjectKind(int(entry.subject_kind)),
                ready_peer_count=int(entry.ready_peer_count),
                active_peer_count=int(entry.active_peer_count),
                last_changed_ms=int(entry.last_changed_ms),
            )
            for entry in entries[: int(count.value)]
        ]

    def internal_sockets(self, filter_=None):
        count = ctypes.c_size_t()
        filter_ptr = None
        filter_native = None
        if filter_ is not None:
            filter_native = ZlinkSpotNodeSocketFilter()
            filter_native.owner = (
                int(SpotNodeSocketOwner.ANY)
                if filter_.owner is None
                else int(filter_.owner)
            )
            filter_native.socket_type = (
                0 if filter_.socket_type is None else int(filter_.socket_type)
            )
            filter_native.socket_name = _fixed_buffer_value(filter_.socket_name, 64)
            filter_ptr = ctypes.byref(filter_native)
        rc = lib().zlink_spot_node_internal_sockets(
            self._handle, filter_ptr, None, ctypes.byref(count)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        if count.value == 0:
            return []
        entries = (ZlinkSpotNodeSocketEntry * int(count.value))()
        rc = lib().zlink_spot_node_internal_sockets(
            self._handle, filter_ptr, entries, ctypes.byref(count)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return [
            SpotNodeSocketEntry(
                owner=SpotNodeSocketOwner(int(entry.owner)),
                owner_id=int(entry.owner_id),
                owner_name=_decode_fixed(entry.owner_name),
                socket_name=_decode_fixed(entry.socket_name),
                socket_type=SocketType(int(entry.socket_type)),
                auto_hwm_visible=bool(entry.auto_hwm_visible),
                snapshot=_monitor_status_from_native(entry.monitor_status),
            )
            for entry in entries[: int(count.value)]
        ]

    def actors(self):
        count = ctypes.c_size_t()
        rc = lib().zlink_spot_node_actors(self._handle, None, ctypes.byref(count))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        if count.value == 0:
            return []
        entries = (ZlinkSpotNodeActorEntry * int(count.value))()
        rc = lib().zlink_spot_node_actors(
            self._handle, entries, ctypes.byref(count)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return [
            SpotNodeActorEntry(
                actor=_actor_ref_from_native(entry.actor),
                current_spot_rid=_routing_id_bytes(entry.current_spot_rid),
                current_spot_kind=SpotKind(int(entry.current_spot_kind)),
                route_synced=bool(entry.route_synced),
                pending_message_count=int(entry.pending_message_count),
                last_changed_ms=int(entry.last_changed_ms),
            )
            for entry in entries[: int(count.value)]
        ]
