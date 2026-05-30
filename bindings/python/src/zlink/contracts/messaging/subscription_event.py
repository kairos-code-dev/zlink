# SPDX-License-Identifier: MPL-2.0

from typing import Protocol, runtime_checkable

from . import message as _message_contract


def create_subscription_event(*args, **kwargs):
    """Create an empty :class:`SubscriptionEvent` for reuse across receives."""
    return _message_contract._require(
        _message_contract._subscription_event_factory, "subscription event"
    )(*args, **kwargs)


@runtime_checkable
class SubscriptionEvent(Protocol):
    """A subscriber's subscribe or unsubscribe as observed by an XPUB socket:
    its routing id, topic, and whether it subscribed."""

    def _adopt_from(self, source): ...


__all__ = ["SubscriptionEvent"]
