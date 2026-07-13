import { AsyncLocalStorage } from 'node:async_hooks';
import { randomBytes } from 'node:crypto';
import type { ZlinkFlowOrigin } from '../Contracts';

export interface ZlinkFlowContextValue {
  readonly flowId: string;
  readonly flowOrigin: ZlinkFlowOrigin;
}

const storage = new AsyncLocalStorage<ZlinkFlowContextValue>();

export function currentOrCreateFlow(): ZlinkFlowContextValue {
  return storage.getStore() ?? { flowId: createUuidV7(), flowOrigin: 'Application' };
}

export function runWithFlow<T>(flow: ZlinkFlowContextValue, callback: () => T): T {
  return storage.run(flow, callback);
}

export function createInboundFlow(flowId?: string, flowOrigin?: ZlinkFlowOrigin): ZlinkFlowContextValue {
  return { flowId: flowId ?? createUuidV7(), flowOrigin: flowOrigin ?? 'Inbound' };
}

export function createUuidV7(): string {
  const bytes = randomBytes(16);
  const timestamp = BigInt(Date.now());
  for (let index = 5; index >= 0; index -= 1) {
    bytes[index] = Number(timestamp >> BigInt((5 - index) * 8) & 0xffn);
  }
  bytes[6] = 0x70 | (bytes[6] & 0x0f);
  bytes[8] = 0x80 | (bytes[8] & 0x3f);
  const hex = [...bytes].map((byte) => byte.toString(16).padStart(2, '0')).join('');
  return `${hex.slice(0, 8)}-${hex.slice(8, 12)}-${hex.slice(12, 16)}-${hex.slice(16, 20)}-${hex.slice(20)}`;
}
