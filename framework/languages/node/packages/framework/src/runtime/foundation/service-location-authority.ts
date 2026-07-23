export interface ServiceAuthoritySnapshot {
  readonly key: string;
  readonly storeVersion: bigint;
  readonly objectGeneration: bigint;
  readonly authorityOwnerGeneration: bigint;
  readonly payload: Uint8Array;
}

export type ServiceAuthorityRead =
  | { readonly kind: 'missing'; readonly storeNowMs: number }
  | ({ readonly kind: 'snapshot'; readonly storeNowMs: number } & ServiceAuthoritySnapshot);

export type ServiceAuthorityExpectation =
  | { readonly kind: 'missing' }
  | { readonly kind: 'snapshot'; readonly storeVersion: bigint };

export type ServiceAuthorityMutation =
  | { readonly kind: 'preserve'; readonly payload: Uint8Array }
  | { readonly kind: 'newOwner'; readonly payload: Uint8Array }
  | { readonly kind: 'newObject'; readonly payload: Uint8Array }
  | { readonly kind: 'delete' };

export type ServiceAuthorityCasResult =
  | ({ readonly kind: 'stored'; readonly storeNowMs: number } & ServiceAuthoritySnapshot)
  | { readonly kind: 'deleted'; readonly storeVersion: bigint; readonly storeNowMs: number }
  | { readonly kind: 'conflict'; readonly current: ServiceAuthorityRead };

export interface ServiceAuthorityChange {
  readonly sequence: bigint;
  readonly key: string;
  readonly storeVersion: bigint;
  readonly kind: 'stored' | 'deleted';
}

const MAX_GENERATION = 0x7fff_ffff_ffff_ffffn;
const MAX_PAYLOAD_BYTES = 1024 * 1024;

/**
 * Deterministic provider used by the service runtime contract tests.
 * Opaque payload bytes never participate in provider-side decisions.
 */
export class InMemoryServiceLocationAuthority {
  private readonly rows = new Map<string, ServiceAuthoritySnapshot>();
  private readonly listeners = new Set<(change: ServiceAuthorityChange) => void>();
  private storeVersion = 0n;
  private objectGeneration = 0n;
  private ownerGeneration = 0n;
  private sequence = 0n;

  constructor(private readonly nowMs: () => number = () => Date.now()) {}

  read(key: string): ServiceAuthorityRead {
    requireKey(key);
    const current = this.rows.get(key);
    return current === undefined
      ? { kind: 'missing', storeNowMs: this.nowMs() }
      : { kind: 'snapshot', storeNowMs: this.nowMs(), ...cloneSnapshot(current) };
  }

  compareExchange(
    key: string,
    expectation: ServiceAuthorityExpectation,
    mutation: ServiceAuthorityMutation
  ): ServiceAuthorityCasResult {
    requireKey(key);
    const current = this.rows.get(key);
    if (!matches(current, expectation)) {
      return { kind: 'conflict', current: this.read(key) };
    }
    if (
      (mutation.kind === 'delete' && current === undefined)
      || ((mutation.kind === 'preserve' || mutation.kind === 'newOwner') && current === undefined)
      || (mutation.kind === 'newObject' && current !== undefined)
    ) {
      return { kind: 'conflict', current: this.read(key) };
    }
    if (mutation.kind !== 'delete') validatePayload(mutation.payload);
    const storeNowMs = this.nowMs();
    const storeVersion = this.next('storeVersion');
    if (mutation.kind === 'delete') {
      this.rows.delete(key);
      this.publish({ sequence: this.nextSequence(), key, storeVersion, kind: 'deleted' });
      return { kind: 'deleted', storeVersion, storeNowMs };
    }
    let objectGeneration: bigint;
    let authorityOwnerGeneration: bigint;
    switch (mutation.kind) {
      case 'preserve':
        if (current === undefined) throw new Error('Validated authority snapshot is missing.');
        objectGeneration = current.objectGeneration;
        authorityOwnerGeneration = current.authorityOwnerGeneration;
        break;
      case 'newOwner':
        if (current === undefined) throw new Error('Validated authority snapshot is missing.');
        objectGeneration = current.objectGeneration;
        authorityOwnerGeneration = this.next('ownerGeneration');
        break;
      case 'newObject':
        objectGeneration = this.next('objectGeneration');
        authorityOwnerGeneration = this.next('ownerGeneration');
        break;
    }
    const stored: ServiceAuthoritySnapshot = {
      key,
      storeVersion,
      objectGeneration,
      authorityOwnerGeneration,
      payload: Buffer.from(mutation.payload)
    };
    this.rows.set(key, stored);
    this.publish({ sequence: this.nextSequence(), key, storeVersion, kind: 'stored' });
    return { kind: 'stored', storeNowMs, ...cloneSnapshot(stored) };
  }

  subscribe(listener: (change: ServiceAuthorityChange) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  private publish(change: ServiceAuthorityChange): void {
    for (const listener of this.listeners) queueMicrotask(() => listener(change));
  }

  private next(kind: 'storeVersion' | 'objectGeneration' | 'ownerGeneration'): bigint {
    const current = this[kind];
    if (current >= MAX_GENERATION) throw new RangeError(`${kind} is exhausted.`);
    const next = current + 1n;
    this[kind] = next;
    return next;
  }

  private nextSequence(): bigint {
    if (this.sequence >= MAX_GENERATION) throw new RangeError('Change sequence is exhausted.');
    return ++this.sequence;
  }
}

function matches(
  current: ServiceAuthoritySnapshot | undefined,
  expectation: ServiceAuthorityExpectation
): boolean {
  return expectation.kind === 'missing'
    ? current === undefined
    : current?.storeVersion === expectation.storeVersion;
}

function cloneSnapshot(snapshot: ServiceAuthoritySnapshot): ServiceAuthoritySnapshot {
  return { ...snapshot, payload: Buffer.from(snapshot.payload) };
}

function validatePayload(payload: Uint8Array): void {
  if (payload.byteLength > MAX_PAYLOAD_BYTES) {
    throw new RangeError('Authority payload exceeds 1 MiB.');
  }
}

function requireKey(key: string): void {
  if (key.length === 0 || key.includes('\0')) {
    throw new TypeError('Authority key must be non-empty text without NUL.');
  }
}
