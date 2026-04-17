# SPDX-License-Identifier: MPL-2.0

import ctypes
import ctypes.util
import os
import pathlib


class ZlinkMsg(ctypes.Union):
    # zlink_msg_t is a 64-byte opaque value aligned to sizeof(void *).
    # Using a Union with a pointer member preserves the 64-byte size while
    # forcing ctypes to match the required native alignment.
    _fields_ = [
        ("_", ctypes.c_ubyte * 64),
        ("_align", ctypes.c_void_p),
    ]


class ZlinkRoutingId(ctypes.Structure):
    _fields_ = [
        ("size", ctypes.c_size_t),
        ("data", ctypes.POINTER(ctypes.c_uint8)),
    ]


class ZlinkMonitorEvent(ctypes.Structure):
    _fields_ = [
        ("event", ctypes.c_uint64),
        ("value", ctypes.c_uint64),
        ("routing_id", ZlinkRoutingId),
        ("local_addr", ctypes.c_char * 256),
        ("remote_addr", ctypes.c_char * 256),
    ]


class ZlinkSocketMonitorOpenOptions(ctypes.Structure):
    _fields_ = [("events", ctypes.c_uint64)]


class ZlinkMonitorSnapshot(ctypes.Structure):
    _fields_ = [
        ("source_kind", ctypes.c_uint32),
        ("state_flags", ctypes.c_uint32),
        ("detail_flags", ctypes.c_uint32),
        ("snd_pending_msgs", ctypes.c_uint64),
        ("rcv_pending_msgs", ctypes.c_uint64),
    ]


class ZlinkServiceEvent(ctypes.Structure):
    _fields_ = [
        ("service_kind", ctypes.c_uint32),
        ("event_type", ctypes.c_uint32),
        ("status", ctypes.c_int32),
        ("error_code", ctypes.c_int32),
        ("value", ctypes.c_uint32),
        ("detail_flags", ctypes.c_uint32),
        ("service_name", ctypes.c_char * 256),
        ("endpoint", ctypes.c_char * 256),
        ("routing_id", ZlinkRoutingId),
        ("subject", ctypes.c_char * 256),
        ("subject_kind", ctypes.c_uint32),
    ]


class ZlinkServiceMonitorOpenOptions(ctypes.Structure):
    _fields_ = [("events", ctypes.c_uint32)]


class ZlinkSpotNodeStatus(ctypes.Structure):
    _fields_ = [
        ("service_name", ctypes.c_char * 256),
        ("local_endpoint", ctypes.c_char * 256),
        ("node_routing_id", ZlinkRoutingId),
        ("state", ctypes.c_uint32),
        ("configured_peer_count", ctypes.c_uint32),
        ("active_peer_count", ctypes.c_uint32),
        ("connected_peer_count", ctypes.c_uint32),
        ("subject_count", ctypes.c_uint32),
        ("ready_subject_count", ctypes.c_uint32),
        ("last_error", ctypes.c_int32),
        ("last_changed_ms", ctypes.c_uint64),
    ]


class ZlinkSpotNodePeerEntry(ctypes.Structure):
    _fields_ = [
        ("service_name", ctypes.c_char * 256),
        ("local_endpoint", ctypes.c_char * 256),
        ("peer_endpoint", ctypes.c_char * 256),
        ("source", ctypes.c_uint32),
        ("state", ctypes.c_uint32),
        ("admission_state", ctypes.c_int32),
        ("connected_since_ms", ctypes.c_uint64),
        ("last_changed_ms", ctypes.c_uint64),
    ]


class ZlinkSpotNodePeerFilter(ctypes.Structure):
    _fields_ = [
        ("peer_endpoint", ctypes.c_char * 256),
        ("source", ctypes.c_uint32),
        ("state", ctypes.c_uint32),
    ]


class ZlinkSpotNodeSubjectEntry(ctypes.Structure):
    _fields_ = [
        ("role", ctypes.c_uint32),
        ("subject", ctypes.c_char * 256),
        ("subject_kind", ctypes.c_uint32),
        ("ready_peer_count", ctypes.c_uint32),
        ("active_peer_count", ctypes.c_uint32),
        ("last_changed_ms", ctypes.c_uint64),
    ]


class ZlinkSpotNodeSubjectFilter(ctypes.Structure):
    _fields_ = [
        ("role", ctypes.c_uint32),
        ("subject", ctypes.c_char * 256),
        ("subject_kind", ctypes.c_uint32),
    ]


