# SPDX-License-Identifier: MPL-2.0

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
        if os.name == "nt":
            _prepare_windows_runtime(path)
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

        L.zlink_discovery_new_typed.argtypes = [ctypes.c_void_p, ctypes.c_uint16]
        L.zlink_discovery_new_typed.restype = ctypes.c_void_p
        L.zlink_discovery_connect_registry.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_discovery_connect_registry.restype = ctypes.c_int
        L.zlink_discovery_subscribe.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_discovery_subscribe.restype = ctypes.c_int
        L.zlink_discovery_unsubscribe.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_discovery_unsubscribe.restype = ctypes.c_int
        L.zlink_discovery_get_receivers.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t)]
        L.zlink_discovery_get_receivers.restype = ctypes.c_int
        L.zlink_discovery_receiver_count.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_discovery_receiver_count.restype = ctypes.c_int
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

        L.zlink_receiver_new.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_receiver_new.restype = ctypes.c_void_p
        L.zlink_gateway_setsockopt.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                               ctypes.c_void_p, ctypes.c_size_t]
        L.zlink_gateway_setsockopt.restype = ctypes.c_int
        L.zlink_receiver_setsockopt.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                                ctypes.c_int, ctypes.c_void_p,
                                                ctypes.c_size_t]
        L.zlink_receiver_setsockopt.restype = ctypes.c_int
        L.zlink_registry_setsockopt.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                                ctypes.c_int, ctypes.c_void_p,
                                                ctypes.c_size_t]
        L.zlink_registry_setsockopt.restype = ctypes.c_int
        L.zlink_discovery_setsockopt.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                                 ctypes.c_int, ctypes.c_void_p,
                                                 ctypes.c_size_t]
        L.zlink_discovery_setsockopt.restype = ctypes.c_int
        L.zlink_receiver_bind.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_receiver_bind.restype = ctypes.c_int
        L.zlink_receiver_connect_registry.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_receiver_connect_registry.restype = ctypes.c_int
        L.zlink_receiver_register.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint]
        L.zlink_receiver_register.restype = ctypes.c_int
        L.zlink_receiver_update_weight.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint]
        L.zlink_receiver_update_weight.restype = ctypes.c_int
        L.zlink_receiver_unregister.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_receiver_unregister.restype = ctypes.c_int
        L.zlink_receiver_register_result.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_int), ctypes.c_char_p, ctypes.c_char_p]
        L.zlink_receiver_register_result.restype = ctypes.c_int
        L.zlink_receiver_set_tls_server.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]
        L.zlink_receiver_set_tls_server.restype = ctypes.c_int
        L.zlink_receiver_router.argtypes = [ctypes.c_void_p]
        L.zlink_receiver_router.restype = ctypes.c_void_p
        L.zlink_receiver_destroy.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
        L.zlink_receiver_destroy.restype = ctypes.c_int

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

        L.zlink_spot_pub_new.argtypes = [ctypes.c_void_p]
        L.zlink_spot_pub_new.restype = ctypes.c_void_p
        L.zlink_spot_pub_destroy.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
        L.zlink_spot_pub_destroy.restype = ctypes.c_int
        L.zlink_spot_pub_publish.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
        L.zlink_spot_pub_publish.restype = ctypes.c_int

        L.zlink_spot_sub_new.argtypes = [ctypes.c_void_p]
        L.zlink_spot_sub_new.restype = ctypes.c_void_p
        L.zlink_spot_sub_destroy.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
        L.zlink_spot_sub_destroy.restype = ctypes.c_int
        L.zlink_spot_sub_subscribe.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_spot_sub_subscribe.restype = ctypes.c_int
        L.zlink_spot_sub_subscribe_pattern.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_spot_sub_subscribe_pattern.restype = ctypes.c_int
        L.zlink_spot_sub_unsubscribe.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.zlink_spot_sub_unsubscribe.restype = ctypes.c_int
        L.zlink_spot_sub_recv.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_size_t), ctypes.c_int, ctypes.c_char_p, ctypes.POINTER(ctypes.c_size_t)]
        L.zlink_spot_sub_recv.restype = ctypes.c_int
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


def _prepare_windows_runtime(lib_path):
    dep_names = ["libcrypto-3-x64.dll", "libssl-3-x64.dll"]
    search_dirs = []

    lib_dir = pathlib.Path(lib_path).resolve().parent
    search_dirs.append(lib_dir)

    for env_key in ("ZLINK_OPENSSL_BIN", "OPENSSL_BIN"):
        v = os.environ.get(env_key)
        if v:
            search_dirs.append(pathlib.Path(v))

    for entry in os.environ.get("PATH", "").split(";"):
        if entry:
            search_dirs.append(pathlib.Path(entry))

    search_dirs.extend([
        pathlib.Path(r"C:\Program Files\OpenSSL-Win64\bin"),
        pathlib.Path(r"C:\Program Files\Git\mingw64\bin"),
        pathlib.Path(
            r"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\mingw64\bin"
        ),
    ])

    seen = set()
    for d in search_dirs:
        s = str(d)
        if s in seen:
            continue
        seen.add(s)
        if not d.exists():
            continue
        try:
            os.add_dll_directory(s)
        except (AttributeError, OSError):
            pass
        for dep in dep_names:
            dep_path = d / dep
            if dep_path.exists():
                try:
                    ctypes.CDLL(str(dep_path))
                except OSError:
                    pass
