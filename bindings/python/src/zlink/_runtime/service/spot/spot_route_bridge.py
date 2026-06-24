# SPDX-License-Identifier: MPL-2.0

import ctypes
from dataclasses import dataclass
from enum import IntFlag

from ...._native.ffi import (
    ZlinkSpotRouteBridgeEndpointOptions,
    ZlinkSpotRouteBridgeOptions,
    lib,
)
from ....contracts.errors.codes import CloseResult, ConfigResult
from ....contracts.errors.errors import CloseError, ConfigError, SubmitError
from ....contracts.sockets.codes import RequestResult, SubmitResult
from ...handles.native_support import (
    _REPLY_HANDLER,
    _copy_routing_id,
    _raise_config_error_from_errno,
    _raise_result_error,
    _report_unhandled_callback_exception,
    _request_result_from_code,
    _request_result_native_errno,
    _validated_c_string_value,
)
from ...messaging.request_reply import (
    _PendingRequest,
    _RequestProgressPump,
    _clone_payload,
    _message_list_from_parts,
    _prepare_native_parts,
    _timeout_to_ms,
)
from .spot_ops import PublishOp, RequestOp, SendOp


class SpotRouteBridgeEndpointCapabilities(IntFlag):
    NONE = 0
    SPOT_ROUTE = 0x00000001
    ROUTE_ONLY = SPOT_ROUTE


@dataclass(frozen=True)
class SpotRouteBridgeOptions:
    default_request_timeout: float | int = 0
    error_reply_policy: int = 0
    receive_mode: int = 0


@dataclass(frozen=True)
class SpotRouteBridgeEndpointOptions:
    capabilities: SpotRouteBridgeEndpointCapabilities = (
        SpotRouteBridgeEndpointCapabilities.ROUTE_ONLY
    )
    inbound_relay_policy: int = 0


