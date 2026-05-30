# SPDX-License-Identifier: MPL-2.0

from typing import Protocol, runtime_checkable

from . import message as _message_contract
from .received import _BaseReceived


def create_topic_message(*args, **kwargs):
    """Create an empty :class:`TopicMessage` envelope for reuse across
    receives."""
    return _message_contract._require(
        _message_contract._topic_message_factory, "topic message"
    )(*args, **kwargs)


@runtime_checkable
class TopicMessage(_BaseReceived, Protocol):
    """A received publish: its topic and message parts. Owns its parts until
    closed."""

    @property
    def topic(self):
        """The topic the message was published under."""
        ...

    @topic.setter
    def topic(self, value): ...

    def _adopt_from(self, source): ...

    def _replace(self, owner, **kwargs): ...


__all__ = ["TopicMessage"]
