import ctypes
import ctypes.util
import os
import pathlib


class _Lib:
    def __init__(self):
        path = os.environ.get("ZLINK_LIBRARY_PATH")
        if not path:
            path = ctypes.util.find_library("zlink")
        if not path:
            path = _find_bundled_library()
        if not path:
            raise OSError("zlink native library not found")
        self.lib = ctypes.CDLL(path)
        self._bind()

    def _bind(self):
        L = self.lib
        L.zlink_version.argtypes = [ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
        L.zlink_version.restype = None

        L.zlink_ctx_new.argtypes = []
        L.zlink_ctx_new.restype = ctypes.c_void_p
        L.zlink_ctx_term.argtypes = [ctypes.c_void_p]
        L.zlink_ctx_term.restype = ctypes.c_int
        L.zlink_ctx_shutdown.argtypes = [ctypes.c_void_p]
        L.zlink_ctx_shutdown.restype = ctypes.c_int
        L.zlink_ctx_set.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
        L.zlink_ctx_set.restype = ctypes.c_int
        L.zlink_ctx_get.argtypes = [ctypes.c_void_p, ctypes.c_int]
        L.zlink_ctx_get.restype = ctypes.c_int

        L.zlink_socket.argtypes = [ctypes.c_void_p, ctypes.c_int]
        L.zlink_socket.restype = ctypes.c_void_p
        L.zlink_close.argtypes = [ctypes.c_void_p]
        L.zlink_close.restype = ctypes.c_int
        L.zlink_bind.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_bind.restype = ctypes.c_int
        L.zlink_connect.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_connect.restype = ctypes.c_int
        L.zlink_unbind.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_unbind.restype = ctypes.c_int
        L.zlink_disconnect.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_disconnect.restype = ctypes.c_int
        L.zlink_send.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
        L.zlink_send.restype = ctypes.c_int
        L.zlink_send_const.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
        L.zlink_send_const.restype = ctypes.c_int
        L.zlink_recv.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
        L.zlink_recv.restype = ctypes.c_int
        L.zlink_setsockopt.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_size_t]
        L.zlink_setsockopt.restype = ctypes.c_int
        L.zlink_getsockopt.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t)]
        L.zlink_getsockopt.restype = ctypes.c_int

        L.zlink_msg_init.argtypes = [ctypes.c_void_p]
        L.zlink_msg_init.restype = ctypes.c_int
        L.zlink_msg_init_size.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        L.zlink_msg_init_size.restype = ctypes.c_int
        L.zlink_msg_send.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int]
        L.zlink_msg_send.restype = ctypes.c_int
        L.zlink_msg_recv.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int]
        L.zlink_msg_recv.restype = ctypes.c_int
        L.zlink_msg_close.argtypes = [ctypes.c_void_p]
        L.zlink_msg_close.restype = ctypes.c_int
        L.zlink_msg_move.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        L.zlink_msg_move.restype = ctypes.c_int
        L.zlink_msg_copy.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        L.zlink_msg_copy.restype = ctypes.c_int
        L.zlink_msg_data.argtypes = [ctypes.c_void_p]
        L.zlink_msg_data.restype = ctypes.c_void_p
        L.zlink_msg_size.argtypes = [ctypes.c_void_p]
        L.zlink_msg_size.restype = ctypes.c_size_t
        L.zlink_msg_more.argtypes = [ctypes.c_void_p]
        L.zlink_msg_more.restype = ctypes.c_int

        L.zlink_poll.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_long]
        L.zlink_poll.restype = ctypes.c_int

        L.zlink_socket_monitor.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        L.zlink_socket_monitor.restype = ctypes.c_int
        L.zlink_socket_monitor_open.argtypes = [ctypes.c_void_p, ctypes.c_int]
        L.zlink_socket_monitor_open.restype = ctypes.c_void_p
        L.zlink_monitor_recv.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int]
        L.zlink_monitor_recv.restype = ctypes.c_int

        L.zlink_msgv_close.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        L.zlink_msgv_close.restype = None

        L.zlink_registry_new.argtypes = [ctypes.c_void_p]
        L.zlink_registry_new.restype = ctypes.c_void_p
        L.zlink_registry_set_endpoints.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]
        L.zlink_registry_set_endpoints.restype = ctypes.c_int
        L.zlink_registry_set_id.argtypes = [ctypes.c_void_p, ctypes.c_uint]
        L.zlink_registry_set_id.restype = ctypes.c_int
        L.zlink_registry_add_peer.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_registry_add_peer.restype = ctypes.c_int
        L.zlink_registry_set_heartbeat.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint]
        L.zlink_registry_set_heartbeat.restype = ctypes.c_int
        L.zlink_registry_set_broadcast_interval.argtypes = [ctypes.c_void_p, ctypes.c_uint]
        L.zlink_registry_set_broadcast_interval.restype = ctypes.c_int
        L.zlink_registry_start.argtypes = [ctypes.c_void_p]
        L.zlink_registry_start.restype = ctypes.c_int
        L.zlink_registry_destroy.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
        L.zlink_registry_destroy.restype = ctypes.c_int

        L.zlink_discovery_new.argtypes = [ctypes.c_void_p]
        L.zlink_discovery_new.restype = ctypes.c_void_p
        L.zlink_discovery_connect_registry.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_discovery_connect_registry.restype = ctypes.c_int
        L.zlink_discovery_subscribe.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_discovery_subscribe.restype = ctypes.c_int
        L.zlink_discovery_unsubscribe.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_discovery_unsubscribe.restype = ctypes.c_int
        L.zlink_discovery_get_providers.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t)]
        L.zlink_discovery_get_providers.restype = ctypes.c_int
        L.zlink_discovery_provider_count.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_discovery_provider_count.restype = ctypes.c_int
        L.zlink_discovery_service_available.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_discovery_service_available.restype = ctypes.c_int
        L.zlink_discovery_destroy.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
        L.zlink_discovery_destroy.restype = ctypes.c_int

        L.zlink_gateway_new.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                        ctypes.c_char_p]
        L.zlink_gateway_new.restype = ctypes.c_void_p
        L.zlink_gateway_send.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
        L.zlink_gateway_send.restype = ctypes.c_int
        L.zlink_gateway_recv.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_size_t), ctypes.c_int, ctypes.c_char_p]
        L.zlink_gateway_recv.restype = ctypes.c_int
        L.zlink_gateway_set_lb_strategy.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        L.zlink_gateway_set_lb_strategy.restype = ctypes.c_int
        L.zlink_gateway_set_tls_client.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
        L.zlink_gateway_set_tls_client.restype = ctypes.c_int
        L.zlink_gateway_connection_count.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_gateway_connection_count.restype = ctypes.c_int
        L.zlink_gateway_destroy.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
        L.zlink_gateway_destroy.restype = ctypes.c_int

        L.zlink_provider_new.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_provider_new.restype = ctypes.c_void_p
        L.zlink_gateway_setsockopt.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                               ctypes.c_void_p, ctypes.c_size_t]
        L.zlink_gateway_setsockopt.restype = ctypes.c_int
        L.zlink_provider_setsockopt.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                                ctypes.c_int, ctypes.c_void_p,
                                                ctypes.c_size_t]
        L.zlink_provider_setsockopt.restype = ctypes.c_int
        L.zlink_registry_setsockopt.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                                ctypes.c_int, ctypes.c_void_p,
                                                ctypes.c_size_t]
        L.zlink_registry_setsockopt.restype = ctypes.c_int
        L.zlink_discovery_setsockopt.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                                 ctypes.c_int, ctypes.c_void_p,
                                                 ctypes.c_size_t]
        L.zlink_discovery_setsockopt.restype = ctypes.c_int
        L.zlink_provider_bind.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_provider_bind.restype = ctypes.c_int
        L.zlink_provider_connect_registry.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_provider_connect_registry.restype = ctypes.c_int
        L.zlink_provider_register.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint]
        L.zlink_provider_register.restype = ctypes.c_int
        L.zlink_provider_update_weight.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint]
        L.zlink_provider_update_weight.restype = ctypes.c_int
        L.zlink_provider_unregister.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_provider_unregister.restype = ctypes.c_int
        L.zlink_provider_register_result.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_int), ctypes.c_char_p, ctypes.c_char_p]
        L.zlink_provider_register_result.restype = ctypes.c_int
        L.zlink_provider_set_tls_server.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]
        L.zlink_provider_set_tls_server.restype = ctypes.c_int
        L.zlink_provider_router.argtypes = [ctypes.c_void_p]
        L.zlink_provider_router.restype = ctypes.c_void_p
        L.zlink_provider_destroy.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
        L.zlink_provider_destroy.restype = ctypes.c_int

        L.zlink_spot_node_new.argtypes = [ctypes.c_void_p]
        L.zlink_spot_node_new.restype = ctypes.c_void_p
        L.zlink_spot_node_destroy.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
        L.zlink_spot_node_destroy.restype = ctypes.c_int
        L.zlink_spot_node_bind.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_spot_node_bind.restype = ctypes.c_int
        L.zlink_spot_node_connect_registry.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_spot_node_connect_registry.restype = ctypes.c_int
        L.zlink_spot_node_connect_peer_pub.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_spot_node_connect_peer_pub.restype = ctypes.c_int
        L.zlink_spot_node_disconnect_peer_pub.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_spot_node_disconnect_peer_pub.restype = ctypes.c_int
        L.zlink_spot_node_register.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]
        L.zlink_spot_node_register.restype = ctypes.c_int
        L.zlink_spot_node_unregister.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_spot_node_unregister.restype = ctypes.c_int
        L.zlink_spot_node_set_discovery.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_spot_node_set_discovery.restype = ctypes.c_int
        L.zlink_spot_node_set_tls_server.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]
        L.zlink_spot_node_set_tls_server.restype = ctypes.c_int
        L.zlink_spot_node_set_tls_client.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
        L.zlink_spot_node_set_tls_client.restype = ctypes.c_int
        L.zlink_spot_node_pub_socket.argtypes = [ctypes.c_void_p]
        L.zlink_spot_node_pub_socket.restype = ctypes.c_void_p
        L.zlink_spot_node_sub_socket.argtypes = [ctypes.c_void_p]
        L.zlink_spot_node_sub_socket.restype = ctypes.c_void_p

        L.zlink_spot_new.argtypes = [ctypes.c_void_p]
        L.zlink_spot_new.restype = ctypes.c_void_p
        L.zlink_spot_destroy.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
        L.zlink_spot_destroy.restype = ctypes.c_int
        L.zlink_spot_topic_create.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        L.zlink_spot_topic_create.restype = ctypes.c_int
        L.zlink_spot_topic_destroy.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_spot_topic_destroy.restype = ctypes.c_int
        L.zlink_spot_publish.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
        L.zlink_spot_publish.restype = ctypes.c_int
        L.zlink_spot_subscribe.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_spot_subscribe.restype = ctypes.c_int
        L.zlink_spot_subscribe_pattern.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_spot_subscribe_pattern.restype = ctypes.c_int
        L.zlink_spot_unsubscribe.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_spot_unsubscribe.restype = ctypes.c_int
        L.zlink_spot_recv.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_size_t), ctypes.c_int, ctypes.c_char_p, ctypes.POINTER(ctypes.c_size_t)]
        L.zlink_spot_recv.restype = ctypes.c_int
        L.zlink_spot_pub_socket.argtypes = [ctypes.c_void_p]
        L.zlink_spot_pub_socket.restype = ctypes.c_void_p
        L.zlink_spot_sub_socket.argtypes = [ctypes.c_void_p]
        L.zlink_spot_sub_socket.restype = ctypes.c_void_p

        L.zlink_errno.argtypes = []
        L.zlink_errno.restype = ctypes.c_int
        L.zlink_strerror.argtypes = [ctypes.c_int]
        L.zlink_strerror.restype = ctypes.c_char_p


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
        os_dir = "windows-x86_64" if "64" in os.environ.get("PROCESSOR_ARCHITECTURE", "") else "windows-x86"
        name = "zlink.dll"
    else:
        uname = os.uname().sysname.lower()
        if "darwin" in uname or "mac" in uname:
            os_dir = "darwin-aarch64" if os.uname().machine in ("arm64", "aarch64") else "darwin-x86_64"
            name = "libzlink.dylib"
        else:
            os_dir = "linux-aarch64" if os.uname().machine in ("arm64", "aarch64") else "linux-x86_64"
            name = "libzlink.so"
    candidate = base / "native" / os_dir / name
    if candidate.exists():
        return str(candidate)
    return None
