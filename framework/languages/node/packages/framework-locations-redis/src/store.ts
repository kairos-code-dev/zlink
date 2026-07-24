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
import { encodeAuthorityKey } from './authority-key-codec';
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
  type ZLinkCapacityVector,
  type ZLinkAuthorityStoreVersion,
  type ZLinkMeshNodeDescriptor,
  type ZLinkMeshNodeDescriptorKey,
  type ZLinkObjectAbortRequest,
  type ZLinkObjectAbortResult,
  type ZLinkObjectCommitRequest,
  type ZLinkObjectCommitResult,
  type ZLinkObjectCreationCompleteRequest,
  type ZLinkObjectCreationCompleteResult,
  type ZLinkObjectReserveRequest,
  type ZLinkObjectReserveResult,
  type ZLinkCreationOperationIdentity,
  type ZLinkCreationTerminalPublication,
  type ZLinkCreationTerminalReadResult,
  type ZLinkCreationTerminalRecord,
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
const CREATION_TERMINAL_RETENTION_MS = 5 * 60 * 1000;
const MAX_CREATION_TERMINAL_BYTES = 1024 * 1024;

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
    const entryAuthorityKey = descriptor.entrySpotId === undefined
      ? ''
      : encodeAuthorityKey('user_spot', descriptor.entrySpotId);
    const entryIdentityClaimKey = entryAuthorityKey.length === 0
      ? this.keys.schema()
      : this.keys.entrySpotIdentityClaim(entryAuthorityKey);
    const raw = asArray(await this.eval(MESH_DESCRIPTOR_WRITE_SCRIPT, [
      this.keys.descriptorMesh(rowKey),
      this.keys.descriptorAdmissionMesh(rowKey),
      this.keys.descriptorMeshIndex(),
      this.keys.lease(descriptor.ownerId),
      this.keys.counter(),
      this.keys.descriptorMeshOwnerIndex(
        descriptor.ownerId,
        descriptor.leaseGeneration.toString()
      ),
      entryIdentityClaimKey
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
      String(descriptor.populationCapacity.actors.limit),
      String(descriptor.populationCapacity.spots.limit),
      String(descriptor.activationConcurrency.limit),
      descriptor.meshName,
      rowKey,
      descriptor.applicationVersion.toString(),
      entryAuthorityKey,
      descriptor.entrySpotId ?? ''
    ], signal));
    return toWriteResult(raw);
  }

  async removeMeshNode(
    key: ZLinkMeshNodeDescriptorKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult['status']> {
    const rowKey = encodeMeshNodeKey(key);
    const fields = asArray(await this.command([
      'HMGET',
      this.keys.descriptorMesh(rowKey),
      'json',
      'gen',
      'updatedAtMs'
    ], signal));
    const descriptor = materialize(kindMeshNode, fields);
    const entryAuthorityKey = descriptor?.entrySpotId === undefined
      ? ''
      : encodeAuthorityKey('user_spot', descriptor.entrySpotId);
    const raw = asArray(await this.eval(MESH_DESCRIPTOR_REMOVE_SCRIPT, [
      this.keys.descriptorMesh(rowKey),
      this.keys.descriptorAdmissionMesh(rowKey),
      this.keys.descriptorMeshIndex(),
      this.keys.descriptorMeshOwnerIndex(owner.ownerId, owner.leaseGeneration.toString()),
      entryAuthorityKey.length === 0
        ? this.keys.schema()
        : this.keys.entrySpotIdentityClaim(entryAuthorityKey)
    ], [
      owner.ownerId,
      owner.leaseGeneration.toString(),
      rowKey,
      descriptor?.entrySpotId ?? ''
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
        readonly actorActive: Readonly<Record<string, number>>;
        readonly actorReserved: Readonly<Record<string, number>>;
        readonly spotActive: Readonly<Record<string, number>>;
        readonly spotReserved: Readonly<Record<string, number>>;
        readonly typeActive: Readonly<Record<string, number>>;
        readonly typeReserved: Readonly<Record<string, number>>;
      }>
    ]);
    return rows
      .filter(row => row.meshName === meshName)
      .map(row => {
        const descriptorKey = encodeMeshNodeKey({
          meshName: row.meshName,
          rid: row.rid
        });
        const lifecycleGeneration = row.lifecycleGeneration.toString();
        const actorKey = encodeKeySegments(
          descriptorKey,
          lifecycleGeneration,
          'actor'
        );
        const spotKey = encodeKeySegments(
          descriptorKey,
          lifecycleGeneration,
          'spot'
        );
        const spotTypes = row.populationCapacity.spotTypes.map(capability => {
          const typeKey = encodeKeySegments(
            descriptorKey,
            lifecycleGeneration,
            'spot',
            capability.objectKind,
            capability.stableType
          );
          return {
            ...capability,
            active: Number(projection.typeActive[typeKey] ?? 0),
            reserved: Number(projection.typeReserved[typeKey] ?? 0)
          };
        });
        return {
          ...row,
          populationCapacity: {
            actors: {
              ...row.populationCapacity.actors,
              active: Number(projection.actorActive[actorKey] ?? 0),
              reserved: Number(projection.actorReserved[actorKey] ?? 0)
            },
            spots: {
              ...row.populationCapacity.spots,
              active: Number(projection.spotActive[spotKey] ?? 0),
              reserved: Number(projection.spotReserved[spotKey] ?? 0)
            },
            spotTypes
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
    const meshNodeRemoved = await this.removeOwnedServiceDescriptors(
      kindMeshNode,
      this.keys.descriptorMeshOwnerIndex(
        ownerId,
        owner.leaseGeneration.toString()
      ),
      (rowKey) => this.keys.descriptorMesh(rowKey),
      (_rowKey, descriptor) => this.removeMeshNode(
        { meshName: descriptor.meshName, rid: descriptor.rid },
        owner,
        signal
      ),
      signal
    );
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
    return BigInt(meshNodeRemoved + removed + clientServerRemoved + fanoutRemoved);
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
      this.keys.capacityActor('active'), this.keys.capacityActor('reserved'),
      this.keys.capacitySpotType('active'), this.keys.capacitySpotType('reserved'),
      placeholder, placeholder, placeholder, placeholder, placeholder,
      this.keys.authorityIndexGc(), this.keys.scansExpiry(),
      this.keys.scansWatermark(), scanKey,
      this.keys.capacitySpot('active'), this.keys.capacitySpot('reserved')
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
        dynamicStart: 22
      })
    ], signal))) as {
      readonly kind: 'page' | 'scanExpired';
      readonly rows?: readonly { readonly key: string; readonly row: AuthorityJson }[];
      readonly lastHex?: string;
      readonly hasMore?: boolean;
      readonly storeNowMs?: number;
    };
    if (raw.kind === 'scanExpired') return { kind: 'scanExpired' };
    // Redis cjson encodes an empty Lua table as `{}` rather than `[]`.
    const rows = Array.isArray(raw.rows) ? raw.rows : [];
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
    validateCapacityVector(request.capacity);
    validateAuthorityPayload(request.creatingPayload);
    requireText(request.intent.requestContentReference, 'creation content reference');
    if (
      request.intent.requestSha256.byteLength !== 32
      || request.intent.requestEncodedSize < 0n
      || request.intent.requestEncodedSize > 1024n * 1024n
    ) {
      throw new TypeError('Object creation content receipt is invalid.');
    }
    const reservationId = randomUUID();
    const raw = await this.authorityCall('reserve', {
      key: encodeAuthorityKey(
        request.key.kind,
        requireText(request.key.globalId, 'object global ID')
      ),
      objectKind: request.key.kind,
      stableType: requireText(request.intent.stableType, 'stable type'),
      capacity: capacityJson(request.capacity),
      capacityBundle: encodeCapacityBundle(request.capacity),
      payload: encodePayload(request.creatingPayload),
      reservationId,
      intent: {
        requestContentReference: request.intent.requestContentReference,
        requestSha256: Buffer.from(request.intent.requestSha256).toString('hex'),
        requestEncodedSize: request.intent.requestEncodedSize.toString()
      },
      target: creationTargetJson(request.target)
    }, signal);
    return objectReserveResult(raw);
  }

  async readCreationTerminal(
    operation: ZLinkCreationOperationIdentity,
    signal?: AbortSignal
  ): Promise<ZLinkCreationTerminalReadResult> {
    validateCreationOperation(operation);
    const raw = await this.authorityCall('readCreationTerminal', {
      operation: creationOperationJson(operation)
    }, signal) as Record<string, unknown>;
    if (raw.kind === 'missing') {
      return { kind: 'missing', storeNow: fromUnixMs(Number(raw.storeNowMs)) };
    }
    if (raw.kind !== 'terminal') {
      throw new Error('Redis creation terminal result is invalid.');
    }
    const record = creationTerminalRecord(raw);
    if (String(record.operation.sourceNodeRid) !== String(operation.sourceNodeRid)
      || record.operation.sourceNodeGeneration !== operation.sourceNodeGeneration
      || record.operation.operationId.high !== operation.operationId.high
      || record.operation.operationId.low !== operation.operationId.low) {
      throw new Error('Redis creation terminal identity does not match its exact key.');
    }
    return { kind: 'found', record };
  }

  async commit(
    request: ZLinkObjectCommitRequest,
    signal?: AbortSignal
  ): Promise<ZLinkObjectCommitResult> {
    validateAuthorityPayload(request.readyPayload);
    if (request.key.kind === 'actor') {
      throw new TypeError('Actor creation must use completeCreation.');
    }
    const raw = await this.authorityCall('commit', {
      key: encodeAuthorityKey(
        request.key.kind,
        requireText(request.key.globalId, 'object global ID')
      ),
      reservationId: request.reservationId,
      expectedStoreVersion: request.expectedStoreVersion,
      target: creationTargetJson(request.target),
      payload: encodePayload(request.readyPayload)
    }, signal);
    return objectCommitResult(raw);
  }

  async completeCreation(
    request: ZLinkObjectCreationCompleteRequest,
    signal?: AbortSignal
  ): Promise<ZLinkObjectCreationCompleteResult> {
    if (request.key.kind !== 'actor') {
      throw new TypeError('completeCreation is reserved for Actor creation.');
    }
    if (request.completion.kind === 'created') {
      validateAuthorityPayload(request.completion.readyPayload);
    }
    const terminal = creationTerminalJson(request.completion.terminal);
    const raw = await this.authorityCall('completeCreation', {
      key: encodeAuthorityKey(
        request.key.kind,
        requireText(request.key.globalId, 'object global ID')
      ),
      reservationId: request.reservationId,
      expectedStoreVersion: request.expectedStoreVersion,
      target: creationTargetJson(request.target),
      completion: {
        kind: request.completion.kind,
        terminal,
        readyPayload: request.completion.kind === 'created'
          ? encodePayload(request.completion.readyPayload)
          : undefined
      }
    }, signal) as Record<string, unknown>;
    switch (raw.kind) {
      case 'created':
        return {
          kind: 'created',
          ready: authorityRead(raw.ready) as ZLinkAuthoritySnapshot,
          terminal: creationTerminalRecord(raw.terminal as Record<string, unknown>)
        };
      case 'rejected':
      case 'failed':
      case 'alreadyCompleted':
        return {
          kind: raw.kind,
          terminal: creationTerminalRecord(raw.terminal as Record<string, unknown>)
        };
      case 'generationExhausted': return { kind: 'generationExhausted' };
      default: return { kind: 'stale' };
    }
  }

  async abort(
    request: ZLinkObjectAbortRequest,
    signal?: AbortSignal
  ): Promise<ZLinkObjectAbortResult> {
    return await this.authorityCall('abort', {
      key: encodeAuthorityKey(
        request.key.kind,
        requireText(request.key.globalId, 'object global ID')
      ),
      reservationId: request.reservationId,
      expectedStoreVersion: request.expectedStoreVersion,
      target: creationTargetJson(request.target)
    }, signal) as ZLinkObjectAbortResult;
  }

  async reserveRelocationCapacity(
    request: ZLinkRelocationCapacityReservationRequest,
    signal?: AbortSignal
  ): Promise<ZLinkRelocationCapacityReserveResult> {
    validateCapacityVector(request.capacity);
    requireText(request.reservationId, 'relocation reservation ID');
    const comparable = relocationRequestJson(request);
    const raw = await this.authorityCall('reserveRelocation', {
      ...comparable,
      key: request.authorityKey.value,
      target: {
        descriptor: comparable.targetDescriptor,
        descriptorKey: encodeMeshNodeKey(request.targetDescriptor),
        lifecycleGeneration: comparable.targetNodeLifecycleGeneration,
        owner: comparable.targetOwner
      }
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
      target: {
        descriptor: comparable.targetDescriptor,
        descriptorKey: comparable.targetDescriptorKey,
        lifecycleGeneration: comparable.targetDescriptorLifecycleGeneration,
        owner: comparable.targetOwner
      }
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
      lifecycleGeneration?: string;
      owner?: { ownerId: string; leaseGeneration?: string };
    } | undefined;
    const recordKey = operation === 'reserve' || operation === 'commit'
      || operation === 'completeCreation' || operation === 'abort'
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
      const stored = await this.readHash(recordKey, signal);
      if (stored.targetDescriptorKey !== undefined) {
        effectiveTarget = {
          descriptor: decodeMeshNodeKey(stored.targetDescriptorKey),
          descriptorKey: stored.targetDescriptorKey,
          lifecycleGeneration: stored.targetDescriptorLifecycleGeneration,
          owner: {
            ownerId: stored.targetOwnerId,
            leaseGeneration: stored.targetOwnerLeaseGeneration
          }
        };
      }
    }
    if ((operation === 'commitAggregate' || operation === 'abortAggregate')
      && typeof value.aggregateKey === 'string') {
      const stored = await this.readHash(recordKey, signal);
      if (stored.targetDescriptorKey !== undefined) {
        value.participants = JSON.parse(
          Buffer.from(stored.participants, 'hex').toString('utf8')
        );
        value.targetDescriptor = decodeMeshNodeKey(stored.targetDescriptorKey);
        value.targetDescriptorKey = stored.targetDescriptorKey;
        value.targetDescriptorLifecycleGeneration =
          stored.targetDescriptorLifecycleGeneration;
        value.targetOwner = {
          ownerId: stored.targetOwnerId,
          leaseGeneration: stored.targetOwnerLeaseGeneration
        };
        value.capacityBundle = stored.capacityBundle;
        value.target = {
          descriptor: value.targetDescriptor,
          descriptorKey: stored.targetDescriptorKey,
          lifecycleGeneration: stored.targetDescriptorLifecycleGeneration,
          owner: value.targetOwner
        };
        effectiveTarget = {
          descriptor: value.targetDescriptor,
          descriptorKey: stored.targetDescriptorKey,
          lifecycleGeneration: stored.targetDescriptorLifecycleGeneration,
          owner: value.targetOwner
        };
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
    const terminalOperation = (
      operation === 'readCreationTerminal'
        ? value.operation
        : ((value.completion as Record<string, unknown> | undefined)
            ?.terminal as Record<string, unknown> | undefined)?.operation
    ) as {
      sourceNodeRid?: string;
      sourceNodeGeneration?: string;
      operationIdHigh?: string;
      operationIdLow?: string;
    } | undefined;
    const creationTerminalKey = terminalOperation?.sourceNodeRid === undefined
      || terminalOperation.sourceNodeGeneration === undefined
      || terminalOperation.operationIdHigh === undefined
      || terminalOperation.operationIdLow === undefined
      ? placeholder
      : this.keys.creationTerminal(
          terminalOperation.sourceNodeRid,
          BigInt(terminalOperation.sourceNodeGeneration),
          BigInt(terminalOperation.operationIdHigh),
          BigInt(terminalOperation.operationIdLow)
        );
    const terminalOrEntryIdentityKey = operation === 'reserve' && authorityKey.length > 0
      ? this.keys.entrySpotIdentityClaim(authorityKey)
      : creationTerminalKey;
    const keys = [
      authorityKey.length === 0 ? placeholder : this.keys.authorityCurrent(authorityKey),
      authorityKey.length === 0 ? placeholder : this.keys.authorityHistory(authorityKey),
      authorityKey.length === 0 ? placeholder : this.keys.authorityHistoryRevisions(authorityKey),
      this.keys.counter(),
      this.keys.authorityKeyIndex(),
      this.keys.membershipCurrent(),
      this.keys.capacityActor('active'),
      this.keys.capacityActor('reserved'),
      this.keys.capacitySpotType('active'),
      this.keys.capacitySpotType('reserved'),
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
      terminalOrEntryIdentityKey,
      this.keys.capacitySpot('active'),
      this.keys.capacitySpot('reserved')
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
    const maxRequestBytes = operation === 'completeCreation'
      ? MAX_CREATION_TERMINAL_BYTES * 2 + 64 * 1024
      : 1024 * 1024;
    if (Buffer.byteLength(encoded) > maxRequestBytes) {
      throw new RangeError('Redis authority request exceeds its operation-specific bound.');
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
    _aggregateKey: string,
    keys: string[],
    signal?: AbortSignal
  ): Promise<void> {
    const aggregate = value;
    const participants = aggregate.participants as readonly Record<string, any>[];
    if (!Array.isArray(participants)) return;
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
    }
  }

  private async readHash(
    key: string,
    signal?: AbortSignal
  ): Promise<Record<string, string>> {
    const raw = await this.command(['HGETALL', key], signal);
    if (raw !== null && typeof raw === 'object' && !Array.isArray(raw)) {
      return Object.fromEntries(
        Object.entries(raw).map(([field, value]) => [field, asString(value)])
      );
    }
    const values = asArray(raw);
    const result: Record<string, string> = {};
    for (let index = 0; index < values.length; index += 2) {
      result[asString(values[index])] = asString(values[index + 1]);
    }
    return result;
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
        'format', 'location-authority-hybrid-v3',
        'epoch', '3'
      ]);
      return;
    }
    if (asString(existing[0]) !== 'location-authority-hybrid-v3'
        || asString(existing[1]) !== '3') {
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
    readonly state: 'reserved' | 'active';
    readonly objectKind: 'actor' | 'user_spot' | 'instance_spot';
    readonly stableType: string;
    readonly descriptor: { readonly meshName: string; readonly rid: string };
    readonly descriptorLifecycleGeneration: string;
    readonly capacity: {
      readonly actors: number;
      readonly spots: number;
      readonly spotType?: {
        readonly objectKind: 'user_spot' | 'instance_spot';
        readonly stableType: string;
        readonly count: number;
      };
    };
  };
  readonly pendingCreation?: {
    readonly reservationId: string;
    readonly requestContentReference: string;
    readonly requestSha256: string;
    readonly requestEncodedSize: string;
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
  const pendingCreation = value.pendingCreation === undefined
    ? undefined
    : {
        reservationId: requireText(
          value.pendingCreation.reservationId,
          'creation reservation ID'
        ),
        requestContentReference: requireText(
          value.pendingCreation.requestContentReference,
          'creation content reference'
        ),
        requestSha256: Buffer.from(value.pendingCreation.requestSha256, 'hex'),
        requestEncodedSize: BigInt(value.pendingCreation.requestEncodedSize)
      };
  if (
    value.allocation.state === 'reserved'
      ? pendingCreation === undefined
      : pendingCreation !== undefined
  ) {
    throw new Error('Redis authority creation projection does not match its allocation state.');
  }
  if (
    pendingCreation !== undefined
    && (
      pendingCreation.requestSha256.byteLength !== 32
      || pendingCreation.requestEncodedSize < 0n
      || pendingCreation.requestEncodedSize > 1024n * 1024n
    )
  ) {
    throw new Error('Redis authority creation projection is invalid.');
  }
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
      ,
      capacity: cloneCapacityJson(value.allocation.capacity)
    },
    ...(pendingCreation === undefined ? {} : { pendingCreation }),
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

function creationTerminalRecord(raw: Record<string, unknown>): ZLinkCreationTerminalRecord {
  const state = String(raw.state);
  if (state !== 'Created' && state !== 'Rejected' && state !== 'Failed') {
    throw new Error('Redis creation terminal state is invalid.');
  }
  const terminalEnvelope = Buffer.from(String(raw.terminalEnvelope), 'hex');
  const terminalEnvelopeSha256 = Buffer.from(String(raw.terminalEnvelopeSha256), 'hex');
  const actualSha256 = createHash('sha256').update(terminalEnvelope).digest();
  if (String(raw.objectKind) !== 'actor'
    || terminalEnvelope.byteLength > MAX_CREATION_TERMINAL_BYTES
    || terminalEnvelopeSha256.byteLength !== 32
    || !actualSha256.equals(terminalEnvelopeSha256)) {
    throw new Error('Redis creation terminal envelope is invalid.');
  }
  return {
    state: state.toLowerCase() as ZLinkCreationTerminalRecord['state'],
    operation: {
      sourceNodeRid: String(raw.sourceNodeRid),
      sourceNodeGeneration: BigInt(String(raw.sourceNodeGeneration)),
      operationId: {
        high: BigInt(String(raw.operationIdHigh)),
        low: BigInt(String(raw.operationIdLow))
      }
    },
    reservationId: String(raw.reservationId),
    objectKind: String(raw.objectKind) as ZLinkCreationTerminalRecord['objectKind'],
    terminalEnvelope,
    terminalEnvelopeSha256,
    expiresAt: fromUnixMs(Number(raw.expiresAtUnixMs)),
    storeNow: fromUnixMs(Number(raw.storeNowMs))
  };
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

function creationOperationJson(operation: ZLinkCreationOperationIdentity): Record<string, string> {
  validateCreationOperation(operation);
  return {
    sourceNodeRid: String(operation.sourceNodeRid),
    sourceNodeGeneration: operation.sourceNodeGeneration.toString(),
    operationIdHigh: operation.operationId.high.toString(),
    operationIdLow: operation.operationId.low.toString()
  };
}

function creationTerminalJson(
  terminal: ZLinkCreationTerminalPublication
): Record<string, unknown> {
  const operation = creationOperationJson(terminal.operation);
  if (terminal.terminalEnvelope.byteLength > MAX_CREATION_TERMINAL_BYTES
    || terminal.terminalEnvelopeSha256.byteLength !== 32) {
    throw new RangeError('Creation terminal envelope or SHA-256 exceeds its exact bound.');
  }
  const envelope = Buffer.from(terminal.terminalEnvelope);
  const sha256 = Buffer.from(terminal.terminalEnvelopeSha256);
  if (!createHash('sha256').update(envelope).digest().equals(sha256)) {
    throw new TypeError('Creation terminal envelope SHA-256 does not match its bytes.');
  }
  const deadlineMs = terminal.operationDeadline.getTime();
  const expiresAtUnixMs = deadlineMs + CREATION_TERMINAL_RETENTION_MS;
  if (!Number.isSafeInteger(deadlineMs)
    || !Number.isSafeInteger(expiresAtUnixMs)
    || expiresAtUnixMs <= Date.now()) {
    throw new RangeError('Creation terminal expiry must be an unexpired absolute instant.');
  }
  return {
    operation,
    terminalEnvelope: envelope.toString('hex'),
    terminalEnvelopeSha256: sha256.toString('hex'),
    expiresAtUnixMs: String(expiresAtUnixMs)
  };
}

function validateCreationOperation(operation: ZLinkCreationOperationIdentity): void {
  requireDescriptorText(String(operation.sourceNodeRid), 'creation source Node RID');
  const maxGeneration = 0x7fff_ffff_ffff_ffffn;
  const maxU64 = 0xffff_ffff_ffff_ffffn;
  if (operation.sourceNodeGeneration < 1n
    || operation.sourceNodeGeneration > maxGeneration
    || operation.operationId.high < 0n
    || operation.operationId.high > maxU64
    || operation.operationId.low < 0n
    || operation.operationId.low > maxU64
    || operation.operationId.high === 0n && operation.operationId.low === 0n) {
    throw new RangeError('Creation operation identity is invalid.');
  }
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
    capacity: capacityJson(request.capacity),
    capacityBundle: encodeCapacityBundle(request.capacity)
  };
}

function aggregateRequestJson(request: ZLinkAggregatePrepareRequest): Record<string, unknown> {
  const participants = request.participants.map(participant => ({
    key: participant.authorityKey.value,
    expectedStoreVersion: participant.expectedStoreVersion.value,
    ownerTransition: participant.ownerTransition,
    authorityPayload: encodePayload(participant.authorityPayload),
    membershipMutation: encodePayload(participant.membershipMutation)
  }));
  return {
    aggregateId: request.aggregateId.value,
    aggregateGeneration: request.aggregateGeneration.toString(),
    participants,
    participantsEncoded: Buffer.from(
      JSON.stringify(participants), 'utf8').toString('hex'),
    inventoryDigest: Buffer.from(request.inventoryDigest).toString('hex'),
    targetDescriptor: {
      meshName: request.targetDescriptor.meshName,
      rid: routingIdHex(request.targetDescriptor.rid)
    },
    targetDescriptorKey: encodeMeshNodeKey(request.targetDescriptor),
    targetDescriptorLifecycleGeneration:
      request.targetDescriptorLifecycleGeneration.toString(),
    capacity: capacityJson(request.capacity),
    capacityBundle: encodeCapacityBundle(request.capacity),
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
  validateCapacityVector(request.capacity);
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

function validateCapacityVector(value: ZLinkCapacityVector): void {
  for (const count of [
    value.actors,
    value.spots,
    value.spotType?.count ?? 0
  ]) {
    if (!Number.isInteger(count) || count < 0 || count > 0x7fff_ffff) {
      throw new RangeError('Placement capacity slots must be in 0..2147483647.');
    }
  }
  if (value.actors === 0 && value.spots === 0 && value.spotType === undefined) {
    throw new RangeError('Placement capacity vector must reserve at least one slot.');
  }
  if (value.spotType !== undefined) {
    requireText(value.spotType.stableType, 'Spot type capacity stable type');
    if (value.spotType.count < 1) {
      throw new RangeError('Spot type capacity count must be positive.');
    }
  }
}

function capacityJson(value: ZLinkCapacityVector): Record<string, unknown> {
  return {
    actors: value.actors,
    spots: value.spots,
    spotType: value.spotType === undefined ? undefined : {
      objectKind: value.spotType.objectKind,
      stableType: value.spotType.stableType,
      count: value.spotType.count
    }
  };
}

function cloneCapacityJson(value: AuthorityJson['allocation']['capacity']): ZLinkCapacityVector {
  return {
    actors: value.actors,
    spots: value.spots,
    ...(value.spotType === undefined ? {} : { spotType: { ...value.spotType } })
  };
}

function encodeCapacityBundle(value: ZLinkCapacityVector): string {
  validateCapacityVector(value);
  const segments = [
    'zlink-capacity-bundle-v2',
    value.actors.toString(),
    value.spots.toString(),
    value.spotType === undefined ? '0' : '1'
  ];
  if (value.spotType !== undefined) {
    segments.push(
      value.spotType.objectKind,
      value.spotType.stableType,
      value.spotType.count.toString()
    );
  }
  return segments.map(segment =>
    `${Buffer.byteLength(segment, 'utf8')}:${segment}`).join('');
}

function encodePayload(payload: Uint8Array): string {
  return Buffer.from(payload).toString('base64');
}

function requireText(value: string, field: string): string {
  if (value.trim().length === 0) throw new TypeError(`${field} is required.`);
  return value;
}

function decodeMeshNodeKey(value: string): { meshName: string; rid: string } {
  const first = value.indexOf(':');
  const meshLength = Number(value.slice(0, first));
  const meshStart = first + 1;
  const meshName = value.slice(meshStart, meshStart + meshLength);
  const ridLengthStart = meshStart + meshLength;
  const second = value.indexOf(':', ridLengthStart);
  const ridLength = Number(value.slice(ridLengthStart, second));
  const rid = value.slice(second + 1, second + 1 + ridLength);
  if (first < 1 || second < ridLengthStart || !Number.isSafeInteger(meshLength)
    || !Number.isSafeInteger(ridLength)) {
    throw new Error('Redis descriptor key is invalid.');
  }
  return { meshName, rid };
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
    .sort((left, right) => {
      const byKind = compareUtf8(left.objectKind, right.objectKind);
      return byKind !== 0 ? byKind : compareUtf8(left.stableType, right.stableType);
    });
}

function meshDescriptorImmutableFingerprint(descriptor: ZLinkMeshNodeDescriptor): string {
  const segments = [
    'zlink-mesh-node-immutable-v2',
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
    descriptor.objectRole
  );
  segments.push(descriptor.entrySpotId === undefined ? '0' : '1');
  if (descriptor.entrySpotId !== undefined) segments.push(descriptor.entrySpotId);
  segments.push(
    descriptor.populationCapacity.actors.limit.toString(),
    descriptor.populationCapacity.spots.limit.toString(),
    descriptor.activationConcurrency.limit.toString()
  );
  const capabilities = canonicalObjectCapabilities(descriptor.objectCapabilities);
  segments.push(capabilities.length.toString());
  for (const capability of capabilities) {
    segments.push(
      capability.objectKind,
      capability.stableType,
      capability.policy,
      capability.hasSnapshotAdapter ? '1' : '0',
      capability.objectKind === 'actor' ? '' : capability.limit.toString()
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
  if (descriptor.objectRole === 'server') {
    if (descriptor.entrySpotId !== undefined) {
      requireDescriptorText(descriptor.entrySpotId, 'Entry Spot ID');
    }
  } else if (descriptor.entrySpotId !== undefined) {
    throw new TypeError('Only Object Server descriptors may publish an Entry Spot ID.');
  }
  const maxGeneration = 0x7fff_ffff_ffff_ffffn;
  if (descriptor.lifecycleGeneration < 1n || descriptor.lifecycleGeneration > maxGeneration
    || descriptor.descriptorRevision < 1n || descriptor.descriptorRevision > maxGeneration
    || descriptor.leaseGeneration < 1n || descriptor.leaseGeneration > maxGeneration
    || descriptor.applicationVersion < 0n || descriptor.applicationVersion > maxGeneration) {
    throw new RangeError('MeshNode descriptor generations are invalid.');
  }
  const capacities = [
    descriptor.populationCapacity.actors,
    descriptor.populationCapacity.spots,
    ...descriptor.populationCapacity.spotTypes
  ];
  for (const value of capacities.flatMap(capacity =>
    [capacity.active, capacity.reserved, capacity.limit])) {
    if (!Number.isSafeInteger(value) || value < 0) {
      throw new RangeError('MeshNode object capacity must contain non-negative safe integers.');
    }
  }
  if (capacities.some(capacity =>
      capacity.limit !== 0 && capacity.active + capacity.reserved > capacity.limit)
    || descriptor.populationCapacity.actors.limit === 0
    || descriptor.populationCapacity.spots.limit === 0
    || !Number.isSafeInteger(descriptor.activationConcurrency.active)
    || descriptor.activationConcurrency.active < 0
    || !Number.isSafeInteger(descriptor.activationConcurrency.limit)
    || descriptor.activationConcurrency.limit < 1
    || descriptor.activationConcurrency.active > descriptor.activationConcurrency.limit
    || descriptor.placementWeight < 0
    || descriptor.placementWeight > 10_000
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
    if (!Number.isSafeInteger(capability.limit) || capability.limit < 0
      || capability.objectKind === 'actor' && capability.limit !== 0) {
      throw new RangeError('MeshNode capability limit is invalid.');
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
    || descriptor.weight > 10_000) {
    throw new RangeError('ClientServer descriptor weight must be an integer in 0..10000.');
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
