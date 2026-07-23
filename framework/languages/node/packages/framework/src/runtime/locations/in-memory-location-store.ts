import type { RoutingId } from '../../contracts/Common';
import {
  ZLinkLocationKind,
  ZLinkFrameworkRuntimeState,
  ZLinkLocationWriteIntent,
  ZLinkLocationWriteStatus,
  ZLinkObjectRole,
  type ZLinkLocationChangeStampStore,
  type ZLinkLocationStore,
  type ZLinkActorLocation,
  type ZLinkAggregateAbortResult,
  type ZLinkAggregateCommitResult,
  type ZLinkAggregateFence,
  type ZLinkAggregatePrepareRequest,
  type ZLinkAggregatePrepareResult,
  type ZLinkAuthorityCompareExchangeResult,
  type ZLinkAuthorityKey,
  type ZLinkAuthorityMutation,
  type ZLinkAuthorityReadResult,
  type ZLinkAuthorityScanCursor,
  type ZLinkAuthorityScanResult,
  type ZLinkAuthorityStoreVersion,
  type ZLinkActorTransferPrepareRequest,
  type ZLinkActorTransferRecord,
  type ZLinkActorTransferState,
  type ZLinkActorTransferWriteResult,
  type ZLinkActorLocationFilter,
  type ZLinkActorLocationKey,
  type ZLinkLocationChangeStampScope,
  type ZLinkLocationOwnerToken,
  type ZLinkLocationPage,
  type ZLinkLocationWriteResult,
  type ZLinkMeshNodeDescriptor,
  type ZLinkMeshNodeDescriptorKey,
  type ZLinkClientServerServerDescriptor,
  type ZLinkClientServerServerDescriptorKey,
  type ZLinkFanoutLocationStore,
  type ZLinkFanoutPublisherDescriptor,
  type ZLinkFanoutPublisherDescriptorKey,
  type ZLinkRoutingIdSlotAcquireRequest,
  type ZLinkRoutingIdSlotAcquireResult,
  type ZLinkRoutingIdSlotAllocation,
  type ZLinkRoutingIdSlotAllocationMember,
  type ZLinkRoutingIdSlotAllocationSnapshot,
  type ZLinkRoutingIdSlotAllocationStore,
  type ZLinkRoutingIdSlotReleaseResult,
  type ZLinkOwnerLeaseClaimResult,
  type ZLinkOwnerLeaseReadResult,
  type ZLinkOwnerLeaseReleaseResult,
  type ZLinkOwnerLeaseRenewResult,
  type ZLinkObjectAbortRequest,
  type ZLinkObjectAbortResult,
  type ZLinkObjectCommitRequest,
  type ZLinkObjectCommitResult,
  type ZLinkObjectReserveRequest,
  type ZLinkObjectReserveResult,
  type ZLinkPageRequest,
  type ZLinkPeerLocation,
  type ZLinkPeerLocationFilter,
  type ZLinkPeerLocationKey,
  type ZLinkRouteLocation,
  type ZLinkRouteLocationFilter,
  type ZLinkRouteLocationKey,
  type ZLinkRelocationCapacityAbortResult,
  type ZLinkRelocationCapacityFence,
  type ZLinkRelocationCapacityReservationRequest,
  type ZLinkRelocationCapacityReserveResult,
  type ZLinkSpotLocation,
  type ZLinkSpotLocationFilter,
  type ZLinkSpotLocationKey
} from '../../contracts/Locations';
import { ZLinkInMemoryAuthorityStore } from './in-memory-authority-store';
import { ZLinkLocationKeyCodec } from './key-codec';
import {
  matchesActorLocation,
  matchesPeerLocation,
  matchesRouteLocation,
  matchesSpotLocation
} from '../../location-store-integration';

