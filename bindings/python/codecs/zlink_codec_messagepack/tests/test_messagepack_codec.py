from __future__ import annotations

from contextlib import contextmanager
from dataclasses import dataclass
from typing import Iterator

import pytest
from zlink import Message as ZlinkMessage

pytest.importorskip("msgpack")

import zlink_codec_messagepack as messagepack_codec
from zlink_codec_messagepack import decode, encode


@contextmanager
def managed_message(msg: object) -> Iterator[object]:
    if hasattr(msg, "__enter__") and hasattr(msg, "__exit__"):
        with msg:
            yield msg
        return

    try:
        yield msg
    finally:
        close = getattr(msg, "close", None)
        if callable(close):
            close()


@dataclass
class SampleData:
    name: str
    count: int


@pytest.fixture(autouse=True)
def message_constructor_compat(monkeypatch: pytest.MonkeyPatch) -> None:
    try:
        msg = ZlinkMessage(b"probe")
    except Exception:
        class MessageCompat:
            from_ = staticmethod(ZlinkMessage.from_)

            def __new__(cls, data: bytes) -> ZlinkMessage:
                raise TypeError("Message(bytes) is not supported in this build")

        monkeypatch.setattr(messagepack_codec, "Message", MessageCompat)
    else:
        msg.close()


def test_messagepack_codec_roundtrip_dict() -> None:
    payload = {"name": "alice", "count": 3}

    with managed_message(encode(payload)) as msg:
        decoded = decode(msg, dict)

    assert decoded == payload


def test_messagepack_codec_roundtrip_dataclass() -> None:
    payload = SampleData(name="alice", count=3)

    with managed_message(encode(payload)) as msg:
        decoded = decode(msg, SampleData)

    assert decoded == payload
