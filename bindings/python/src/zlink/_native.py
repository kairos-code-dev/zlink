# SPDX-License-Identifier: MPL-2.0

import ctypes
import ctypes.util
import os
import sys

_lib = None


class ZlinkMsg(ctypes.Structure):
    _fields_ = [("_", ctypes.c_ubyte * 64)]


class ZlinkRoutingId(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint8), ("data", ctypes.c_uint8 * 255)]


class ZlinkMonitorEvent(ctypes.Structure):
    _fields_ = [
        ("event", ctypes.c_uint64),
        ("value", ctypes.c_uint64),
        ("routing_id", ZlinkRoutingId),
        ("local_addr", ctypes.c_char * 256),
        ("remote_addr", ctypes.c_char * 256),
    ]


class ZlinkProviderInfo(ctypes.Structure):
    _fields_ = [
        ("service_name", ctypes.c_char * 256),
        ("endpoint", ctypes.c_char * 256),
        ("routing_id", ZlinkRoutingId),
        ("weight", ctypes.c_uint32),
        ("registered_at", ctypes.c_uint64),
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


def _load_lib():
    global _lib
    if _lib is not None:
        return _lib

    path = os.environ.get("ZLINK_LIBRARY_PATH")
    if not path:
        found = ctypes.util.find_library("zlink")
        if found:
            path = found
    if not path:
        raise OSError("zlink native library not found")

    _lib = ctypes.CDLL(path)

    _lib.zlink_errno.argtypes = []
    _lib.zlink_errno.restype = ctypes.c_int

    _lib.zlink_strerror.argtypes = [ctypes.c_int]
    _lib.zlink_strerror.restype = ctypes.c_char_p

    _lib.zlink_version.argtypes = [
        ctypes.POINTER(ctypes.c_int),
        ctypes.POINTER(ctypes.c_int),
        ctypes.POINTER(ctypes.c_int),
    ]
    _lib.zlink_version.restype = None

    _lib.zlink_ctx_new.argtypes = []
    _lib.zlink_ctx_new.restype = ctypes.c_void_p

    _lib.zlink_ctx_term.argtypes = [ctypes.c_void_p]
    _lib.zlink_ctx_term.restype = ctypes.c_int

    _lib.zlink_ctx_shutdown.argtypes = [ctypes.c_void_p]
    _lib.zlink_ctx_shutdown.restype = ctypes.c_int

    _lib.zlink_ctx_set.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
    _lib.zlink_ctx_set.restype = ctypes.c_int

    _lib.zlink_ctx_get.argtypes = [ctypes.c_void_p, ctypes.c_int]
    _lib.zlink_ctx_get.restype = ctypes.c_int

    _lib.zlink_socket.argtypes = [ctypes.c_void_p, ctypes.c_int]
    _lib.zlink_socket.restype = ctypes.c_void_p

    _lib.zlink_close.argtypes = [ctypes.c_void_p]
    _lib.zlink_close.restype = ctypes.c_int

    _lib.zlink_setsockopt.argtypes = [
        ctypes.c_void_p,
        ctypes.c_int,
        ctypes.c_void_p,
        ctypes.c_size_t,
    ]
    _lib.zlink_setsockopt.restype = ctypes.c_int

    _lib.zlink_getsockopt.argtypes = [
        ctypes.c_void_p,
        ctypes.c_int,
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_size_t),
    ]
    _lib.zlink_getsockopt.restype = ctypes.c_int

    _lib.zlink_bind.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _lib.zlink_bind.restype = ctypes.c_int

    _lib.zlink_connect.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _lib.zlink_connect.restype = ctypes.c_int

    _lib.zlink_unbind.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _lib.zlink_unbind.restype = ctypes.c_int

    _lib.zlink_disconnect.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _lib.zlink_disconnect.restype = ctypes.c_int

    _lib.zlink_send.argtypes = [
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.c_size_t,
        ctypes.c_int,
    ]
    _lib.zlink_send.restype = ctypes.c_int

    _lib.zlink_send_const.argtypes = [
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.c_size_t,
        ctypes.c_int,
    ]
    _lib.zlink_send_const.restype = ctypes.c_int

    _lib.zlink_recv.argtypes = [
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.c_size_t,
        ctypes.c_int,
    ]
    _lib.zlink_recv.restype = ctypes.c_int

    _lib.zlink_msg_init.argtypes = [ctypes.POINTER(ZlinkMsg)]
    _lib.zlink_msg_init.restype = ctypes.c_int

    _lib.zlink_msg_init_size.argtypes = [ctypes.POINTER(ZlinkMsg), ctypes.c_size_t]
    _lib.zlink_msg_init_size.restype = ctypes.c_int

    _lib.zlink_msg_init_data.argtypes = [
        ctypes.POINTER(ZlinkMsg),
        ctypes.c_void_p,
        ctypes.c_size_t,
        ctypes.c_void_p,
        ctypes.c_void_p,
    ]
    _lib.zlink_msg_init_data.restype = ctypes.c_int

    _lib.zlink_msg_send.argtypes = [
        ctypes.POINTER(ZlinkMsg),
        ctypes.c_void_p,
        ctypes.c_int,
    ]
    _lib.zlink_msg_send.restype = ctypes.c_int

    _lib.zlink_msg_recv.argtypes = [
        ctypes.POINTER(ZlinkMsg),
        ctypes.c_void_p,
        ctypes.c_int,
    ]
    _lib.zlink_msg_recv.restype = ctypes.c_int

    _lib.zlink_msg_close.argtypes = [ctypes.POINTER(ZlinkMsg)]
    _lib.zlink_msg_close.restype = ctypes.c_int

    _lib.zlink_msg_move.argtypes = [ctypes.POINTER(ZlinkMsg), ctypes.POINTER(ZlinkMsg)]
    _lib.zlink_msg_move.restype = ctypes.c_int

    _lib.zlink_msg_copy.argtypes = [ctypes.POINTER(ZlinkMsg), ctypes.POINTER(ZlinkMsg)]
    _lib.zlink_msg_copy.restype = ctypes.c_int

    _lib.zlink_msg_data.argtypes = [ctypes.POINTER(ZlinkMsg)]
    _lib.zlink_msg_data.restype = ctypes.c_void_p

    _lib.zlink_msg_size.argtypes = [ctypes.POINTER(ZlinkMsg)]
    _lib.zlink_msg_size.restype = ctypes.c_size_t

    _lib.zlink_msg_more.argtypes = [ctypes.POINTER(ZlinkMsg)]
    _lib.zlink_msg_more.restype = ctypes.c_int

    _lib.zlink_msg_get.argtypes = [ctypes.POINTER(ZlinkMsg), ctypes.c_int]
    _lib.zlink_msg_get.restype = ctypes.c_int

    _lib.zlink_msg_set.argtypes = [ctypes.POINTER(ZlinkMsg), ctypes.c_int, ctypes.c_int]
    _lib.zlink_msg_set.restype = ctypes.c_int

    _lib.zlink_msg_gets.argtypes = [ctypes.POINTER(ZlinkMsg), ctypes.c_char_p]
    _lib.zlink_msg_gets.restype = ctypes.c_char_p

    _lib.zlink_msgv_close.argtypes = [ctypes.POINTER(ZlinkMsg), ctypes.c_size_t]
    _lib.zlink_msgv_close.restype = None

    _lib.zlink_socket_monitor.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_int,
    ]
    _lib.zlink_socket_monitor.restype = ctypes.c_int

    _lib.zlink_socket_monitor_open.argtypes = [ctypes.c_void_p, ctypes.c_int]
    _lib.zlink_socket_monitor_open.restype = ctypes.c_void_p

    _lib.zlink_monitor_recv.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ZlinkMonitorEvent),
        ctypes.c_int,
    ]
    _lib.zlink_monitor_recv.restype = ctypes.c_int

    _lib.zlink_poll.argtypes = [
        ctypes.POINTER(ZlinkPollItem),
        ctypes.c_int,
        ctypes.c_long,
    ]
    _lib.zlink_poll.restype = ctypes.c_int

    _lib.zlink_registry_new.argtypes = [ctypes.c_void_p]
    _lib.zlink_registry_new.restype = ctypes.c_void_p

    _lib.zlink_registry_set_endpoints.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_char_p,
    ]
    _lib.zlink_registry_set_endpoints.restype = ctypes.c_int

    _lib.zlink_registry_set_id.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    _lib.zlink_registry_set_id.restype = ctypes.c_int

    _lib.zlink_registry_add_peer.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _lib.zlink_registry_add_peer.restype = ctypes.c_int

    _lib.zlink_registry_set_heartbeat.argtypes = [
        ctypes.c_void_p,
        ctypes.c_uint32,
        ctypes.c_uint32,
    ]
    _lib.zlink_registry_set_heartbeat.restype = ctypes.c_int

    _lib.zlink_registry_set_broadcast_interval.argtypes = [
        ctypes.c_void_p,
        ctypes.c_uint32,
    ]
    _lib.zlink_registry_set_broadcast_interval.restype = ctypes.c_int

    _lib.zlink_registry_start.argtypes = [ctypes.c_void_p]
    _lib.zlink_registry_start.restype = ctypes.c_int

    _lib.zlink_registry_destroy.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
    _lib.zlink_registry_destroy.restype = ctypes.c_int

    _lib.zlink_discovery_new_typed.argtypes = [ctypes.c_void_p, ctypes.c_uint16]
    _lib.zlink_discovery_new_typed.restype = ctypes.c_void_p

    _lib.zlink_discovery_connect_registry.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
    ]
    _lib.zlink_discovery_connect_registry.restype = ctypes.c_int

    _lib.zlink_discovery_subscribe.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _lib.zlink_discovery_subscribe.restype = ctypes.c_int

    _lib.zlink_discovery_unsubscribe.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _lib.zlink_discovery_unsubscribe.restype = ctypes.c_int

    _lib.zlink_discovery_get_receivers.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.POINTER(ZlinkProviderInfo),
        ctypes.POINTER(ctypes.c_size_t),
    ]
    _lib.zlink_discovery_get_receivers.restype = ctypes.c_int

    _lib.zlink_discovery_receiver_count.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
    ]
    _lib.zlink_discovery_receiver_count.restype = ctypes.c_int

    _lib.zlink_discovery_service_available.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
    ]
    _lib.zlink_discovery_service_available.restype = ctypes.c_int

    _lib.zlink_discovery_destroy.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
    _lib.zlink_discovery_destroy.restype = ctypes.c_int

    _lib.zlink_gateway_new.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                       ctypes.c_char_p]
    _lib.zlink_gateway_new.restype = ctypes.c_void_p

    _lib.zlink_gateway_send.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.POINTER(ZlinkMsg),
        ctypes.c_size_t,
        ctypes.c_int,
    ]
    _lib.zlink_gateway_send.restype = ctypes.c_int

    _lib.zlink_gateway_recv.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.POINTER(ZlinkMsg)),
        ctypes.POINTER(ctypes.c_size_t),
        ctypes.c_int,
        ctypes.c_char_p,
    ]
    _lib.zlink_gateway_recv.restype = ctypes.c_int

    _lib.zlink_gateway_set_lb_strategy.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_int,
    ]
    _lib.zlink_gateway_set_lb_strategy.restype = ctypes.c_int

    _lib.zlink_gateway_set_tls_client.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_char_p,
        ctypes.c_int,
    ]
    _lib.zlink_gateway_set_tls_client.restype = ctypes.c_int

    _lib.zlink_gateway_connection_count.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
    ]
    _lib.zlink_gateway_connection_count.restype = ctypes.c_int

    _lib.zlink_gateway_destroy.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
    _lib.zlink_gateway_destroy.restype = ctypes.c_int

    _lib.zlink_receiver_new.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _lib.zlink_receiver_new.restype = ctypes.c_void_p

    _lib.zlink_gateway_setsockopt.argtypes = [
        ctypes.c_void_p,
        ctypes.c_int,
        ctypes.c_void_p,
        ctypes.c_size_t,
    ]
    _lib.zlink_gateway_setsockopt.restype = ctypes.c_int

    _lib.zlink_receiver_setsockopt.argtypes = [
        ctypes.c_void_p,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_void_p,
        ctypes.c_size_t,
    ]
    _lib.zlink_receiver_setsockopt.restype = ctypes.c_int

    _lib.zlink_registry_setsockopt.argtypes = [
        ctypes.c_void_p,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_void_p,
        ctypes.c_size_t,
    ]
    _lib.zlink_registry_setsockopt.restype = ctypes.c_int

    _lib.zlink_discovery_setsockopt.argtypes = [
        ctypes.c_void_p,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_void_p,
        ctypes.c_size_t,
    ]
    _lib.zlink_discovery_setsockopt.restype = ctypes.c_int

    _lib.zlink_receiver_bind.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _lib.zlink_receiver_bind.restype = ctypes.c_int

    _lib.zlink_receiver_connect_registry.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
    ]
    _lib.zlink_receiver_connect_registry.restype = ctypes.c_int

    _lib.zlink_receiver_register.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_char_p,
        ctypes.c_uint32,
    ]
    _lib.zlink_receiver_register.restype = ctypes.c_int

    _lib.zlink_receiver_update_weight.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_uint32,
    ]
    _lib.zlink_receiver_update_weight.restype = ctypes.c_int

    _lib.zlink_receiver_unregister.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _lib.zlink_receiver_unregister.restype = ctypes.c_int

    _lib.zlink_receiver_register_result.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.POINTER(ctypes.c_int),
        ctypes.c_char_p,
        ctypes.c_char_p,
    ]
    _lib.zlink_receiver_register_result.restype = ctypes.c_int

    _lib.zlink_receiver_set_tls_server.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_char_p,
    ]
    _lib.zlink_receiver_set_tls_server.restype = ctypes.c_int

    _lib.zlink_receiver_router.argtypes = [ctypes.c_void_p]
    _lib.zlink_receiver_router.restype = ctypes.c_void_p

    _lib.zlink_receiver_destroy.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
    _lib.zlink_receiver_destroy.restype = ctypes.c_int

    _lib.zlink_spot_node_new.argtypes = [ctypes.c_void_p]
    _lib.zlink_spot_node_new.restype = ctypes.c_void_p

    _lib.zlink_spot_node_destroy.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
    _lib.zlink_spot_node_destroy.restype = ctypes.c_int

    _lib.zlink_spot_node_bind.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _lib.zlink_spot_node_bind.restype = ctypes.c_int

    _lib.zlink_spot_node_connect_registry.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
    ]
    _lib.zlink_spot_node_connect_registry.restype = ctypes.c_int

    _lib.zlink_spot_node_connect_peer_pub.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
    ]
    _lib.zlink_spot_node_connect_peer_pub.restype = ctypes.c_int

    _lib.zlink_spot_node_disconnect_peer_pub.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
    ]
    _lib.zlink_spot_node_disconnect_peer_pub.restype = ctypes.c_int

    _lib.zlink_spot_node_register.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_char_p,
    ]
    _lib.zlink_spot_node_register.restype = ctypes.c_int

    _lib.zlink_spot_node_unregister.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _lib.zlink_spot_node_unregister.restype = ctypes.c_int

    _lib.zlink_spot_node_set_discovery.argtypes = [
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.c_char_p,
    ]
    _lib.zlink_spot_node_set_discovery.restype = ctypes.c_int

    _lib.zlink_spot_node_set_tls_server.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_char_p,
    ]
    _lib.zlink_spot_node_set_tls_server.restype = ctypes.c_int

    _lib.zlink_spot_node_set_tls_client.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_char_p,
        ctypes.c_int,
    ]
    _lib.zlink_spot_node_set_tls_client.restype = ctypes.c_int

    _lib.zlink_spot_node_pub_socket.argtypes = [ctypes.c_void_p]
    _lib.zlink_spot_node_pub_socket.restype = ctypes.c_void_p

    _lib.zlink_spot_node_sub_socket.argtypes = [ctypes.c_void_p]
    _lib.zlink_spot_node_sub_socket.restype = ctypes.c_void_p

    _lib.zlink_spot_new.argtypes = [ctypes.c_void_p]
    _lib.zlink_spot_new.restype = ctypes.c_void_p

    _lib.zlink_spot_destroy.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
    _lib.zlink_spot_destroy.restype = ctypes.c_int

    _lib.zlink_spot_topic_create.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_int,
    ]
    _lib.zlink_spot_topic_create.restype = ctypes.c_int

    _lib.zlink_spot_topic_destroy.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _lib.zlink_spot_topic_destroy.restype = ctypes.c_int

    _lib.zlink_spot_publish.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.POINTER(ZlinkMsg),
        ctypes.c_size_t,
        ctypes.c_int,
    ]
    _lib.zlink_spot_publish.restype = ctypes.c_int

    _lib.zlink_spot_subscribe.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _lib.zlink_spot_subscribe.restype = ctypes.c_int

    _lib.zlink_spot_subscribe_pattern.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
    ]
    _lib.zlink_spot_subscribe_pattern.restype = ctypes.c_int

    _lib.zlink_spot_unsubscribe.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _lib.zlink_spot_unsubscribe.restype = ctypes.c_int

    _lib.zlink_spot_recv.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.POINTER(ZlinkMsg)),
        ctypes.POINTER(ctypes.c_size_t),
        ctypes.c_int,
        ctypes.c_char_p,
        ctypes.POINTER(ctypes.c_size_t),
    ]
    _lib.zlink_spot_recv.restype = ctypes.c_int

    _lib.zlink_spot_pub_socket.argtypes = [ctypes.c_void_p]
    _lib.zlink_spot_pub_socket.restype = ctypes.c_void_p

    _lib.zlink_spot_sub_socket.argtypes = [ctypes.c_void_p]
    _lib.zlink_spot_sub_socket.restype = ctypes.c_void_p

    return _lib


def lib():
    return _load_lib()


def version():
    lib = _load_lib()
    major = ctypes.c_int()
    minor = ctypes.c_int()
    patch = ctypes.c_int()
    lib.zlink_version(ctypes.byref(major), ctypes.byref(minor), ctypes.byref(patch))
    return major.value, minor.value, patch.value


def strerror(code):
    lib = _load_lib()
    msg = lib.zlink_strerror(code)
    if not msg:
        return ""
    return msg.decode("utf-8", errors="replace")


def errno():
    lib = _load_lib()
    return lib.zlink_errno()
