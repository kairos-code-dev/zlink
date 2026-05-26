# SPDX-License-Identifier: MPL-2.0

from __future__ import annotations

from typing import TypeVar

from google.protobuf.message import Message as ProtobufMessage

from zlink import Message

T = TypeVar("T", bound=ProtobufMessage)

__all__ = ["decode", "encode"]


def decode(msg: Message, cls: type[T]) -> T:
    value = cls()
    value.ParseFromString(msg.to_bytes())
    return value


def encode(v: T) -> Message:
    return _new_message(v.SerializeToString())


def _new_message(data: bytes) -> Message:
    return Message.from_(data)
