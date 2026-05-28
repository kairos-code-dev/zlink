# SPDX-License-Identifier: MPL-2.0

from dataclasses import dataclass

from ...core.routing_id import RoutingId
from ..codes import SpotKind


@dataclass(frozen=True)
class SpotRoute:
    spot_rid: RoutingId
    owner_node_rid: RoutingId
    spot_kind: SpotKind


__all__ = ["SpotRoute"]
