# SPDX-License-Identifier: MPL-2.0

from ..._runtime.core.core import _validated_routing_id_bytes


class RoutingId:
    def __init__(self, data):
        self._raw = _validated_routing_id_bytes(data)

    @classmethod
    def from_bytes(cls, data):
        return cls(data)

    @classmethod
    def from_string(cls, value):
        if not isinstance(value, str):
            raise TypeError("value must be str")
        if len(value) == 0 or len(value) % 2 != 0 or any(
            c not in "0123456789abcdefABCDEF" for c in value
        ):
            raise ValueError("routing id string must be a non-empty even-length hex string")
        if len(value) > 510:
            raise ValueError("routing id string must decode to at most 255 bytes")
        return cls(bytes.fromhex(value))

    def to_bytes(self):
        return self._raw

    @property
    def size(self):
        return len(self._raw)

    def __bytes__(self):
        return self._raw

    def __len__(self):
        return len(self._raw)

    def __hash__(self):
        return hash(self._raw)

    def __eq__(self, other):
        if isinstance(other, RoutingId):
            return self._raw == other._raw
        try:
            return self._raw == _validated_routing_id_bytes(other)
        except Exception:
            return False

    def __repr__(self):
        return f"RoutingId({self._raw!r})"

    def to_hex(self):
        return self._raw.hex()

    def __str__(self):
        return self.to_hex()
