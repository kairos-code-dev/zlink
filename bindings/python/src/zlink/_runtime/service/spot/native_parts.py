# SPDX-License-Identifier: MPL-2.0

import ctypes

from ...._native.ffi import ZLINK_PART_FINAL, ZLINK_PART_MORE, ZlinkMsg, lib
from ...messaging.message_materializer import Message
from ...handles.native_support import (
    _clone_native_msg,
    _init_msg_from_buffer,
)


def clone_payload(payload_parts):
    native_parts = []
    for part in payload_parts:
        if isinstance(part, Message):
            native_parts.append(_clone_native_msg(part._msg))
            continue
        native = ZlinkMsg()
        _init_msg_from_buffer(native, part, borrow=False)
        native_parts.append(native)
    return native_parts


def prepare_native_parts(native_parts):
    parts_array = (ZlinkMsg * len(native_parts))()
    for index, native in enumerate(native_parts):
        parts_array[index] = native
    return parts_array


def close_native_parts(native_parts, start=0):
    for native in native_parts[start:]:
        lib().zlink_msg_close(ctypes.byref(native))


def close_native_parts_array(parts_array, part_count):
    for index in range(part_count):
        lib().zlink_msg_close(ctypes.byref(parts_array[index]))


def submit_parts(native_parts, submit_part):
    part_count = len(native_parts)
    for index, native in enumerate(native_parts):
        part_flag = ZLINK_PART_FINAL if index == part_count - 1 else ZLINK_PART_MORE
        rc = submit_part(ctypes.byref(native), part_flag)
        if rc != 0:
            err = lib().zlink_errno()
            close_native_parts(native_parts, index)
            return rc, err
    return 0, 0
