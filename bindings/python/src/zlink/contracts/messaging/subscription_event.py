# SPDX-License-Identifier: MPL-2.0

from typing import Protocol, runtime_checkable

from . import message as _message_contract


def create_subscription_event(*args, **kwargs):
    return _message_contract._require(
        _message_contract._subscription_event_factory, "subscription event"
    )(*args, **kwargs)


@runtime_checkable
class SubscriptionEvent(Protocol):
    def _adopt_from(self, source): ...


__all__ = ["SubscriptionEvent"]
