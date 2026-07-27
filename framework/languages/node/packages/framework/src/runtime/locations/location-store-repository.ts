import type {
  ZLinkLocationStore,
  ZLinkLocationOwnerToken,
  ZLinkLocationPage,
  ZLinkLocationWriteIntent,
  ZLinkLocationWriteResult,
  ZLinkLocationWriteStatus,
  ZLinkMeshNodeDescriptor,
  ZLinkMeshNodeDescriptorKey,
  ZLinkOwnerLeaseClaimResult,
  ZLinkOwnerLeaseReadResult,
  ZLinkOwnerLeaseReleaseResult,
  ZLinkOwnerLeaseRenewResult,
  ZLinkPageRequest,
  ZLinkStoreCondition,
  ZLinkStoreScanCursor
} from '../../contracts';
import { ZLinkLocationWriteStatus as WriteStatus } from '../../contracts';
import { ZLinkInMemoryLocationStore } from './in-memory-location-store';
import { storeKey } from './in-memory-provider-location-store';

const PREFIX = 'zlink:v11:';
const OWNER_COUNTER_KEY = storeKey(`${PREFIX}owner-counter`);
const MAX_GENERATION = 0x7fff_ffff_ffff_ffffn;

interface OwnerRecord {
  readonly ownerId: string;
  readonly leaseGeneration: string;
}

interface MeshRecord {
  readonly generation: string;
  readonly descriptor: ZLinkMeshNodeDescriptor;
}

/**
 * Maps framework domain records to the minimal provider SPI.
 *
 * Owner lease and MeshNode descriptor publication are persisted through the
 * provider. Remaining domain families still use the inherited framework-owned
 * repository while their record codecs are migrated.
 */
export class ZLinkLocationStoreRepository extends ZLinkInMemoryLocationStore {
  constructor(
    private readonly provider: ZLinkLocationStore,
    now: () => Date = () => new Date()
  ) {
    super(now);
  }

  override async claimOwnerLease(
    ownerId: string,
    leaseTtlMs: number,
    signal?: AbortSignal
  ): Promise<ZLinkOwnerLeaseClaimResult> {
    requireOwnerInput(ownerId, leaseTtlMs);
    for (;;) {
      const [owner, counter] = await Promise.all([
        this.provider.read(ownerKey(ownerId), signal),
        this.provider.read(OWNER_COUNTER_KEY, signal)
      ]);
      if (owner.kind === 'found') return { kind: 'conflict' };
      const generation = counter.kind === 'missing'
        ? 1n
        : BigInt(decodeText(counter.value.bytes));
      if (generation > MAX_GENERATION) return { kind: 'generationExhausted' };
      const token = { ownerId, leaseGeneration: generation };
      const conditions: ZLinkStoreCondition[] = [
        { kind: 'missing', key: ownerKey(ownerId) },
        counter.kind === 'missing'
          ? { kind: 'missing', key: OWNER_COUNTER_KEY }
          : { kind: 'version', key: OWNER_COUNTER_KEY, expected: counter.value.version }
      ];
      const result = await this.provider.write({
        conditions,
        mutations: [
          {
            kind: 'put',
            key: ownerKey(ownerId),
            bytes: encodeJson<OwnerRecord>({
              ownerId,
              leaseGeneration: generation.toString()
            }),
            retentionMs: leaseTtlMs
          },
          {
            kind: 'put',
            key: OWNER_COUNTER_KEY,
            bytes: encodeText((generation + 1n).toString())
          }
        ]
      }, signal);
      if (result.kind === 'conflict') continue;
      return {
        kind: 'claimed',
        token,
        leaseExpiresAt: new Date(result.storeNow.getTime() + leaseTtlMs),
        storeNow: result.storeNow
      };
    }
  }

  override async readOwnerLease(
    ownerId: string,
    signal?: AbortSignal
  ): Promise<ZLinkOwnerLeaseReadResult> {
    const result = await this.provider.read(ownerKey(ownerId), signal);
    if (result.kind === 'missing') return { kind: 'missing' };
    const record = decodeJson<OwnerRecord>(result.value.bytes);
    if (record.ownerId !== ownerId || result.value.expiresAt === undefined) {
      throw new Error('Location Store owner lease record is invalid.');
    }
    return {
      kind: 'found',
      token: { ownerId, leaseGeneration: BigInt(record.leaseGeneration) },
      leaseExpiresAt: result.value.expiresAt,
      storeNow: result.value.storeNow
    };
  }

