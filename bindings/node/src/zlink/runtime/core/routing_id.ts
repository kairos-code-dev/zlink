// SPDX-License-Identifier: MPL-2.0

import { RoutingId } from '../../contracts/core';

function requireRoutingId(routingId: RoutingId, name = 'routingId'): Buffer {
  if (!(routingId instanceof RoutingId)) {
    throw new TypeError(`${name} must be a RoutingId`);
  }
  return routingId.borrowedBytes();
}

export function normalizeRoutingId(routingId: RoutingId, name = 'routingId'): Buffer {
  return requireRoutingId(routingId, name);
}
