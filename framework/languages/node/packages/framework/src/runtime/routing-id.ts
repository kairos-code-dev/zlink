import { RoutingId as BindingRoutingId } from '@zlink-systems/zlink';
import type { RoutingId } from '../contracts';

export function normalizeRoutingId(value: RoutingId | string): RoutingId {
  return String(value);
}

export function decodeRoutingId(text: string, hex: unknown): RoutingId {
  return typeof hex === 'string'
    ? String(BindingRoutingId.fromHex(hex))
    : normalizeRoutingId(text);
}

export function routingIdWireHex(routingId: RoutingId): string | undefined {
  const value = routingId as unknown as { toHex?: () => string };
  return typeof value.toHex === 'function'
    ? value.toHex.call(routingId).toLowerCase()
    : undefined;
}

export function encodeRoutingIdStorageHex(routingId: RoutingId): string {
  const wireHex = routingIdWireHex(routingId);
  if (wireHex !== undefined) {
    return wireHex;
  }

  if (typeof routingId !== 'string') {
    throw new TypeError('RoutingId must be a string or expose toHex().');
  }

  return Buffer.from(routingId, 'utf8').toString('hex');
}

export function routingIdsEqual(
  left: RoutingId | undefined,
  right: RoutingId | undefined
): boolean {
  if (left === undefined || right === undefined) {
    return left === right;
  }
  return encodeRoutingIdStorageHex(left) === encodeRoutingIdStorageHex(right);
}
