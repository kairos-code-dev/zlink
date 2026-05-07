[Python Binding Specification](README.md) · [Bindings Policy](../README.md)

# Python Codec Extension Specification

This document defines the public contract for Python codec extension packages.
The core `zlink` package does not expose these entrypoints, so applications
opt in to codec dependencies explicitly.

## Packages And Modules

- PyPI `zlink-codec-protobuf` -> import `zlink_codec_protobuf`
- PyPI `zlink-codec-json` -> import `zlink_codec_json`
- PyPI `zlink-codec-messagepack` -> import `zlink_codec_messagepack`

These are separate public modules layered on top of the core `zlink` package.
They must not be merged into `zlink.__init__` as unconditional dependencies,
and they do not extend a shared `zlink.codec.*` namespace.

JSON codec baseline: stdlib `json`.
MessagePack codec baseline: `msgpack`.

## Protobuf

```python
# zlink_codec_protobuf
from google.protobuf.message import Message as ProtobufMessage
from typing import TypeVar

TProto = TypeVar("TProto", bound=ProtobufMessage)

def decode(message: Message, cls: type[TProto]) -> TProto: ...
def encode(value: TProto) -> Message: ...
```

## JSON

```python
# zlink_codec_json
from typing import Any, TypeVar

TJson = TypeVar("TJson")

def decode(message: Message, cls: type[TJson]) -> TJson: ...
def encode(value: Any) -> Message: ...
```

## MessagePack

```python
# zlink_codec_messagepack
from typing import Any, TypeVar

TMessagePack = TypeVar("TMessagePack")

def decode(message: Message, cls: type[TMessagePack]) -> TMessagePack: ...
def encode(value: Any) -> Message: ...
```
