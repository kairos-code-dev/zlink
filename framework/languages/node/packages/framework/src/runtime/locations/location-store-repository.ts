import type {
  ZLinkLocationStore,
  ZLinkLocationOwnerToken,
  ZLinkLocationPage,
  ZLinkLocationWriteIntent,
  ZLinkLocationWriteResult,
  ZLinkLocationWriteStatus,
  ZLinkClientServerServerDescriptor,
  ZLinkClientServerServerDescriptorKey,
  ZLinkFanoutPublisherDescriptor,
  ZLinkFanoutPublisherDescriptorKey,
  ZLinkMeshNodeDescriptor,
  ZLinkMeshNodeDescriptorKey,
  ZLinkOwnerLeaseClaimResult,
  ZLinkOwnerLeaseReadResult,
  ZLinkOwnerLeaseReleaseResult,
  ZLinkOwnerLeaseRenewResult,
  ZLinkPageRequest,
  ZLinkStoreCondition,
  ZLinkStoreKey,
  ZLinkStoreScanCursor
} from '../../contracts';
import type {
  ZLinkActorLocation,
  ZLinkActorLocationFilter,
  ZLinkActorLocationKey,
  ZLinkRouteLocation,
  ZLinkRouteLocationFilter,
  ZLinkRouteLocationKey,
  ZLinkSpotLocation,
  ZLinkSpotLocationFilter,
  ZLinkSpotLocationKey
} from './internal-location-contracts';
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

interface DescriptorRecord<T> {
  readonly generation: string;
  readonly descriptor: T;
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

  override async updateClientServer(
    descriptor: ZLinkClientServerServerDescriptor,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    validateClientServerDescriptor(descriptor);
    const normalized = reviveClientServerDescriptor({
      ...descriptor,
      serverRid: String(descriptor.serverRid)
    });
    return this.updateDescriptor(
      clientServerKey(normalized.channelName, String(normalized.serverRid)),
      normalized,
      intent,
      reviveClientServerDescriptor,
      sameClientServerDescriptor,
      canRenewClientServer,
      signal
    );
  }

