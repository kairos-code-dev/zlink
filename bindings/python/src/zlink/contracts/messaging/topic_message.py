# SPDX-License-Identifier: MPL-2.0

from typing import Protocol, runtime_checkable

from . import message as _message_contract
from .received import _BaseReceived


def create_topic_message(*args, **kwargs):
    return _message_contract._require(
        _message_contract._topic_message_factory, "topic message"
    )(*args, **kwargs)


@runtime_checkable
class TopicMessage(_BaseReceived, Protocol):
    @property
    def topic(self): ...

    @topic.setter
    def topic(self, value): ...

    def _adopt_from(self, source): ...

    def _replace(self, owner, **kwargs): ...


__all__ = ["TopicMessage"]
