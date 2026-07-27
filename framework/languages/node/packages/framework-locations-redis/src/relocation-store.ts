import type {
  ZLinkRelocationDeleteResult,
  ZLinkRelocationReadResult,
  ZLinkRelocationReference,
  ZLinkRelocationRenewResult,
  ZLinkRelocationStore,
  ZLinkRelocationStored
} from '@zlink-systems/framework';
import type { ZLinkRedisRelocationOptions } from './redis-options';
import { ZLinkRedisLocationStore } from './store';

class RedisRelocationStoreBackend extends ZLinkRedisLocationStore {
  put(
    payload: Uint8Array,
    retentionMs: number,
    signal?: AbortSignal
  ): Promise<ZLinkRelocationStored> {
    return this.putRelocationPayload(payload, retentionMs, signal);
  }

  get(
    reference: ZLinkRelocationReference,
    signal?: AbortSignal
  ): Promise<ZLinkRelocationReadResult> {
    return this.getRelocationPayload(reference, signal);
  }

  renew(
    reference: ZLinkRelocationReference,
    retentionMs: number,
    signal?: AbortSignal
  ): Promise<ZLinkRelocationRenewResult> {
    return this.renewRelocationPayload(reference, retentionMs, signal);
  }

  delete(
    reference: ZLinkRelocationReference,
    signal?: AbortSignal
  ): Promise<ZLinkRelocationDeleteResult> {
    return this.deleteRelocationPayload(reference, signal);
  }
}

/** Redis relocation payload capability with an independent prefix and lifecycle. */
export class ZLinkRedisRelocationStore implements ZLinkRelocationStore {
  private readonly store: RedisRelocationStoreBackend;

  constructor(options: ZLinkRedisRelocationOptions) {
    this.store = new RedisRelocationStoreBackend(options);
  }

  putRelocation(payload: Uint8Array, retentionMs: number, signal?: AbortSignal): Promise<ZLinkRelocationStored> {
    return this.store.put(payload, retentionMs, signal);
  }

  getRelocation(reference: ZLinkRelocationReference, signal?: AbortSignal): Promise<ZLinkRelocationReadResult> {
    return this.store.get(reference, signal);
  }

  renewRelocation(
    reference: ZLinkRelocationReference,
    retentionMs: number,
    signal?: AbortSignal
  ): Promise<ZLinkRelocationRenewResult> {
    return this.store.renew(reference, retentionMs, signal);
  }

  deleteRelocation(
    reference: ZLinkRelocationReference,
    signal?: AbortSignal
  ): Promise<ZLinkRelocationDeleteResult> {
    return this.store.delete(reference, signal);
  }

  close(): Promise<void> { return this.store.dispose(); }
  dispose(): Promise<void> { return this.store.dispose(); }
}