  override async renewOwnerLease(
    token: ZLinkLocationOwnerToken,
    leaseTtlMs: number,
    signal?: AbortSignal
  ): Promise<ZLinkOwnerLeaseRenewResult> {
    requireOwnerInput(token.ownerId, leaseTtlMs);
    const key = ownerKey(token.ownerId);
    const current = await this.provider.read(key, signal);
    if (current.kind === 'missing') return { kind: 'stale' };
    const record = decodeJson<OwnerRecord>(current.value.bytes);
    if (BigInt(record.leaseGeneration) !== token.leaseGeneration) return { kind: 'stale' };
    const result = await this.provider.write({
      conditions: [{ kind: 'version', key, expected: current.value.version }],
      mutations: [{
        kind: 'put',
        key,
        bytes: current.value.bytes,
        retentionMs: leaseTtlMs
      }]
    }, signal);
    if (result.kind === 'conflict') return { kind: 'stale' };
    return {
      kind: 'renewed',
      leaseExpiresAt: new Date(result.storeNow.getTime() + leaseTtlMs),
      storeNow: result.storeNow
    };
  }

  override async releaseOwnerLease(
    token: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkOwnerLeaseReleaseResult> {
    const key = ownerKey(token.ownerId);
    const current = await this.provider.read(key, signal);
    if (current.kind === 'missing') return 'stale';
    const record = decodeJson<OwnerRecord>(current.value.bytes);
    if (BigInt(record.leaseGeneration) !== token.leaseGeneration) return 'stale';
    const result = await this.provider.write({
      conditions: [{ kind: 'version', key, expected: current.value.version }],
      mutations: [{ kind: 'delete', key }]
    }, signal);
    return result.kind === 'applied' ? 'released' : 'stale';
  }

  override async updateMeshNode(
    descriptor: ZLinkMeshNodeDescriptor,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    const leaseKey = ownerKey(descriptor.ownerId);
    const rowKey = meshKey(descriptor.meshName, String(descriptor.rid));
    const [lease, current] = await Promise.all([
      this.provider.read(leaseKey, signal),
      this.provider.read(rowKey, signal)
    ]);
    if (lease.kind === 'missing'
      || BigInt(decodeJson<OwnerRecord>(lease.value.bytes).leaseGeneration)
        !== descriptor.leaseGeneration) {
      return rejected(lease.kind === 'missing' ? new Date() : lease.value.storeNow);
    }

    let generation = 1n;
    let rowCondition: ZLinkStoreCondition = { kind: 'missing', key: rowKey };
    if (current.kind === 'found') {
      const record = decodeJson<MeshRecord>(current.value.bytes);
      const stored = reviveMeshDescriptor(record.descriptor);
      generation = BigInt(record.generation);
      if (sameMeshDescriptor(stored, descriptor)) {
        return { status: WriteStatus.Stored, generation, updatedAt: current.value.storeNow };
      }
      const currentLease = await this.provider.read(ownerKey(stored.ownerId), signal);
      const takeover = (intent === 1 || intent === 3) && currentLease.kind === 'missing';
      const renew = intent === 2
        && stored.ownerId === descriptor.ownerId
        && stored.leaseGeneration === descriptor.leaseGeneration
        && stored.lifecycleGeneration === descriptor.lifecycleGeneration
        && descriptor.descriptorRevision > stored.descriptorRevision;
      if (!takeover && !renew) {
        return {
          status: WriteStatus.IgnoredStale,
          generation,
          updatedAt: current.value.storeNow
        };
      }
      if (takeover) generation += 1n;
      rowCondition = { kind: 'version', key: rowKey, expected: current.value.version };
    } else if (intent === 2) {
      return rejected(lease.value.storeNow);
    }

    const result = await this.provider.write({
      conditions: [
        { kind: 'version', key: leaseKey, expected: lease.value.version },
        rowCondition
      ],
      mutations: [{
        kind: 'put',
        key: rowKey,
        bytes: encodeJson<MeshRecord>({
          generation: generation.toString(),
          descriptor: persistMeshDescriptor(descriptor)
        })
      }]
    }, signal);
    if (result.kind === 'conflict') return rejected(result.storeNow);
    return { status: WriteStatus.Stored, generation, updatedAt: result.storeNow };
  }

  override async removeMeshNode(
    key: ZLinkMeshNodeDescriptorKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteStatus> {
    const rowKey = meshKey(key.meshName, String(key.rid));
    const current = await this.provider.read(rowKey, signal);
    if (current.kind === 'missing') return WriteStatus.IgnoredStale;
    const descriptor = reviveMeshDescriptor(decodeJson<MeshRecord>(current.value.bytes).descriptor);
    if (descriptor.ownerId !== owner.ownerId
      || descriptor.leaseGeneration !== owner.leaseGeneration) {
      return WriteStatus.IgnoredStale;
    }
    const result = await this.provider.write({
      conditions: [{ kind: 'version', key: rowKey, expected: current.value.version }],
      mutations: [{ kind: 'delete', key: rowKey }]
    }, signal);
    return result.kind === 'applied' ? WriteStatus.Stored : WriteStatus.IgnoredStale;
  }

  override async listMeshNodes(
    meshName: string,
    page: ZLinkPageRequest = {},
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkMeshNodeDescriptor>> {
    const result = await this.provider.scan({
      prefix: meshPrefix(meshName),
      cursor: page.continuationToken === undefined
        ? undefined
        : ({ value: page.continuationToken } as ZLinkStoreScanCursor),
      limit: page.pageSize ?? 100
    }, signal);
    if (result.kind === 'expired') {
      throw new Error('Location Store scan snapshot expired.');
    }
    return {
      items: result.value.items.map(item =>
        reviveMeshDescriptor(decodeJson<MeshRecord>(item.value.bytes).descriptor)),
      continuationToken: result.value.nextCursor?.value
    };
  }

  override async removeAllByOwner(
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<bigint> {
    let removed = 0n;
    let cursor: ZLinkStoreScanCursor | undefined;
    do {
      const result = await this.provider.scan({
        prefix: `${PREFIX}mesh:`,
        cursor,
        limit: 1_000
      }, signal);
      if (result.kind === 'expired') {
        cursor = undefined;
        continue;
      }
      for (const item of result.value.items) {
        const descriptor = reviveMeshDescriptor(
          decodeJson<MeshRecord>(item.value.bytes).descriptor);
        if (descriptor.ownerId !== owner.ownerId
          || descriptor.leaseGeneration !== owner.leaseGeneration) continue;
        const deleted = await this.provider.write({
          conditions: [{ kind: 'version', key: item.key, expected: item.value.version }],
          mutations: [{ kind: 'delete', key: item.key }]
        }, signal);
        if (deleted.kind === 'applied') removed += 1n;
      }
      cursor = result.value.nextCursor;
    } while (cursor !== undefined);
    return removed + await super.removeAllByOwner(owner);
  }
}

function ownerKey(ownerId: string) {
  return storeKey(`${PREFIX}owner:${encodeURIComponent(ownerId)}`);
}

function meshPrefix(meshName: string): string {
  return `${PREFIX}mesh:${encodeURIComponent(meshName)}:`;
}

function meshKey(meshName: string, nodeRid: string) {
  return storeKey(`${meshPrefix(meshName)}${encodeURIComponent(nodeRid)}`);
}

function encodeText(value: string): Uint8Array {
  return Buffer.from(value, 'utf8');
}

function decodeText(value: Uint8Array): string {
  return Buffer.from(value).toString('utf8');
}

function encodeJson<T>(value: T): Uint8Array {
  return Buffer.from(JSON.stringify(value, (_key, candidate) =>
    typeof candidate === 'bigint' ? { $bigint: candidate.toString() } : candidate), 'utf8');
}

function decodeJson<T>(value: Uint8Array): T {
  return JSON.parse(decodeText(value), (_key, candidate) =>
    candidate !== null
      && typeof candidate === 'object'
      && Object.keys(candidate).length === 1
      && typeof candidate.$bigint === 'string'
      ? BigInt(candidate.$bigint)
      : candidate) as T;
}

function persistMeshDescriptor(descriptor: ZLinkMeshNodeDescriptor): ZLinkMeshNodeDescriptor {
  return { ...descriptor, rid: String(descriptor.rid), updatedAt: descriptor.updatedAt.toISOString() as never };
}

function reviveMeshDescriptor(descriptor: ZLinkMeshNodeDescriptor): ZLinkMeshNodeDescriptor {
  return {
    ...descriptor,
    rid: String(descriptor.rid),
    updatedAt: descriptor.updatedAt instanceof Date
      ? descriptor.updatedAt
      : new Date(descriptor.updatedAt)
  };
}

function sameMeshDescriptor(
  left: ZLinkMeshNodeDescriptor,
  right: ZLinkMeshNodeDescriptor
): boolean {
  return Buffer.from(encodeJson(persistMeshDescriptor(left))).equals(
    Buffer.from(encodeJson(persistMeshDescriptor(right))));
}

function rejected(updatedAt: Date): ZLinkLocationWriteResult {
  return {
    status: WriteStatus.RejectedConflict,
    generation: 0n,
    updatedAt
  };
}

function requireOwnerInput(ownerId: string, leaseTtlMs: number): void {
  if (Buffer.byteLength(ownerId, 'utf8') < 1 || ownerId.includes('\0')) {
    throw new TypeError('Owner ID must be non-empty UTF-8 without NUL.');
  }
  if (!Number.isSafeInteger(leaseTtlMs) || leaseTtlMs < 1) {
    throw new RangeError('Owner lease TTL must be a positive safe integer.');
  }
}
