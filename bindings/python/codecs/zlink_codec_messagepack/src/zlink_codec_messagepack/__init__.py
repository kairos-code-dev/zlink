# SPDX-License-Identifier: MPL-2.0

from __future__ import annotations

from dataclasses import asdict, is_dataclass
from typing import Any, TypeVar, cast

import msgpack

from zlink import Message

T = TypeVar("T")

__all__ = ["decode", "encode"]


def decode(msg: Message, cls: type[T]) -> T:
    value = msgpack.unpackb(msg.to_bytes(), raw=False)
    return _coerce_decoded(value, cls)


def encode(v: T) -> Message:
    data = msgpack.packb(_normalize_encoded(v), use_bin_type=True)
    return _new_message(data)


def _coerce_decoded(value: Any, cls: type[T]) -> T:
    if cls in (dict, list, str, int, float, bool):
        return cast(T, value)
    if cls is object:
        return cast(T, value)
    if is_dataclass(cls) and isinstance(value, dict):
        return cls(**value)
    model_validate = getattr(cls, "model_validate", None)
    if callable(model_validate):
        return cast(T, model_validate(value))
    from_dict = getattr(cls, "from_dict", None)
    if callable(from_dict):
        return cast(T, from_dict(value))
    if isinstance(value, dict):
        return cls(**value)
    return cls(value)


def _normalize_encoded(value: Any) -> Any:
    if is_dataclass(value):
        return asdict(value)
    model_dump = getattr(value, "model_dump", None)
    if callable(model_dump):
        return model_dump(mode="python")
    to_dict = getattr(value, "to_dict", None)
    if callable(to_dict):
        return to_dict()
    return value


def _new_message(data: bytes) -> Message:
    try:
        return Message(data)  # type: ignore[arg-type]
    except TypeError:
        return Message.from_bytes(data)
