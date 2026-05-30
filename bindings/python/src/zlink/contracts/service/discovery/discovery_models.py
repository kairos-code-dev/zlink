# SPDX-License-Identifier: MPL-2.0

from dataclasses import dataclass

from ...core.routing_id import RoutingId
from ..codes import SpotKind


@dataclass(frozen=True)
class SpotRoute:
    """The resolved route to a spot.

    Attributes:
        spot_rid: The spot's routing id.
        owner_node_rid: The routing id of the node that owns the spot.
        spot_kind: The kind of spot.
    """

    spot_rid: RoutingId
    owner_node_rid: RoutingId
    spot_kind: SpotKind


__all__ = ["SpotRoute"]