  override async removeClientServer(
    key: ZLinkClientServerServerDescriptorKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteStatus> {
    return this.removeDescriptor(
      clientServerKey(key.channelName, String(key.serverRid)),
      owner,
      reviveClientServerDescriptor,
      signal
    );
  }

  override async listClientServers(
    channelName: string,
    page: ZLinkPageRequest = {},
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkClientServerServerDescriptor>> {
    return this.listDescriptors(
      clientServerPrefix(channelName),
      page,
      reviveClientServerDescriptor,
      signal
    );
  }

  override async updateFanoutPublisher(
    descriptor: ZLinkFanoutPublisherDescriptor,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    validateFanoutPublisherDescriptor(descriptor);
    const normalized = reviveFanoutDescriptor({
      ...descriptor,
      publisherRid: String(descriptor.publisherRid)
    });
    return this.updateDescriptor(
      fanoutKey(normalized.channelName, String(normalized.publisherRid)),
      normalized,
      intent,
      reviveFanoutDescriptor,
      sameFanoutDescriptor,
      canRenewFanout,
      signal
    );
  }

  override async removeFanoutPublisher(
    key: ZLinkFanoutPublisherDescriptorKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteStatus> {
    return this.removeDescriptor(
      fanoutKey(key.channelName, String(key.publisherRid)),
      owner,
      reviveFanoutDescriptor,
      signal
    );
  }

  override async listFanoutPublishers(
    channelName: string,
    page: ZLinkPageRequest = {},
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkFanoutPublisherDescriptor>> {
    return this.listDescriptors(
      fanoutPrefix(channelName),
      page,
      reviveFanoutDescriptor,
      signal
    );
  }

  override async updateSpot(
    location: ZLinkSpotLocation,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    return this.updateOwnedLocation(
      spotKey(location.meshName, String(location.spotId)),
      normalizeSpot(location),
      intent,
      signal
    );
  }

  override async removeSpot(
    key: ZLinkSpotLocationKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteStatus> {
    return this.removeOwnedLocation(
      spotKey(key.meshName, String(key.spotId)),
      owner,
      signal
    );
  }

  override async resolveSpot(
    key: ZLinkSpotLocationKey,
    signal?: AbortSignal
  ): Promise<ZLinkSpotLocation | undefined> {
    const value = await this.readOwnedLocation<ZLinkSpotLocation>(
      spotKey(key.meshName, String(key.spotId)),
      signal
    );
    return value === undefined ? undefined : normalizeSpot(value);
  }

  override async listSpots(
    filter: ZLinkSpotLocationFilter,
    page: ZLinkPageRequest = {},
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkSpotLocation>> {
    return this.listOwnedLocations(
      `${PREFIX}spot:`,
      page,
      normalizeSpot,
      value => matchesSpot(value, filter),
      signal
    );
  }

  override async updateActor(
    location: ZLinkActorLocation,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    return this.updateOwnedLocation(
      actorKey(location.meshName, location.actorId),
      normalizeActor(location),
      intent,
      signal
    );
  }

  override async removeActor(
    key: ZLinkActorLocationKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteStatus> {
    return this.removeOwnedLocation(actorKey(key.meshName, key.actorId), owner, signal);
  }

  override async resolveActor(
    key: ZLinkActorLocationKey,
    signal?: AbortSignal
  ): Promise<ZLinkActorLocation | undefined> {
    const value = await this.readOwnedLocation<ZLinkActorLocation>(
      actorKey(key.meshName, key.actorId),
      signal
    );
    return value === undefined ? undefined : normalizeActor(value);
  }

  override async listActors(
    filter: ZLinkActorLocationFilter,
    page: ZLinkPageRequest = {},
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkActorLocation>> {
    return this.listOwnedLocations(
      `${PREFIX}actor:`,
      page,
      normalizeActor,
      value => matchesActor(value, filter),
      signal
    );
  }

  override async updateRoute(
    location: ZLinkRouteLocation,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    const rowKey = routeKey(String(location.routeKind), location.routeKey);
    const normalized = normalizeRoute(location);
    const current = await this.provider.read(rowKey, signal);
    let generation = 1n;
    let condition: ZLinkStoreCondition = { kind: 'missing', key: rowKey };
    if (current.kind === 'found') {
      const record = decodeJson<DescriptorRecord<ZLinkRouteLocation>>(current.value.bytes);
      const stored = normalizeRoute(record.descriptor);
      generation = BigInt(record.generation);
      if (intent === 1) {
        const ownerLease = await this.provider.read(ownerKey(stored.ownerId), signal);
        if (ownerLease.kind === 'found') {
          return {
            status: WriteStatus.RejectedConflict,
            generation,
            updatedAt: current.value.storeNow
          };
        }
      } else if (intent === 2
        && (stored.ownerId !== normalized.ownerId
          || normalized.generation !== 0n && stored.generation !== normalized.generation)) {
        return {
          status: WriteStatus.IgnoredStale,
          generation,
          updatedAt: current.value.storeNow
        };
      }
      if (intent !== 2) generation += 1n;
      condition = { kind: 'version', key: rowKey, expected: current.value.version };
    } else if (intent === 2) {
      return rejected(new Date());
    }
    const stored = { ...normalized, generation };
    const result = await this.provider.write({
      conditions: [condition],
      mutations: [{
        kind: 'put',
        key: rowKey,
        bytes: encodeJson<DescriptorRecord<ZLinkRouteLocation>>({
          generation: generation.toString(),
          descriptor: stored
        })
      }]
    }, signal);
    return result.kind === 'applied'
      ? { status: WriteStatus.Stored, generation, updatedAt: result.storeNow }
      : rejected(result.storeNow);
  }

  override async removeRoute(
    key: ZLinkRouteLocationKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    const rowKey = routeKey(String(key.routeKind), key.routeKey);
    const current = await this.provider.read(rowKey, signal);
    if (current.kind === 'missing') return rejected(current.storeNow);
    const record = decodeJson<DescriptorRecord<ZLinkRouteLocation>>(current.value.bytes);
    if (record.descriptor.ownerId !== owner.ownerId
      || record.descriptor.generation !== owner.leaseGeneration) {
      return {
        status: WriteStatus.IgnoredStale,
        generation: BigInt(record.generation),
        updatedAt: current.value.storeNow
      };
    }
    const result = await this.provider.write({
      conditions: [{ kind: 'version', key: rowKey, expected: current.value.version }],
      mutations: [{ kind: 'delete', key: rowKey }]
    }, signal);
    return result.kind === 'applied'
      ? { status: WriteStatus.Stored, generation: BigInt(record.generation), updatedAt: result.storeNow }
      : rejected(result.storeNow);
  }

  override async resolveRoute(
    key: ZLinkRouteLocationKey,
    signal?: AbortSignal
  ): Promise<ZLinkRouteLocation | undefined> {
    const value = await this.readOwnedLocation<ZLinkRouteLocation>(
      routeKey(String(key.routeKind), key.routeKey),
      signal
    );
    return value === undefined ? undefined : normalizeRoute(value);
  }

  override async listRoutes(
    filter: ZLinkRouteLocationFilter,
    page: ZLinkPageRequest = {},
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkRouteLocation>> {
    return this.listOwnedLocations(
      `${PREFIX}route:`,
      page,
      normalizeRoute,
      value => matchesRoute(value, filter),
      signal
    );
  }

  override async removeAllByOwner(
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<bigint> {
    let removed = 0n;
    let cursor: ZLinkStoreScanCursor | undefined;
    for (const prefix of [
      `${PREFIX}mesh:`,
      `${PREFIX}client-server:`,
      `${PREFIX}fanout:`,
      `${PREFIX}spot:`,
      `${PREFIX}actor:`,
      `${PREFIX}route:`
    ]) {
      cursor = undefined;
      do {
        const result = await this.provider.scan({ prefix, cursor, limit: 1_000 }, signal);
        if (result.kind === 'expired') {
          cursor = undefined;
          continue;
        }
        for (const item of result.value.items) {
          const record = decodeJson<DescriptorRecord<OwnedStoreRecord>>(item.value.bytes);
          const descriptor = record.descriptor;
          const leaseGeneration = 'leaseGeneration' in descriptor
            ? descriptor.leaseGeneration
            : descriptor.generation;
          if (descriptor.ownerId !== owner.ownerId
            || BigInt(leaseGeneration) !== owner.leaseGeneration) continue;
          const deleted = await this.provider.write({
            conditions: [{ kind: 'version', key: item.key, expected: item.value.version }],
            mutations: [{ kind: 'delete', key: item.key }]
          }, signal);
          if (deleted.kind === 'applied') removed += 1n;
        }
        cursor = result.value.nextCursor;
      } while (cursor !== undefined);
    }
    return removed + await super.removeAllByOwner(owner);
  }

  private async updateDescriptor<T extends OwnedDescriptor>(
    rowKey: ZLinkStoreKey,
    descriptor: T,
    intent: ZLinkLocationWriteIntent,
    revive: (descriptor: T) => T,
    same: (left: T, right: T) => boolean,
    canRenew: (current: T, next: T) => boolean,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    const leaseKey = ownerKey(descriptor.ownerId);
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
      const record = decodeJson<DescriptorRecord<T>>(current.value.bytes);
      const stored = revive(record.descriptor);
      generation = BigInt(record.generation);
      if (same(stored, descriptor)) {
        return { status: WriteStatus.Stored, generation, updatedAt: current.value.storeNow };
      }
      const currentLease = await this.provider.read(ownerKey(stored.ownerId), signal);
      const takeover = (intent === 1 || intent === 3) && currentLease.kind === 'missing';
      const renew = intent === 2 && canRenew(stored, descriptor);
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
        bytes: encodeJson<DescriptorRecord<T>>({
          generation: generation.toString(),
          descriptor
        })
      }]
    }, signal);
    if (result.kind === 'conflict') return rejected(result.storeNow);
    return { status: WriteStatus.Stored, generation, updatedAt: result.storeNow };
  }

  private async removeDescriptor<T extends OwnedDescriptor>(
    rowKey: ZLinkStoreKey,
    owner: ZLinkLocationOwnerToken,
    revive: (descriptor: T) => T,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteStatus> {
    const current = await this.provider.read(rowKey, signal);
    if (current.kind === 'missing') return WriteStatus.IgnoredStale;
    const descriptor = revive(
      decodeJson<DescriptorRecord<T>>(current.value.bytes).descriptor);
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

  private async listDescriptors<T extends OwnedDescriptor>(
    prefix: string,
    page: ZLinkPageRequest,
    revive: (descriptor: T) => T,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<T>> {
    const result = await this.provider.scan({
      prefix,
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
        revive(decodeJson<DescriptorRecord<T>>(item.value.bytes).descriptor)),
      continuationToken: result.value.nextCursor?.value
    };
  }

  private async updateOwnedLocation<T extends LeaseOwnedLocation>(
    rowKey: ZLinkStoreKey,
    location: T,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    const leaseKey = ownerKey(location.ownerId);
    const [lease, current] = await Promise.all([
      this.provider.read(leaseKey, signal),
      this.provider.read(rowKey, signal)
    ]);
    if (lease.kind === 'missing'
      || BigInt(decodeJson<OwnerRecord>(lease.value.bytes).leaseGeneration)
        !== locationOwnerGeneration(location)) {
      return rejected(lease.kind === 'missing' ? new Date() : lease.value.storeNow);
    }

    let generation = 1n;
    let rowCondition: ZLinkStoreCondition = { kind: 'missing', key: rowKey };
    if (current.kind === 'found') {
      const record = decodeJson<DescriptorRecord<T>>(current.value.bytes);
      generation = BigInt(record.generation);
      const stored = record.descriptor;
      if (intent === 2) {
        if (stored.ownerId !== location.ownerId
          || locationOwnerGeneration(stored) !== locationOwnerGeneration(location)) {
          return {
            status: WriteStatus.IgnoredStale,
            generation,
            updatedAt: current.value.storeNow
          };
        }
      } else {
        const currentLease = await this.provider.read(ownerKey(stored.ownerId), signal);
        if (currentLease.kind === 'found') {
          return {
            status: WriteStatus.RejectedConflict,
            generation,
            updatedAt: current.value.storeNow
          };
        }
        generation += 1n;
      }
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
        bytes: encodeJson<DescriptorRecord<T>>({
          generation: generation.toString(),
          descriptor: location
        })
      }]
    }, signal);
    return result.kind === 'applied'
      ? { status: WriteStatus.Stored, generation, updatedAt: result.storeNow }
      : rejected(result.storeNow);
  }

  private async removeOwnedLocation(
    rowKey: ZLinkStoreKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteStatus> {
    const current = await this.provider.read(rowKey, signal);
    if (current.kind === 'missing') return WriteStatus.IgnoredStale;
    const record = decodeJson<DescriptorRecord<LeaseOwnedLocation>>(current.value.bytes);
    if (record.descriptor.ownerId !== owner.ownerId
      || locationOwnerGeneration(record.descriptor) !== owner.leaseGeneration) {
      return WriteStatus.IgnoredStale;
    }
    const result = await this.provider.write({
      conditions: [{ kind: 'version', key: rowKey, expected: current.value.version }],
      mutations: [{ kind: 'delete', key: rowKey }]
    }, signal);
    return result.kind === 'applied' ? WriteStatus.Stored : WriteStatus.IgnoredStale;
  }

  private async readOwnedLocation<T extends StoredLocation>(
    rowKey: ZLinkStoreKey,
    signal?: AbortSignal
  ): Promise<T | undefined> {
    const result = await this.provider.read(rowKey, signal);
    return result.kind === 'missing'
      ? undefined
      : decodeJson<DescriptorRecord<T>>(result.value.bytes).descriptor;
  }

  private async listOwnedLocations<T extends StoredLocation>(
    prefix: string,
    page: ZLinkPageRequest,
    revive: (value: T) => T,
    matches: (value: T) => boolean,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<T>> {
    const requested = page.pageSize ?? 100;
    let cursor = page.continuationToken === undefined
      ? undefined
      : ({ value: page.continuationToken } as ZLinkStoreScanCursor);
    const items: T[] = [];
    do {
      const result = await this.provider.scan({
        prefix,
        cursor,
        limit: requested
      }, signal);
      if (result.kind === 'expired') {
        throw new Error('Location Store scan snapshot expired.');
      }
      for (const item of result.value.items) {
        const value = revive(
          decodeJson<DescriptorRecord<T>>(item.value.bytes).descriptor);
        if (matches(value)) items.push(value);
        if (items.length === requested) {
          return {
            items,
            continuationToken: result.value.nextCursor?.value
          };
        }
      }
      cursor = result.value.nextCursor;
    } while (cursor !== undefined);
    return { items };
  }
}

type OwnedDescriptor =
  | ZLinkMeshNodeDescriptor
  | ZLinkClientServerServerDescriptor
  | ZLinkFanoutPublisherDescriptor;

type LeaseOwnedLocation = ZLinkSpotLocation | ZLinkActorLocation;
type StoredLocation = LeaseOwnedLocation | ZLinkRouteLocation;
type OwnedStoreRecord = OwnedDescriptor | StoredLocation;

function ownerKey(ownerId: string) {
  return storeKey(`${PREFIX}owner:${encodeURIComponent(ownerId)}`);
}

function meshPrefix(meshName: string): string {
  return `${PREFIX}mesh:${encodeURIComponent(meshName)}:`;
}

function meshKey(meshName: string, nodeRid: string) {
  return storeKey(`${meshPrefix(meshName)}${encodeURIComponent(nodeRid)}`);
}

function clientServerPrefix(channelName: string): string {
  return `${PREFIX}client-server:${encodeURIComponent(channelName)}:`;
}

function clientServerKey(channelName: string, serverRid: string) {
  return storeKey(`${clientServerPrefix(channelName)}${encodeURIComponent(serverRid)}`);
}

function fanoutPrefix(channelName: string): string {
  return `${PREFIX}fanout:${encodeURIComponent(channelName)}:`;
}

function fanoutKey(channelName: string, publisherRid: string) {
  return storeKey(`${fanoutPrefix(channelName)}${encodeURIComponent(publisherRid)}`);
}

function spotKey(meshName: string, spotId: string) {
  return storeKey(
    `${PREFIX}spot:${encodeURIComponent(meshName)}:${encodeURIComponent(spotId)}`);
}

function actorKey(meshName: string, actorId: string) {
  return storeKey(
    `${PREFIX}actor:${encodeURIComponent(meshName)}:${encodeURIComponent(actorId)}`);
}

function routeKey(routeKind: string, value: string) {
  return storeKey(
    `${PREFIX}route:${encodeURIComponent(routeKind)}:${encodeURIComponent(value)}`);
}

function encodeText(value: string): Uint8Array {
  return Buffer.from(value, 'utf8');
}

function decodeText(value: Uint8Array): string {
  return Buffer.from(value).toString('utf8');
}

function encodeJson<T>(value: T): Uint8Array {
  return Buffer.from(JSON.stringify(value, (_key, candidate) => {
    if (typeof candidate === 'bigint') return { $bigint: candidate.toString() };
    if (candidate instanceof Uint8Array) {
      return { $bytes: Buffer.from(candidate).toString('base64') };
    }
    return candidate;
  }), 'utf8');
}

function decodeJson<T>(value: Uint8Array): T {
  return JSON.parse(decodeText(value), (_key, candidate) => {
    if (candidate !== null
      && typeof candidate === 'object'
      && Object.keys(candidate).length === 1
      && typeof candidate.$bigint === 'string') {
      return BigInt(candidate.$bigint);
    }
    if (candidate !== null
      && typeof candidate === 'object'
      && Object.keys(candidate).length === 1
      && typeof candidate.$bytes === 'string') {
      return Buffer.from(candidate.$bytes, 'base64');
    }
    return candidate;
  }) as T;
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

function reviveClientServerDescriptor(
  descriptor: ZLinkClientServerServerDescriptor
): ZLinkClientServerServerDescriptor {
  return {
    ...descriptor,
    serverRid: String(descriptor.serverRid),
    updatedAt: reviveDate(descriptor.updatedAt)
  };
}

function reviveFanoutDescriptor(
  descriptor: ZLinkFanoutPublisherDescriptor
): ZLinkFanoutPublisherDescriptor {
  return {
    ...descriptor,
    publisherRid: String(descriptor.publisherRid),
    updatedAt: reviveDate(descriptor.updatedAt)
  };
}

function reviveDate(value: Date): Date {
  return value instanceof Date ? value : new Date(value);
}

function normalizeSpot(value: ZLinkSpotLocation): ZLinkSpotLocation {
  return {
    ...value,
    spotId: String(value.spotId),
    ownerNodeRid: String(value.ownerNodeRid),
    updatedAt: reviveDate(value.updatedAt)
  };
}

function normalizeActor(value: ZLinkActorLocation): ZLinkActorLocation {
  return {
    ...value,
    actorRef: {
      ...value.actorRef,
      nodeRid: String(value.actorRef.nodeRid)
    },
    ownerNodeRid: String(value.ownerNodeRid),
    spotId: String(value.spotId),
    updatedAt: reviveDate(value.updatedAt)
  };
}

function normalizeRoute(value: ZLinkRouteLocation): ZLinkRouteLocation {
  return {
    ...value,
    ownerNodeRid: String(value.ownerNodeRid),
    value: Buffer.from(value.value),
    updatedAt: reviveDate(value.updatedAt)
  };
}

function matchesSpot(value: ZLinkSpotLocation, filter: ZLinkSpotLocationFilter): boolean {
  return (filter.meshName === undefined || value.meshName === filter.meshName)
    && (filter.spotType === undefined || value.spotType === filter.spotType)
    && (filter.nodeRid === undefined || String(value.ownerNodeRid) === String(filter.nodeRid))
    && (filter.spotKind === undefined || value.spotKind === filter.spotKind);
}

function matchesActor(value: ZLinkActorLocation, filter: ZLinkActorLocationFilter): boolean {
  return (filter.actorType === undefined || value.actorType === filter.actorType)
    && (filter.nodeRid === undefined || String(value.ownerNodeRid) === String(filter.nodeRid))
    && (filter.spotId === undefined || String(value.spotId) === String(filter.spotId))
    && (filter.locationKind === undefined || value.spotKind === filter.locationKind);
}

function matchesRoute(value: ZLinkRouteLocation, filter: ZLinkRouteLocationFilter): boolean {
  return (filter.routeKind === undefined || value.routeKind === filter.routeKind)
    && (filter.ownerNodeRid === undefined
      || String(value.ownerNodeRid) === String(filter.ownerNodeRid))
    && (filter.ownerId === undefined || value.ownerId === filter.ownerId);
}

function locationOwnerGeneration(value: LeaseOwnedLocation): bigint {
  return value.leaseGeneration;
}

function sameMeshDescriptor(
  left: ZLinkMeshNodeDescriptor,
  right: ZLinkMeshNodeDescriptor
): boolean {
  return Buffer.from(encodeJson(persistMeshDescriptor(left))).equals(
    Buffer.from(encodeJson(persistMeshDescriptor(right))));
}

function sameClientServerDescriptor(
  left: ZLinkClientServerServerDescriptor,
  right: ZLinkClientServerServerDescriptor
): boolean {
  return descriptorFingerprint(left, ['updatedAt']) === descriptorFingerprint(right, ['updatedAt']);
}

function sameFanoutDescriptor(
  left: ZLinkFanoutPublisherDescriptor,
  right: ZLinkFanoutPublisherDescriptor
): boolean {
  return descriptorFingerprint(left, ['updatedAt']) === descriptorFingerprint(right, ['updatedAt']);
}

function canRenewClientServer(
  current: ZLinkClientServerServerDescriptor,
  next: ZLinkClientServerServerDescriptor
): boolean {
  return sameOwnerGeneration(current, next)
    && next.descriptorRevision > current.descriptorRevision
    && descriptorFingerprint(current, ['descriptorRevision', 'weight', 'state', 'updatedAt'])
      === descriptorFingerprint(next, ['descriptorRevision', 'weight', 'state', 'updatedAt']);
}

function canRenewFanout(
  current: ZLinkFanoutPublisherDescriptor,
  next: ZLinkFanoutPublisherDescriptor
): boolean {
  return sameOwnerGeneration(current, next)
    && next.descriptorRevision > current.descriptorRevision
    && descriptorFingerprint(current, ['descriptorRevision', 'state', 'updatedAt'])
      === descriptorFingerprint(next, ['descriptorRevision', 'state', 'updatedAt']);
}

function sameOwnerGeneration(left: OwnedDescriptor, right: OwnedDescriptor): boolean {
  return left.ownerId === right.ownerId
    && left.leaseGeneration === right.leaseGeneration
    && left.lifecycleGeneration === right.lifecycleGeneration;
}

function descriptorFingerprint(
  descriptor: OwnedDescriptor,
  omitted: readonly string[]
): string {
  const copy = { ...descriptor } as Record<string, unknown>;
  for (const key of omitted) delete copy[key];
  return Buffer.from(encodeJson(copy)).toString('base64');
}

function validateClientServerDescriptor(descriptor: ZLinkClientServerServerDescriptor): void {
  validateDescriptorIdentity([
    descriptor.channelName,
    String(descriptor.serverRid),
    descriptor.endpoint,
    descriptor.securityIdentity,
    descriptor.ownerId
  ], 'ClientServer');
  validateDescriptorGenerations(descriptor, 'ClientServer');
  if (!Number.isInteger(descriptor.weight) || descriptor.weight < 0 || descriptor.weight > 10_000) {
    throw new RangeError('ClientServer descriptor weight must be an integer in 0..10000.');
  }
}

function validateFanoutPublisherDescriptor(descriptor: ZLinkFanoutPublisherDescriptor): void {
  validateDescriptorIdentity([
    descriptor.channelName,
    String(descriptor.publisherRid),
    descriptor.endpoint,
    descriptor.securityIdentity,
    descriptor.ownerId
  ], 'Fanout publisher');
  validateDescriptorGenerations(descriptor, 'Fanout publisher');
}

function validateDescriptorIdentity(values: readonly string[], kind: string): void {
  if (values.some(value => Buffer.byteLength(value, 'utf8') < 1 || value.includes('\0'))) {
    throw new TypeError(`${kind} descriptor identity and endpoint are required.`);
  }
}

function validateDescriptorGenerations(descriptor: OwnedDescriptor, kind: string): void {
  if (descriptor.lifecycleGeneration < 1n || descriptor.lifecycleGeneration > MAX_GENERATION
    || descriptor.descriptorRevision < 1n || descriptor.descriptorRevision > MAX_GENERATION
    || descriptor.leaseGeneration < 1n || descriptor.leaseGeneration > MAX_GENERATION) {
    throw new RangeError(`${kind} descriptor generations are invalid.`);
  }
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
