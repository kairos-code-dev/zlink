# SPDX-License-Identifier: MPL-2.0

from ..errors.errors import RecvError, SubmitError
from ..sockets.codes import RecvResult, SubmitResult
from . import message as _message_contract


class ReceivedMessage:
    def __new__(cls, *args, **kwargs):
        if cls is ReceivedMessage:
            return _message_contract._require(
                _message_contract._received_message_factory, "received message"
            )(*args, **kwargs)
        return super().__new__(cls)

    @classmethod
    def _from_owner(cls, owner, index, routing_id=None):
        if cls is ReceivedMessage:
            return _message_contract._require(
                _message_contract._received_message_from_owner_factory,
                "received message",
            )(owner, index, routing_id)
        return cls(routing_id=routing_id, owner=owner, index=index)

    def __len__(self): ...

    @property
    def data(self): ...

    def to_bytes(self): ...

    def close(self): ...

    def __enter__(self): ...

    def __exit__(self, exc_type, exc, tb): ...

    async def __aenter__(self): ...

    async def __aexit__(self, exc_type, exc, tb): ...


class _BaseReceived:
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


class ReceivedMultipart(_BaseReceived):
    def __new__(cls, *args, **kwargs):
        if cls is ReceivedMultipart:
            return _message_contract._require(
                _message_contract._received_multipart_factory, "received multipart"
            )(*args, **kwargs)
        return super().__new__(cls)

    def _adopt_from(self, source): ...

    def _replace(self, owner, routing_id=None, request_seq=None, **kwargs): ...


class Received(ReceivedMultipart):
    def __new__(cls, *args, **kwargs):
        if cls is Received:
            return _message_contract._require(
                _message_contract._received_factory, "received"
            )(*args, **kwargs)
        return super().__new__(cls)

    def send(self):
        if self._send_sender is None:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        return self._send_sender()

    def reply(self):
        if self.request_seq is None or self._reply_sender is None:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        return self._reply_sender()


__all__ = ["Received", "ReceivedMessage", "ReceivedMultipart"]
