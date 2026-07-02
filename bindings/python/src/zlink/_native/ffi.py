# SPDX-License-Identifier: MPL-2.0

import ctypes
import os

from ._native_loader import load_native_library


class ZlinkMsg(ctypes.Union):
    # zlink_msg_t is a 64-byte opaque value aligned to sizeof(void *).
    # Using a Union with a pointer member preserves the 64-byte size while
    # forcing ctypes to match the required native alignment.
    _fields_ = [
        ("_", ctypes.c_ubyte * 64),
        ("_align", ctypes.c_void_p),
    ]


class ZlinkRoutingId(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint8), ("data", ctypes.c_uint8 * 255)]


ZLINK_ACTOR_ID_MAX = 256
ZLINK_PART_FINAL = 0
ZLINK_PART_MORE = 1


class ZlinkActorRef(ctypes.Structure):
    _fields_ = [
        ("node_rid", ZlinkRoutingId),
        ("actor_id", ctypes.c_char * ZLINK_ACTOR_ID_MAX),
        ("generation", ctypes.c_uint64),
    ]


class ZlinkActorRecvInfo(ctypes.Structure):
    _fields_ = [
        ("actor", ZlinkActorRef),
        ("source_node_rid", ZlinkRoutingId),
        ("source_session_rid", ZlinkRoutingId),
        ("flags", ctypes.c_uint32),
    ]


class ZlinkActorJoinInfo(ctypes.Structure):
    _fields_ = [
        ("source_actor", ZlinkActorRef),
        ("target_actor", ZlinkActorRef),
        ("source_node_rid", ZlinkRoutingId),
        ("source_spot_rid", ZlinkRoutingId),
        ("target_node_rid", ZlinkRoutingId),
        ("target_spot_rid", ZlinkRoutingId),
        ("join_epoch", ctypes.c_uint64),
        ("request", ctypes.c_void_p),
        ("flags", ctypes.c_uint32),
    ]


class ZlinkActorJoinResult(ctypes.Structure):
    _fields_ = [
        ("result", ctypes.c_int),
        ("join_result_code", ctypes.c_int32),
        ("actor", ZlinkActorRef),
        ("joined_spot_rid", ZlinkRoutingId),
        ("join_epoch", ctypes.c_uint64),
        ("flags", ctypes.c_uint32),
    ]


class ZlinkActorJoinEntrySpotResult(ctypes.Structure):
    _fields_ = [
        ("result", ctypes.c_int),
        ("join_result_code", ctypes.c_int32),
        ("actor", ZlinkActorRef),
        ("target_node_rid", ZlinkRoutingId),
        ("joined_spot_rid", ZlinkRoutingId),
        ("join_epoch", ctypes.c_uint64),
        ("flags", ctypes.c_uint32),
    ]


class ZlinkActorLookupResult(ctypes.Structure):
    _fields_ = [
        ("result", ctypes.c_int),
        ("actor", ZlinkActorRef),
        ("flags", ctypes.c_uint32),
    ]


class ZlinkActorRoute(ctypes.Structure):
    _fields_ = [
        ("actor", ZlinkActorRef),
        ("current_spot_rid", ZlinkRoutingId),
        ("current_spot_kind", ctypes.c_int),
    ]


class ZlinkSpotRoute(ctypes.Structure):
    _fields_ = [
        ("spot_rid", ZlinkRoutingId),
        ("owner_node_rid", ZlinkRoutingId),
        ("spot_kind", ctypes.c_int),
    ]


class ZlinkSpotActorLifecycleInfo(ctypes.Structure):
    _fields_ = [
        ("previous_actor", ZlinkActorRef),
        ("current_actor", ZlinkActorRef),
        ("previous_spot_rid", ZlinkRoutingId),
        ("current_spot_rid", ZlinkRoutingId),
        ("join_epoch", ctypes.c_uint64),
        ("flags", ctypes.c_uint32),
    ]