class ZlinkSpotServiceAttachmentStats(ctypes.Structure):
    _fields_ = [
        ("service_name", ctypes.c_char * 256),
        ("router_count", ctypes.c_uint32),
        ("pub_count", ctypes.c_uint32),
        ("sub_count", ctypes.c_uint32),
        ("auto_router_count", ctypes.c_uint32),
        ("auto_pub_count", ctypes.c_uint32),
        ("auto_sub_count", ctypes.c_uint32),
    ]


class ZlinkSpotServiceMonitorEvent(ctypes.Structure):
    _fields_ = [
        ("service_name", ctypes.c_char * 256),
        ("role", ctypes.c_uint32),
        ("event", ZlinkMonitorEvent),
    ]


class ZlinkMemberPeerEntry(ctypes.Structure):
    _fields_ = [
        ("service_type", ctypes.c_uint16),
        ("service_role", ctypes.c_uint16),
        ("service_name", ctypes.c_char * 256),
        ("endpoint", ctypes.c_char * 256),
        ("routing_id", ZlinkRoutingId),
        ("admission_state", ctypes.c_int32),
        ("value", ctypes.c_int64),
    ]


class ZlinkRegistryStatus(ctypes.Structure):
    _fields_ = [
        ("registry_id", ctypes.c_uint32),
        ("bind_endpoint", ctypes.c_char * 256),
        ("state", ctypes.c_uint32),
        ("topology_entry_count", ctypes.c_uint32),
        ("peer_registry_count", ctypes.c_uint32),
        ("connected_peer_registry_count", ctypes.c_uint32),
        ("list_seq", ctypes.c_uint64),
        ("last_error", ctypes.c_int32),
        ("last_changed_ms", ctypes.c_uint64),
    ]


class ZlinkRegistryServiceSummaryEntry(ctypes.Structure):
    _fields_ = [
        ("service_kind", ctypes.c_uint32),
        ("service_role", ctypes.c_uint32),
        ("service_name", ctypes.c_char * 256),
        ("total_count", ctypes.c_uint32),
        ("connecting_count", ctypes.c_uint32),
        ("ready_count", ctypes.c_uint32),
        ("error_count", ctypes.c_uint32),
        ("stopped_count", ctypes.c_uint32),
        ("last_reported_ms", ctypes.c_uint64),
    ]


class ZlinkRegistryServiceSummaryFilter(ctypes.Structure):
    _fields_ = [
        ("service_kind", ctypes.c_uint32),
        ("service_role", ctypes.c_uint32),
        ("service_name", ctypes.c_char * 256),
    ]


class ZlinkRegistryTopologyEntry(ctypes.Structure):
    _fields_ = [
        ("routing_id", ZlinkRoutingId),
        ("service_kind", ctypes.c_uint32),
        ("service_role", ctypes.c_uint32),
        ("service_name", ctypes.c_char * 256),
        ("endpoint", ctypes.c_char * 256),
        ("source", ctypes.c_uint32),
        ("state", ctypes.c_uint32),
        ("desired_count", ctypes.c_uint32),
        ("ready_count", ctypes.c_uint32),
        ("error_code", ctypes.c_uint32),
        ("last_reported_ms", ctypes.c_uint64),
    ]


class ZlinkRegistryTopologyFilter(ctypes.Structure):
    _fields_ = [
        ("service_kind", ctypes.c_uint32),
        ("service_role", ctypes.c_uint32),
        ("service_name", ctypes.c_char * 256),
        ("routing_id", ZlinkRoutingId),
        ("state", ctypes.c_uint32),
        ("source", ctypes.c_uint32),
    ]


if os.name == "nt":
    if ctypes.sizeof(ctypes.c_void_p) == 8:
        ZlinkFD = ctypes.c_ulonglong
    else:
        ZlinkFD = ctypes.c_uint
else:
    ZlinkFD = ctypes.c_int


class ZlinkPollItem(ctypes.Structure):
    _fields_ = [
        ("socket", ctypes.c_void_p),
        ("fd", ZlinkFD),
        ("events", ctypes.c_short),
        ("revents", ctypes.c_short),
    ]


class ZlinkPollerEvent(ctypes.Structure):
    _fields_ = [
        ("socket", ctypes.c_void_p),
        ("fd", ZlinkFD),
        ("user_data", ctypes.c_void_p),
        ("events", ctypes.c_short),
    ]