class SpotRouteBridge:
    def __init__(self, node, options: SpotRouteBridgeOptions | None = None):
        native_options = None
        options_ptr = None
        if options is not None:
            native_options = ZlinkSpotRouteBridgeOptions()
            native_options.struct_size = ctypes.sizeof(ZlinkSpotRouteBridgeOptions)
            native_options.default_request_timeout_ms = _timeout_to_ms(
                options.default_request_timeout
            )
            native_options.error_reply_policy = int(options.error_reply_policy)
            native_options.receive_mode = int(options.receive_mode)
            options_ptr = ctypes.byref(native_options)
        self._handle = lib().zlink_spot_route_bridge_new(
            node._ctx._handle, node._handle, options_ptr
        )
        if not self._handle:
            _raise_config_error_from_errno()
        self._endpoint_handles = {}
        self._pending_requests = {}
        self._reply_handler = _REPLY_HANDLER(self._on_reply)
        self._request_progress = _RequestProgressPump(
            self._request_progress_handle,
            lambda: bool(self._pending_requests),
        )

    def attach_router_channel(self, channel_name, router, options=None):
        self._attach_channel(
            channel_name,
            router,
            options,
            lib().zlink_spot_route_bridge_attach_router_channel,
        )

    def send(self, channel_name, target_node_rid, target_spot_rid):
        return SendOp(
            self,
            lambda parts, flags=0: self._send_submit(
                channel_name, target_node_rid, target_spot_rid, parts, flags
            ),
        )

    def request(self, channel_name, target_node_rid, target_spot_rid):
        return RequestOp(
            self,
            lambda parts, callback, *, flags=0, timeout=0: self._request_submit(
                channel_name, target_node_rid, target_spot_rid, parts, callback, flags=flags, timeout=timeout
            ),
        )

    def drain(self):
        rc = lib().zlink_spot_route_bridge_drain(self._handle)
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def close(self):
        if not self._handle:
            return
        rc = lib().zlink_spot_route_bridge_close(self._handle)
        self._handle = None
        self._pending_requests.clear()
        self._endpoint_handles.clear()
        if rc != 0:
            _raise_result_error(CloseError, CloseResult, rc, lib().zlink_errno())

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc, tb):
        self.close()

    def _attach_channel(self, channel_name, socket, options, native_func):
        native_options = None
        options_ptr = None
        if options is not None:
            native_options = ZlinkSpotRouteBridgeEndpointOptions()
            native_options.struct_size = ctypes.sizeof(
                ZlinkSpotRouteBridgeEndpointOptions
            )
            native_options.capabilities = int(options.capabilities)
            native_options.inbound_relay_policy = int(options.inbound_relay_policy)
            options_ptr = ctypes.byref(native_options)
        channel_bytes = _validated_c_string_value(
            channel_name, field="channel_name", max_length=255
        )
        rc = native_func(self._handle, channel_bytes, socket._handle, options_ptr)
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        self._endpoint_handles[channel_bytes] = socket._handle

    def _send_submit(self, channel_name, target_node_rid, target_spot_rid, payload, flags=0):
        native_parts = _clone_payload(payload)
        parts_array = _prepare_native_parts(native_parts)
        native_target_node_rid = _copy_routing_id(target_node_rid)
        native_target_spot_rid = _copy_routing_id(target_spot_rid)
        try:
            rc = lib().zlink_spot_route_bridge_send(
                self._handle,
                _validated_c_string_value(
                    channel_name, field="channel_name", max_length=255
                ),
                ctypes.byref(native_target_node_rid),
                ctypes.byref(native_target_spot_rid),
                parts_array,
                len(parts_array),
                int(flags),
            )
            if rc != 0:
                err = lib().zlink_errno()
                if int(flags) & 1 and err == 11:
                    return False
                _raise_result_error(SubmitError, SubmitResult, rc, err)
            return True
        finally:
            # The native bridge moves each part on success. Closing moved-from
            # messages is valid and keeps failure paths balanced.
            for part in native_parts:
                lib().zlink_msg_close(ctypes.byref(part))

    def _request_submit(self, channel_name, target_node_rid, target_spot_rid, payload, callback, *, flags=0, timeout=0):
        pending = _PendingRequest(callback=callback)
        handle = id(pending)
        self._pending_requests[handle] = pending
        channel_bytes = _validated_c_string_value(
            channel_name, field="channel_name", max_length=255
        )
        native_parts = _clone_payload(payload)
        parts_array = _prepare_native_parts(native_parts)
        native_target_node_rid = _copy_routing_id(target_node_rid)
        native_target_spot_rid = _copy_routing_id(target_spot_rid)
        try:
            rc = lib().zlink_spot_route_bridge_request(
                self._handle,
                channel_bytes,
                ctypes.byref(native_target_node_rid),
                ctypes.byref(native_target_spot_rid),
                parts_array,
                len(parts_array),
                self._reply_handler,
                ctypes.c_void_p(handle),
                int(flags),
                _timeout_to_ms(timeout),
            )
            if rc != 0:
                err = lib().zlink_errno()
                self._pending_requests.pop(handle, None)
                if int(flags) & 1 and err == 11:
                    return False
                _raise_result_error(SubmitError, SubmitResult, rc, err)
            self._request_progress.ensure_running()
            return True
        except Exception:
            self._pending_requests.pop(handle, None)
            raise
        finally:
            for part in native_parts:
                lib().zlink_msg_close(ctypes.byref(part))

    def _request_progress_handle(self):
        for handle in self._endpoint_handles.values():
            if handle:
                return handle
        return None

    def _on_reply(self, result_code, parts, part_count, userdata):
        handle = ctypes.cast(userdata, ctypes.c_void_p).value
        pending = self._pending_requests.pop(handle, None)
        if pending is None:
            return
        result = _request_result_from_code(int(result_code))
        reply = []
        if result == RequestResult.OK:
            reply = _message_list_from_parts(parts, part_count)
        try:
            pending.resolve(result, reply, _request_result_native_errno(result))
        except Exception:
            _report_unhandled_callback_exception(pending.callback)


class SpotNodePublisher:
    def __init__(self, node):
        self._handle = lib().zlink_spot_node_publisher_new(node._handle)
        if not self._handle:
            _raise_config_error_from_errno()

    def publish(self, topic):
        return PublishOp(self, topic)

    def _publish_topic_bytes(self, topic):
        return _validated_c_string_value(topic, field="topic")

    def _publish_submit_prevalidated(self, topic_bytes, payload, flags=0):
        native_parts = _clone_payload(payload)
        parts_array = _prepare_native_parts(native_parts)
        try:
            rc = lib().zlink_spot_node_publisher_publish(
                self._handle,
                topic_bytes,
                parts_array,
                len(parts_array),
                int(flags),
            )
            if rc != 0:
                err = lib().zlink_errno()
                if int(flags) & 1 and err == 11:
                    return False
                _raise_result_error(SubmitError, SubmitResult, rc, err)
            return True
        finally:
            for part in native_parts:
                lib().zlink_msg_close(ctypes.byref(part))

    def close(self):
        if not self._handle:
            return
        rc = lib().zlink_spot_node_publisher_close(self._handle)
        self._handle = None
        if rc != 0:
            _raise_result_error(CloseError, CloseResult, rc, lib().zlink_errno())

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc, tb):
        self.close()
