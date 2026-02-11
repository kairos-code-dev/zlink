# SPDX-License-Identifier: MPL-2.0

import ctypes
from ._ffi import lib
from ._core import _raise_last_error, Message, ZlinkMsg
from ._discovery import _parts_to_bytes, _build_msg_array, _close_msg_array


class SpotNode:
    def __init__(self, ctx):
        self._handle = lib().zlink_spot_node_new(ctx._handle)
        if not self._handle:
            _raise_last_error()

    def bind(self, endpoint):
        rc = lib().zlink_spot_node_bind(self._handle, endpoint.encode())
        if rc != 0:
            _raise_last_error()

    def connect_registry(self, registry_endpoint):
        rc = lib().zlink_spot_node_connect_registry(self._handle, registry_endpoint.encode())
        if rc != 0:
            _raise_last_error()

    def connect_peer_pub(self, endpoint):
        rc = lib().zlink_spot_node_connect_peer_pub(self._handle, endpoint.encode())
        if rc != 0:
            _raise_last_error()

    def disconnect_peer_pub(self, endpoint):
        rc = lib().zlink_spot_node_disconnect_peer_pub(self._handle, endpoint.encode())
        if rc != 0:
            _raise_last_error()

    def register(self, service_name, advertise_endpoint):
        rc = lib().zlink_spot_node_register(self._handle, service_name.encode(), advertise_endpoint.encode())
        if rc != 0:
            _raise_last_error()

    def unregister(self, service_name):
        rc = lib().zlink_spot_node_unregister(self._handle, service_name.encode())
        if rc != 0:
            _raise_last_error()

    def set_discovery(self, discovery, service_name):
        rc = lib().zlink_spot_node_set_discovery(self._handle, discovery._handle, service_name.encode())
        if rc != 0:
            _raise_last_error()

    def set_tls_server(self, cert, key):
        rc = lib().zlink_spot_node_set_tls_server(self._handle, cert.encode(), key.encode())
        if rc != 0:
            _raise_last_error()

    def set_tls_client(self, ca_cert, hostname, trust_system=0):
        rc = lib().zlink_spot_node_set_tls_client(self._handle, ca_cert.encode(), hostname.encode(), trust_system)
        if rc != 0:
            _raise_last_error()

    def close(self):
        if self._handle:
            handle = ctypes.c_void_p(self._handle)
            lib().zlink_spot_node_destroy(ctypes.byref(handle))
            self._handle = None


class Spot:
    def __init__(self, node):
        self._node_handle = node._handle
        self._pub_handle = lib().zlink_spot_pub_new(node._handle)
        self._sub_handle = lib().zlink_spot_sub_new(node._handle)
        if not self._pub_handle or not self._sub_handle:
            if self._pub_handle:
                h = ctypes.c_void_p(self._pub_handle)
                lib().zlink_spot_pub_destroy(ctypes.byref(h))
            if self._sub_handle:
                h = ctypes.c_void_p(self._sub_handle)
                lib().zlink_spot_sub_destroy(ctypes.byref(h))
            self._pub_handle = None
            self._sub_handle = None
            _raise_last_error()

    def publish(self, topic_id, parts, flags=0):
        arr, built = _build_msg_array(parts)
        rc = lib().zlink_spot_pub_publish(self._pub_handle, topic_id.encode(), ctypes.byref(arr), len(parts), flags)
        if rc != 0:
            _close_msg_array(arr, built)
            _raise_last_error()

    def subscribe(self, topic_id):
        rc = lib().zlink_spot_sub_subscribe(self._sub_handle, topic_id.encode())
        if rc != 0:
            _raise_last_error()

    def subscribe_pattern(self, pattern):
        rc = lib().zlink_spot_sub_subscribe_pattern(self._sub_handle, pattern.encode())
        if rc != 0:
            _raise_last_error()

    def unsubscribe(self, topic_id_or_pattern):
        rc = lib().zlink_spot_sub_unsubscribe(self._sub_handle, topic_id_or_pattern.encode())
        if rc != 0:
            _raise_last_error()

    def recv(self, flags=0):
        parts = ctypes.c_void_p()
        count = ctypes.c_size_t()
        topic_buf = ctypes.create_string_buffer(256)
        topic_len = ctypes.c_size_t(256)
        rc = lib().zlink_spot_sub_recv(self._sub_handle, ctypes.byref(parts), ctypes.byref(count), flags, topic_buf, ctypes.byref(topic_len))
        if rc != 0:
            _raise_last_error()
        topic = topic_buf.value.decode()
        messages = _parts_to_bytes(parts, count.value)
        return topic, messages

    def close(self):
        if self._pub_handle:
            handle = ctypes.c_void_p(self._pub_handle)
            lib().zlink_spot_pub_destroy(ctypes.byref(handle))
            self._pub_handle = None
        if self._sub_handle:
            handle = ctypes.c_void_p(self._sub_handle)
            lib().zlink_spot_sub_destroy(ctypes.byref(handle))
            self._sub_handle = None
