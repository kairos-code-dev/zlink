import { RoutingId as BindingRoutingId } from '@zlink-systems/zlink';
import type { RoutingId } from '../contracts';

export function normalizeRoutingId(value: RoutingId | string): RoutingId {
  return String(value);
}

export function normalizeOpaqueRoutingId(value: unknown): RoutingId {
  if (typeof value === 'string') return value;
  if (typeof value === 'object' && value !== null) {
    const candidate = value as { toHex?: () => unknown; toString?: () => unknown };
    if (typeof candidate.toHex === 'function') {
      const hex = candidate.toHex.call(value);
      if (typeof hex === 'string') return hex;
    }
    if (typeof candidate.toString === 'function') {
      const text = candidate.toString.call(value);
      if (typeof text === 'string' && text !== '[object Object]') return text;
    }
  }
  throw new TypeError('RoutingId must be a string or expose a string conversion.');
}

export function decodeRoutingId(text: string, hex: unknown): RoutingId {
  if (typeof hex !== 'string') {
    return normalizeRoutingId(text);
  }
  const decoded = BindingRoutingId.fromHex(hex);
  if (BindingRoutingId.from(text).toHex() === decoded.toHex()) {
    return normalizeRoutingId(text);
  }
  return decoded as unknown as RoutingId;
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

export function toBindingRoutingId(routingId: unknown): BindingRoutingId {
  if (routingId instanceof BindingRoutingId) {
    return routingId;
  }

  if (typeof routingId === 'string') {
    return BindingRoutingId.from(routingId);
  }

  const value = routingId as unknown as {
    toBytes?: () => Uint8Array;
    toHex?: () => string;
  };
  if (typeof value.toBytes === 'function') {
    return BindingRoutingId.from(value.toBytes.call(routingId));
  }
  if (typeof value.toHex === 'function') {
    return BindingRoutingId.fromHex(value.toHex.call(routingId));
  }

  throw new TypeError('RoutingId must be a string or expose toBytes() or toHex().');
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