class _Lib:
    def __init__(self):
        candidates = []
        path = os.environ.get("ZLINK_LIBRARY_PATH")
        if path:
            candidates.append(path)
        else:
            for candidate in (
                _find_bundled_library(),
                _find_dev_library(),
                ctypes.util.find_library("zlink"),
            ):
                if candidate and candidate not in candidates:
                    candidates.append(candidate)
        if not candidates:
            raise OSError("zlink native library not found")

        last_error = None
        for candidate in candidates:
            try:
                if os.name == "nt":
                    _prepare_windows_runtime(candidate)
                self.lib = ctypes.CDLL(candidate)
                self._bind()
                return
            except (AttributeError, OSError) as exc:
                last_error = exc
        raise OSError("zlink native library not found or incompatible") from last_error

    def _require(self, name, argtypes, restype):
        func = getattr(self.lib, name)
        func.argtypes = argtypes
        func.restype = restype
        return func

    def _bind(self):
        self._require(
            "zlink_version",
            [
                ctypes.POINTER(ctypes.c_int),
                ctypes.POINTER(ctypes.c_int),
                ctypes.POINTER(ctypes.c_int),
            ],
            None,
        )
        self._require("zlink_errno", [], ctypes.c_int)
        self._require("zlink_strerror", [ctypes.c_int], ctypes.c_char_p)
        self._require(
            "zlink_set_admission_state",
            [ctypes.c_void_p, ctypes.c_int32],
            ctypes.c_int,
        )
        self._require(
            "zlink_get_admission_state",
            [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int32)],
            ctypes.c_int,
        )

        self._require("zlink_ctx_new", [], ctypes.c_void_p)
        self._require("zlink_ctx_term", [ctypes.c_void_p], ctypes.c_int)
        self._require("zlink_ctx_shutdown", [ctypes.c_void_p], ctypes.c_int)
        self._require(
            "zlink_ctx_set",
            [ctypes.c_void_p, ctypes.c_int, ctypes.c_int],
            ctypes.c_int,
        )
        self._require(
            "zlink_ctx_get",
            [ctypes.c_void_p, ctypes.c_int],
            ctypes.c_int,
        )

        self._require("zlink_msg_init", [ctypes.POINTER(ZlinkMsg)], ctypes.c_int)
        self._require(
            "zlink_msg_init_size",
            [ctypes.POINTER(ZlinkMsg), ctypes.c_size_t],
            ctypes.c_int,
        )
        self._require(
            "zlink_msg_init_data",
            [
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_void_p,
                ctypes.c_size_t,
                ctypes.c_void_p,
                ctypes.c_void_p,
            ],
            ctypes.c_int,
        )
        self._require("zlink_msg_close", [ctypes.POINTER(ZlinkMsg)], ctypes.c_int)
        self._require(
            "zlink_msg_move",
            [ctypes.POINTER(ZlinkMsg), ctypes.POINTER(ZlinkMsg)],
            ctypes.c_int,
        )
        self._require(
            "zlink_msg_copy",
            [ctypes.POINTER(ZlinkMsg), ctypes.POINTER(ZlinkMsg)],
            ctypes.c_int,
        )
        self._require("zlink_msg_data", [ctypes.POINTER(ZlinkMsg)], ctypes.c_void_p)
        self._require(
            "zlink_msg_size",
            [ctypes.POINTER(ZlinkMsg)],
            ctypes.c_size_t,
        )
        self._require(
            "zlink_msg_refcnt",
            [ctypes.POINTER(ZlinkMsg)],
            ctypes.c_int,
        )
        self._require(
            "zlink_msg_gets",
            [ctypes.POINTER(ZlinkMsg), ctypes.c_char_p],
            ctypes.c_char_p,
        )

        self._require("zlink_socket", [ctypes.c_void_p, ctypes.c_int], ctypes.c_void_p)
        self._require("zlink_close", [ctypes.c_void_p], ctypes.c_int)
        self._require(
            "zlink_recv_handler",
            [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_stream_packet_handler",
            [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_send_ready_handler",
            [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_dealer_request",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_size_t,
                ctypes.c_void_p,
                ctypes.c_void_p,
                ctypes.c_int,
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_router_request",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_size_t,
                ctypes.c_void_p,
                ctypes.c_void_p,
                ctypes.c_int,
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_router_reply",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.c_uint64,
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_size_t,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_router_recv",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ctypes.POINTER(ZlinkRoutingId)),
                ctypes.POINTER(ctypes.POINTER(ZlinkRoutingId)),
                ctypes.POINTER(ctypes.c_uint64),
                ctypes.POINTER(ctypes.POINTER(ZlinkMsg)),
                ctypes.POINTER(ctypes.c_size_t),
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_set_option",
            [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_size_t],
            ctypes.c_int,
        )
        self._require(
            "zlink_get_option",
            [
                ctypes.c_void_p,
                ctypes.c_int,
                ctypes.c_void_p,
                ctypes.POINTER(ctypes.c_size_t),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_set_router_option",
            [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_size_t],
            ctypes.c_int,
        )
        self._require(
            "zlink_get_router_option",
            [
                ctypes.c_void_p,
                ctypes.c_int,
                ctypes.c_void_p,
                ctypes.POINTER(ctypes.c_size_t),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_set_dealer_option",
            [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_size_t],
            ctypes.c_int,
        )
        self._require(
            "zlink_set_stream_option",
            [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_size_t],
            ctypes.c_int,
        )
        self._require(
            "zlink_get_stream_option",
            [
                ctypes.c_void_p,
                ctypes.c_int,
                ctypes.c_void_p,
                ctypes.POINTER(ctypes.c_size_t),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_set_pub_option",
            [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_size_t],
            ctypes.c_int,
        )
        self._require(
            "zlink_get_pub_option",
            [
                ctypes.c_void_p,
                ctypes.c_int,
                ctypes.c_void_p,
                ctypes.POINTER(ctypes.c_size_t),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_set_sub_option",
            [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_size_t],
            ctypes.c_int,
        )
        self._require(
            "zlink_get_sub_option",
            [
                ctypes.c_void_p,
                ctypes.c_int,
                ctypes.c_void_p,
                ctypes.POINTER(ctypes.c_size_t),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_set_routing_id",
            [ctypes.c_void_p, ctypes.POINTER(ZlinkRoutingId)],
            ctypes.c_int,
        )
        self._require(
            "zlink_get_routing_id",
            [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(ZlinkRoutingId))],
            ctypes.c_int,
        )
        self._require(
            "zlink_routing_id_from_u32",
            [
                ctypes.c_uint32,
                ctypes.POINTER(ctypes.c_uint8),
                ctypes.c_size_t,
                ctypes.POINTER(ZlinkRoutingId),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_routing_id_to_u32",
            [ctypes.POINTER(ZlinkRoutingId), ctypes.POINTER(ctypes.c_uint32)],
            ctypes.c_int,
        )
        self._require(
            "zlink_routing_id_from_text",
            [
                ctypes.c_char_p,
                ctypes.POINTER(ctypes.c_uint8),
                ctypes.c_size_t,
                ctypes.POINTER(ZlinkRoutingId),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_routing_id_to_text",
            [ctypes.POINTER(ZlinkRoutingId), ctypes.c_char_p, ctypes.POINTER(ctypes.c_size_t)],
            ctypes.c_int,
        )
        self._require(
            "zlink_routing_id_to_hex",
            [ctypes.POINTER(ZlinkRoutingId), ctypes.c_char_p, ctypes.POINTER(ctypes.c_size_t)],
            ctypes.c_int,
        )
        self._require(
            "zlink_set_subscription",
            [ctypes.c_void_p, ctypes.c_char_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_unset_subscription",
            [ctypes.c_void_p, ctypes.c_char_p],
            ctypes.c_int,
        )
        self._require("zlink_bind", [ctypes.c_void_p, ctypes.c_char_p], ctypes.c_int)
        self._require(
            "zlink_connect",
            [ctypes.c_void_p, ctypes.c_char_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_unbind",
            [ctypes.c_void_p, ctypes.c_char_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_disconnect",
            [ctypes.c_void_p, ctypes.c_char_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_socket_attach_discovery",
            [ctypes.c_void_p, ctypes.c_void_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_send",
            [ctypes.c_void_p, ctypes.POINTER(ZlinkMsg), ctypes.c_size_t, ctypes.c_uint32],
            ctypes.c_int,
        )
        self._require(
            "zlink_send_rid",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_size_t,
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_recv",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ctypes.POINTER(ZlinkRoutingId)),
                ctypes.POINTER(ctypes.POINTER(ZlinkMsg)),
                ctypes.POINTER(ctypes.c_size_t),
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_publish",
            [
                ctypes.c_void_p,
                ctypes.c_char_p,
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_size_t,
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_subscribe",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ctypes.POINTER(ZlinkRoutingId)),
                ctypes.POINTER(ctypes.POINTER(ZlinkMsg)),
                ctypes.POINTER(ctypes.c_size_t),
                ctypes.POINTER(ctypes.c_char),
                ctypes.POINTER(ctypes.c_size_t),
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_subscription_event",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ctypes.POINTER(ZlinkRoutingId)),
                ctypes.POINTER(ctypes.c_int),
                ctypes.POINTER(ctypes.c_char),
                ctypes.POINTER(ctypes.c_size_t),
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )

        self._require(
            "zlink_socket_monitor_open",
            [ctypes.c_void_p, ctypes.POINTER(ZlinkSocketMonitorOpenOptions)],
            ctypes.c_void_p,
        )
        self._require(
            "zlink_socket_monitor_handler",
            [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_socket_monitor_recv",
            [ctypes.c_void_p, ctypes.POINTER(ZlinkMonitorEvent), ctypes.c_uint32],
            ctypes.c_int,
        )
        self._require(
            "zlink_monitor_snapshot",
            [ctypes.c_void_p, ctypes.POINTER(ZlinkMonitorSnapshot)],
            ctypes.c_int,
        )
        self._require(
            "zlink_monitor_close",
            [ctypes.POINTER(ctypes.c_void_p)],
            ctypes.c_int,
        )
        self._require(
            "zlink_multipart_close",
            [ctypes.POINTER(ZlinkMsg), ctypes.c_size_t],
            None,
        )

        self._require("zlink_has", [ctypes.c_char_p], ctypes.c_bool)
        self._require(
            "zlink_proxy",
            [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_proxy_steerable",
            [
                ctypes.c_void_p,
                ctypes.c_void_p,
                ctypes.c_void_p,
                ctypes.c_void_p,
            ],
            ctypes.c_int,
        )
        self._require("zlink_sleep", [ctypes.c_int], None)
        self._require("zlink_stopwatch_start", [], ctypes.c_void_p)
        self._require(
            "zlink_stopwatch_intermediate",
            [ctypes.c_void_p],
            ctypes.c_ulong,
        )
        self._require(
            "zlink_stopwatch_stop",
            [ctypes.c_void_p],
            ctypes.c_ulong,
        )
        self._require("zlink_thread_start", [ctypes.c_void_p, ctypes.c_void_p], ctypes.c_void_p)
        self._require("zlink_thread_join", [ctypes.c_void_p], None)
        self._require("zlink_atomic_counter_new", [], ctypes.c_void_p)
        self._require("zlink_atomic_counter_set", [ctypes.c_void_p, ctypes.c_int], None)
        self._require("zlink_atomic_counter_inc", [ctypes.c_void_p], ctypes.c_int)
        self._require("zlink_atomic_counter_dec", [ctypes.c_void_p], ctypes.c_int)
        self._require("zlink_atomic_counter_value", [ctypes.c_void_p], ctypes.c_int)
        self._require("zlink_atomic_counter_destroy", [ctypes.POINTER(ctypes.c_void_p)], None)
        self._require("zlink_timer_new", [], ctypes.c_void_p)
        self._require("zlink_spot_timer_new", [ctypes.c_void_p], ctypes.c_void_p)
        self._require("zlink_timer_destroy", [ctypes.POINTER(ctypes.c_void_p)], ctypes.c_int)
        self._require("zlink_timer_start", [ctypes.c_void_p, ctypes.c_uint64, ctypes.c_uint64], ctypes.c_int)
        self._require("zlink_timer_stop", [ctypes.c_void_p], ctypes.c_int)
        self._require(
            "zlink_timer_recv",
            [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint64)],
            ctypes.c_int,
        )
        self._require("zlink_timer_handler", [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p], ctypes.c_int)

        self._require("zlink_registry_new", [ctypes.c_void_p], ctypes.c_void_p)
        self._require(
            "zlink_registry_bind",
            [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_registry_set_id",
            [ctypes.c_void_p, ctypes.c_uint32],
            ctypes.c_int,
        )
        self._require(
            "zlink_registry_add_peer",
            [ctypes.c_void_p, ctypes.c_char_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_registry_set_heartbeat",
            [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32],
            ctypes.c_int,
        )
        self._require(
            "zlink_registry_set_broadcast_interval",
            [ctypes.c_void_p, ctypes.c_uint32],
            ctypes.c_int,
        )
        self._require(
            "zlink_registry_destroy",
            [ctypes.POINTER(ctypes.c_void_p)],
            ctypes.c_int,
        )
        self._require(
            "zlink_registry_status_snapshot",
            [ctypes.c_void_p, ctypes.POINTER(ZlinkRegistryStatus)],
            ctypes.c_int,
        )
        self._require(
            "zlink_registry_service_summary_snapshot",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRegistryServiceSummaryFilter),
                ctypes.POINTER(ZlinkRegistryServiceSummaryEntry),
                ctypes.POINTER(ctypes.c_size_t),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_registry_member_peers",
            [
                ctypes.c_void_p,
                ctypes.c_uint16,
                ctypes.c_char_p,
                ctypes.POINTER(ZlinkMemberPeerEntry),
                ctypes.POINTER(ctypes.c_size_t),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_registry_member_peer_metadata",
            [
                ctypes.c_void_p,
                ctypes.c_uint16,
                ctypes.c_char_p,
                ctypes.c_uint16,
                ctypes.c_char_p,
                ctypes.POINTER(ZlinkMsg),
            ],
            ctypes.c_int,
        )

        self._require(
            "zlink_discovery_new",
            [ctypes.c_void_p, ctypes.c_uint16, ctypes.c_char_p],
            ctypes.c_void_p,
        )
        self._require(
            "zlink_discovery_connect_registry",
            [ctypes.c_void_p, ctypes.c_char_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_discovery_set_value",
            [ctypes.c_void_p, ctypes.c_int64],
            ctypes.c_int,
        )
        self._require(
            "zlink_discovery_get_value",
            [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int64)],
            ctypes.c_int,
        )
        self._require(
            "zlink_discovery_set_metadata",
            [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t],
            ctypes.c_int,
        )
        self._require(
            "zlink_discovery_get_metadata",
            [ctypes.c_void_p, ctypes.POINTER(ZlinkMsg)],
            ctypes.c_int,
        )
        self._require(
            "zlink_discovery_resolve_spot",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkRoutingId),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_discovery_set_dealer_peer_mode",
            [ctypes.c_void_p, ctypes.c_uint],
            ctypes.c_int,
        )
        self._require(
            "zlink_discovery_destroy",
            [ctypes.POINTER(ctypes.c_void_p)],
            ctypes.c_int,
        )

        self._require("zlink_spot_new", [ctypes.c_void_p], ctypes.c_void_p)
        self._require(
            "zlink_spot_destroy",
            [ctypes.POINTER(ctypes.c_void_p)],
            ctypes.c_int,
        )
        self._require("zlink_spot_node_new", [ctypes.c_void_p], ctypes.c_void_p)
        self._require(
            "zlink_spot_node_destroy",
            [ctypes.POINTER(ctypes.c_void_p)],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_bind",
            [ctypes.c_void_p, ctypes.c_char_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_connect_peer",
            [ctypes.c_void_p, ctypes.c_char_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_disconnect_peer",
            [ctypes.c_void_p, ctypes.c_char_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_attach_discovery",
            [ctypes.c_void_p, ctypes.c_void_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_attach_router",
            [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_attach_pubsub",
            [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_void_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_service_attachment_count",
            [ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t)],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_service_attachment_at",
            [
                ctypes.c_void_p,
                ctypes.c_size_t,
                ctypes.POINTER(ZlinkSpotServiceAttachmentStats),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_monitor_recv",
            [ctypes.c_void_p, ctypes.POINTER(ZlinkSpotServiceMonitorEvent), ctypes.c_uint32],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_status_snapshot",
            [ctypes.c_void_p, ctypes.POINTER(ZlinkSpotNodeStatus)],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_peers_snapshot",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkSpotNodePeerEntry),
                ctypes.POINTER(ctypes.c_size_t),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_peers_query",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkSpotNodePeerFilter),
                ctypes.POINTER(ZlinkSpotNodePeerEntry),
                ctypes.POINTER(ctypes.c_size_t),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_subjects_snapshot",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkSpotNodeSubjectFilter),
                ctypes.POINTER(ZlinkSpotNodeSubjectEntry),
                ctypes.POINTER(ctypes.c_size_t),
            ],
            ctypes.c_int,
        )

        self._require(
            "zlink_spot_request_spot",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_size_t,
                ctypes.c_void_p,
                ctypes.c_void_p,
                ctypes.c_uint32,
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_send_service",
            [
                ctypes.c_void_p,
                ctypes.c_char_p,
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_size_t,
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_publish",
            [
                ctypes.c_void_p,
                ctypes.c_char_p,
                ctypes.c_char_p,
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_size_t,
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_subscribe",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ctypes.POINTER(ZlinkRoutingId)),
                ctypes.POINTER(ctypes.POINTER(ZlinkMsg)),
                ctypes.POINTER(ctypes.c_size_t),
                ctypes.POINTER(ctypes.c_char),
                ctypes.POINTER(ctypes.c_size_t),
                ctypes.POINTER(ctypes.c_char),
                ctypes.POINTER(ctypes.c_size_t),
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_subscription_event",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ctypes.POINTER(ZlinkRoutingId)),
                ctypes.POINTER(ctypes.c_int),
                ctypes.POINTER(ctypes.c_char),
                ctypes.POINTER(ctypes.c_size_t),
                ctypes.POINTER(ctypes.c_char),
                ctypes.POINTER(ctypes.c_size_t),
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_send_spot",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_size_t,
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_send_router",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_size_t,
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_request_router",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_size_t,
                ctypes.c_void_p,
                ctypes.c_void_p,
                ctypes.c_uint32,
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_request_service",
            [
                ctypes.c_void_p,
                ctypes.c_char_p,
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_size_t,
                ctypes.c_void_p,
                ctypes.c_void_p,
                ctypes.c_uint32,
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_reply_spot",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.c_uint64,
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_size_t,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_reply_router",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.c_uint64,
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_size_t,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_handler",
            [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_dispatch_event_handler",
            [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_recv",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ctypes.POINTER(ZlinkRoutingId)),
                ctypes.POINTER(ctypes.POINTER(ZlinkRoutingId)),
                ctypes.POINTER(ctypes.c_uint64),
                ctypes.POINTER(ctypes.POINTER(ZlinkMsg)),
                ctypes.POINTER(ctypes.c_size_t),
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_router_request_spot",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_size_t,
                ctypes.c_void_p,
                ctypes.c_void_p,
                ctypes.c_uint32,
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_router_reply_spot",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.c_uint64,
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_size_t,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_router_send_spot",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_size_t,
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )

        self._require(
            "zlink_service_monitor_open",
            [ctypes.c_void_p, ctypes.POINTER(ZlinkServiceMonitorOpenOptions)],
            ctypes.c_void_p,
        )
        self._require(
            "zlink_service_monitor_handler",
            [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_service_monitor_recv",
            [ctypes.c_void_p, ctypes.POINTER(ZlinkServiceEvent), ctypes.c_uint32],
            ctypes.c_int,
        )

        self._require(
            "zlink_discovery_member_peers",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkMemberPeerEntry),
                ctypes.POINTER(ctypes.c_size_t),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_discovery_member_peer_metadata",
            [ctypes.c_void_p, ctypes.c_uint16, ctypes.c_char_p, ctypes.POINTER(ZlinkMsg)],
            ctypes.c_int,
        )
        self._require(
            "zlink_registry_topology_snapshot",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRegistryTopologyEntry),
                ctypes.POINTER(ctypes.c_size_t),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_registry_topology_query",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRegistryTopologyFilter),
                ctypes.POINTER(ZlinkRegistryTopologyEntry),
                ctypes.POINTER(ctypes.c_size_t),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_registry_query_client_new",
            [ctypes.c_void_p],
            ctypes.c_void_p,
        )
        self._require(
            "zlink_registry_query_client_connect",
            [ctypes.c_void_p, ctypes.c_char_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_registry_query_snapshot",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRegistryTopologyFilter),
                ctypes.POINTER(ZlinkRegistryTopologyEntry),
                ctypes.POINTER(ctypes.c_size_t),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_registry_query_destroy",
            [ctypes.POINTER(ctypes.c_void_p)],
            ctypes.c_int,
        )

        self._require(
            "zlink_poll",
            [ctypes.POINTER(ZlinkPollItem), ctypes.c_int, ctypes.c_long],
            ctypes.c_int,
        )
        self._require("zlink_poller_new", [], ctypes.c_void_p)
        self._require(
            "zlink_poller_destroy",
            [ctypes.POINTER(ctypes.c_void_p)],
            ctypes.c_int,
        )
        self._require("zlink_poller_size", [ctypes.c_void_p], ctypes.c_int)
        self._require(
            "zlink_poller_add",
            [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_short],
            ctypes.c_int,
        )
        self._require(
            "zlink_poller_modify",
            [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_short],
            ctypes.c_int,
        )
        self._require(
            "zlink_poller_remove",
            [ctypes.c_void_p, ctypes.c_void_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_poller_add_fd",
            [ctypes.c_void_p, ZlinkFD, ctypes.c_void_p, ctypes.c_short],
            ctypes.c_int,
        )
        self._require(
            "zlink_poller_modify_fd",
            [ctypes.c_void_p, ZlinkFD, ctypes.c_short],
            ctypes.c_int,
        )
        self._require(
            "zlink_poller_remove_fd",
            [ctypes.c_void_p, ZlinkFD],
            ctypes.c_int,
        )
        self._require(
            "zlink_poller_add_timer",
            [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_poller_remove_timer",
            [ctypes.c_void_p, ctypes.c_void_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_poller_wait",
            [ctypes.c_void_p, ctypes.POINTER(ZlinkPollerEvent), ctypes.c_long],
            ctypes.c_int,
        )
        self._require(
            "zlink_poller_wait_all",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkPollerEvent),
                ctypes.c_int,
                ctypes.c_long,
            ],
            ctypes.c_int,
        )


_lib = None


def lib():
    global _lib
    if _lib is None:
        _lib = _Lib()
    return _lib.lib


def _find_bundled_library():
    base = pathlib.Path(__file__).resolve().parent
    os_name = os.name
    if os_name == "nt":
        os_dir = (
            "windows-x86_64"
            if "64" in os.environ.get("PROCESSOR_ARCHITECTURE", "")
            else "windows-x86"
        )
        name = "zlink.dll"
    else:
        uname = os.uname().sysname.lower()
        if "darwin" in uname or "mac" in uname:
            os_dir = (
                "darwin-aarch64"
                if os.uname().machine in ("arm64", "aarch64")
                else "darwin-x86_64"
            )
            name = "libzlink.dylib"
        else:
            os_dir = (
                "linux-aarch64"
                if os.uname().machine in ("arm64", "aarch64")
                else "linux-x86_64"
            )
            name = "libzlink.so"
    candidate = base / "native" / os_dir / name
    if candidate.exists():
        return str(candidate)
    return None


def _find_dev_library():
    base = pathlib.Path(__file__).resolve()
    repo = base.parents[4]
    candidates = []
    if os.name == "nt":
        candidates.extend(
            [
                repo / "core" / "build" / "lib" / "zlink.dll",
                repo / "core" / "build" / "windows-x64" / "lib" / "zlink.dll",
            ]
        )
    else:
        uname = os.uname().sysname.lower()
        if "darwin" in uname or "mac" in uname:
            candidates.extend(
                [
                    repo / "core" / "build" / "lib" / "libzlink.dylib",
                    repo / "core" / "build" / "darwin-x64" / "lib" / "libzlink.dylib",
                ]
            )
        else:
            candidates.extend(
                [
                    repo / "build" / "lib" / "libzlink.so",
                    repo / "core" / "build" / "lib" / "libzlink.so",
                    repo / "core" / "build" / "linux-x64" / "lib" / "libzlink.so",
                ]
            )
    for candidate in candidates:
        if candidate.exists():
            return str(candidate)
    return None


def _prepare_windows_runtime(lib_path):
    dep_names = ["libcrypto-3-x64.dll", "libssl-3-x64.dll"]
    search_dirs = []

    lib_dir = pathlib.Path(lib_path).resolve().parent
    search_dirs.append(lib_dir)

    for env_key in ("ZLINK_OPENSSL_BIN", "OPENSSL_BIN"):
        value = os.environ.get(env_key)
        if value:
            search_dirs.append(pathlib.Path(value))

    for entry in os.environ.get("PATH", "").split(";"):
        if entry:
            search_dirs.append(pathlib.Path(entry))

    search_dirs.extend(
        [
            pathlib.Path(r"C:\Program Files\OpenSSL-Win64\bin"),
            pathlib.Path(r"C:\Program Files\Git\mingw64\bin"),
            pathlib.Path(
                r"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\mingw64\bin"
            ),
        ]
    )

    seen = set()
    for directory in search_dirs:
        text = str(directory)
        if not text or text in seen or not directory.exists():
            continue
        seen.add(text)
        os.add_dll_directory(text)
        for dep_name in dep_names:
            dep_path = directory / dep_name
            if dep_path.exists():
                try:
                    ctypes.CDLL(str(dep_path))
                except OSError:
                    pass
