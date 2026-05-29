# SPDX-License-Identifier: MPL-2.0

from typing import Protocol, runtime_checkable


@runtime_checkable
class _FluentMessageOp(Protocol):
    def message(self, payload): ...
    def messages(self, *payloads): ...
    def flags(self, flags): ...


@runtime_checkable
class SendOp(_FluentMessageOp, Protocol):
    def submit(self): ...


@runtime_checkable
class RequestOp(_FluentMessageOp, Protocol):
    def timeout(self, timeout): ...
    def submit_async(self): ...
    def submit(self, callback): ...


@runtime_checkable
class RequestCallbackOp(_FluentMessageOp, Protocol):
    def timeout(self, timeout): ...
    def submit(self, callback): ...


@runtime_checkable
class ReplyOp(_FluentMessageOp, Protocol):
    def submit(self): ...


@runtime_checkable
class ActorJoinOp(_FluentMessageOp, Protocol):
    def timeout(self, timeout): ...
    def submit_async(self): ...
    def submit(self, callback): ...


@runtime_checkable
class ActorJoinCallbackOp(_FluentMessageOp, Protocol):
    def timeout(self, timeout): ...
    def submit(self, callback): ...


@runtime_checkable
class ActorJoinEntrySpotOp(Protocol):
    def timeout(self, timeout): ...
    def submit_async(self): ...
    def submit(self, callback): ...


@runtime_checkable
class ActorJoinReplyOp(_FluentMessageOp, Protocol):
    def submit(self): ...


@runtime_checkable
class ActorLeaveOp(Protocol):
    def timeout(self, timeout): ...
    def submit_async(self): ...
    def submit(self, callback): ...


@runtime_checkable
class ActorDestroyOp(ActorLeaveOp, Protocol): ...


@runtime_checkable
class ActorLookupOp(Protocol):
    def timeout(self, timeout): ...
    def submit_async(self): ...
    def submit(self, callback): ...


@runtime_checkable
class ActorBindOp(ActorLeaveOp, Protocol): ...


@runtime_checkable
class ActorUnbindOp(ActorLeaveOp, Protocol): ...


__all__ = [
    "ActorBindOp",
    "ActorDestroyOp",
    "ActorJoinCallbackOp",
    "ActorJoinEntrySpotOp",
    "ActorJoinOp",
    "ActorJoinReplyOp",
    "ActorLeaveOp",
    "ActorLookupOp",
    "ActorUnbindOp",
    "ReplyOp",
    "RequestCallbackOp",
    "RequestOp",
    "SendOp",
]
