import { createHash, randomUUID } from 'node:crypto';
import { createClient } from 'redis';
import { RedisStoreKeys } from './redis-store-key-prefixes';
import {
  configureOptions,
  type RedisCommandClient,
  type RedisCommandValue,
  type MutableZLinkRedisLocationOptions,
  type ZLinkRedisLocationOptions
} from './redis-options';
import {
  encodeActorKey,
  encodeClientServerServerKey,
  encodeFanoutPublisherKey,
  encodeKeySegments,
  encodeMeshNodeKey,
  encodePeerKey,
  encodeRouteKey,
  encodeSpotKey,
  routingIdHex
} from './redis-row-keys';
import {
  ACQUIRE_ROUTING_ID_SLOT_SCRIPT,
  ABORT_ACTOR_TRANSFER_SCRIPT,
  ACTIVATE_ACTOR_TRANSFER_SCRIPT,
  SERVICE_DESCRIPTOR_REMOVE_SCRIPT,
  SERVICE_DESCRIPTOR_WRITE_SCRIPT,
  CLAIM_LEASE_SCRIPT,
  COMMIT_ACTOR_TRANSFER_SCRIPT,
  LIST_ROUTING_ID_SLOTS_SCRIPT,
  MESH_DESCRIPTOR_WRITE_SCRIPT,
  MESH_DESCRIPTOR_REMOVE_SCRIPT,
  READ_LEASE_SCRIPT,
  RELEASE_LEASE_SCRIPT,
  RELEASE_ROUTING_ID_SLOT_SCRIPT,
  PREPARE_ACTOR_TRANSFER_SCRIPT,
  REMOVE_ALL_BY_OWNER_SCRIPT,
  REMOVE_SCRIPT,
  RENEW_LEASE_SCRIPT,
  TAKE_OVER_ACTOR_TRANSFER_SCRIPT,
  WRITE_SCRIPT
} from './redis-scripts';
import { AUTHORITY_HYBRID_SCRIPT } from './redis-authority-scripts';
import {
  kindActor,
  kindClientServer,
  kindFanoutPublisher,
  kindMeshNode,
  kindPeer,
  kindRoute,
  kindSpot,
  kindTagOf,
  materialize,
  ridOf,
  type LocationKind
} from './redis-row-codec';
import { matchesActor, matchesPeer, matchesRoute, matchesSpot } from './location-filter-predicates';
import { asArray, asString, toNumber } from './redis-values';
import { fromUnixMs, intentName, toWriteResult } from './redis-write-result';
import {
  ZLinkLocationWriteIntent,
  ZLinkLocationWriteStatus,
  ZLinkFrameworkRuntimeState,
  type RoutingId,
  type ZLinkLocationChangeStampStore,
  type ZLinkActorLocation,
  type ZLinkClientServerLocationStore,
  type ZLinkClientServerServerDescriptor,
  type ZLinkClientServerServerDescriptorKey,
  type ZLinkFanoutLocationStore,
  type ZLinkFanoutPublisherDescriptor,
  type ZLinkFanoutPublisherDescriptorKey,
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
  type ZLinkAggregateAbortResult,
  type ZLinkAggregateCommitResult,
  type ZLinkAggregateFence,
  type ZLinkAggregatePrepareRequest,
  type ZLinkAggregatePrepareResult,
  type ZLinkAuthorityCompareExchangeResult,
  type ZLinkAuthorityKey,
  type ZLinkAuthorityMutation,
  type ZLinkAuthorityReadResult,
  ZLinkAuthorityScanCursor,
  type ZLinkAuthorityScanResult,
  type ZLinkAuthoritySnapshot,
  type ZLinkAuthorityStoreVersion,
  type ZLinkMeshNodeDescriptor,
  type ZLinkMeshNodeDescriptorKey,
  type ZLinkObjectAbortRequest,
  type ZLinkObjectAbortResult,
  type ZLinkObjectCommitRequest,
  type ZLinkObjectCommitResult,
  type ZLinkObjectReserveRequest,
  type ZLinkObjectReserveResult,
  type ZLinkRelocationCapacityAbortResult,
  type ZLinkRelocationCapacityFence,
  type ZLinkRelocationCapacityReservationRequest,
  type ZLinkRelocationCapacityReserveResult,
  type ZLinkOwnerLeaseClaimResult,
  type ZLinkOwnerLeaseReadResult,
  type ZLinkOwnerLeaseReleaseResult,
  type ZLinkOwnerLeaseRenewResult,
  type ZLinkOwnerLeaseStore,
  type ZLinkPageRequest,
  type ZLinkPeerLocation,
  type ZLinkPeerLocationFilter,
  type ZLinkPeerLocationKey,
  type ZLinkRouteLocation,
  type ZLinkRouteLocationFilter,
  type ZLinkRouteLocationKey,
  type ZLinkSpotLocation,
  type ZLinkSpotLocationFilter,
  type ZLinkSpotLocationKey,
  type ZLinkRoutingIdSlotAcquireRequest,
  type ZLinkRoutingIdSlotAcquireResult,
  type ZLinkRoutingIdSlotAllocationMember,
  type ZLinkRoutingIdSlotAllocationSnapshot,
  type ZLinkRoutingIdSlotAllocationStore,
  type ZLinkRoutingIdSlotReleaseResult
} from '@zlink-systems/framework';

const CLIENT_SERVER_PAGE_MAX_BYTES = 4 * 1024 * 1024;
const FANOUT_PAGE_MAX_BYTES = 4 * 1024 * 1024;

