// SPDX-License-Identifier: MPL-2.0

import { RoutingId } from '../../contracts/core';

function requireRoutingId(routingId: RoutingId, name = 'routingId'): Buffer {
  if (!(routingId instanceof RoutingId)) {
    throw new TypeError(`${name} must be a RoutingId`);
  }
  return routingId.borrowedBytes();
}

export function normalizeRoutingId(routingId: RoutingId, name = 'routingId'): Buffer {
  const normalized = requireRoutingId(routingId, name);
  if (normalized.length === 0 || normalized.length > 255) {
    throw new RangeError(`${name} must be 1..255 bytes`);
  }
  return normalized;
}
