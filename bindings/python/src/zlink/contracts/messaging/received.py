# SPDX-License-Identifier: MPL-2.0

from typing import Protocol, runtime_checkable

from ..errors.errors import RecvError, SubmitError
from ..sockets.codes import RecvResult, SubmitResult
from . import message as _message_contract


def create_received_message(*args, **kwargs):
    return _message_contract._require(
        _message_contract._received_message_factory, "received message"
    )(*args, **kwargs)


def received_message_from_owner(owner, index, routing_id=None):
    return _message_contract._require(
        _message_contract._received_message_from_owner_factory,
        "received message",
    )(owner, index, routing_id)


@runtime_checkable
class ReceivedMessage(Protocol):
    def __len__(self): ...

    @property
    def data(self): ...

    def to_bytes(self): ...

    def close(self): ...

    def __enter__(self): ...

    def __exit__(self, exc_type, exc, tb): ...

    async def __aenter__(self): ...

    async def __aexit__(self, exc_type, exc, tb): ...


class _BaseReceived(Protocol):
    _owner = None
    parts = ()

    def __iter__(self):
        return iter(self.parts)

    def __len__(self):
        return len(self.parts)

    def to_bytes_list(self):
        return [message.to_bytes() for message in self.parts]

    def is_single_part(self):
        return len(self.parts) == 1

    def first_part(self):
        if not self.parts:
            raise RecvError(RecvResult.NO_DATA, 0)
        return self.parts[0]

    def single_part_or_throw(self):
        if len(self.parts) != 1:
            raise RecvError(RecvResult.NO_DATA, 0)
        return self.parts[0]

    def close(self):
        if self._owner is not None:
            self._owner.close()
            self._owner = None
        self.parts = ()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc, tb):
        self.close()


def create_received_multipart(*args, **kwargs):
    return _message_contract._require(
        _message_contract._received_multipart_factory, "received multipart"
    )(*args, **kwargs)


@runtime_checkable
class ReceivedMultipart(_BaseReceived, Protocol):
    def _adopt_from(self, source): ...

    def _replace(self, owner, routing_id=None, request_seq=None, **kwargs): ...


def create_received(*args, **kwargs):
    return _message_contract._require(
        _message_contract._received_factory, "received"
    )(*args, **kwargs)


@runtime_checkable
class Received(ReceivedMultipart, Protocol):
    def send(self):
        if self._send_sender is None:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        return self._send_sender()

    def reply(self):
        if self.request_seq is None or self._reply_sender is None:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        return self._reply_sender()


__all__ = ["Received", "ReceivedMessage", "ReceivedMultipart"]