export class ZLinkInMemoryLocationStore implements
  ZLinkLocationStore,
  ZLinkFanoutLocationStore,
  ZLinkLocationChangeStampStore,
  ZLinkRoutingIdSlotAllocationStore {
  private readonly leases = new Map<string, InMemoryOwnerLease>();
  private readonly meshNodes = new RowTable<ZLinkMeshNodeDescriptor>();
  private readonly clientServers = new RowTable<ZLinkClientServerServerDescriptor>();
  private readonly fanoutPublishers = new RowTable<ZLinkFanoutPublisherDescriptor>();
  private readonly peers = new RowTable<ZLinkPeerLocation>();
  private readonly spots = new RowTable<ZLinkSpotLocation>();
  private readonly actors = new RowTable<ZLinkActorLocation>();
  private readonly routes = new RowTable<ZLinkRouteLocation>();
  private readonly stamps = new Map<string, bigint>();
  private readonly routingIdGroups = new Map<string, InMemoryRoutingIdGroup>();
  private readonly actorTransfers = new Map<string, InMemoryActorTransferSlot>();
  private ownerLeaseGeneration = 0n;
  private readonly authority: ZLinkInMemoryAuthorityStore;

  constructor(private readonly now: () => Date = () => new Date()) {
    this.authority = new ZLinkInMemoryAuthorityStore({
      isTargetLive: (key, lifecycleGeneration, owner) => {
        const descriptor = this.meshNodes.rows.get(meshNodeKey(key.meshName, key.rid));
        const lease = this.leases.get(owner.ownerId);
        return descriptor !== undefined
          && descriptor.lifecycleGeneration === lifecycleGeneration
          && descriptor.ownerId === owner.ownerId
          && descriptor.leaseGeneration === owner.leaseGeneration
          && descriptor.objectRole === ZLinkObjectRole.Server
          && descriptor.state === ZLinkFrameworkRuntimeState.Serving
          && lease !== undefined
          && lease.token.leaseGeneration === owner.leaseGeneration
          && lease.leaseExpiresAt.getTime() > this.now().getTime();
      },
      placementCapacityAvailable: (key, objectKind, stableType, delta, pending, active, profile) => {
        const descriptor = this.meshNodes.rows.get(meshNodeKey(key.meshName, key.rid));
        const capability = descriptor?.objectCapabilities.find(candidate =>
          candidate.objectKind === objectKind && candidate.stableType === stableType);
        if (descriptor === undefined || capability === undefined) return false;
        if (profile !== undefined && !capability.placementProfiles.includes(profile)) return false;
        return pending + delta <= descriptor.objectCapacity.maxPendingActivations
          && active + delta <= descriptor.objectCapacity.maxActiveObjects
          && (capability.pendingLimit === undefined || pending + delta <= capability.pendingLimit)
          && (capability.activeLimit === undefined || active + delta <= capability.activeLimit);
      }
    }, now);
  }

  async readAuthority(
    key: ZLinkAuthorityKey,
    signal?: AbortSignal
  ): Promise<ZLinkAuthorityReadResult> {
    return this.authority.readAuthority(key, signal);
  }

  async compareExchangeAuthority(
    key: ZLinkAuthorityKey,
    expectedStoreVersion: ZLinkAuthorityStoreVersion,
    mutation: ZLinkAuthorityMutation,
    signal?: AbortSignal
  ): Promise<ZLinkAuthorityCompareExchangeResult> {
    return this.authority.compareExchangeAuthority(key, expectedStoreVersion, mutation, signal);
  }

  async listAuthorities(
    prefix: string,
    cursor: ZLinkAuthorityScanCursor | undefined,
    limit: number,
    signal?: AbortSignal
  ): Promise<ZLinkAuthorityScanResult> {
    return this.authority.listAuthorities(prefix, cursor, limit, signal);
  }

  async reserve(
    request: ZLinkObjectReserveRequest,
    signal?: AbortSignal
  ): Promise<ZLinkObjectReserveResult> {
    return this.authority.reserve(request, signal);
  }

  async commit(
    request: ZLinkObjectCommitRequest,
    signal?: AbortSignal
  ): Promise<ZLinkObjectCommitResult> {
    return this.authority.commit(request, signal);
  }

  async abort(
    request: ZLinkObjectAbortRequest,
    signal?: AbortSignal
  ): Promise<ZLinkObjectAbortResult> {
    return this.authority.abort(request, signal);
  }

  async reserveRelocationCapacity(
    request: ZLinkRelocationCapacityReservationRequest,
    signal?: AbortSignal
  ): Promise<ZLinkRelocationCapacityReserveResult> {
    return this.authority.reserveRelocationCapacity(request, signal);
  }

  async abortRelocationCapacity(
    fence: ZLinkRelocationCapacityFence,
    signal?: AbortSignal
  ): Promise<ZLinkRelocationCapacityAbortResult> {
    return this.authority.abortRelocationCapacity(fence, signal);
  }

  async prepareAggregate(
    request: ZLinkAggregatePrepareRequest,
    signal?: AbortSignal
  ): Promise<ZLinkAggregatePrepareResult> {
    return this.authority.prepareAggregate(request, signal);
  }

  async commitAggregate(
    fence: ZLinkAggregateFence,
    signal?: AbortSignal
  ): Promise<ZLinkAggregateCommitResult> {
    return this.authority.commitAggregate(fence, signal);
  }

  async abortAggregate(
    fence: ZLinkAggregateFence,
    signal?: AbortSignal
  ): Promise<ZLinkAggregateAbortResult> {
    return this.authority.abortAggregate(fence, signal);
  }

  async updateMeshNode(
    descriptor: ZLinkMeshNodeDescriptor,
    intent: ZLinkLocationWriteIntent
  ): Promise<ZLinkLocationWriteResult> {
    validateMeshNodeDescriptor(descriptor);
    const key = meshNodeKey(descriptor.meshName, descriptor.rid);
    const current = this.meshNodes.rows.get(key);
    const lease = this.leases.get(descriptor.ownerId);
    const updatedAt = this.now();
    if (lease === undefined
      || lease.token.leaseGeneration !== descriptor.leaseGeneration
      || lease.leaseExpiresAt.getTime() <= updatedAt.getTime()) {
      return rejectedConflict();
    }
    if (current === undefined) {
      if (intent !== ZLinkLocationWriteIntent.NewClaim
        && intent !== ZLinkLocationWriteIntent.Takeover) return ignoredStale();
      this.meshNodes.rows.set(key, { ...descriptor, updatedAt });
      this.meshNodes.generations.set(key, 1n);
      this.bump(ZLinkLocationKind.Peer, descriptor.meshName);
      return stored(1n, updatedAt);
    }
    const currentLease = this.leases.get(current.ownerId);
    if ((currentLease === undefined || currentLease.leaseExpiresAt.getTime() <= updatedAt.getTime())
      && (intent === ZLinkLocationWriteIntent.NewClaim
        || intent === ZLinkLocationWriteIntent.Takeover)) {
      const next = (this.meshNodes.generations.get(key) ?? 0n) + 1n;
      this.meshNodes.rows.set(key, { ...descriptor, updatedAt });
      this.meshNodes.generations.set(key, next);
      this.bump(ZLinkLocationKind.Peer, descriptor.meshName);
      return stored(next, updatedAt);
    }
    if (current.ownerId === descriptor.ownerId
      && current.leaseGeneration === descriptor.leaseGeneration
      && current.lifecycleGeneration === descriptor.lifecycleGeneration
      && descriptor.descriptorRevision === current.descriptorRevision
      && meshNodeDescriptorFingerprint(current) === meshNodeDescriptorFingerprint(descriptor)) {
      return stored(this.meshNodes.generations.get(key) ?? 1n, current.updatedAt);
    }
    if (current.ownerId !== descriptor.ownerId
      || current.leaseGeneration !== descriptor.leaseGeneration
      || current.lifecycleGeneration !== descriptor.lifecycleGeneration
      || descriptor.descriptorRevision <= current.descriptorRevision
      || meshNodeImmutableFingerprint(current) !== meshNodeImmutableFingerprint(descriptor)) {
      return ignoredStale();
    }
    this.meshNodes.rows.set(key, { ...descriptor, updatedAt });
    this.bump(ZLinkLocationKind.Peer, descriptor.meshName);
    return stored(this.meshNodes.generations.get(key) ?? 1n, updatedAt);
  }

  async removeMeshNode(
    key: ZLinkMeshNodeDescriptorKey,
    owner: ZLinkLocationOwnerToken
  ): Promise<ZLinkLocationWriteStatus> {
    const encoded = meshNodeKey(key.meshName, key.rid);
    const current = this.meshNodes.rows.get(encoded);
    if (
      current === undefined
      || current.ownerId !== owner.ownerId
      || current.leaseGeneration !== owner.leaseGeneration
    ) {
      return ZLinkLocationWriteStatus.IgnoredStale;
    }
    this.meshNodes.rows.delete(encoded);
    this.bump(ZLinkLocationKind.Peer, key.meshName);
    return ZLinkLocationWriteStatus.Stored;
  }

  async listMeshNodes(meshName: string): Promise<readonly ZLinkMeshNodeDescriptor[]> {
    return [...this.meshNodes.rows.values()].filter((row) => row.meshName === meshName);
  }

  async updateClientServer(
    descriptor: ZLinkClientServerServerDescriptor,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    signal?.throwIfAborted();
    validateClientServerDescriptor(descriptor);
    const key = clientServerKey(descriptor.channelName, descriptor.serverRid);
    const current = this.clientServers.rows.get(key);
    const lease = this.leases.get(descriptor.ownerId);
    const updatedAt = this.now();
    if (lease === undefined
      || lease.token.leaseGeneration !== descriptor.leaseGeneration
      || lease.leaseExpiresAt.getTime() <= updatedAt.getTime()) {
      return rejectedConflict();
    }
    if (current === undefined) {
      if (intent !== ZLinkLocationWriteIntent.NewClaim
        && intent !== ZLinkLocationWriteIntent.Takeover) return ignoredStale();
      const generation = (this.clientServers.generations.get(key) ?? 0n) + 1n;
      this.clientServers.rows.set(key, { ...descriptor, updatedAt });
      this.clientServers.generations.set(key, generation);
      this.bump(ZLinkLocationKind.ClientServer, descriptor.channelName);
      return stored(generation, updatedAt);
    }
    const currentLease = this.leases.get(current.ownerId);
    if ((currentLease === undefined || currentLease.leaseExpiresAt.getTime() <= updatedAt.getTime())
      && (intent === ZLinkLocationWriteIntent.NewClaim
        || intent === ZLinkLocationWriteIntent.Takeover)) {
      const generation = (this.clientServers.generations.get(key) ?? 0n) + 1n;
      this.clientServers.rows.set(key, { ...descriptor, updatedAt });
      this.clientServers.generations.set(key, generation);
      this.bump(ZLinkLocationKind.ClientServer, descriptor.channelName);
      return stored(generation, updatedAt);
    }
    if (clientServerDescriptorFingerprint(current) === clientServerDescriptorFingerprint(descriptor)) {
      return stored(this.clientServers.generations.get(key) ?? 1n, current.updatedAt);
    }
    if (current.ownerId !== descriptor.ownerId
      || current.leaseGeneration !== descriptor.leaseGeneration
      || current.lifecycleGeneration !== descriptor.lifecycleGeneration
      || descriptor.descriptorRevision <= current.descriptorRevision
      || clientServerImmutableFingerprint(current) !== clientServerImmutableFingerprint(descriptor)) {
      return ignoredStale();
    }
    this.clientServers.rows.set(key, { ...descriptor, updatedAt });
    this.bump(ZLinkLocationKind.ClientServer, descriptor.channelName);
    return stored(this.clientServers.generations.get(key) ?? 1n, updatedAt);
  }

  async removeClientServer(
    key: ZLinkClientServerServerDescriptorKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteStatus> {
    signal?.throwIfAborted();
    const encoded = clientServerKey(key.channelName, key.serverRid);
    const current = this.clientServers.rows.get(encoded);
    if (current === undefined
      || current.ownerId !== owner.ownerId
      || current.leaseGeneration !== owner.leaseGeneration) {
      return ZLinkLocationWriteStatus.IgnoredStale;
    }
    this.clientServers.rows.delete(encoded);
    this.bump(ZLinkLocationKind.ClientServer, key.channelName);
    return ZLinkLocationWriteStatus.Stored;
  }

  async listClientServers(
    channelName: string,
    page: ZLinkPageRequest = {},
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkClientServerServerDescriptor>> {
    signal?.throwIfAborted();
    return pageRows(this.clientServers, (row) => row.channelName === channelName, page);
  }

  async updateFanoutPublisher(
    descriptor: ZLinkFanoutPublisherDescriptor,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    signal?.throwIfAborted();
    validateFanoutPublisherDescriptor(descriptor);
    const key = fanoutPublisherKey(descriptor.channelName, descriptor.publisherRid);
    const current = this.fanoutPublishers.rows.get(key);
    const lease = this.leases.get(descriptor.ownerId);
    const updatedAt = this.now();
    if (lease === undefined
      || lease.token.leaseGeneration !== descriptor.leaseGeneration
      || lease.leaseExpiresAt.getTime() <= updatedAt.getTime()) {
      return rejectedConflict();
    }
    if (current === undefined) {
      if (intent !== ZLinkLocationWriteIntent.NewClaim
        && intent !== ZLinkLocationWriteIntent.Takeover) return ignoredStale();
      const generation = (this.fanoutPublishers.generations.get(key) ?? 0n) + 1n;
      this.fanoutPublishers.rows.set(key, { ...descriptor, updatedAt });
      this.fanoutPublishers.generations.set(key, generation);
      return stored(generation, updatedAt);
    }
    const currentLease = this.leases.get(current.ownerId);
    if ((currentLease === undefined || currentLease.leaseExpiresAt.getTime() <= updatedAt.getTime())
      && (intent === ZLinkLocationWriteIntent.NewClaim
        || intent === ZLinkLocationWriteIntent.Takeover)) {
      const generation = (this.fanoutPublishers.generations.get(key) ?? 0n) + 1n;
      this.fanoutPublishers.rows.set(key, { ...descriptor, updatedAt });
      this.fanoutPublishers.generations.set(key, generation);
      return stored(generation, updatedAt);
    }
    if (fanoutPublisherDescriptorFingerprint(current)
      === fanoutPublisherDescriptorFingerprint(descriptor)) {
      return stored(this.fanoutPublishers.generations.get(key) ?? 1n, current.updatedAt);
    }
    if (current.ownerId !== descriptor.ownerId
      || current.leaseGeneration !== descriptor.leaseGeneration
      || current.lifecycleGeneration !== descriptor.lifecycleGeneration
      || descriptor.descriptorRevision <= current.descriptorRevision
      || fanoutPublisherImmutableFingerprint(current)
        !== fanoutPublisherImmutableFingerprint(descriptor)) {
      return ignoredStale();
    }
    this.fanoutPublishers.rows.set(key, { ...descriptor, updatedAt });
    return stored(this.fanoutPublishers.generations.get(key) ?? 1n, updatedAt);
  }

  async removeFanoutPublisher(
    key: ZLinkFanoutPublisherDescriptorKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteStatus> {
    signal?.throwIfAborted();
    const encoded = fanoutPublisherKey(key.channelName, key.publisherRid);
    const current = this.fanoutPublishers.rows.get(encoded);
    if (current === undefined
      || current.ownerId !== owner.ownerId
      || current.leaseGeneration !== owner.leaseGeneration) {
      return ZLinkLocationWriteStatus.IgnoredStale;
    }
    this.fanoutPublishers.rows.delete(encoded);
    return ZLinkLocationWriteStatus.Stored;
  }

  async listFanoutPublishers(
    channelName: string,
    page: ZLinkPageRequest = {},
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkFanoutPublisherDescriptor>> {
    signal?.throwIfAborted();
    const storeNow = this.now();
    return pageRows(
      this.fanoutPublishers,
      (row) => row.channelName === channelName && this.isOwnerLive(row.ownerId, storeNow),
      page
    );
  }

  async acquireRoutingIdSlot(
    request: ZLinkRoutingIdSlotAcquireRequest,
    signal?: AbortSignal
  ): Promise<ZLinkRoutingIdSlotAcquireResult> {
    signal?.throwIfAborted();
    validateRoutingIdSlotAcquireRequest(request);
    const storeNow = this.now();
    const members = normalizeRoutingIdMembers(request.members);
    let group = this.routingIdGroups.get(request.groupName);
    if (group === undefined) {
      const memberNames = new Set(members.map((member) => member.meshName));
      if ([...this.peers.rows.values()].some((peer) =>
        memberNames.has(peer.meshName) && this.isOwnerLive(peer.ownerId, storeNow))) {
        return { kind: 'identityModeConflict' };
      }
      group = {
        members,
        slotCount: request.slotCount,
        allocations: new Map(),
        generations: new Map()
      };
      this.routingIdGroups.set(request.groupName, group);
    } else if (group.slotCount !== request.slotCount || !sameRoutingIdMembers(group.members, members)) {
      return {
        kind: 'groupConfigurationMismatch',
        expectedMembers: group.members,
        expectedSlotCount: group.slotCount,
        actualMembers: members,
        actualSlotCount: request.slotCount
      };
    }

    removeExpiredRoutingIdAllocations(group, this.leases, storeNow);
    for (const [slot, current] of group.allocations) {
      if (current.owner.ownerId !== request.ownerId) continue;
      const allocation = routingIdAllocation(
        slot,
        current.owner,
        storeNow,
        request.leaseTtlMs
      );
      group.allocations.set(slot, allocation);
      this.renewExistingAllocationOwnerLease(request.ownerId, request.leaseTtlMs, storeNow);
      return { kind: 'acquired', allocation };
    }

    for (let slot = 1; slot <= group.slotCount; slot += 1) {
      if (group.allocations.has(slot)) continue;
      const generation = (group.generations.get(slot) ?? 0n) + 1n;
      group.generations.set(slot, generation);
      const allocation = routingIdAllocation(
        slot,
        { ownerId: request.ownerId, leaseGeneration: generation },
        storeNow,
        request.leaseTtlMs
      );
      group.allocations.set(slot, allocation);
      return { kind: 'acquired', allocation };
    }
    return { kind: 'groupExhausted' };
  }

  async releaseRoutingIdSlot(
    groupName: string,
    slot: number,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkRoutingIdSlotReleaseResult> {
    signal?.throwIfAborted();
    validateRoutingIdSlotRelease(groupName, slot, owner);
    const group = this.routingIdGroups.get(groupName);
    const current = group?.allocations.get(slot);
    if (current === undefined
      || current.owner.ownerId !== owner.ownerId
      || current.owner.leaseGeneration !== owner.leaseGeneration) {
      return 'ignoredStale';
    }
    group?.allocations.delete(slot);
    return 'released';
  }

  async listRoutingIdSlots(
    groupName: string,
    signal?: AbortSignal
  ): Promise<ZLinkRoutingIdSlotAllocationSnapshot> {
    signal?.throwIfAborted();
    validateRoutingIdGroupName(groupName);
    const storeNow = this.now();
    const group = this.routingIdGroups.get(groupName);
    if (group === undefined) {
      return { groupName, members: [], slotCount: 0, allocations: [], storeNow };
    }
    removeExpiredRoutingIdAllocations(group, this.leases, storeNow);
    return {
      groupName,
      members: group.members,
      slotCount: group.slotCount,
      allocations: [...group.allocations.values()].sort((left, right) => left.slot - right.slot),
      storeNow
    };
  }

  async updatePeer(
    peer: ZLinkPeerLocation,
    intent: ZLinkLocationWriteIntent
  ): Promise<ZLinkLocationWriteResult> {
    return this.write(
      this.peers,
      ZLinkLocationKeyCodec.encodePeerKey({
        autoConnectType: peer.autoConnectType,
        meshName: peer.meshName,
        role: peer.role,
        nodeRid: peer.nodeRid,
        endpoint: peer.endpoint
      }),
      peer,
      intent,
      peer.ownerId,
      peer.generation,
      (row) => row.ownerId,
      (row) => row.generation,
      (row, generation, updatedAt) => ({ ...row, generation, updatedAt }),
      ZLinkLocationKind.Peer,
      peer.meshName
    );
  }

  async removePeer(key: ZLinkPeerLocationKey, owner: ZLinkLocationOwnerToken): Promise<ZLinkLocationWriteResult> {
    return this.remove(
      this.peers,
      ZLinkLocationKeyCodec.encodePeerKey(key),
      owner,
      (row) => row.ownerId,
      (row) => row.generation,
      ZLinkLocationKind.Peer,
      key.meshName
    );
  }

  async listPeers(filter: ZLinkPeerLocationFilter): Promise<readonly ZLinkPeerLocation[]> {
    return [...this.peers.rows.values()].filter((row) => matchesPeerLocation(row, filter));
  }

  async updateSpot(
    spot: ZLinkSpotLocation,
    intent: ZLinkLocationWriteIntent
  ): Promise<ZLinkLocationWriteResult> {
    const key = ZLinkLocationKeyCodec.encodeSpotKey({ meshName: spot.meshName, spotRid: spot.spotRid });
    const updatedAt = this.now();
    const current = this.spots.rows.get(key);
    if (intent === ZLinkLocationWriteIntent.NewClaim
      && current !== undefined
      && this.isOwnerLive(current.ownerId, updatedAt)) {
      return rejectedConflict();
    }
    if (intent === ZLinkLocationWriteIntent.Renew) {
      if (current === undefined || current.ownerId !== spot.ownerId) return ignoredStale();
      this.spots.rows.set(key, { ...spot, updatedAt });
      this.bump(ZLinkLocationKind.Spot, spot.meshName);
      return stored(this.spots.generations.get(key) ?? 0n, updatedAt);
    }
    const generation = (this.spots.generations.get(key) ?? 0n) + 1n;
    this.spots.generations.set(key, generation);
    this.spots.rows.set(key, { ...spot, updatedAt });
    this.bump(ZLinkLocationKind.Spot, spot.meshName);
    return stored(generation, updatedAt);
  }

  async removeSpot(key: ZLinkSpotLocationKey, owner: ZLinkLocationOwnerToken): Promise<ZLinkLocationWriteStatus> {
    const encoded = ZLinkLocationKeyCodec.encodeSpotKey(key);
    const current = this.spots.rows.get(encoded);
    if (current === undefined
      || current.ownerId !== owner.ownerId
      || this.spots.generations.get(encoded) !== owner.leaseGeneration) {
      return ZLinkLocationWriteStatus.IgnoredStale;
    }
    this.spots.rows.delete(encoded);
    this.bump(ZLinkLocationKind.Spot, key.meshName);
    return ZLinkLocationWriteStatus.Stored;
  }

  async resolveSpot(key: ZLinkSpotLocationKey): Promise<ZLinkSpotLocation | undefined> {
    return this.spots.rows.get(ZLinkLocationKeyCodec.encodeSpotKey(key));
  }

  async listSpots(
    filter: ZLinkSpotLocationFilter,
    page: ZLinkPageRequest = {}
  ): Promise<ZLinkLocationPage<ZLinkSpotLocation>> {
    return pageRows(this.spots, (row) => matchesSpotLocation(row, filter), page);
  }

  async updateActor(
    actor: ZLinkActorLocation,
    intent: ZLinkLocationWriteIntent
  ): Promise<ZLinkLocationWriteResult> {
    const key = ZLinkLocationKeyCodec.encodeActorKey({ meshName: actor.meshName, actorId: actor.actorId });
    const updatedAt = this.now();
    const current = this.actors.rows.get(key);
    if (intent === ZLinkLocationWriteIntent.NewClaim
      && current !== undefined
      && this.isOwnerLive(current.ownerId, updatedAt)) {
      return rejectedConflict();
    }
    if (intent === ZLinkLocationWriteIntent.Renew) {
      if (current === undefined || current.ownerId !== actor.ownerId) return ignoredStale();
      this.actors.rows.set(key, { ...actor, updatedAt });
      this.bump(ZLinkLocationKind.Actor, actor.meshName);
      return stored(this.actors.generations.get(key) ?? 0n, updatedAt);
    }
    const generation = (this.actors.generations.get(key) ?? 0n) + 1n;
    this.actors.generations.set(key, generation);
    this.actors.rows.set(key, { ...actor, updatedAt });
    this.bump(ZLinkLocationKind.Actor, actor.meshName);
    return stored(generation, updatedAt);
  }

  async removeActor(key: ZLinkActorLocationKey, owner: ZLinkLocationOwnerToken): Promise<ZLinkLocationWriteStatus> {
    const encoded = ZLinkLocationKeyCodec.encodeActorKey(key);
    const current = this.actors.rows.get(encoded);
    if (current === undefined
      || current.ownerId !== owner.ownerId
      || this.actors.generations.get(encoded) !== owner.leaseGeneration) {
      return ZLinkLocationWriteStatus.IgnoredStale;
    }
    this.actors.rows.delete(encoded);
    this.bump(ZLinkLocationKind.Actor, key.meshName);
    return ZLinkLocationWriteStatus.Stored;
  }

  async resolveActor(key: ZLinkActorLocationKey): Promise<ZLinkActorLocation | undefined> {
    return this.actors.rows.get(ZLinkLocationKeyCodec.encodeActorKey(key));
  }

  async listActors(
    filter: ZLinkActorLocationFilter,
    page: ZLinkPageRequest = {}
  ): Promise<ZLinkLocationPage<ZLinkActorLocation>> {
    return pageRows(this.actors, (row) => matchesActorLocation(row, filter), page);
  }

  async updateRoute(
    route: ZLinkRouteLocation,
    intent: ZLinkLocationWriteIntent
  ): Promise<ZLinkLocationWriteResult> {
    return this.write(
      this.routes,
      ZLinkLocationKeyCodec.encodeRouteKey({ routeKind: route.routeKind, routeKey: route.routeKey }),
      route,
      intent,
      route.ownerId,
      route.generation,
      (row) => row.ownerId,
      (row) => row.generation,
      (row, generation, updatedAt) => ({ ...row, generation, updatedAt }),
      ZLinkLocationKind.Route,
      undefined
    );
  }

  async removeRoute(key: ZLinkRouteLocationKey, owner: ZLinkLocationOwnerToken): Promise<ZLinkLocationWriteResult> {
    return this.remove(
      this.routes,
      ZLinkLocationKeyCodec.encodeRouteKey(key),
      owner,
      (row) => row.ownerId,
      (row) => row.generation,
      ZLinkLocationKind.Route,
      undefined
    );
  }

  async resolveRoute(key: ZLinkRouteLocationKey): Promise<ZLinkRouteLocation | undefined> {
    return this.routes.rows.get(ZLinkLocationKeyCodec.encodeRouteKey(key));
  }

  async listRoutes(
    filter: ZLinkRouteLocationFilter,
    page: ZLinkPageRequest = {}
  ): Promise<ZLinkLocationPage<ZLinkRouteLocation>> {
    return pageRows(this.routes, (row) => matchesRouteLocation(row, filter), page);
  }

  async prepareActorTransfer(
    request: ZLinkActorTransferPrepareRequest,
    signal?: AbortSignal
  ): Promise<ZLinkActorTransferWriteResult> {
    signal?.throwIfAborted();
    validateTransferRequest(request);
    const key = actorTransferKey(request.meshName, request.actorId);
    let slot = this.actorTransfers.get(key);
    if (slot === undefined) {
      slot = { records: new Map() };
      this.actorTransfers.set(key, slot);
    }
    if (slot.activeTransferId !== undefined) {
      const existing = slot.records.get(slot.activeTransferId);
      if (slot.activeTransferId === request.transferId && existing?.state === 'prepared') {
        return transferStored(existing);
      }
      return transferResult('rejectedConflict');
    }
    const updatedAt = this.now();
    const record: ZLinkActorTransferRecord = {
      meshName: request.meshName,
      actorId: request.actorId,
      transferId: request.transferId,
      source: request.source,
      target: request.target,
      expectedActorGeneration: request.expectedActorGeneration,
      expectedMembershipEpoch: request.expectedMembershipEpoch,
      participants: new Set(request.participants),
      state: 'prepared',
      recoveryOwnerId: request.recoveryOwnerId,
      recoveryLeaseExpiresAt: new Date(updatedAt.getTime() + request.recoveryLeaseTtlMs),
      updatedAt
    };
    slot.records.set(request.transferId, record);
    slot.activeTransferId = request.transferId;
    return transferStored(record);
  }

  async commitActorTransfer(
    meshName: string,
    actorId: string,
    transferId: string,
    recoveryOwnerId: string,
    signal?: AbortSignal
  ): Promise<ZLinkActorTransferWriteResult> {
    signal?.throwIfAborted();
    return this.transitionActorTransfer(
      meshName, actorId, transferId, recoveryOwnerId, 'prepared', 'committed', false
    );
  }

  async activateActorTransfer(
    meshName: string,
    actorId: string,
    transferId: string,
    recoveryOwnerId: string,
    signal?: AbortSignal
  ): Promise<ZLinkActorTransferWriteResult> {
    signal?.throwIfAborted();
    return this.transitionActorTransfer(
      meshName, actorId, transferId, recoveryOwnerId, 'committed', 'activated', true
    );
  }

  async abortActorTransfer(
    meshName: string,
    actorId: string,
    transferId: string,
    recoveryOwnerId: string,
    signal?: AbortSignal
  ): Promise<ZLinkActorTransferWriteResult> {
    signal?.throwIfAborted();
    return this.transitionActorTransfer(
      meshName, actorId, transferId, recoveryOwnerId, 'prepared', 'aborted', true
    );
  }

  async takeOverActorTransfer(
    meshName: string,
    actorId: string,
    transferId: string,
    successorOwnerId: string,
    recoveryLeaseTtlMs: number,
    signal?: AbortSignal
  ): Promise<ZLinkActorTransferWriteResult> {
    signal?.throwIfAborted();
    validateTransferIdentity(meshName, actorId, transferId);
    validateTransferLease(successorOwnerId, recoveryLeaseTtlMs);
    const record = this.actorTransfers.get(actorTransferKey(meshName, actorId))?.records.get(transferId);
    if (record === undefined) return transferResult('notFound');
    if (record.state !== 'prepared' && record.state !== 'committed') {
      return transferResult('invalidState');
    }
    const updatedAt = this.now();
    if (record.recoveryLeaseExpiresAt.getTime() > updatedAt.getTime()) {
      return transferResult('rejectedConflict');
    }
    const updated: ZLinkActorTransferRecord = {
      ...record,
      recoveryOwnerId: successorOwnerId,
      recoveryLeaseExpiresAt: new Date(updatedAt.getTime() + recoveryLeaseTtlMs),
      updatedAt
    };
    this.actorTransfers.get(actorTransferKey(meshName, actorId))?.records.set(transferId, updated);
    return transferStored(updated);
  }

  async resolveActorTransfer(
    meshName: string,
    actorId: string,
    signal?: AbortSignal
  ): Promise<ZLinkActorTransferRecord | undefined> {
    signal?.throwIfAborted();
    const slot = this.actorTransfers.get(actorTransferKey(meshName, actorId));
    return slot?.activeTransferId === undefined ? undefined : slot.records.get(slot.activeTransferId);
  }

  async claimOwnerLease(
    ownerId: string,
    leaseTtlMs: number,
    signal?: AbortSignal
  ): Promise<ZLinkOwnerLeaseClaimResult> {
    signal?.throwIfAborted();
    validateOwnerLeaseInput(ownerId, leaseTtlMs);
    const storeNow = this.now();
    const current = this.leases.get(ownerId);
    if (current !== undefined && current.leaseExpiresAt.getTime() > storeNow.getTime()) {
      return { kind: 'conflict' };
    }
    if (this.ownerLeaseGeneration >= 0x7fff_ffff_ffff_ffffn) {
      return { kind: 'generationExhausted' };
    }
    const token = { ownerId, leaseGeneration: ++this.ownerLeaseGeneration };
    const leaseExpiresAt = new Date(storeNow.getTime() + leaseTtlMs);
    this.leases.set(ownerId, { token, leaseExpiresAt });
    return { kind: 'claimed', token, leaseExpiresAt, storeNow };
  }

  async readOwnerLease(
    ownerId: string,
    signal?: AbortSignal
  ): Promise<ZLinkOwnerLeaseReadResult> {
    signal?.throwIfAborted();
    if (ownerId.trim().length === 0) throw new TypeError('ownerId is required.');
    const storeNow = this.now();
    const current = this.leases.get(ownerId);
    if (current === undefined || current.leaseExpiresAt.getTime() <= storeNow.getTime()) {
      this.leases.delete(ownerId);
      return { kind: 'missing' };
    }
    return {
      kind: 'found',
      token: { ...current.token },
      leaseExpiresAt: new Date(current.leaseExpiresAt),
      storeNow
    };
  }

  async renewOwnerLease(
    token: ZLinkLocationOwnerToken,
    leaseTtlMs: number,
    signal?: AbortSignal
  ): Promise<ZLinkOwnerLeaseRenewResult> {
    signal?.throwIfAborted();
    validateOwnerLeaseInput(token.ownerId, leaseTtlMs);
    const storeNow = this.now();
    const current = this.leases.get(token.ownerId);
    if (
      current === undefined
      || current.leaseExpiresAt.getTime() <= storeNow.getTime()
      || current.token.leaseGeneration !== token.leaseGeneration
    ) {
      return { kind: 'stale' };
    }
    const leaseExpiresAt = new Date(storeNow.getTime() + leaseTtlMs);
    this.leases.set(token.ownerId, { token: { ...token }, leaseExpiresAt });
    return { kind: 'renewed', leaseExpiresAt, storeNow };
  }

  async releaseOwnerLease(
    token: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkOwnerLeaseReleaseResult> {
    signal?.throwIfAborted();
    const current = this.leases.get(token.ownerId);
    if (current === undefined || current.token.leaseGeneration !== token.leaseGeneration) {
      return 'stale';
    }
    this.leases.delete(token.ownerId);
    return 'released';
  }

  async removeAllByOwner(owner: ZLinkLocationOwnerToken): Promise<bigint> {
    const ownerId = owner.ownerId;
    let removed = 0;
    removed += this.removeByOwner(this.peers, ownerId, (row) => row.ownerId, ZLinkLocationKind.Peer, (row) => row.meshName);
    removed += this.removeByOwner(this.spots, ownerId, (row) => row.ownerId, ZLinkLocationKind.Spot, (row) => row.meshName);
    removed += this.removeByOwner(this.actors, ownerId, (row) => row.ownerId, ZLinkLocationKind.Actor, () => undefined);
    removed += this.removeByOwner(this.routes, ownerId, (row) => row.ownerId, ZLinkLocationKind.Route, () => undefined);
    removed += this.removeByOwner(
      this.clientServers,
      ownerId,
      (row) => row.ownerId,
      ZLinkLocationKind.ClientServer,
      (row) => row.channelName
    );
    removed += this.removeFanoutByOwner(owner);
    return BigInt(removed);
  }

  async getChangeStamp(scope: ZLinkLocationChangeStampScope): Promise<bigint> {
    return this.stamps.get(stampKey(scope)) ?? 0n;
  }

  private write<TRow>(
    table: RowTable<TRow>,
    key: string,
    row: TRow,
    intent: ZLinkLocationWriteIntent,
    ownerId: string,
    generation: bigint,
    ownerOf: (row: TRow) => string,
    generationOf: (row: TRow) => bigint,
    finalize: (row: TRow, generation: bigint, updatedAt: Date) => TRow,
    kind: ZLinkLocationKind,
    meshName: string | undefined
  ): ZLinkLocationWriteResult {
    const updatedAt = this.now();
    const current = table.rows.get(key);

    if (intent === ZLinkLocationWriteIntent.NewClaim
      && current !== undefined
      && this.isOwnerLive(ownerOf(current), updatedAt)) {
      return rejectedConflict();
    }

    if (intent === ZLinkLocationWriteIntent.NewClaim || intent === ZLinkLocationWriteIntent.Takeover) {
      const next = (table.generations.get(key) ?? 0n) + 1n;
      table.generations.set(key, next);
      table.rows.set(key, finalize(row, next, updatedAt));
      this.bump(kind, meshName);
      return stored(next, updatedAt);
    }

    if (current !== undefined
      && ownerOf(current) === ownerId
      && (generation === 0n || generationOf(current) === generation)) {
      table.rows.set(key, finalize(row, generation, updatedAt));
      this.bump(kind, meshName);
      return stored(table.generations.get(key) ?? generation, updatedAt);
    }

    return ignoredStale();
  }

  private transitionActorTransfer(
    meshName: string,
    actorId: string,
    transferId: string,
    recoveryOwnerId: string,
    requiredState: ZLinkActorTransferState,
    nextState: ZLinkActorTransferState,
    clearActive: boolean
  ): ZLinkActorTransferWriteResult {
    validateTransferIdentity(meshName, actorId, transferId);
    const slot = this.actorTransfers.get(actorTransferKey(meshName, actorId));
    const record = slot?.records.get(transferId);
    if (record === undefined || slot?.activeTransferId !== transferId) {
      return transferResult('notFound');
    }
    if (record.state !== requiredState) return transferResult('invalidState');
    if (record.recoveryOwnerId !== recoveryOwnerId) return transferResult('rejectedConflict');
    const updated: ZLinkActorTransferRecord = {
      ...record,
      state: nextState,
      updatedAt: this.now()
    };
    slot.records.set(transferId, updated);
    if (clearActive) slot.activeTransferId = undefined;
    return transferStored(updated);
  }

  private remove<TRow>(
    table: RowTable<TRow>,
    key: string,
    owner: ZLinkLocationOwnerToken,
    ownerOf: (row: TRow) => string,
    generationOf: (row: TRow) => bigint,
    kind: ZLinkLocationKind,
    meshName: string | undefined
  ): ZLinkLocationWriteResult {
    const current = table.rows.get(key);
    if (current === undefined || ownerOf(current) !== owner.ownerId
      || generationOf(current) !== owner.leaseGeneration) {
      return ignoredStale();
    }

    table.rows.delete(key);
    this.bump(kind, meshName);
    return stored(owner.leaseGeneration, this.now());
  }

  private removeByOwner<TRow>(
    table: RowTable<TRow>,
    ownerId: string,
    ownerOf: (row: TRow) => string,
    kind: ZLinkLocationKind,
    meshOf: (row: TRow) => string | undefined
  ): number {
    let removed = 0;
    for (const [key, row] of [...table.rows.entries()]) {
      if (ownerOf(row) === ownerId) {
        table.rows.delete(key);
        this.bump(kind, meshOf(row));
        removed++;
      }
    }
    return removed;
  }

  private isOwnerLive(ownerId: string, now: Date): boolean {
    const lease = this.leases.get(ownerId);
    return lease !== undefined && lease.leaseExpiresAt.getTime() > now.getTime();
  }

  private renewExistingAllocationOwnerLease(ownerId: string, leaseTtlMs: number, now: Date): void {
    const lease = this.leases.get(ownerId);
    if (lease === undefined) return;
    this.leases.set(ownerId, {
      ...lease,
      leaseExpiresAt: new Date(now.getTime() + leaseTtlMs)
    });
  }

  private bump(kind: ZLinkLocationKind, meshName: string | undefined): void {
    this.bumpScope({ kind, meshName });
    if (meshName !== undefined) {
      this.bumpScope({ kind });
    }
  }

  private removeFanoutByOwner(owner: ZLinkLocationOwnerToken): number {
    let removed = 0;
    for (const [key, row] of this.fanoutPublishers.rows) {
      if (row.ownerId !== owner.ownerId || row.leaseGeneration !== owner.leaseGeneration) continue;
      this.fanoutPublishers.rows.delete(key);
      removed++;
    }
    return removed;
  }

  private bumpScope(scope: ZLinkLocationChangeStampScope): void {
    const key = stampKey(scope);
    this.stamps.set(key, (this.stamps.get(key) ?? 0n) + 1n);
  }
}

class RowTable<TRow> {
  readonly rows = new Map<string, TRow>();
  readonly generations = new Map<string, bigint>();
}

function validateMeshNodeDescriptor(descriptor: ZLinkMeshNodeDescriptor): void {
  if (!validDescriptorText(descriptor.meshName) || !validDescriptorText(descriptor.ownerId)) {
    throw new TypeError('MeshNode descriptor identity is required.');
  }
  const maxGeneration = 0x7fff_ffff_ffff_ffffn;
  if (descriptor.lifecycleGeneration < 1n || descriptor.lifecycleGeneration > maxGeneration
    || descriptor.descriptorRevision < 1n || descriptor.descriptorRevision > maxGeneration
    || descriptor.leaseGeneration < 1n || descriptor.leaseGeneration > maxGeneration
    || descriptor.applicationVersion < 0n || descriptor.applicationVersion > maxGeneration) {
    throw new RangeError('MeshNode descriptor generations are invalid.');
  }
  const values = [
    descriptor.objectCapacity.activeObjects,
    descriptor.objectCapacity.pendingActivations,
    descriptor.objectCapacity.maxActiveObjects,
    descriptor.objectCapacity.maxPendingActivations
  ];
  if (values.some(value => !Number.isSafeInteger(value) || value < 0)
    || descriptor.objectCapacity.activeObjects > descriptor.objectCapacity.maxActiveObjects
    || descriptor.objectCapacity.pendingActivations > descriptor.objectCapacity.maxPendingActivations
    || descriptor.objectCapacity.maxActiveObjects === 0
    || descriptor.objectCapacity.maxPendingActivations === 0
    || descriptor.placementWeight < 0
    || descriptor.placementWeight > 100
    || descriptor.objectCapabilities.length > 1024
    || (descriptor.objectRole !== ZLinkObjectRole.Server
      && descriptor.objectCapabilities.length !== 0)) {
    throw new RangeError('MeshNode object capacity is invalid.');
  }
  const keys = new Set<string>();
  for (const capability of descriptor.objectCapabilities) {
    if (!validDescriptorText(capability.stableType)) {
      throw new TypeError('MeshNode capability stable type is required.');
    }
    const key = `${capability.objectKind}\0${capability.stableType}`;
    if (keys.has(key)) throw new TypeError('MeshNode object capabilities must be unique.');
    keys.add(key);
    if ((capability.policy === 'snapshot') !== capability.hasSnapshotAdapter) {
      throw new TypeError('Snapshot adapter presence does not match the maintenance policy.');
    }
    for (const limit of [capability.activeLimit, capability.pendingLimit]) {
      if (limit !== undefined && (!Number.isSafeInteger(limit) || limit <= 0)) {
        throw new RangeError('MeshNode capability limits must be non-negative safe integers.');
      }
    }
    if (capability.placementProfiles.length > 1024
      || new Set(capability.placementProfiles).size !== capability.placementProfiles.length
      || capability.placementProfiles.some(profile => !validDescriptorText(profile))) {
      throw new TypeError('MeshNode placement profiles must be unique non-empty values.');
    }
  }
}

function validDescriptorText(value: string): boolean {
  const size = Buffer.byteLength(value, 'utf8');
  return size >= 1 && size <= 255 && !value.includes('\0');
}

function meshNodeImmutableFingerprint(descriptor: ZLinkMeshNodeDescriptor): string {
  const capabilities = [...descriptor.objectCapabilities]
    .map(capability => ({
      ...capability,
      placementProfiles: [...capability.placementProfiles].sort()
    }))
    .sort((left, right) => {
      const byKind = left.objectKind.localeCompare(right.objectKind);
      return byKind !== 0 ? byKind : left.stableType.localeCompare(right.stableType);
    });
  return JSON.stringify({
    meshName: descriptor.meshName,
    rid: String(descriptor.rid),
    lifecycleGeneration: descriptor.lifecycleGeneration.toString(),
    endpoint: descriptor.endpoint,
    objectRole: descriptor.objectRole,
    maxActiveObjects: descriptor.objectCapacity.maxActiveObjects,
    maxPendingActivations: descriptor.objectCapacity.maxPendingActivations,
    channelNames: Object.keys(descriptor.channelWeights).sort(),
    applicationVersion: descriptor.applicationVersion.toString(),
    spotTypes: [...descriptor.spotTypes].sort(),
    objectCapabilities: capabilities,
    securityIdentity: descriptor.securityIdentity,
    ownerId: descriptor.ownerId,
    leaseGeneration: descriptor.leaseGeneration.toString()
  });
}

function meshNodeDescriptorFingerprint(descriptor: ZLinkMeshNodeDescriptor): string {
  return JSON.stringify({
    immutable: meshNodeImmutableFingerprint(descriptor),
    descriptorRevision: descriptor.descriptorRevision.toString(),
    placementWeight: descriptor.placementWeight,
    objectCapacity: descriptor.objectCapacity,
    channelWeights: Object.fromEntries(
      Object.entries(descriptor.channelWeights).sort(([left], [right]) => left.localeCompare(right))
    ),
    maintenanceWave: descriptor.maintenanceWave ?? null,
    state: descriptor.state
  });
}

interface InMemoryRoutingIdGroup {
  readonly members: readonly ZLinkRoutingIdSlotAllocationMember[];
  readonly slotCount: number;
  readonly allocations: Map<number, ZLinkRoutingIdSlotAllocation>;
  readonly generations: Map<number, bigint>;
}

interface InMemoryActorTransferSlot {
  readonly records: Map<string, ZLinkActorTransferRecord>;
  activeTransferId?: string;
}

interface InMemoryOwnerLease {
  readonly token: ZLinkLocationOwnerToken;
  readonly leaseExpiresAt: Date;
}

const TRANSFER_ID_PATTERN = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;

function meshNodeKey(meshName: string, rid: RoutingId): string {
  const value = typeof rid === 'string'
    ? rid
    : (rid as unknown as { toHex(): string }).toHex();
  return `${meshName.length}:${meshName}:${value.length}:${value}`;
}

function clientServerKey(channelName: string, serverRid: RoutingId): string {
  const value = typeof serverRid === 'string'
    ? serverRid
    : (serverRid as unknown as { toHex(): string }).toHex();
  return `${channelName.length}:${channelName}:${value.length}:${value}`;
}

function fanoutPublisherKey(channelName: string, publisherRid: RoutingId): string {
  const value = typeof publisherRid === 'string'
    ? publisherRid
    : (publisherRid as unknown as { toHex(): string }).toHex();
  return `${channelName.length}:${channelName}:${value.length}:${value}`;
}

function validateClientServerDescriptor(descriptor: ZLinkClientServerServerDescriptor): void {
  if (!validDescriptorText(descriptor.channelName)
    || !validDescriptorText(String(descriptor.serverRid))
    || !validDescriptorText(descriptor.endpoint)
    || !validDescriptorText(descriptor.securityIdentity)
    || !validDescriptorText(descriptor.ownerId)) {
    throw new TypeError('ClientServer descriptor identity and endpoint are required.');
  }
  const maxGeneration = 0x7fff_ffff_ffff_ffffn;
  if (descriptor.lifecycleGeneration < 1n || descriptor.lifecycleGeneration > maxGeneration
    || descriptor.descriptorRevision < 1n || descriptor.descriptorRevision > maxGeneration
    || descriptor.leaseGeneration < 1n || descriptor.leaseGeneration > maxGeneration) {
    throw new RangeError('ClientServer descriptor generations are invalid.');
  }
  if (!Number.isInteger(descriptor.weight) || descriptor.weight < 0 || descriptor.weight > 100) {
    throw new RangeError('ClientServer descriptor weight must be between 0 and 100.');
  }
}

function clientServerImmutableFingerprint(descriptor: ZLinkClientServerServerDescriptor): string {
  return JSON.stringify({
    channelName: descriptor.channelName,
    serverRid: String(descriptor.serverRid),
    lifecycleGeneration: descriptor.lifecycleGeneration.toString(),
    endpoint: descriptor.endpoint,
    securityIdentity: descriptor.securityIdentity,
    ownerId: descriptor.ownerId,
    leaseGeneration: descriptor.leaseGeneration.toString()
  });
}

function clientServerDescriptorFingerprint(descriptor: ZLinkClientServerServerDescriptor): string {
  return JSON.stringify({
    immutable: clientServerImmutableFingerprint(descriptor),
    descriptorRevision: descriptor.descriptorRevision.toString(),
    weight: descriptor.weight,
    state: descriptor.state
  });
}

function validateFanoutPublisherDescriptor(descriptor: ZLinkFanoutPublisherDescriptor): void {
  if (!validDescriptorText(descriptor.channelName)
    || !validDescriptorText(String(descriptor.publisherRid))
    || !validDescriptorText(descriptor.endpoint)
    || !validDescriptorText(descriptor.securityIdentity)
    || !validDescriptorText(descriptor.ownerId)) {
    throw new TypeError('Fanout publisher descriptor identity and endpoint are required.');
  }
  const maxGeneration = 0x7fff_ffff_ffff_ffffn;
  if (descriptor.lifecycleGeneration < 1n || descriptor.lifecycleGeneration > maxGeneration
    || descriptor.descriptorRevision < 1n || descriptor.descriptorRevision > maxGeneration
    || descriptor.leaseGeneration < 1n || descriptor.leaseGeneration > maxGeneration) {
    throw new RangeError('Fanout publisher descriptor generations are invalid.');
  }
}

function fanoutPublisherImmutableFingerprint(descriptor: ZLinkFanoutPublisherDescriptor): string {
  return JSON.stringify({
    channelName: descriptor.channelName,
    publisherRid: String(descriptor.publisherRid),
    lifecycleGeneration: descriptor.lifecycleGeneration.toString(),
    endpoint: descriptor.endpoint,
    securityIdentity: descriptor.securityIdentity,
    ownerId: descriptor.ownerId,
    leaseGeneration: descriptor.leaseGeneration.toString()
  });
}

function fanoutPublisherDescriptorFingerprint(descriptor: ZLinkFanoutPublisherDescriptor): string {
  return JSON.stringify({
    immutable: fanoutPublisherImmutableFingerprint(descriptor),
    descriptorRevision: descriptor.descriptorRevision.toString(),
    state: descriptor.state
  });
}

function actorTransferKey(meshName: string, actorId: string): string {
  return `${meshName.length}:${meshName}${actorId.length}:${actorId}`;
}

function validateTransferRequest(request: ZLinkActorTransferPrepareRequest): void {
  validateTransferIdentity(request.meshName, request.actorId, request.transferId);
  validateTransferLease(request.recoveryOwnerId, request.recoveryLeaseTtlMs);
  if (request.expectedActorGeneration < 1n || request.expectedMembershipEpoch < 1n) {
    throw new RangeError('Actor transfer generation and membership epoch must be positive.');
  }
  if (request.participants.size === 0) {
    throw new TypeError('Actor transfer requires at least one participant.');
  }
}

function validateTransferIdentity(meshName: string, actorId: string, transferId: string): void {
  if (meshName.trim().length === 0 || actorId.trim().length === 0) {
    throw new TypeError('Actor transfer meshName and actorId are required.');
  }
  if (!TRANSFER_ID_PATTERN.test(transferId)) {
    throw new TypeError('Actor transferId must be a lowercase canonical UUID.');
  }
}

function validateTransferLease(ownerId: string, ttlMs: number): void {
  if (ownerId.trim().length === 0 || !Number.isFinite(ttlMs) || ttlMs <= 0) {
    throw new TypeError('Actor transfer recovery owner and positive lease TTL are required.');
  }
}

function transferStored(record: ZLinkActorTransferRecord): ZLinkActorTransferWriteResult {
  return { status: 'stored', record };
}

function transferResult(status: ZLinkActorTransferWriteResult['status']): ZLinkActorTransferWriteResult {
  return { status };
}

function validateRoutingIdSlotAcquireRequest(request: ZLinkRoutingIdSlotAcquireRequest): void {
  validateRoutingIdGroupName(request.groupName);
  if (!Number.isInteger(request.slotCount) || request.slotCount < 1) {
    throw new RangeError('Routing-id slotCount must be a positive integer.');
  }
  if (request.ownerId.trim().length === 0) {
    throw new TypeError('Routing-id allocation ownerId must not be empty.');
  }
  if (!Number.isFinite(request.leaseTtlMs) || request.leaseTtlMs <= 0) {
    throw new RangeError('Routing-id allocation leaseTtlMs must be greater than zero.');
  }
  if (request.members.length === 0) {
    throw new TypeError('Routing-id allocation requires at least one member.');
  }
  const names = new Set<string>();
  for (const member of request.members) {
    if (member.meshName.trim().length === 0 || member.routingIdPrefix.trim().length === 0) {
      throw new TypeError('Routing-id allocation member names and prefixes must not be empty.');
    }
    if (names.has(member.meshName)) {
      throw new TypeError(`Routing-id allocation member '${member.meshName}' is duplicated.`);
    }
    names.add(member.meshName);
  }
}

function validateRoutingIdSlotRelease(
  groupName: string,
  slot: number,
  owner: ZLinkLocationOwnerToken
): void {
  validateRoutingIdGroupName(groupName);
  if (!Number.isInteger(slot) || slot < 1) {
    throw new RangeError('Routing-id allocation slot must be a positive integer.');
  }
  if (owner.ownerId.trim().length === 0 || owner.leaseGeneration < 1n) {
    throw new TypeError('Routing-id allocation owner token is invalid.');
  }
}

function validateRoutingIdGroupName(groupName: string): void {
  if (groupName.trim().length === 0) {
    throw new TypeError('Routing-id allocation group name must not be empty.');
  }
}

function normalizeRoutingIdMembers(
  members: readonly ZLinkRoutingIdSlotAllocationMember[]
): readonly ZLinkRoutingIdSlotAllocationMember[] {
  return members
    .map((member) => ({ ...member }))
    .sort((left, right) => left.meshName.localeCompare(right.meshName));
}

function sameRoutingIdMembers(
  left: readonly ZLinkRoutingIdSlotAllocationMember[],
  right: readonly ZLinkRoutingIdSlotAllocationMember[]
): boolean {
  return left.length === right.length && left.every((member, index) =>
    member.meshName === right[index]?.meshName
      && member.routingIdPrefix === right[index]?.routingIdPrefix);
}

function removeExpiredRoutingIdAllocations(
  group: InMemoryRoutingIdGroup,
  leases: ReadonlyMap<string, InMemoryOwnerLease>,
  storeNow: Date
): void {
  for (const [slot, allocation] of group.allocations) {
    const leaseExpiresAt = leases.get(allocation.owner.ownerId)?.leaseExpiresAt
      ?? allocation.leaseExpiresAt;
    if (leaseExpiresAt.getTime() <= storeNow.getTime()) {
      group.allocations.delete(slot);
    }
  }
}

function validateOwnerLeaseInput(ownerId: string, leaseTtlMs: number): void {
  if (ownerId.trim().length === 0) throw new TypeError('ownerId is required.');
  if (!Number.isSafeInteger(leaseTtlMs) || leaseTtlMs < 1) {
    throw new RangeError('leaseTtlMs must be a positive safe integer.');
  }
}

function routingIdAllocation(
  slot: number,
  owner: ZLinkLocationOwnerToken,
  storeNow: Date,
  leaseTtlMs: number
): ZLinkRoutingIdSlotAllocation {
  return {
    slot,
    owner,
    leaseExpiresAt: new Date(storeNow.getTime() + leaseTtlMs),
    storeNow
  };
}

function pageRows<TRow>(
  table: RowTable<TRow>,
  matches: (row: TRow) => boolean,
  page: ZLinkPageRequest
): ZLinkLocationPage<TRow> {
  const ordered = [...table.rows.entries()]
    .filter(([, row]) => matches(row))
    .sort(([left], [right]) => left.localeCompare(right))
    .map(([, row]) => row);
  const offset = parseContinuationToken(page.continuationToken);
  const size = page.pageSize !== undefined && page.pageSize > 0 ? page.pageSize : Number.MAX_SAFE_INTEGER;
  const items = ordered.slice(offset, offset + size);
  const nextOffset = offset + items.length;
  return {
    items,
    continuationToken: nextOffset < ordered.length ? String(nextOffset) : undefined
  };
}

function parseContinuationToken(token: string | undefined): number {
  if (token === undefined) {
    return 0;
  }
  const parsed = Number.parseInt(token, 10);
  return Number.isFinite(parsed) && parsed >= 0 ? parsed : 0;
}

function stampKey(scope: ZLinkLocationChangeStampScope): string {
  return `${scope.kind}:${scope.meshName ?? ''}`;
}

function stored(generation: bigint, updatedAt: Date): ZLinkLocationWriteResult {
  return { status: ZLinkLocationWriteStatus.Stored, generation, updatedAt };
}

function ignoredStale(): ZLinkLocationWriteResult {
  return { status: ZLinkLocationWriteStatus.IgnoredStale, generation: 0n, updatedAt: new Date(0) };
}

function rejectedConflict(): ZLinkLocationWriteResult {
  return { status: ZLinkLocationWriteStatus.RejectedConflict, generation: 0n, updatedAt: new Date(0) };
}
