from __future__ import annotations

from contextlib import contextmanager
from dataclasses import dataclass
from typing import Iterator

from zlink_codec_json import decode, encode


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


def test_json_codec_roundtrip_dict() -> None:
    payload = {"name": "alice", "count": 3}

    with managed_message(encode(payload)) as msg:
        decoded = decode(msg, dict)

    assert decoded == payload


def test_json_codec_roundtrip_dataclass() -> None:
    payload = SampleData(name="alice", count=3)

    with managed_message(encode(payload)) as msg:
        decoded = decode(msg, SampleData)

    assert decoded == payload