export class ZLinkRedisLocationStore implements
  ZLinkClientServerLocationStore,
  ZLinkFanoutLocationStore,
  ZLinkLocationChangeStampStore,
  ZLinkOwnerLeaseStore,
  ZLinkRoutingIdSlotAllocationStore {
  private readonly keys: RedisStoreKeys;
  private readonly providedClient?: RedisCommandClient;
  private client?: RedisCommandClient;
  private schemaPromise?: Promise<void>;

  constructor(options: ZLinkRedisLocationOptions | ((options: MutableZLinkRedisLocationOptions) => void)) {
    const resolved = typeof options === 'function' ? configureOptions(options) : options;
    if (resolved.keyPrefix.length === 0) {
      throw new Error('ZLinkRedisLocationOptions.keyPrefix is required.');
    }
    if (resolved.client === undefined && resolved.url === undefined && resolved.clientOptions === undefined) {
      throw new Error('ZLinkRedisLocationOptions requires url, clientOptions, or client.');
    }
    this.keys = new RedisStoreKeys(resolved.keyPrefix);
    this.providedClient = resolved.client;
    if (resolved.client === undefined) {
      this.client = createClient({
        disableOfflineQueue: true,
        ...(resolved.clientOptions ?? {}),
        socket: {
          reconnectStrategy: false,
          ...(resolved.clientOptions?.socket ?? {})
        },
        url: resolved.url ?? resolved.clientOptions?.url
      }) as RedisCommandClient;
      this.client.on?.('error', () => {});
    }
  }

  async updateMeshNode(
    descriptor: ZLinkMeshNodeDescriptor,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    validateMeshDescriptor(descriptor);
    const rowKey = encodeMeshNodeKey(descriptor);
    const raw = asArray(await this.eval(MESH_DESCRIPTOR_WRITE_SCRIPT, [
      this.keys.descriptorMesh(rowKey),
      this.keys.descriptorAdmissionMesh(rowKey),
      this.keys.descriptorMeshIndex(),
      this.keys.lease(descriptor.ownerId),
      this.keys.counter(),
      this.keys.descriptorMeshOwnerIndex(
        descriptor.ownerId,
        descriptor.leaseGeneration.toString()
      )
    ], [
      intentName(intent),
      descriptor.ownerId,
      descriptor.leaseGeneration.toString(),
      descriptor.lifecycleGeneration.toString(),
      descriptor.descriptorRevision.toString(),
      meshDescriptorImmutableFingerprint(descriptor),
      kindMeshNode.toJsonText?.(descriptor) ?? JSON.stringify(kindMeshNode.toJson(descriptor)),
      descriptor.objectRole,
      String(descriptor.state),
      JSON.stringify(canonicalObjectCapabilities(descriptor.objectCapabilities)),
      String(descriptor.objectCapacity.maxActiveObjects),
      String(descriptor.objectCapacity.maxPendingActivations),
      descriptor.meshName,
      rowKey,
      descriptor.applicationVersion.toString()
    ], signal));
    return toWriteResult(raw);
  }

  async removeMeshNode(
    key: ZLinkMeshNodeDescriptorKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult['status']> {
    const rowKey = encodeMeshNodeKey(key);
    const raw = asArray(await this.eval(MESH_DESCRIPTOR_REMOVE_SCRIPT, [
      this.keys.descriptorMesh(rowKey),
      this.keys.descriptorAdmissionMesh(rowKey),
      this.keys.descriptorMeshIndex(),
      this.keys.descriptorMeshOwnerIndex(owner.ownerId, owner.leaseGeneration.toString())
    ], [
      owner.ownerId,
      owner.leaseGeneration.toString(),
      rowKey
    ], signal));
    return toWriteResult(raw).status;
  }

  async listMeshNodes(
    meshName: string,
    signal?: AbortSignal
  ): Promise<readonly ZLinkMeshNodeDescriptor[]> {
    const [rows, projection] = await Promise.all([
      this.loadMeshNodes(signal),
      this.authorityCall('capacityProjection', {}, signal) as Promise<{
        readonly nodeActive: Readonly<Record<string, number>>;
        readonly nodePending: Readonly<Record<string, number>>;
      }>
    ]);
    return rows
      .filter(row => row.meshName === meshName)
      .map(row => {
        const key = encodeKeySegments(
          encodeMeshNodeKey({ meshName: row.meshName, rid: row.rid }),
          row.lifecycleGeneration.toString()
        );
        return {
          ...row,
          objectCapacity: {
            ...row.objectCapacity,
            activeObjects: Number(projection.nodeActive[key] ?? 0),
            pendingActivations: Number(projection.nodePending[key] ?? 0)
          }
        };
      });
  }

  async updateClientServer(
    descriptor: ZLinkClientServerServerDescriptor,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    validateClientServerDescriptor(descriptor);
    const rowKey = encodeClientServerServerKey(descriptor);
    const admissionKey = this.keys.descriptorAdmissionClientServer(rowKey);
    const current = asArray(await this.command([
      'HMGET', admissionKey, 'ownerId', 'ownerLeaseGeneration'
    ], signal));
    const currentOwner = current[0] === null ? '' : asString(current[0]);
    const currentLeaseGeneration = current[1] === null ? '' : asString(current[1]);
    const placeholder = this.keys.schema();
    const raw = asArray(await this.eval(SERVICE_DESCRIPTOR_WRITE_SCRIPT, [
      this.keys.descriptorClientServer(rowKey),
      admissionKey,
      this.keys.descriptorClientServerIndex(),
      this.keys.lease(descriptor.ownerId),
      this.keys.counter(),
      this.keys.descriptorClientServerOwnerIndex(
        descriptor.ownerId,
        descriptor.leaseGeneration.toString()
      ),
      currentOwner.length === 0 ? placeholder : this.keys.lease(currentOwner),
      currentOwner.length === 0
        ? placeholder
        : this.keys.descriptorClientServerOwnerIndex(
          currentOwner,
          currentLeaseGeneration
        ),
      this.keys.stamp(kindClientServer.tag, undefined),
      this.keys.stamp(kindClientServer.tag, descriptor.channelName),
      this.keys.descriptorClientServerChannelIndex(descriptor.channelName)
    ], [
      intentName(intent),
      descriptor.ownerId,
      descriptor.leaseGeneration.toString(),
      descriptor.lifecycleGeneration.toString(),
      descriptor.descriptorRevision.toString(),
      clientServerImmutableFingerprint(descriptor),
      kindClientServer.toJsonText?.(descriptor)
        ?? JSON.stringify(kindClientServer.toJson(descriptor)),
      String(descriptor.state),
      String(descriptor.weight),
      descriptor.channelName,
      rowKey,
      currentOwner,
      currentLeaseGeneration
    ], signal));
    return toWriteResult(raw);
  }

  async removeClientServer(
    key: ZLinkClientServerServerDescriptorKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteStatus> {
    const rowKey = encodeClientServerServerKey(key);
    return await this.removeClientServerByRowKey(rowKey, key.channelName, owner, signal);
  }

  async listClientServers(
    channelName: string,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkClientServerServerDescriptor>> {
    requireDescriptorText(channelName, 'ClientServer channel name');
    return await this.listServiceDescriptors(
      kindClientServer,
      'client-server-v1',
      'ClientServer',
      channelName,
      this.keys.descriptorClientServerChannelIndex(channelName),
      (rowKey) => this.keys.descriptorClientServer(rowKey),
      CLIENT_SERVER_PAGE_MAX_BYTES,
      page,
      signal
    );
  }

  async updateFanoutPublisher(
    descriptor: ZLinkFanoutPublisherDescriptor,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    validateFanoutPublisherDescriptor(descriptor);
    const rowKey = encodeFanoutPublisherKey(descriptor);
    const admissionKey = this.keys.descriptorAdmissionFanoutPublisher(rowKey);
    const current = asArray(await this.command([
      'HMGET', admissionKey, 'ownerId', 'ownerLeaseGeneration'
    ], signal));
    const currentOwner = current[0] === null ? '' : asString(current[0]);
    const currentLeaseGeneration = current[1] === null ? '' : asString(current[1]);
    const placeholder = this.keys.schema();
    const raw = asArray(await this.eval(SERVICE_DESCRIPTOR_WRITE_SCRIPT, [
      this.keys.descriptorFanoutPublisher(rowKey),
      admissionKey,
      this.keys.descriptorFanoutPublisherIndex(),
      this.keys.lease(descriptor.ownerId),
      this.keys.counter(),
      this.keys.descriptorFanoutPublisherOwnerIndex(
        descriptor.ownerId,
        descriptor.leaseGeneration.toString()
      ),
      currentOwner.length === 0 ? placeholder : this.keys.lease(currentOwner),
      currentOwner.length === 0
        ? placeholder
        : this.keys.descriptorFanoutPublisherOwnerIndex(
          currentOwner,
          currentLeaseGeneration
        ),
      this.keys.stamp(kindFanoutPublisher.tag, undefined),
      this.keys.stamp(kindFanoutPublisher.tag, descriptor.channelName),
      this.keys.descriptorFanoutPublisherChannelIndex(descriptor.channelName)
    ], [
      intentName(intent),
      descriptor.ownerId,
      descriptor.leaseGeneration.toString(),
      descriptor.lifecycleGeneration.toString(),
      descriptor.descriptorRevision.toString(),
      fanoutPublisherImmutableFingerprint(descriptor),
      kindFanoutPublisher.toJsonText?.(descriptor)
        ?? JSON.stringify(kindFanoutPublisher.toJson(descriptor)),
      String(descriptor.state),
      '',
      descriptor.channelName,
      rowKey,
      currentOwner,
      currentLeaseGeneration
    ], signal));
    return toWriteResult(raw);
  }

  async removeFanoutPublisher(
    key: ZLinkFanoutPublisherDescriptorKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteStatus> {
    const rowKey = encodeFanoutPublisherKey(key);
    return await this.removeFanoutPublisherByRowKey(rowKey, key.channelName, owner, signal);
  }

  async listFanoutPublishers(
    channelName: string,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkFanoutPublisherDescriptor>> {
    requireDescriptorText(channelName, 'fanout channel name');
    return await this.listServiceDescriptors(
      kindFanoutPublisher,
      'fanout-publisher-v1',
      'Fanout publisher',
      channelName,
      this.keys.descriptorFanoutPublisherChannelIndex(channelName),
      (rowKey) => this.keys.descriptorFanoutPublisher(rowKey),
      FANOUT_PAGE_MAX_BYTES,
      page,
      signal
    );
  }

  async updatePeer(
    peer: ZLinkPeerLocation,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    return await this.write(kindPeer, peer, intent, signal);
  }

  async removePeer(
    key: ZLinkPeerLocationKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    return await this.remove(kindPeer, encodePeerKey(key), key.meshName, owner, signal);
  }

  async listPeers(filter: ZLinkPeerLocationFilter, signal?: AbortSignal): Promise<readonly ZLinkPeerLocation[]> {
    const rows = await this.loadAll(kindPeer, signal);
    return rows.filter((row) => matchesPeer(row, filter));
  }

  async updateSpot(
    spot: ZLinkSpotLocation,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    return await this.write(kindSpot, spot, intent, signal);
  }

  async removeSpot(
    key: ZLinkSpotLocationKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteStatus> {
    return (await this.remove(kindSpot, encodeSpotKey(key), key.meshName, owner, signal)).status;
  }

  async resolveSpot(key: ZLinkSpotLocationKey, signal?: AbortSignal): Promise<ZLinkSpotLocation | undefined> {
    return await this.resolve(kindSpot, encodeSpotKey(key), signal);
  }

  async listSpots(
    filter: ZLinkSpotLocationFilter,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkSpotLocation>> {
    return await this.listPage(kindSpot, (row) => matchesSpot(row, filter), page, signal);
  }

  async updateActor(
    actor: ZLinkActorLocation,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    return await this.write(kindActor, actor, intent, signal);
  }

  async removeActor(
    key: ZLinkActorLocationKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteStatus> {
    return (await this.remove(kindActor, encodeActorKey(key), key.meshName, owner, signal)).status;
  }

  async resolveActor(key: ZLinkActorLocationKey, signal?: AbortSignal): Promise<ZLinkActorLocation | undefined> {
    return await this.resolve(kindActor, encodeActorKey(key), signal);
  }

  async listActors(
    filter: ZLinkActorLocationFilter,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkActorLocation>> {
    return await this.listPage(kindActor, (row) => matchesActor(row, filter), page, signal);
  }

  async updateRoute(
    route: ZLinkRouteLocation,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    return await this.write(kindRoute, route, intent, signal);
  }

  async removeRoute(
    key: ZLinkRouteLocationKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    return await this.remove(kindRoute, encodeRouteKey(key), undefined, owner, signal);
  }

  async resolveRoute(key: ZLinkRouteLocationKey, signal?: AbortSignal): Promise<ZLinkRouteLocation | undefined> {
    return await this.resolve(kindRoute, encodeRouteKey(key), signal);
  }

  async listRoutes(
    filter: ZLinkRouteLocationFilter,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkRouteLocation>> {
    return await this.listPage(kindRoute, (row) => matchesRoute(row, filter), page, signal);
  }

  async prepareActorTransfer(
    request: ZLinkActorTransferPrepareRequest,
    signal?: AbortSignal
  ): Promise<ZLinkActorTransferWriteResult> {
    validateTransferRequest(request);
    const actorRowKey = transferActorRowKey(request.meshName, request.actorId);
    const raw = asArray(await this.eval(PREPARE_ACTOR_TRANSFER_SCRIPT, [
      this.keys.actorTransfer(actorRowKey, request.transferId),
      this.keys.actorTransferByActor(actorRowKey)
    ], [
      request.transferId,
      serializeActorRef(request.source),
      serializeActorRef(request.target),
      request.expectedActorGeneration.toString(),
      request.expectedMembershipEpoch.toString(),
      serializeParticipants(request.participants),
      request.recoveryOwnerId,
      String(Math.max(1, Math.trunc(request.recoveryLeaseTtlMs)))
    ], signal));
    return transferResultOf(raw, request.meshName, request.actorId, request.transferId);
  }

  async commitActorTransfer(
    meshName: string,
    actorId: string,
    transferId: string,
    recoveryOwnerId: string,
    signal?: AbortSignal
  ): Promise<ZLinkActorTransferWriteResult> {
    return await this.transitionActorTransfer(
      COMMIT_ACTOR_TRANSFER_SCRIPT,
      meshName, actorId, transferId, recoveryOwnerId, signal
    );
  }

  async activateActorTransfer(
    meshName: string,
    actorId: string,
    transferId: string,
    recoveryOwnerId: string,
    signal?: AbortSignal
  ): Promise<ZLinkActorTransferWriteResult> {
    return await this.transitionActorTransfer(
      ACTIVATE_ACTOR_TRANSFER_SCRIPT,
      meshName, actorId, transferId, recoveryOwnerId, signal
    );
  }

  async abortActorTransfer(
    meshName: string,
    actorId: string,
    transferId: string,
    recoveryOwnerId: string,
    signal?: AbortSignal
  ): Promise<ZLinkActorTransferWriteResult> {
    return await this.transitionActorTransfer(
      ABORT_ACTOR_TRANSFER_SCRIPT,
      meshName, actorId, transferId, recoveryOwnerId, signal
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
    validateTransferIdentity(meshName, actorId, transferId);
    validateTransferLease(successorOwnerId, recoveryLeaseTtlMs);
    const actorRowKey = transferActorRowKey(meshName, actorId);
    const raw = asArray(await this.eval(TAKE_OVER_ACTOR_TRANSFER_SCRIPT, [
      this.keys.actorTransfer(actorRowKey, transferId),
      this.keys.actorTransferByActor(actorRowKey)
    ], [
      transferId,
      successorOwnerId,
      String(Math.max(1, Math.trunc(recoveryLeaseTtlMs)))
    ], signal));
    return transferResultOf(raw, meshName, actorId, transferId);
  }

  async resolveActorTransfer(
    meshName: string,
    actorId: string,
    signal?: AbortSignal
  ): Promise<ZLinkActorTransferRecord | undefined> {
    const actorRowKey = transferActorRowKey(meshName, actorId);
    const active = await this.command([
      'GET',
      this.keys.actorTransferByActor(actorRowKey)
    ], signal);
    if (active === null) return undefined;
    const transferId = asString(active);
    const fields = asArray(await this.command([
      'HMGET',
      this.keys.actorTransfer(actorRowKey, transferId),
      ...TRANSFER_HASH_FIELDS
    ], signal));
    return materializeTransfer(meshName, actorId, transferId, fields);
  }

  async claimOwnerLease(
    ownerId: string,
    leaseTtlMs: number,
    signal?: AbortSignal
  ): Promise<ZLinkOwnerLeaseClaimResult> {
    validateOwnerLeaseInput(ownerId, leaseTtlMs);
    const raw = asArray(await this.eval(CLAIM_LEASE_SCRIPT, [
      this.keys.lease(ownerId),
      this.keys.leaseGenerationCounter()
    ], [ownerId, String(leaseTtlMs)], signal));
    const kind = asString(raw[0]);
    if (kind === 'conflict') return { kind: 'conflict' };
    if (kind === 'exhausted') return { kind: 'generationExhausted' };
    if (kind !== 'claimed') throw new Error(`Unknown owner lease claim result '${kind}'.`);
    return {
      kind: 'claimed',
      token: { ownerId, leaseGeneration: BigInt(asString(raw[1])) },
      leaseExpiresAt: fromUnixMs(toNumber(raw[2])),
      storeNow: fromUnixMs(toNumber(raw[3]))
    };
  }

  async readOwnerLease(
    ownerId: string,
    signal?: AbortSignal
  ): Promise<ZLinkOwnerLeaseReadResult> {
    if (ownerId.trim().length === 0) throw new TypeError('ownerId is required.');
    const raw = asArray(await this.eval(
      READ_LEASE_SCRIPT,
      [this.keys.lease(ownerId)],
      [ownerId],
      signal
    ));
    if (asString(raw[0]) === 'missing') return { kind: 'missing' };
    return {
      kind: 'found',
      token: { ownerId, leaseGeneration: BigInt(asString(raw[1])) },
      leaseExpiresAt: fromUnixMs(toNumber(raw[2])),
      storeNow: fromUnixMs(toNumber(raw[3]))
    };
  }

  async renewOwnerLease(
    token: ZLinkLocationOwnerToken,
    leaseTtlMs: number,
    signal?: AbortSignal
  ): Promise<ZLinkOwnerLeaseRenewResult> {
    validateOwnerLeaseInput(token.ownerId, leaseTtlMs);
    const raw = asArray(await this.eval(
      RENEW_LEASE_SCRIPT,
      [this.keys.lease(token.ownerId)],
      [token.ownerId, String(token.leaseGeneration), String(leaseTtlMs)],
      signal
    ));
    if (asString(raw[0]) === 'stale') return { kind: 'stale' };
    return {
      kind: 'renewed',
      leaseExpiresAt: fromUnixMs(toNumber(raw[1])),
      storeNow: fromUnixMs(toNumber(raw[2]))
    };
  }

  async releaseOwnerLease(
    token: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkOwnerLeaseReleaseResult> {
    const raw = asArray(await this.eval(RELEASE_LEASE_SCRIPT, [
      this.keys.lease(token.ownerId)
    ], [token.ownerId, String(token.leaseGeneration)], signal));
    return asString(raw[0]) === 'released' ? 'released' : 'stale';
  }

  async removeAllByOwner(
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<bigint> {
    const ownerId = owner.ownerId;
    const removed = toNumber(await this.eval(REMOVE_ALL_BY_OWNER_SCRIPT, [
      this.keys.ownerIndexPrefix(kindMeshNode.tag) + ownerId,
      this.keys.ownerIndexPrefix(kindPeer.tag) + ownerId,
      this.keys.ownerIndexPrefix(kindSpot.tag) + ownerId,
      this.keys.ownerIndexPrefix(kindActor.tag) + ownerId,
      this.keys.ownerIndexPrefix(kindRoute.tag) + ownerId,
      this.keys.kindIndex(kindMeshNode.tag),
      this.keys.kindIndex(kindPeer.tag),
      this.keys.kindIndex(kindSpot.tag),
      this.keys.kindIndex(kindActor.tag),
      this.keys.kindIndex(kindRoute.tag),
      this.keys.lease(ownerId)
    ], [
      this.keys.rowHashPrefix(kindMeshNode.tag),
      this.keys.rowHashPrefix(kindPeer.tag),
      this.keys.rowHashPrefix(kindSpot.tag),
      this.keys.rowHashPrefix(kindActor.tag),
      this.keys.rowHashPrefix(kindRoute.tag),
      this.keys.stamp(kindMeshNode.tag, undefined),
      this.keys.stamp(kindPeer.tag, undefined),
      this.keys.stamp(kindSpot.tag, undefined),
      this.keys.stamp(kindActor.tag, undefined),
      this.keys.stamp(kindRoute.tag, undefined),
      String(owner.leaseGeneration),
      ownerId
    ], signal));
    if (removed < 0) {
      throw new Error('Owner cleanup token is stale.');
    }
    const clientServerRemoved = await this.removeOwnedServiceDescriptors(
      kindClientServer,
      this.keys.descriptorClientServerOwnerIndex(
        ownerId,
        owner.leaseGeneration.toString()
      ),
      (rowKey) => this.keys.descriptorClientServer(rowKey),
      (rowKey, descriptor) => this.removeClientServerByRowKey(
        rowKey,
        descriptor.channelName,
        owner,
        signal
      ),
      signal
    );
    const fanoutRemoved = await this.removeOwnedServiceDescriptors(
      kindFanoutPublisher,
      this.keys.descriptorFanoutPublisherOwnerIndex(
        ownerId,
        owner.leaseGeneration.toString()
      ),
      (rowKey) => this.keys.descriptorFanoutPublisher(rowKey),
      (rowKey, descriptor) => this.removeFanoutPublisherByRowKey(
        rowKey,
        descriptor.channelName,
        owner,
        signal
      ),
      signal
    );
    return BigInt(removed + clientServerRemoved + fanoutRemoved);
  }

  async getChangeStamp(scope: ZLinkLocationChangeStampScope, signal?: AbortSignal): Promise<bigint> {
    const value = await this.command(['GET', this.keys.stamp(kindTagOf(scope.kind), scope.meshName)], signal);
    return value === null ? 0n : BigInt(asString(value));
  }

  async acquireRoutingIdSlot(
    request: ZLinkRoutingIdSlotAcquireRequest,
    signal?: AbortSignal
  ): Promise<ZLinkRoutingIdSlotAcquireResult> {
    validateAcquire(request);
    const members = [...request.members]
      .sort((left, right) => left.meshName.localeCompare(right.meshName));
    const config = JSON.stringify(members.map((member) => ({
      MeshName: member.meshName,
      RoutingIdPrefix: member.routingIdPrefix
    })));
    const groupKey = this.keys.routingIdAllocationGroup(request.groupName);
    const slotLeaseKeys = await this.routingSlotLeaseKeys(
      groupKey,
      request.slotCount,
      signal
    );
    const raw = asArray(await this.eval(ACQUIRE_ROUTING_ID_SLOT_SCRIPT, [
      groupKey,
      this.keys.lease(request.ownerId),
      ...slotLeaseKeys
    ], [
      config,
      String(request.slotCount),
      request.ownerId,
      String(Math.max(1, Math.trunc(request.leaseTtlMs)))
    ], signal));
    const kind = asString(raw[0]);
    if (kind === 'exhausted') return { kind: 'groupExhausted' };
    if (kind === 'identity-conflict') return { kind: 'identityModeConflict' };
    if (kind === 'mismatch') {
      return {
        kind: 'groupConfigurationMismatch',
        expectedMembers: decodeMembers(asString(raw[1])),
        expectedSlotCount: toNumber(raw[2]),
        actualMembers: members,
        actualSlotCount: request.slotCount
      };
    }
    if (kind !== 'acquired') throw new Error(`Unknown routing-id slot acquire result '${kind}'.`);
    return {
      kind: 'acquired',
      allocation: {
        slot: toNumber(raw[1]),
        owner: { ownerId: request.ownerId, leaseGeneration: BigInt(asString(raw[2])) },
        leaseExpiresAt: fromUnixMs(toNumber(raw[3])),
        storeNow: fromUnixMs(toNumber(raw[4]))
      }
    };
  }

  async releaseRoutingIdSlot(
    groupName: string,
    slot: number,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkRoutingIdSlotReleaseResult> {
    validateRelease(groupName, slot, owner);
    const raw = asArray(await this.eval(RELEASE_ROUTING_ID_SLOT_SCRIPT, [
      this.keys.routingIdAllocationGroup(groupName)
    ], [String(slot), owner.ownerId, String(owner.leaseGeneration)], signal));
    return asString(raw[0]) === 'released' ? 'released' : 'ignoredStale';
  }

  async listRoutingIdSlots(
    groupName: string,
    signal?: AbortSignal
  ): Promise<ZLinkRoutingIdSlotAllocationSnapshot> {
    validateGroupName(groupName);
    const groupKey = this.keys.routingIdAllocationGroup(groupName);
    const slotCountValue = await this.command(['HGET', groupKey, 'slotCount'], signal);
    const slotCount = slotCountValue === null ? 0 : toNumber(slotCountValue);
    const slotLeaseKeys = await this.routingSlotLeaseKeys(groupKey, slotCount, signal);
    const raw = asArray(await this.eval(LIST_ROUTING_ID_SLOTS_SCRIPT, [
      groupKey,
      ...slotLeaseKeys
    ], [], signal));
    const storeNow = fromUnixMs(toNumber(raw[2]));
    const entries = asArray(raw[3]);
    const allocations = [];
    for (let index = 0; index + 3 < entries.length; index += 4) {
      allocations.push({
        slot: toNumber(entries[index]),
        owner: {
          ownerId: asString(entries[index + 1]),
          leaseGeneration: BigInt(asString(entries[index + 2]))
        },
        leaseExpiresAt: fromUnixMs(toNumber(entries[index + 3])),
        storeNow
      });
    }
    return {
      groupName,
      members: asString(raw[0]).length === 0 ? [] : decodeMembers(asString(raw[0])),
      slotCount: toNumber(raw[1]),
      allocations,
      storeNow
    };
  }

  async readAuthority(
    key: ZLinkAuthorityKey,
    signal?: AbortSignal
  ): Promise<ZLinkAuthorityReadResult> {
    requireText(key.value, 'authority key');
    return authorityRead(await this.authorityCall('read', { key: key.value }, signal));
  }

  async compareExchangeAuthority(
    key: ZLinkAuthorityKey,
    expectedStoreVersion: ZLinkAuthorityStoreVersion,
    mutation: ZLinkAuthorityMutation,
    signal?: AbortSignal
  ): Promise<ZLinkAuthorityCompareExchangeResult> {
    requireText(key.value, 'authority key');
    const before = await this.readAuthority(key, signal);
    const request = mutation.kind === 'delete'
      ? {
          key: key.value,
          expectedStoreVersion: expectedStoreVersion.value,
          mutationKind: 'delete',
          currentOwner: before.kind === 'snapshot'
            ? { ownerId: before.ownerId, leaseGeneration: before.ownerLeaseGeneration.toString() }
            : undefined
        }
      : {
          key: key.value,
          expectedStoreVersion: expectedStoreVersion.value,
          mutationKind: 'put',
          transition: mutation.generationTransition,
          payload: encodePayload(mutation.payload),
          targetOwner: mutation.targetOwner === undefined ? undefined : ownerJson(mutation.targetOwner),
          fence: mutation.relocationCapacityFence?.value,
          currentOwner: before.kind === 'snapshot'
            ? { ownerId: before.ownerId, leaseGeneration: before.ownerLeaseGeneration.toString() }
            : undefined
        };
    const raw = await this.authorityCall('cas', request, signal);
    return authorityCasResult(raw);
  }

  async listAuthorities(
    prefix: string,
    cursor: ZLinkAuthorityScanCursor | undefined,
    limit: number,
    signal?: AbortSignal
  ): Promise<ZLinkAuthorityScanResult> {
    if (!Number.isInteger(limit) || limit < 1 || limit > 1000) {
      throw new RangeError('Authority scan limit must be in 1..1000.');
    }
    const decoded = cursor === undefined
      ? { scanId: randomUUID(), lastHex: '', prefix }
      : decodeAuthorityCursor(cursor.encoded);
    if (decoded.prefix !== prefix) return { kind: 'scanExpired' };
    const scanKey = this.keys.scan(decoded.scanId);
    const placeholder = this.keys.schema();
    const baseKeys = [
      placeholder, placeholder, placeholder, this.keys.counter(),
      this.keys.authorityKeyIndex(), this.keys.membershipCurrent(),
      this.keys.capacityNode('active'), this.keys.capacityNode('pending'),
      this.keys.capacityType('active'), this.keys.capacityType('pending'),
      placeholder, placeholder, placeholder, placeholder, placeholder,
      this.keys.authorityIndexGc(), this.keys.scansExpiry(),
      this.keys.scansWatermark(), scanKey
    ];
    if (cursor === undefined) {
      await this.eval(AUTHORITY_HYBRID_SCRIPT, baseKeys, [
        'scan',
        JSON.stringify({
          start: true,
          scanId: decoded.scanId,
          prefix,
          retentionMs: 60_000
        })
      ], signal);
    }
    const min = decoded.lastHex.length === 0 ? '-' : `(${decoded.lastHex}`;
    const candidates = asArray(await this.command([
      'ZRANGEBYLEX', this.keys.authorityKeyIndex(), min, '+',
      'LIMIT', '0', String(Math.min(limit * 4, 4000))
    ], signal)).map(asString);
    const keys = [...baseKeys];
    for (const candidate of candidates) {
      const authorityKey = Buffer.from(candidate, 'hex').toString('utf8');
      keys.push(
        this.keys.authorityCurrent(authorityKey),
        this.keys.authorityHistory(authorityKey),
        this.keys.authorityHistoryRevisions(authorityKey)
      );
    }
    const raw = JSON.parse(asString(await this.eval(AUTHORITY_HYBRID_SCRIPT, keys, [
      'scan',
      JSON.stringify({
        start: false,
        scanId: decoded.scanId,
        prefix,
        limit,
        retentionMs: 60_000,
        expectedLastHex: decoded.lastHex,
        candidates,
        dynamicStart: 20
      })
    ], signal))) as {
      readonly kind: 'page' | 'scanExpired';
      readonly rows?: readonly { readonly key: string; readonly row: AuthorityJson }[];
      readonly lastHex?: string;
      readonly hasMore?: boolean;
      readonly storeNowMs?: number;
    };
    if (raw.kind === 'scanExpired') return { kind: 'scanExpired' };
    const rows = raw.rows ?? [];
    return {
      kind: 'page',
      items: rows.map(item => ({
        key: { value: item.key } as ZLinkAuthorityKey,
        snapshot: authoritySnapshot(item.row, raw.storeNowMs ?? 0)
      })),
      nextCursor: raw.hasMore === true
        ? ZLinkAuthorityScanCursor.from(encodeAuthorityCursor(
            decoded.scanId,
            raw.lastHex ?? decoded.lastHex,
            prefix
          ))
        : undefined
    };
  }

  async reserve(
    request: ZLinkObjectReserveRequest,
    signal?: AbortSignal
  ): Promise<ZLinkObjectReserveResult> {
    validateCapacityDelta(request.pendingCapacityDelta);
    validateAuthorityPayload(request.creatingPayload);
    requireText(request.intent.requestContentReference, 'creation content reference');
    if (request.intent.requestSha256.byteLength !== 32 || request.intent.requestEncodedSize < 0n) {
      throw new TypeError('Object creation content receipt is invalid.');
    }
    const reservationId = randomUUID();
    const raw = await this.authorityCall('reserve', {
      key: `${request.key.kind}:${requireText(request.key.globalId, 'object global ID')}`,
      objectKind: request.key.kind,
      stableType: requireText(request.intent.stableType, 'stable type'),
      placementProfile: request.intent.placementProfile ?? '',
      capacityDelta: request.pendingCapacityDelta,
      payload: encodePayload(request.creatingPayload),
      reservationId,
      intent: {
        placementProfile: request.intent.placementProfile ?? '',
        affinityKey: request.intent.affinityKey ?? '',
        requestContentReference: request.intent.requestContentReference,
        requestSha256: encodePayload(request.intent.requestSha256),
        requestEncodedSize: request.intent.requestEncodedSize.toString()
      },
      target: creationTargetJson(request.target)
    }, signal);
    return objectReserveResult(raw);
  }

  async commit(
    request: ZLinkObjectCommitRequest,
    signal?: AbortSignal
  ): Promise<ZLinkObjectCommitResult> {
    validateAuthorityPayload(request.readyPayload);
    const raw = await this.authorityCall('commit', {
      key: `${request.key.kind}:${requireText(request.key.globalId, 'object global ID')}`,
      reservationId: request.reservationId,
      expectedStoreVersion: request.expectedStoreVersion,
      target: creationTargetJson(request.target),
      payload: encodePayload(request.readyPayload),
      placementProfile: ''
    }, signal);
    return objectCommitResult(raw);
  }

  async abort(
    request: ZLinkObjectAbortRequest,
    signal?: AbortSignal
  ): Promise<ZLinkObjectAbortResult> {
    return await this.authorityCall('abort', {
      key: `${request.key.kind}:${requireText(request.key.globalId, 'object global ID')}`,
      reservationId: request.reservationId,
      expectedStoreVersion: request.expectedStoreVersion,
      target: creationTargetJson(request.target)
    }, signal) as ZLinkObjectAbortResult;
  }

  async reserveRelocationCapacity(
    request: ZLinkRelocationCapacityReservationRequest,
    signal?: AbortSignal
  ): Promise<ZLinkRelocationCapacityReserveResult> {
    validateCapacityDelta(request.capacityDelta);
    requireText(request.reservationId, 'relocation reservation ID');
    const comparable = relocationRequestJson(request);
    const raw = await this.authorityCall('reserveRelocation', {
      ...comparable,
      key: request.authorityKey.value,
      requestJson: JSON.stringify(comparable),
      target: {
        descriptor: comparable.targetDescriptor,
        descriptorKey: encodeMeshNodeKey(request.targetDescriptor),
        lifecycleGeneration: comparable.targetNodeLifecycleGeneration,
        owner: comparable.targetOwner
      },
      placementProfile: ''
    }, signal);
    return relocationReserveResult(raw);
  }

  async abortRelocationCapacity(
    fence: ZLinkRelocationCapacityFence,
    signal?: AbortSignal
  ): Promise<ZLinkRelocationCapacityAbortResult> {
    const raw = await this.authorityCall('abortRelocation', { fence: fence.value }, signal) as {
      readonly kind: ZLinkRelocationCapacityAbortResult;
    };
    return raw.kind;
  }

  async prepareAggregate(
    request: ZLinkAggregatePrepareRequest,
    signal?: AbortSignal
  ): Promise<ZLinkAggregatePrepareResult> {
    validateAggregate(request);
    const comparable = aggregateRequestJson(request);
    const aggregateKey = `${request.aggregateId.value}:${request.aggregateGeneration}`;
    const fence = {
      aggregateId: request.aggregateId.value,
      aggregateGeneration: request.aggregateGeneration.toString()
    };
    const raw = await this.authorityCall('prepareAggregate', {
      ...comparable,
      aggregateKey,
      fence,
      requestJson: JSON.stringify(comparable)
    }, signal);
    return aggregatePrepareResult(raw);
  }

  async commitAggregate(
    fence: ZLinkAggregateFence,
    signal?: AbortSignal
  ): Promise<ZLinkAggregateCommitResult> {
    return await this.authorityCall('commitAggregate', {
      aggregateKey: `${fence.aggregateId.value}:${fence.aggregateGeneration}`
    }, signal) as ZLinkAggregateCommitResult;
  }

  async abortAggregate(
    fence: ZLinkAggregateFence,
    signal?: AbortSignal
  ): Promise<ZLinkAggregateAbortResult> {
    return await this.authorityCall('abortAggregate', {
      aggregateKey: `${fence.aggregateId.value}:${fence.aggregateGeneration}`
    }, signal) as ZLinkAggregateAbortResult;
  }

  async dispose(): Promise<void> {
    const client = this.client;
    this.client = undefined;
    if (client !== undefined && client !== this.providedClient) {
      if (client.quit !== undefined) {
        await client.quit();
      } else {
        await client.disconnect?.();
      }
    }
  }

  private async write<TRow>(
    kind: LocationKind<TRow>,
    row: TRow,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    try {
      const rowKey = kind.encodeKey(row);
      const meshName = kind.meshOf(row);
      const currentOwnerValue = await this.command([
        'HGET',
        this.keys.rowHash(kind.tag, rowKey),
        'owner'
      ], signal);
      const currentOwner = currentOwnerValue === null ? '' : asString(currentOwnerValue);
      const raw = asArray(await this.eval(WRITE_SCRIPT, [
        this.keys.rowHash(kind.tag, rowKey),
        this.keys.generation(kind.tag, rowKey),
        this.keys.kindIndex(kind.tag),
        currentOwner.length === 0 ? this.keys.schema() : this.keys.lease(currentOwner)
      ], [
        intentName(intent),
        kind.ownerOf(row),
        String(kind.generationOf(row)),
        kind.toJsonText?.(row) ?? JSON.stringify(kind.toJson(row)),
        rowKey,
        this.keys.leasePrefix(),
        this.keys.ownerIndexPrefix(kind.tag),
        this.keys.stamp(kind.tag, meshName),
        meshName === undefined ? '' : this.keys.stamp(kind.tag, undefined),
        meshName === undefined ? '0' : '1',
        meshName ?? ''
      ], signal));
      return toWriteResult(raw);
    } catch (error) {
      throw error;
    }
  }

  private async transitionActorTransfer(
    script: string,
    meshName: string,
    actorId: string,
    transferId: string,
    recoveryOwnerId: string,
    signal?: AbortSignal
  ): Promise<ZLinkActorTransferWriteResult> {
    validateTransferIdentity(meshName, actorId, transferId);
    if (recoveryOwnerId.trim().length === 0) throw new TypeError('recoveryOwnerId is required.');
    const actorRowKey = transferActorRowKey(meshName, actorId);
    const raw = asArray(await this.eval(script, [
      this.keys.actorTransfer(actorRowKey, transferId),
      this.keys.actorTransferByActor(actorRowKey)
    ], [transferId, recoveryOwnerId], signal));
    return transferResultOf(raw, meshName, actorId, transferId);
  }

  private async remove<TRow>(
    kind: LocationKind<TRow>,
    rowKey: string,
    meshName: string | undefined,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult> {
    try {
      const raw = asArray(await this.eval(REMOVE_SCRIPT, [
        this.keys.rowHash(kind.tag, rowKey),
        this.keys.kindIndex(kind.tag)
      ], [
        owner.ownerId,
        String(owner.leaseGeneration),
        rowKey,
        this.keys.ownerIndexPrefix(kind.tag),
        this.keys.stamp(kind.tag, meshName),
        meshName === undefined ? '' : this.keys.stamp(kind.tag, undefined)
      ], signal));
      return toWriteResult(raw);
    } catch (error) {
      throw error;
    }
  }

  private async resolve<TRow>(
    kind: LocationKind<TRow>,
    rowKey: string,
    signal?: AbortSignal
  ): Promise<TRow | undefined> {
    const fields = asArray(await this.command([
      'HMGET',
      this.keys.rowHash(kind.tag, rowKey),
      'json',
      'gen',
      'updatedAtMs'
    ], signal));
    return materialize(kind, fields);
  }

  private async listPage<TRow>(
    kind: LocationKind<TRow>,
    matches: (row: TRow) => boolean,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<TRow>> {
    const pageSize = page?.pageSize ?? 0;
    let members: readonly unknown[];
    let continuationToken: string | undefined;
    if (pageSize <= 0) {
      members = asArray(await this.command(['SMEMBERS', this.keys.kindIndex(kind.tag)], signal));
    } else {
      const scan = asArray(await this.command([
        'SSCAN',
        this.keys.kindIndex(kind.tag),
        page?.continuationToken ?? '0',
        'COUNT',
        String(pageSize)
      ], signal));
      continuationToken = asString(scan[0]) === '0' ? undefined : asString(scan[0]);
      members = asArray(scan[1]);
    }
    const rows = await this.loadRows(kind, members.map(asString), signal);
    return { items: rows.filter(matches), continuationToken };
  }

  private async loadAll<TRow>(kind: LocationKind<TRow>, signal?: AbortSignal): Promise<TRow[]> {
    const members = asArray(await this.command(['SMEMBERS', this.keys.kindIndex(kind.tag)], signal));
    return await this.loadRows(kind, members.map(asString), signal);
  }

  private async loadMeshNodes(signal?: AbortSignal): Promise<ZLinkMeshNodeDescriptor[]> {
    const members = asArray(await this.command([
      'SMEMBERS',
      this.keys.descriptorMeshIndex()
    ], signal)).map(asString);
    const rows: ZLinkMeshNodeDescriptor[] = [];
    for (const rowKey of members) {
      const fields = asArray(await this.command([
        'HMGET',
        this.keys.descriptorMesh(rowKey),
        'json',
        'gen',
        'updatedAtMs'
      ], signal));
      const row = materialize(kindMeshNode, fields);
      if (row !== undefined) rows.push(row);
    }
    return rows;
  }

  private async removeClientServerByRowKey(
    rowKey: string,
    channelName: string,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteStatus> {
    const raw = asArray(await this.eval(SERVICE_DESCRIPTOR_REMOVE_SCRIPT, [
      this.keys.descriptorClientServer(rowKey),
      this.keys.descriptorAdmissionClientServer(rowKey),
      this.keys.descriptorClientServerIndex(),
      this.keys.descriptorClientServerOwnerIndex(
        owner.ownerId,
        owner.leaseGeneration.toString()
      ),
      this.keys.stamp(kindClientServer.tag, undefined),
      this.keys.stamp(kindClientServer.tag, channelName),
      this.keys.descriptorClientServerChannelIndex(channelName)
    ], [
      owner.ownerId,
      owner.leaseGeneration.toString(),
      rowKey
    ], signal));
    return toWriteResult(raw).status;
  }

  private async removeFanoutPublisherByRowKey(
    rowKey: string,
    channelName: string,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteStatus> {
    const raw = asArray(await this.eval(SERVICE_DESCRIPTOR_REMOVE_SCRIPT, [
      this.keys.descriptorFanoutPublisher(rowKey),
      this.keys.descriptorAdmissionFanoutPublisher(rowKey),
      this.keys.descriptorFanoutPublisherIndex(),
      this.keys.descriptorFanoutPublisherOwnerIndex(
        owner.ownerId,
        owner.leaseGeneration.toString()
      ),
      this.keys.stamp(kindFanoutPublisher.tag, undefined),
      this.keys.stamp(kindFanoutPublisher.tag, channelName),
      this.keys.descriptorFanoutPublisherChannelIndex(channelName)
    ], [
      owner.ownerId,
      owner.leaseGeneration.toString(),
      rowKey
    ], signal));
    return toWriteResult(raw).status;
  }

  private async listServiceDescriptors<TRow>(
    kind: LocationKind<TRow>,
    tokenKind: string,
    label: string,
    channelName: string,
    channelIndexKey: string,
    rowPhysicalKey: (rowKey: string) => string,
    maxBytes: number,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<TRow>> {
    const pageSize = page?.pageSize ?? 100;
    if (!Number.isInteger(pageSize) || pageSize < 1 || pageSize > 1000) {
      throw new RangeError(`${label} descriptor pageSize must be between 1 and 1000.`);
    }
    const offset = decodeDescriptorPageToken(
      page?.continuationToken,
      tokenKind,
      channelName,
      label
    );
    const members = asArray(await this.command([
      'ZRANGE',
      channelIndexKey,
      String(offset),
      String(offset + pageSize)
    ], signal)).map(asString);
    const items: TRow[] = [];
    let encodedBytes = 0;
    let consumedMembers = 0;
    for (const rowKey of members) {
      if (items.length === pageSize) break;
      const fields = asArray(await this.command([
        'HMGET',
        rowPhysicalKey(rowKey),
        'json',
        'gen',
        'updatedAtMs'
      ], signal));
      const row = materialize(kind, fields);
      if (row === undefined) {
        consumedMembers++;
        continue;
      }
      const rowBytes = fields[0] === null
        ? 0
        : Buffer.byteLength(asString(fields[0]), 'utf8');
      if (items.length > 0 && encodedBytes + rowBytes > maxBytes) break;
      items.push(row);
      encodedBytes += rowBytes;
      consumedMembers++;
    }
    const nextOffset = offset + consumedMembers;
    return {
      items,
      continuationToken: consumedMembers < members.length || members.length > pageSize
        ? encodeDescriptorPageToken(tokenKind, channelName, nextOffset)
        : undefined
    };
  }

  private async removeOwnedServiceDescriptors<TRow>(
    kind: LocationKind<TRow>,
    ownerIndexKey: string,
    rowPhysicalKey: (rowKey: string) => string,
    remove: (rowKey: string, row: TRow) => Promise<ZLinkLocationWriteStatus>,
    signal?: AbortSignal
  ): Promise<number> {
    const rowKeys = asArray(await this.command(['SMEMBERS', ownerIndexKey], signal)).map(asString);
    let removed = 0;
    for (const rowKey of rowKeys) {
      const fields = asArray(await this.command([
        'HMGET',
        rowPhysicalKey(rowKey),
        'json',
        'gen',
        'updatedAtMs'
      ], signal));
      const row = materialize(kind, fields);
      if (row === undefined) continue;
      if (await remove(rowKey, row) === ZLinkLocationWriteStatus.Stored) removed++;
    }
    return removed;
  }

  private async routingSlotLeaseKeys(
    groupKey: string,
    slotCount: number,
    signal?: AbortSignal
  ): Promise<string[]> {
    const result: string[] = [];
    for (let slot = 1; slot <= slotCount; slot++) {
      const value = await this.command(['HGET', groupKey, `slot:${slot}`], signal);
      if (value === null) {
        result.push(this.keys.schema());
        continue;
      }
      const ownerId = asString(value).split('|', 1)[0] ?? '';
      result.push(ownerId.length === 0 ? this.keys.schema() : this.keys.lease(ownerId));
    }
    return result;
  }

  private async loadRows<TRow>(
    kind: LocationKind<TRow>,
    rowKeys: readonly string[],
    signal?: AbortSignal
  ): Promise<TRow[]> {
    const rows: TRow[] = [];
    for (const rowKey of rowKeys) {
      const row = await this.resolve(kind, rowKey, signal);
      if (row !== undefined) {
        rows.push(row);
      }
    }
    return rows;
  }

  private async eval(script: string, keys: readonly string[], args: readonly string[], signal?: AbortSignal): Promise<unknown> {
    return await this.command(['EVAL', script, String(keys.length), ...keys, ...args], signal);
  }

  private async authorityCall(
    operation: string,
    request: unknown,
    signal?: AbortSignal
  ): Promise<unknown> {
    const value = request as Record<string, any>;
    const authorityKey = typeof value.key === 'string' ? value.key : '';
    const placeholder = this.keys.schema();
    const target = value.target as {
      descriptorKey?: string;
      descriptor?: { meshName: string; rid: string };
      owner?: { ownerId: string };
    } | undefined;
    const recordKey = operation === 'reserve' || operation === 'commit' || operation === 'abort'
      ? this.keys.creation(String(value.reservationId))
      : operation === 'reserveRelocation'
        ? this.keys.relocation(String(value.reservationId))
        : operation === 'abortRelocation'
          ? this.keys.relocation(String(value.fence))
          : operation === 'cas' && typeof value.fence === 'string'
            ? this.keys.relocation(value.fence)
            : operation.startsWith('prepareAggregate')
              || operation.startsWith('commitAggregate')
              || operation.startsWith('abortAggregate')
              ? this.aggregateKeyFromValue(value)
              : placeholder;
    let effectiveTarget = target;
    if (operation === 'cas' && typeof value.fence === 'string') {
      const stored = asString(await this.command(['HGET', recordKey, 'requestJson'], signal));
      if (stored.length > 0) {
        effectiveTarget = (JSON.parse(stored) as Record<string, any>).target;
      }
    }
    const descriptorKey = effectiveTarget?.descriptorKey
      ?? (effectiveTarget?.descriptor === undefined
        ? undefined
        : encodeMeshNodeKey({
            meshName: effectiveTarget.descriptor.meshName,
            rid: ridOf(effectiveTarget.descriptor.rid)
          }));
    const currentOwner = value.currentOwner as { ownerId?: string } | undefined;
    const keys = [
      authorityKey.length === 0 ? placeholder : this.keys.authorityCurrent(authorityKey),
      authorityKey.length === 0 ? placeholder : this.keys.authorityHistory(authorityKey),
      authorityKey.length === 0 ? placeholder : this.keys.authorityHistoryRevisions(authorityKey),
      this.keys.counter(),
      this.keys.authorityKeyIndex(),
      this.keys.membershipCurrent(),
      this.keys.capacityNode('active'),
      this.keys.capacityNode('pending'),
      this.keys.capacityType('active'),
      this.keys.capacityType('pending'),
      descriptorKey === undefined ? placeholder : this.keys.descriptorMesh(descriptorKey),
      descriptorKey === undefined ? placeholder : this.keys.descriptorAdmissionMesh(descriptorKey),
      effectiveTarget?.owner?.ownerId === undefined
        ? placeholder
        : this.keys.lease(effectiveTarget.owner.ownerId),
      recordKey,
      currentOwner?.ownerId === undefined ? placeholder : this.keys.lease(currentOwner.ownerId),
      this.keys.authorityIndexGc(),
      this.keys.scansExpiry(),
      this.keys.scansWatermark(),
      placeholder
    ];
    if (operation === 'prepareAggregate'
      || operation === 'commitAggregate'
      || operation === 'abortAggregate') {
      await this.appendAggregateKeys(operation, value, recordKey, keys, signal);
    }
    if (authorityKey.length > 0) {
      value.keyHex = Buffer.from(authorityKey, 'utf8').toString('hex');
    }
    const encoded = JSON.stringify(value);
    if (Buffer.byteLength(encoded) > 1024 * 1024) {
      throw new RangeError('Redis authority request exceeds 1 MiB.');
    }
    const raw = await this.eval(AUTHORITY_HYBRID_SCRIPT, keys, [
      operation,
      encoded
    ], signal);
    return JSON.parse(asString(raw)) as unknown;
  }

  private aggregateKeyFromValue(value: Record<string, any>): string {
    if (typeof value.aggregateKey === 'string') {
      const separator = value.aggregateKey.lastIndexOf(':');
      return this.keys.aggregate(
        value.aggregateKey.slice(0, separator),
        value.aggregateKey.slice(separator + 1)
      );
    }
    const fence = value.fence as { aggregateId?: string; aggregateGeneration?: string } | undefined;
    if (fence?.aggregateId !== undefined && fence.aggregateGeneration !== undefined) {
      return this.keys.aggregate(fence.aggregateId, fence.aggregateGeneration);
    }
    throw new TypeError('Aggregate identity is required.');
  }

  private async appendAggregateKeys(
    operation: string,
    value: Record<string, any>,
    aggregateKey: string,
    keys: string[],
    signal?: AbortSignal
  ): Promise<void> {
    let aggregate = value;
    if (operation !== 'prepareAggregate') {
      const stored = asString(await this.command(['HGET', aggregateKey, 'requestJson'], signal));
      if (stored.length === 0) return;
      aggregate = JSON.parse(stored) as Record<string, any>;
    }
    const participants = aggregate.participants as readonly Record<string, any>[];
    const fences = aggregate.targetReservations as readonly string[];
    for (const participant of participants) {
      const key = String(participant.key);
      participant.keyHex = Buffer.from(key, 'utf8').toString('hex');
      keys.push(this.keys.authorityCurrent(key));
    }
    if (operation === 'commitAggregate') {
      for (const participant of participants) {
        keys.push(this.keys.authorityHistory(String(participant.key)));
      }
      for (const participant of participants) {
        keys.push(this.keys.authorityHistoryRevisions(String(participant.key)));
      }
      for (const participant of participants) {
        keys.push(this.keys.membershipHistory(String(participant.key)));
      }
      for (const participant of participants) {
        keys.push(this.keys.membershipHistoryRevisions(String(participant.key)));
      }
    }
    const reservationRequests: Record<string, any>[] = [];
    for (const fence of fences) {
      const reservationKey = this.keys.relocation(fence);
      keys.push(reservationKey);
      const raw = await this.command(['HGET', reservationKey, 'requestJson'], signal);
      reservationRequests.push(raw === null
        ? {}
        : JSON.parse(asString(raw)) as Record<string, any>);
    }
    if (operation !== 'abortAggregate') {
      for (const participant of participants) {
        const current = await this.readAuthority(
          { value: String(participant.key) } as ZLinkAuthorityKey,
          signal
        );
        keys.push(current.kind === 'snapshot'
          ? this.keys.lease(current.ownerId)
          : this.keys.schema());
      }
      for (const reservation of reservationRequests) {
        const target = reservation.target as Record<string, any> | undefined;
        const descriptorKey = target?.descriptorKey;
        keys.push(descriptorKey === undefined
          ? this.keys.schema()
          : this.keys.descriptorMesh(String(descriptorKey)));
      }
      for (const reservation of reservationRequests) {
        const target = reservation.target as Record<string, any> | undefined;
        const descriptorKey = target?.descriptorKey;
        keys.push(descriptorKey === undefined
          ? this.keys.schema()
          : this.keys.descriptorAdmissionMesh(String(descriptorKey)));
      }
      for (const reservation of reservationRequests) {
        const target = reservation.target as Record<string, any> | undefined;
        const owner = target?.owner as Record<string, any> | undefined;
        keys.push(owner?.ownerId === undefined
          ? this.keys.schema()
          : this.keys.lease(String(owner.ownerId)));
      }
    }
  }

  private async command(args: RedisCommandValue[], signal?: AbortSignal): Promise<unknown> {
    signal?.throwIfAborted();
    const client = await this.connectedClient(signal);
    return await client.sendCommand(args);
  }

  private async connectedClient(signal?: AbortSignal): Promise<RedisCommandClient> {
    signal?.throwIfAborted();
    const client = this.providedClient ?? this.client;
    if (client === undefined) {
      throw new Error('Redis location store is disposed.');
    }
    if (client.isOpen !== true) {
      await client.connect();
    }
    this.schemaPromise ??= this.ensureSchema(client);
    await this.schemaPromise;
    return client;
  }

  private async ensureSchema(client: RedisCommandClient): Promise<void> {
    const schemaKey = this.keys.schema();
    const existing = asArray(await client.sendCommand([
      'HMGET', schemaKey, 'format', 'epoch'
    ]));
    if (existing[0] === null || existing[0] === undefined) {
      let cursor = '0';
      let occupied = false;
      do {
        const scan = asArray(await client.sendCommand([
          'SCAN', cursor, 'MATCH', this.keys.configuredPrefixPattern(),
          'COUNT', '64'
        ]));
        cursor = asString(scan[0]);
        const found = asArray(scan[1]).map(asString)
          .filter(key => key !== schemaKey);
        occupied ||= found.length > 0;
      } while (!occupied && cursor !== '0');
      if (occupied) {
        throw new Error(
          'Redis location provider schema marker is missing from a non-empty domain.'
        );
      }
      await client.sendCommand([
        'HSET', schemaKey,
        'format', 'location-authority-hybrid-v1',
        'epoch', '1'
      ]);
      return;
    }
    if (asString(existing[0]) !== 'location-authority-hybrid-v1'
        || asString(existing[1]) !== '1') {
      throw new Error('Redis location provider schema is incompatible.');
    }
  }

}

interface AuthorityJson {
  readonly storeVersion: string;
  readonly payload: string;
  readonly objectGeneration: string;
  readonly authorityOwnerGeneration: string;
  readonly ownerId: string;
  readonly ownerLeaseGeneration: string;
  readonly allocation: {
    readonly state: 'pending' | 'active';
    readonly objectKind: 'actor' | 'user_spot' | 'instance_spot';
    readonly stableType: string;
    readonly descriptor: { readonly meshName: string; readonly rid: string };
    readonly descriptorLifecycleGeneration: string;
    readonly capacityDelta: number;
  };
  readonly storeNowMs?: number;
}

function authorityRead(raw: unknown): ZLinkAuthorityReadResult {
  const value = raw as { readonly kind: string; readonly storeNowMs: number } & AuthorityJson;
  return value.kind === 'missing'
    ? { kind: 'missing', storeNow: fromUnixMs(value.storeNowMs) }
    : authoritySnapshot(value, value.storeNowMs);
}

function authoritySnapshot(value: AuthorityJson, storeNowMs: number): ZLinkAuthoritySnapshot {
  return {
    kind: 'snapshot',
    storeVersion: { value: value.storeVersion } as ZLinkAuthorityStoreVersion,
    payload: Buffer.from(value.payload, 'base64'),
    objectGeneration: BigInt(value.objectGeneration),
    authorityOwnerGeneration: BigInt(value.authorityOwnerGeneration),
    ownerId: value.ownerId,
    ownerLeaseGeneration: BigInt(value.ownerLeaseGeneration),
    allocation: {
      ...value.allocation,
      descriptor: {
        meshName: value.allocation.descriptor.meshName,
        rid: ridOf(value.allocation.descriptor.rid)
      },
      descriptorLifecycleGeneration: BigInt(value.allocation.descriptorLifecycleGeneration)
    },
    storeNow: fromUnixMs(storeNowMs)
  };
}

function authorityCasResult(raw: unknown): ZLinkAuthorityCompareExchangeResult {
  const value = raw as Record<string, unknown>;
  switch (value.kind) {
    case 'stored': {
      const snapshot = authoritySnapshot(value as unknown as AuthorityJson, Number(value.storeNowMs));
      const { kind: _, ...stored } = snapshot;
      return { kind: 'stored', ...stored };
    }
    case 'deleted':
      return {
        kind: 'deleted',
        storeVersion: { value: String(value.storeVersion) } as ZLinkAuthorityStoreVersion,
        storeNow: fromUnixMs(Number(value.storeNowMs))
      };
    case 'conflict':
      return { kind: 'conflict', current: authorityRead(value.current) };
    default:
      return { kind: 'generationExhausted' };
  }
}

function objectReserveResult(raw: unknown): ZLinkObjectReserveResult {
  const value = raw as Record<string, unknown>;
  switch (value.kind) {
    case 'reserved':
      return {
        kind: 'reserved',
        reservationId: String(value.reservationId),
        creating: authorityRead(value.creating) as ZLinkAuthoritySnapshot
      };
    case 'alreadyExists':
      return { kind: 'alreadyExists', current: authorityRead(value.current) as ZLinkAuthoritySnapshot };
    case 'typeMismatch':
      return { kind: 'typeMismatch', current: authorityRead(value.current) as ZLinkAuthoritySnapshot };
    case 'conflict':
      return { kind: 'conflict', current: authorityRead(value.current) };
    case 'placementCapacityExhausted':
      return { kind: 'placementCapacityExhausted' };
    default:
      return { kind: 'generationExhausted' };
  }
}

function objectCommitResult(raw: unknown): ZLinkObjectCommitResult {
  const value = raw as Record<string, unknown>;
  if (value.kind === 'committed' || value.kind === 'alreadyCommitted') {
    return {
      kind: value.kind,
      ready: authorityRead(value.ready) as ZLinkAuthoritySnapshot
    };
  }
  return value.kind === 'generationExhausted'
    ? { kind: 'generationExhausted' }
    : { kind: 'stale' };
}

function relocationReserveResult(raw: unknown): ZLinkRelocationCapacityReserveResult {
  const value = raw as Record<string, unknown>;
  if (value.kind === 'reserved' || value.kind === 'alreadyReserved') {
    return {
      kind: value.kind,
      fence: { value: String(value.fence) } as ZLinkRelocationCapacityFence
    };
  }
  if (value.kind === 'conflict') {
    return { kind: 'conflict', current: authorityRead(value.current) };
  }
  return value.kind === 'placementCapacityExhausted'
    ? { kind: 'placementCapacityExhausted' }
    : { kind: 'targetUnavailable' };
}

function aggregatePrepareResult(raw: unknown): ZLinkAggregatePrepareResult {
  const value = raw as Record<string, unknown>;
  if (value.kind === 'prepared' || value.kind === 'alreadyPrepared') {
    const fence = value.fence as { readonly aggregateId: string; readonly aggregateGeneration: string };
    return {
      kind: value.kind,
      fence: {
        aggregateId: { value: fence.aggregateId } as ZLinkAggregateFence['aggregateId'],
        aggregateGeneration: BigInt(fence.aggregateGeneration)
      }
    };
  }
  if (value.kind === 'stale') return { kind: 'stale' };
  if (value.kind === 'generationExhausted') return { kind: 'generationExhausted' };
  return { kind: 'conflict' };
}

function creationTargetJson(target: ZLinkObjectCommitRequest['target']): unknown {
  return {
    descriptor: { meshName: target.meshName, rid: routingIdHex(target.nodeRid) },
    descriptorKey: encodeMeshNodeKey({ meshName: target.meshName, rid: target.nodeRid }),
    lifecycleGeneration: target.nodeLifecycleGeneration.toString(),
    owner: ownerJson(target.owner)
  };
}

function ownerJson(owner: ZLinkLocationOwnerToken): unknown {
  return { ownerId: owner.ownerId, leaseGeneration: owner.leaseGeneration.toString() };
}

function relocationRequestJson(request: ZLinkRelocationCapacityReservationRequest): Record<string, unknown> {
  return {
    reservationId: request.reservationId,
    key: request.authorityKey.value,
    expectedStoreVersion: request.expectedStoreVersion.value,
    objectKind: request.objectKind,
    stableType: request.stableType,
    sourceDescriptor: {
      meshName: request.sourceDescriptor.meshName,
      rid: routingIdHex(request.sourceDescriptor.rid)
    },
    sourceDescriptorKey: encodeMeshNodeKey(request.sourceDescriptor),
    sourceNodeLifecycleGeneration: request.sourceNodeLifecycleGeneration.toString(),
    sourceOwner: ownerJson(request.sourceOwner),
    targetDescriptor: {
      meshName: request.targetDescriptor.meshName,
      rid: routingIdHex(request.targetDescriptor.rid)
    },
    targetDescriptorKey: encodeMeshNodeKey(request.targetDescriptor),
    targetNodeLifecycleGeneration: request.targetNodeLifecycleGeneration.toString(),
    targetOwner: ownerJson(request.targetOwner),
    capacityDelta: request.capacityDelta
  };
}

function aggregateRequestJson(request: ZLinkAggregatePrepareRequest): Record<string, unknown> {
  return {
    aggregateId: request.aggregateId.value,
    aggregateGeneration: request.aggregateGeneration.toString(),
    participants: request.participants.map(participant => ({
      key: participant.authorityKey.value,
      expectedStoreVersion: participant.expectedStoreVersion.value,
      ownerTransition: participant.ownerTransition,
      authorityPayload: encodePayload(participant.authorityPayload),
      membershipMutation: encodePayload(participant.membershipMutation)
    })),
    inventoryDigest: encodePayload(request.inventoryDigest),
    targetReservations: request.targetReservations.map(fence => fence.value),
    targetOwner: ownerJson(request.targetOwner)
  };
}

function validateAggregate(request: ZLinkAggregatePrepareRequest): void {
  if (request.aggregateGeneration < 1n || request.participants.length < 1 || request.participants.length > 1024) {
    throw new RangeError('Aggregate generation and participant count are invalid.');
  }
  if (request.inventoryDigest.byteLength !== 32) {
    throw new TypeError('Aggregate inventory digest must contain 32 bytes.');
  }
  const keys = request.participants.map(participant => participant.authorityKey.value);
  const sorted = [...keys].sort();
  if (new Set(keys).size !== keys.length || keys.some((key, index) => key !== sorted[index])) {
    throw new TypeError('Aggregate participants must be unique and canonically sorted.');
  }
  for (const participant of request.participants) {
    validateAuthorityPayload(participant.authorityPayload);
  }
}

function validateAuthorityPayload(payload: Uint8Array): void {
  if (payload.byteLength > 1024 * 1024) {
    throw new RangeError('Authority payload exceeds 1 MiB.');
  }
}

function validateCapacityDelta(value: number): void {
  if (!Number.isInteger(value) || value < 1 || value > 0x7fff_ffff) {
    throw new RangeError('Placement capacity delta must be in 1..2147483647.');
  }
}

function encodePayload(payload: Uint8Array): string {
  return Buffer.from(payload).toString('base64');
}

function requireText(value: string, field: string): string {
  if (value.trim().length === 0) throw new TypeError(`${field} is required.`);
  return value;
}

function encodeAuthorityCursor(scanId: string, lastHex: string, prefix: string): string {
  const encoded = Buffer.from(JSON.stringify({ scanId, lastHex, prefix })).toString('base64url');
  if (Buffer.byteLength(encoded) > 4096) {
    throw new RangeError('Authority scan cursor exceeds 4096 bytes.');
  }
  return encoded;
}

function decodeAuthorityCursor(encoded: string): { scanId: string; lastHex: string; prefix: string } {
  if (Buffer.byteLength(encoded) > 4096) {
    throw new TypeError('Authority scan cursor is invalid.');
  }
  try {
    const value = JSON.parse(Buffer.from(encoded, 'base64url').toString('utf8')) as {
      readonly scanId: string;
      readonly lastHex: string;
      readonly prefix: string;
    };
    if (typeof value.scanId !== 'string'
      || typeof value.lastHex !== 'string'
      || typeof value.prefix !== 'string') throw new Error();
    return value;
  } catch {
    throw new TypeError('Authority scan cursor is invalid.');
  }
}

function canonicalObjectCapabilities(
  values: ZLinkMeshNodeDescriptor['objectCapabilities']
): ZLinkMeshNodeDescriptor['objectCapabilities'] {
  return [...values]
    .map(value => ({
      ...value,
      placementProfiles: [...value.placementProfiles].sort(compareUtf8)
    }))
    .sort((left, right) => {
      const byKind = compareUtf8(left.objectKind, right.objectKind);
      return byKind !== 0 ? byKind : compareUtf8(left.stableType, right.stableType);
    });
}

function meshDescriptorImmutableFingerprint(descriptor: ZLinkMeshNodeDescriptor): string {
  const segments = [
    'zlink-mesh-node-immutable-v1',
    descriptor.meshName,
    routingIdHex(descriptor.rid).toLowerCase(),
    descriptor.lifecycleGeneration.toString(),
    descriptor.endpoint
  ];
  const channelNames = Object.keys(descriptor.channelWeights).sort(compareUtf8);
  segments.push(channelNames.length.toString(), ...channelNames);
  segments.push(
    descriptor.securityIdentity,
    descriptor.applicationVersion.toString(),
    descriptor.objectRole,
    descriptor.objectCapacity.maxActiveObjects.toString(),
    descriptor.objectCapacity.maxPendingActivations.toString()
  );
  const capabilities = canonicalObjectCapabilities(descriptor.objectCapabilities);
  segments.push(capabilities.length.toString());
  for (const capability of capabilities) {
    segments.push(
      capability.objectKind,
      capability.stableType,
      capability.policy,
      capability.hasSnapshotAdapter ? '1' : '0',
      capability.placementProfiles.length.toString(),
      ...capability.placementProfiles,
      capability.activeLimit?.toString() ?? '',
      capability.pendingLimit?.toString() ?? ''
    );
  }
  const preimage = segments
    .map(value => `${Buffer.byteLength(value, 'utf8')}:${value}`)
    .join('');
  return createHash('sha256').update(preimage, 'utf8').digest('hex');
}

function clientServerImmutableFingerprint(
  descriptor: ZLinkClientServerServerDescriptor
): string {
  const segments = [
    'zlink-client-server-immutable-v1',
    descriptor.channelName,
    routingIdHex(descriptor.serverRid).toLowerCase(),
    descriptor.lifecycleGeneration.toString(),
    descriptor.endpoint,
    descriptor.securityIdentity
  ];
  const preimage = segments
    .map(value => `${Buffer.byteLength(value, 'utf8')}:${value}`)
    .join('');
  return createHash('sha256').update(preimage, 'utf8').digest('hex');
}

function fanoutPublisherImmutableFingerprint(
  descriptor: ZLinkFanoutPublisherDescriptor
): string {
  const segments = [
    'zlink-fanout-publisher-immutable-v1',
    descriptor.channelName,
    routingIdHex(descriptor.publisherRid).toLowerCase(),
    descriptor.lifecycleGeneration.toString(),
    descriptor.endpoint,
    descriptor.securityIdentity
  ];
  const preimage = segments
    .map(value => `${Buffer.byteLength(value, 'utf8')}:${value}`)
    .join('');
  return createHash('sha256').update(preimage, 'utf8').digest('hex');
}

function compareUtf8(left: string, right: string): number {
  return Buffer.compare(Buffer.from(left, 'utf8'), Buffer.from(right, 'utf8'));
}

function encodeDescriptorPageToken(kind: string, channelName: string, offset: number): string {
  return Buffer.from(JSON.stringify({
    kind,
    channelName,
    offset
  }), 'utf8').toString('base64url');
}

function decodeDescriptorPageToken(
  token: string | undefined,
  kind: string,
  channelName: string,
  label: string
): number {
  if (token === undefined) return 0;
  if (Buffer.byteLength(token, 'utf8') > 4096) {
    throw new TypeError(`${label} descriptor continuation token is invalid.`);
  }
  try {
    const value = JSON.parse(Buffer.from(token, 'base64url').toString('utf8')) as {
      readonly kind?: unknown;
      readonly channelName?: unknown;
      readonly offset?: unknown;
    };
    if (value.kind !== kind
      || value.channelName !== channelName
      || !Number.isSafeInteger(value.offset)
      || Number(value.offset) < 0) {
      throw new Error();
    }
    return Number(value.offset);
  } catch {
    throw new TypeError(`${label} descriptor continuation token is invalid.`);
  }
}

function validateMeshDescriptor(descriptor: ZLinkMeshNodeDescriptor): void {
  requireDescriptorText(descriptor.meshName, 'mesh name');
  requireDescriptorText(descriptor.ownerId, 'descriptor owner ID');
  const maxGeneration = 0x7fff_ffff_ffff_ffffn;
  if (descriptor.lifecycleGeneration < 1n || descriptor.lifecycleGeneration > maxGeneration
    || descriptor.descriptorRevision < 1n || descriptor.descriptorRevision > maxGeneration
    || descriptor.leaseGeneration < 1n || descriptor.leaseGeneration > maxGeneration
    || descriptor.applicationVersion < 0n || descriptor.applicationVersion > maxGeneration) {
    throw new RangeError('MeshNode descriptor generations are invalid.');
  }
  const capacity = descriptor.objectCapacity;
  for (const value of [
    capacity.activeObjects,
    capacity.pendingActivations,
    capacity.maxActiveObjects,
    capacity.maxPendingActivations
  ]) {
    if (!Number.isSafeInteger(value) || value < 0) {
      throw new RangeError('MeshNode object capacity must contain non-negative safe integers.');
    }
  }
  if (capacity.activeObjects > capacity.maxActiveObjects
    || capacity.pendingActivations > capacity.maxPendingActivations
    || capacity.maxActiveObjects === 0
    || capacity.maxPendingActivations === 0
    || descriptor.placementWeight < 0
    || descriptor.placementWeight > 100
    || descriptor.objectCapabilities.length > 1024
    || descriptor.objectRole !== 'server' && descriptor.objectCapabilities.length !== 0) {
    throw new RangeError('MeshNode current object capacity exceeds its configured maximum.');
  }
  const capabilityKeys = new Set<string>();
  for (const capability of descriptor.objectCapabilities) {
    const key = `${capability.objectKind}\0${requireDescriptorText(
      capability.stableType, 'capability stable type')}`;
    if (capabilityKeys.has(key)) throw new TypeError('MeshNode object capabilities must be unique.');
    capabilityKeys.add(key);
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
      || capability.placementProfiles.some(profile => {
        try {
          requireDescriptorText(profile, 'placement profile');
          return false;
        } catch {
          return true;
        }
      })) {
      throw new TypeError('MeshNode placement profiles must be unique non-empty values.');
    }
  }
}

function validateClientServerDescriptor(
  descriptor: ZLinkClientServerServerDescriptor
): void {
  requireDescriptorText(descriptor.channelName, 'ClientServer channel name');
  requireDescriptorText(descriptor.endpoint, 'ClientServer endpoint');
  requireDescriptorText(descriptor.securityIdentity, 'ClientServer security identity');
  requireDescriptorText(descriptor.ownerId, 'ClientServer owner ID');
  routingIdHex(descriptor.serverRid);
  const maxGeneration = 0x7fff_ffff_ffff_ffffn;
  if (descriptor.lifecycleGeneration < 1n
    || descriptor.lifecycleGeneration > maxGeneration
    || descriptor.descriptorRevision < 1n
    || descriptor.descriptorRevision > maxGeneration
    || descriptor.leaseGeneration < 1n
    || descriptor.leaseGeneration > maxGeneration) {
    throw new RangeError('ClientServer descriptor generations are invalid.');
  }
  if (!Number.isInteger(descriptor.weight)
    || descriptor.weight < 0
    || descriptor.weight > 100) {
    throw new RangeError('ClientServer descriptor weight must be between 0 and 100.');
  }
  if (!Object.values(ZLinkFrameworkRuntimeState).includes(descriptor.state)) {
    throw new RangeError('ClientServer descriptor runtime state is invalid.');
  }
  const encoded = kindClientServer.toJsonText?.(descriptor)
    ?? JSON.stringify(kindClientServer.toJson(descriptor));
  if (Buffer.byteLength(encoded, 'utf8') > 1024 * 1024) {
    throw new RangeError('ClientServer descriptor exceeds the 1 MiB encoded limit.');
  }
}

function validateFanoutPublisherDescriptor(
  descriptor: ZLinkFanoutPublisherDescriptor
): void {
  requireDescriptorText(descriptor.channelName, 'fanout channel name');
  requireDescriptorText(descriptor.endpoint, 'fanout publisher endpoint');
  requireDescriptorText(descriptor.securityIdentity, 'fanout publisher security identity');
  requireDescriptorText(descriptor.ownerId, 'fanout publisher owner ID');
  routingIdHex(descriptor.publisherRid);
  const maxGeneration = 0x7fff_ffff_ffff_ffffn;
  if (descriptor.lifecycleGeneration < 1n
    || descriptor.lifecycleGeneration > maxGeneration
    || descriptor.descriptorRevision < 1n
    || descriptor.descriptorRevision > maxGeneration
    || descriptor.leaseGeneration < 1n
    || descriptor.leaseGeneration > maxGeneration) {
    throw new RangeError('Fanout publisher descriptor generations are invalid.');
  }
  if (!Object.values(ZLinkFrameworkRuntimeState).includes(descriptor.state)) {
    throw new RangeError('Fanout publisher descriptor runtime state is invalid.');
  }
  const encoded = kindFanoutPublisher.toJsonText?.(descriptor)
    ?? JSON.stringify(kindFanoutPublisher.toJson(descriptor));
  if (Buffer.byteLength(encoded, 'utf8') > 1024 * 1024) {
    throw new RangeError('Fanout publisher descriptor exceeds the 1 MiB encoded limit.');
  }
}

function requireDescriptorText(value: string, field: string): string {
  const size = Buffer.byteLength(value, 'utf8');
  if (size < 1 || size > 255 || value.includes('\0')) {
    throw new TypeError(`${field} must contain 1..255 UTF-8 bytes without NUL.`);
  }
  return value;
}

const TRANSFER_HASH_FIELDS = [
  'state',
  'source',
  'target',
  'expectedActorGeneration',
  'expectedMembershipEpoch',
  'participants',
  'recoveryOwnerId',
  'recoveryLeaseExpiresAtMs',
  'updatedAtMs'
] as const;
const TRANSFER_ID_PATTERN = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;

function transferActorRowKey(meshName: string, actorId: string): string {
  return encodeKeySegments(meshName, actorId);
}

function serializeActorRef(actorRef: ZLinkActorTransferRecord['source']): string {
  return [
    '{',
    `"nodeRid":${JSON.stringify(routingIdHex(actorRef.nodeRid))}`,
    `,"actorId":${JSON.stringify(actorRef.actorId)}`,
    `,"generation":${actorRef.generation}`,
    '}'
  ].join('');
}

function parseActorRef(json: string): ZLinkActorTransferRecord['source'] {
  const row = JSON.parse(json.replace(/("generation":)([0-9]+)/, '$1"$2"')) as {
    nodeRid: string;
    actorId: string;
    generation: string;
  };
  return {
    nodeRid: ridOf(row.nodeRid),
    actorId: row.actorId,
    generation: BigInt(row.generation)
  };
}

function serializeParticipants(participants: ReadonlySet<RoutingId>): string {
  return JSON.stringify([...participants].map(routingIdHex).sort());
}

function transferResultOf(
  raw: readonly unknown[],
  meshName: string,
  actorId: string,
  transferId: string
): ZLinkActorTransferWriteResult {
  const status = asString(raw[0]);
  if (status !== 'stored') {
    const mapped = status === 'notfound'
      ? 'notFound'
      : status === 'conflict'
        ? 'rejectedConflict'
        : status === 'invalid'
          ? 'invalidState'
          : 'ignoredStale';
    return { status: mapped };
  }
  return {
    status: 'stored',
    record: materializeTransfer(meshName, actorId, transferId, asArray(raw[1]))
  };
}

function materializeTransfer(
  meshName: string,
  actorId: string,
  transferId: string,
  fields: readonly unknown[]
): ZLinkActorTransferRecord {
  if (fields.length !== TRANSFER_HASH_FIELDS.length || fields.some((field) => field === null)) {
    throw new Error('Redis Actor transfer record is incomplete.');
  }
  return {
    meshName,
    actorId,
    transferId,
    state: transferStateOf(asString(fields[0])),
    source: parseActorRef(asString(fields[1])),
    target: parseActorRef(asString(fields[2])),
    expectedActorGeneration: BigInt(asString(fields[3])),
    expectedMembershipEpoch: BigInt(asString(fields[4])),
    participants: new Set(
      (JSON.parse(asString(fields[5])) as string[]).map(ridOf)
    ),
    recoveryOwnerId: asString(fields[6]),
    recoveryLeaseExpiresAt: fromUnixMs(toNumber(fields[7])),
    updatedAt: fromUnixMs(toNumber(fields[8]))
  };
}

function transferStateOf(state: string): ZLinkActorTransferState {
  switch (state) {
    case 'Prepared': return 'prepared';
    case 'Committed': return 'committed';
    case 'Activated': return 'activated';
    case 'Aborted': return 'aborted';
    default: throw new Error(`Unknown Redis Actor transfer state '${state}'.`);
  }
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

function decodeMembers(value: string): readonly ZLinkRoutingIdSlotAllocationMember[] {
  const decoded = JSON.parse(value) as Array<{
    MeshName?: string;
    RoutingIdPrefix?: string;
    meshName?: string;
    routingIdPrefix?: string;
  }>;
  return decoded.map((member) => ({
    meshName: member.MeshName ?? member.meshName ?? '',
    routingIdPrefix: member.RoutingIdPrefix ?? member.routingIdPrefix ?? ''
  }));
}

function validateAcquire(request: ZLinkRoutingIdSlotAcquireRequest): void {
  validateGroupName(request.groupName);
  if (!Number.isInteger(request.slotCount) || request.slotCount < 1) throw new RangeError('slotCount must be positive.');
  if (request.ownerId.trim().length === 0 || request.members.length === 0) throw new TypeError('ownerId and members are required.');
  if (!Number.isFinite(request.leaseTtlMs) || request.leaseTtlMs <= 0) throw new RangeError('leaseTtlMs must be positive.');
}

function validateRelease(groupName: string, slot: number, owner: ZLinkLocationOwnerToken): void {
  validateGroupName(groupName);
  if (!Number.isInteger(slot) || slot < 1) throw new RangeError('slot must be positive.');
  if (owner.ownerId.trim().length === 0 || owner.leaseGeneration < 1n) {
    throw new TypeError('owner token is invalid.');
  }
}

function validateGroupName(groupName: string): void {
  if (groupName.trim().length === 0) throw new TypeError('groupName must not be empty.');
}

function validateOwnerLeaseInput(ownerId: string, leaseTtlMs: number): void {
  if (ownerId.trim().length === 0) throw new TypeError('ownerId is required.');
  if (!Number.isSafeInteger(leaseTtlMs) || leaseTtlMs < 1) {
    throw new RangeError('leaseTtlMs must be a positive safe integer.');
  }
}
