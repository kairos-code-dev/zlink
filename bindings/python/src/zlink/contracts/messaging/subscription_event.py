# SPDX-License-Identifier: MPL-2.0

from . import message as _message_contract


class SubscriptionEvent:
    def __new__(cls, *args, **kwargs):
        if cls is SubscriptionEvent:
            return _message_contract._require(
                _message_contract._subscription_event_factory, "subscription event"
            )(*args, **kwargs)
        return super().__new__(cls)

    def _adopt_from(self, source): ...


__all__ = ["SubscriptionEvent"]
