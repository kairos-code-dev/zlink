from google.protobuf.wrappers_pb2 import StringValue, Int32Value

import pytest

from zlink_codec_protobuf import decode, encode


def test_string_roundtrip():
    original = StringValue(value="hello-zlink")
    msg = encode(original)
    try:
        result = decode(msg, StringValue)
        assert result.value == original.value
    finally:
        msg.close()


def test_int_roundtrip():
    original = Int32Value(value=42)
    msg = encode(original)
    try:
        result = decode(msg, Int32Value)
        assert result.value == original.value
    finally:
        msg.close()
