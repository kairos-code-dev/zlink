# SPDX-License-Identifier: MPL-2.0

import ctypes

from ...._native.ffi import lib
from ...handles.native_support import SubmitError, SubmitResult, _raise_result_error
from .native_parts import submit_parts


def submit_channel_send(handle, channel_bytes, native_parts, flags):
    rc, err = submit_parts(
        native_parts,
        lambda part_ptr, part_flag: lib().zlink_spot_send_channel_part(
            handle,
            channel_bytes,
            part_ptr,
            int(flags),
            part_flag,
        ),
    )
    if rc != 0:
        _raise_result_error(SubmitError, SubmitResult, rc, err)
    return True


def submit_spot_send(handle, node_rid, spot_rid, native_parts, flags):
    rc, err = submit_parts(
        native_parts,
        lambda part_ptr, part_flag: lib().zlink_spot_send_spot_part(
            handle,
            ctypes.byref(node_rid),
            ctypes.byref(spot_rid),
            part_ptr,
            int(flags),
            part_flag,
        ),
    )
    if rc != 0:
        _raise_result_error(SubmitError, SubmitResult, rc, err)
    return True


def submit_channel_request(
    handle,
    channel_bytes,
    native_parts,
    reply_handler,
    request_handle,
    flags,
    timeout_ms,
):
    rc, err = submit_parts(
        native_parts,
        lambda part_ptr, part_flag: lib().zlink_spot_request_channel_part(
            handle,
            channel_bytes,
            part_ptr,
            reply_handler,
            ctypes.c_void_p(request_handle),
            int(flags),
            part_flag,
            timeout_ms,
        ),
    )
    if rc != 0:
        _raise_result_error(SubmitError, SubmitResult, rc, err)


def submit_spot_request(
    handle,
    node_rid,
    spot_rid,
    native_parts,
    reply_handler,
    request_handle,
    flags,
    timeout_ms,
):
    rc, err = submit_parts(
        native_parts,
        lambda part_ptr, part_flag: lib().zlink_spot_request_spot_part(
            handle,
            ctypes.byref(node_rid),
            ctypes.byref(spot_rid),
            part_ptr,
            reply_handler,
            ctypes.c_void_p(request_handle),
            int(flags),
            part_flag,
            timeout_ms,
        ),
    )
    if rc != 0:
        _raise_result_error(SubmitError, SubmitResult, rc, err)
