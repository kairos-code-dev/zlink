# SPDX-License-Identifier: MPL-2.0

from . import message as _message_contract
from .received import _BaseReceived


class TopicMessage(_BaseReceived):
    def __new__(cls, *args, **kwargs):
        if cls is TopicMessage:
            return _message_contract._require(
                _message_contract._topic_message_factory, "topic message"
            )(*args, **kwargs)
        return super().__new__(cls)

    @property
    def topic(self): ...

    @topic.setter
    def topic(self, value): ...

    def _adopt_from(self, source): ...

    def _replace(self, owner, **kwargs): ...


__all__ = ["TopicMessage"]