class ZlinkSpotActorLifecycleEvent(ctypes.Structure):
    _fields_ = [
        ("kind", ctypes.c_int),
        ("info", ZlinkSpotActorLifecycleInfo),
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


class ZlinkMonitorStatus(ctypes.Structure):
    _fields_ = [
        ("source_kind", ctypes.c_uint32),
        ("state_flags", ctypes.c_uint32),
        ("detail_flags", ctypes.c_uint32),
        ("snd_pending_msgs", ctypes.c_uint64),
        ("rcv_pending_msgs", ctypes.c_uint64),
        ("auto_hwm_enabled", ctypes.c_uint32),
        ("auto_hwm_profile", ctypes.c_uint32),
        ("auto_hwm_role", ctypes.c_uint32),
        ("auto_hwm_policy_class", ctypes.c_uint32),
        ("auto_hwm_unit_budget_bytes", ctypes.c_uint64),
        ("auto_hwm_size_cap", ctypes.c_uint32),
        ("auto_hwm_socket_message_slots", ctypes.c_uint64),
        ("auto_hwm_connection_bucket_enabled", ctypes.c_uint32),
        ("auto_hwm_connection_bucket_count", ctypes.c_uint32),
        ("auto_hwm_connection_bucket_index", ctypes.c_uint32),
        ("auto_hwm_connection_bucket_hwm_4k", ctypes.c_uint32),
        ("auto_hwm_connection_bucket_hysteresis_retained", ctypes.c_uint32),
        ("auto_hwm_effective_message_bytes", ctypes.c_uint64),
        ("auto_hwm_applied_sndhwm", ctypes.c_int32),
        ("auto_hwm_applied_rcvhwm", ctypes.c_int32),
        ("auto_hwm_effective_sndbuf", ctypes.c_int32),
        ("auto_hwm_effective_rcvbuf", ctypes.c_int32),
        ("auto_hwm_last_recalc_ms", ctypes.c_uint64),
        ("auto_hwm_last_recalc_reason", ctypes.c_uint32),
        ("auto_hwm_send_blocked_ratio_ppm", ctypes.c_uint32),
        ("auto_hwm_deferred_sndhwm", ctypes.c_int32),
        ("auto_hwm_deferred_rcvhwm", ctypes.c_int32),
    ]


class ZlinkSpotNodeStatus(ctypes.Structure):
    _fields_ = [
        ("channel_name", ctypes.c_char * 256),
        ("local_endpoint", ctypes.c_char * 256),
        ("node_routing_id", ZlinkRoutingId),
        ("state", ctypes.c_uint32),
        ("configured_peer_count", ctypes.c_uint32),
        ("active_peer_count", ctypes.c_uint32),
        ("connected_peer_count", ctypes.c_uint32),
        ("subject_count", ctypes.c_uint32),
        ("ready_subject_count", ctypes.c_uint32),
        ("disconnected_sub_target_count", ctypes.c_uint32),
        ("disconnected_routed_target_count", ctypes.c_uint32),
        ("last_error", ctypes.c_int32),
        ("last_changed_ms", ctypes.c_uint64),
    ]


class ZlinkSpotNodePeerEntry(ctypes.Structure):
    _fields_ = [
        ("channel_name", ctypes.c_char * 256),
        ("local_endpoint", ctypes.c_char * 256),
        ("peer_endpoint", ctypes.c_char * 256),
        ("source", ctypes.c_uint32),
        ("kind", ctypes.c_uint32),
        ("state", ctypes.c_uint32),
        ("weight", ctypes.c_uint32),
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


class ZlinkSpotNodeOptions(ctypes.Structure):
    _fields_ = [("mode", ctypes.c_uint32)]


class ZlinkSpotRouteBridgeOptions(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("default_request_timeout_ms", ctypes.c_int),
        ("error_reply_policy", ctypes.c_int),
        ("receive_mode", ctypes.c_int),
    ]


class ZlinkSpotRouteBridgeEndpointOptions(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("capabilities", ctypes.c_uint32),
        ("inbound_relay_policy", ctypes.c_int),
    ]


class ZlinkSpotNodeSocketFilter(ctypes.Structure):
    _fields_ = [
        ("owner", ctypes.c_uint32),
        ("socket_type", ctypes.c_uint32),
        ("socket_name", ctypes.c_char * 64),
    ]


class ZlinkSpotNodeSocketEntry(ctypes.Structure):
    _fields_ = [
        ("owner", ctypes.c_uint32),
        ("owner_id", ctypes.c_uint64),
        ("owner_name", ctypes.c_char * 64),
        ("socket_name", ctypes.c_char * 64),
        ("socket_type", ctypes.c_uint32),
        ("auto_hwm_visible", ctypes.c_uint32),
        ("monitor_status", ZlinkMonitorStatus),
    ]


class ZlinkSpotNodeSpotEntry(ctypes.Structure):
    _fields_ = [
        ("spot_rid", ZlinkRoutingId),
        ("spot_kind", ctypes.c_int),
        ("dispatch_handler_attached", ctypes.c_uint32),
        ("joined_actor_count", ctypes.c_uint32),
        ("pending_actor_join_count", ctypes.c_uint32),
        ("route_synced", ctypes.c_uint32),
        ("last_changed_ms", ctypes.c_uint64),
    ]


class ZlinkSpotNodeActorEntry(ctypes.Structure):
    _fields_ = [
        ("actor", ZlinkActorRef),
        ("current_spot_rid", ZlinkRoutingId),
        ("current_spot_kind", ctypes.c_int),
        ("route_synced", ctypes.c_uint32),
        ("pending_message_count", ctypes.c_uint32),
        ("last_changed_ms", ctypes.c_uint64),
    ]


class ZlinkSpotDispatchInfo(ctypes.Structure):
    _fields_ = [
        ("event", ctypes.c_int),
        ("subject_kind", ctypes.c_int),
        ("subject", ctypes.c_void_p),
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
        ("source_kind", ctypes.c_uint32),
        ("socket", ctypes.c_void_p),
        ("fd", ZlinkFD),
        ("timer", ctypes.c_void_p),
        ("user_data", ctypes.c_void_p),
        ("events", ctypes.c_short),
    ]


class _Lib:
    def __init__(self):
        self.lib = load_native_library(self._bind_loaded)

    def _bind_loaded(self, lib):
        self.lib = lib
        self._bind()

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
        self._require("zlink_ctx_new", [], ctypes.c_void_p)
        self._require("zlink_ctx_term", [ctypes.c_void_p], ctypes.c_int)
        self._require("zlink_ctx_shutdown", [ctypes.c_void_p], ctypes.c_int)
        self._require(
            "zlink_ctx_set",
            [ctypes.c_void_p, ctypes.c_int, ctypes.c_int],
            ctypes.c_int,
        )
        self._require(
            "zlink_ctx_set_data",
            [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_size_t],
            ctypes.c_int,
        )
        self._require(
            "zlink_ctx_get",
            [ctypes.c_void_p, ctypes.c_int],
            ctypes.c_int,
        )
        self._require(
            "zlink_ctx_auto_hwm_recalculate",
            [ctypes.c_void_p],
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
            "zlink_stream_bind_actor",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkActorRef),
                ctypes.c_void_p,
                ctypes.c_void_p,
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_stream_unbind_actor",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.c_char_p,
                ctypes.c_void_p,
                ctypes.c_void_p,
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_stream_send_bound_actor_part",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.c_char_p,
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_uint32,
                ctypes.c_int,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_send_ready_handler",
            [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_dealer_request_part",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_uint32,
                ctypes.c_int,
                ctypes.c_uint32,
                ctypes.c_void_p,
                ctypes.c_void_p,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_router_request_part",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_uint32,
                ctypes.c_int,
                ctypes.c_uint32,
                ctypes.c_void_p,
                ctypes.c_void_p,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_router_reply_part",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.c_uint64,
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_int,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_router_recv_part",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ctypes.POINTER(ZlinkRoutingId)),
                ctypes.POINTER(ctypes.POINTER(ZlinkRoutingId)),
                ctypes.POINTER(ctypes.c_uint64),
                ctypes.POINTER(ZlinkMsg),
                ctypes.POINTER(ctypes.c_int),
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
            "zlink_get_dealer_option",
            [
                ctypes.c_void_p,
                ctypes.c_int,
                ctypes.c_void_p,
                ctypes.POINTER(ctypes.c_size_t),
            ],
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
            [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t],
            ctypes.c_int,
        )
        self._require(
            "zlink_get_routing_id",
            [ctypes.c_void_p, ctypes.POINTER(ZlinkRoutingId)],
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
        self._require(
            "zlink_subscription_at",
            [
                ctypes.c_void_p,
                ctypes.c_size_t,
                ctypes.c_void_p,
                ctypes.POINTER(ctypes.c_size_t),
                ctypes.POINTER(ctypes.c_int),
            ],
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
            "zlink_disconnect_rid",
            [ctypes.c_void_p, ctypes.POINTER(ZlinkRoutingId)],
            ctypes.c_int,
        )
        self._require(
            "zlink_socket_set_channel_name",
            [ctypes.c_void_p, ctypes.c_char_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_socket_get_channel_name",
            [
                ctypes.c_void_p,
                ctypes.c_char_p,
                ctypes.c_size_t,
                ctypes.POINTER(ctypes.c_size_t),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_send_part",
            [ctypes.c_void_p, ctypes.POINTER(ZlinkMsg), ctypes.c_uint32, ctypes.c_int],
            ctypes.c_int,
        )
        self._require(
            "zlink_send_part_rid",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_uint32,
                ctypes.c_int,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_recv_part",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ctypes.POINTER(ZlinkRoutingId)),
                ctypes.POINTER(ZlinkMsg),
                ctypes.POINTER(ctypes.c_int),
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_publish_part",
            [
                ctypes.c_void_p,
                ctypes.c_char_p,
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_uint32,
                ctypes.c_int,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_subscribe_part",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ctypes.POINTER(ZlinkRoutingId)),
                ctypes.POINTER(ctypes.c_char),
                ctypes.c_size_t,
                ctypes.POINTER(ctypes.c_size_t),
                ctypes.POINTER(ZlinkMsg),
                ctypes.POINTER(ctypes.c_int),
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_xpub_recv_part",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ctypes.POINTER(ZlinkRoutingId)),
                ctypes.POINTER(ctypes.c_int),
                ctypes.POINTER(ctypes.c_char),
                ctypes.c_size_t,
                ctypes.POINTER(ctypes.c_size_t),
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_recv_subscription_event",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ctypes.POINTER(ZlinkRoutingId)),
                ctypes.POINTER(ctypes.c_int),
                ctypes.POINTER(ctypes.c_char),
                ctypes.c_size_t,
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
            "zlink_monitor_status",
            [ctypes.c_void_p, ctypes.POINTER(ZlinkMonitorStatus)],
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


        self._require("zlink_spot_new", [ctypes.c_void_p], ctypes.c_void_p)
        self._require(
            "zlink_spot_destroy",
            [ctypes.POINTER(ctypes.c_void_p)],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_new",
            [ctypes.c_void_p, ctypes.POINTER(ZlinkSpotNodeOptions)],
            ctypes.c_void_p,
        )
        self._require(
            "zlink_spot_node_destroy",
            [ctypes.POINTER(ctypes.c_void_p)],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_actor_new",
            [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ZlinkActorRef)],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_actor_lookup",
            [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ZlinkActorRef)],
            ctypes.c_int,
        )
        self._require(
            "zlink_remote_actor_get_ref",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.c_char_p,
                ctypes.c_void_p,
                ctypes.c_void_p,
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_actor_destroy",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkActorRef),
                ctypes.c_void_p,
                ctypes.c_void_p,
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_actor_join_spot",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkActorRef),
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
            "zlink_spot_node_actor_join_entry_spot",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkActorRef),
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.c_void_p,
                ctypes.c_size_t,
                ctypes.c_void_p,
                ctypes.c_void_p,
                ctypes.c_int,
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_actor_join_recv",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkActorJoinInfo),
                ctypes.POINTER(ctypes.POINTER(ZlinkMsg)),
                ctypes.POINTER(ctypes.c_size_t),
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_actor_join_reply",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkActorJoinInfo),
                ctypes.c_int32,
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_size_t,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_actor_leave_spot",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkActorRef),
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.c_void_p,
                ctypes.c_void_p,
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_actor_recv_part",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkActorRef),
                ctypes.POINTER(ZlinkActorRecvInfo),
                ctypes.POINTER(ZlinkMsg),
                ctypes.POINTER(ctypes.c_int),
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_actor_send_bound_session_msg",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkActorRef),
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_stream_bound_actors",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkActorRef),
                ctypes.POINTER(ctypes.c_size_t),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_actor_close_bound_session",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkActorRef),
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_set_pub_bind",
            [ctypes.c_void_p, ctypes.c_char_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_set_router_bind",
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
            "zlink_spot_node_disconnect_peer_rid",
            [ctypes.c_void_p, ctypes.POINTER(ZlinkRoutingId)],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_route_bridge_new",
            [
                ctypes.c_void_p,
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkSpotRouteBridgeOptions),
            ],
            ctypes.c_void_p,
        )
        self._require(
            "zlink_spot_route_bridge_attach_router_channel",
            [
                ctypes.c_void_p,
                ctypes.c_char_p,
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkSpotRouteBridgeEndpointOptions),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_route_bridge_send",
            [
                ctypes.c_void_p,
                ctypes.c_char_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_size_t,
                ctypes.c_int,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_route_bridge_request",
            [
                ctypes.c_void_p,
                ctypes.c_char_p,
                ctypes.POINTER(ZlinkRoutingId),
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
            "zlink_spot_route_bridge_drain",
            [ctypes.c_void_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_route_bridge_close",
            [ctypes.c_void_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_publisher_new",
            [ctypes.c_void_p],
            ctypes.c_void_p,
        )
        self._require(
            "zlink_spot_node_publisher_publish",
            [
                ctypes.c_void_p,
                ctypes.c_char_p,
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_size_t,
                ctypes.c_int,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_publisher_close",
            [ctypes.c_void_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_set_spot_node_option",
            [ctypes.c_void_p, ctypes.c_uint, ctypes.c_void_p, ctypes.c_size_t],
            ctypes.c_int,
        )
        self._require(
            "zlink_get_spot_node_option",
            [
                ctypes.c_void_p,
                ctypes.c_uint,
                ctypes.c_void_p,
                ctypes.POINTER(ctypes.c_size_t),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_entry_spot",
            [ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_spot_lookup",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ctypes.c_void_p),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_spot_get_or_new",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ctypes.c_void_p),
                ctypes.POINTER(ctypes.c_uint32),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_status",
            [ctypes.c_void_p, ctypes.POINTER(ZlinkSpotNodeStatus)],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_peers",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkSpotNodePeerEntry),
                ctypes.POINTER(ctypes.c_size_t),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_peers",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkSpotNodePeerFilter),
                ctypes.POINTER(ZlinkSpotNodePeerEntry),
                ctypes.POINTER(ctypes.c_size_t),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_subjects",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkSpotNodeSubjectFilter),
                ctypes.POINTER(ZlinkSpotNodeSubjectEntry),
                ctypes.POINTER(ctypes.c_size_t),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_internal_sockets",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkSpotNodeSocketFilter),
                ctypes.POINTER(ZlinkSpotNodeSocketEntry),
                ctypes.POINTER(ctypes.c_size_t),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_spots",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkSpotNodeSpotEntry),
                ctypes.POINTER(ctypes.c_size_t),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_node_actors",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkSpotNodeActorEntry),
                ctypes.POINTER(ctypes.c_size_t),
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_actors",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkActorRef),
                ctypes.POINTER(ctypes.c_size_t),
            ],
            ctypes.c_int,
        )

        self._require(
            "zlink_spot_send_channel_part",
            [
                ctypes.c_void_p,
                ctypes.c_char_p,
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_uint32,
                ctypes.c_int,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_request_channel_part",
            [
                ctypes.c_void_p,
                ctypes.c_char_p,
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_void_p,
                ctypes.c_void_p,
                ctypes.c_uint32,
                ctypes.c_int,
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_request_spot_part",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_void_p,
                ctypes.c_void_p,
                ctypes.c_uint32,
                ctypes.c_int,
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_request_router_part",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_void_p,
                ctypes.c_void_p,
                ctypes.c_uint32,
                ctypes.c_int,
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_publish_part",
            [
                ctypes.c_void_p,
                ctypes.c_char_p,
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_uint32,
                ctypes.c_int,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_subscribe_part",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ctypes.POINTER(ZlinkRoutingId)),
                ctypes.POINTER(ctypes.c_char),
                ctypes.c_size_t,
                ctypes.POINTER(ctypes.c_size_t),
                ctypes.POINTER(ZlinkMsg),
                ctypes.POINTER(ctypes.c_int),
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_send_spot_part",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_uint32,
                ctypes.c_int,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_reply_spot_part",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.c_uint64,
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_int,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_reply_router_part",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.c_uint64,
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_int,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_dispatch_event_handler",
            [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_recv_part",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ctypes.POINTER(ZlinkRoutingId)),
                ctypes.POINTER(ctypes.POINTER(ZlinkRoutingId)),
                ctypes.POINTER(ctypes.c_uint64),
                ctypes.POINTER(ZlinkMsg),
                ctypes.POINTER(ctypes.c_int),
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_spot_recv_actor_lifecycle",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkSpotActorLifecycleEvent),
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_router_request_spot_part",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_void_p,
                ctypes.c_void_p,
                ctypes.c_uint32,
                ctypes.c_int,
                ctypes.c_uint32,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_router_reply_spot_part",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.c_uint64,
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_int,
            ],
            ctypes.c_int,
        )
        self._require(
            "zlink_router_send_spot_part",
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkRoutingId),
                ctypes.POINTER(ZlinkMsg),
                ctypes.c_uint32,
                ctypes.c_int,
            ],
            ctypes.c_int,
        )


        self._require(
            "zlink_poll",
            [
                ctypes.POINTER(ZlinkPollItem),
                ctypes.c_int,
                ctypes.c_long,
                ctypes.POINTER(ctypes.c_int),
            ],
            ctypes.c_int,
        )
        self._require("zlink_poller_new", [], ctypes.c_void_p)
        self._require(
            "zlink_poller_destroy",
            [ctypes.POINTER(ctypes.c_void_p)],
            ctypes.c_int,
        )
        self._require(
            "zlink_poller_size",
            [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int)],
            ctypes.c_int,
        )
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
            [
                ctypes.c_void_p,
                ctypes.POINTER(ZlinkPollerEvent),
                ctypes.c_int,
                ctypes.c_long,
                ctypes.POINTER(ctypes.c_int),
            ],
            ctypes.c_int,
        )


_lib = None


def lib():
    global _lib
    if _lib is None:
        _lib = _Lib()
    return _lib.lib
