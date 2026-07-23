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
  CLAIM_LEASE_SCRIPT,
  COMMIT_ACTOR_TRANSFER_SCRIPT,
  LIST_ROUTING_ID_SLOTS_SCRIPT,
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
import {
  kindActor,
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
  type RoutingId,
  type ZLinkLocationChangeStampStore,
  type ZLinkActorLocation,
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
  type ZLinkLocationWriteStatus,
  type ZLinkMeshNodeDescriptor,
  type ZLinkMeshNodeDescriptorKey,
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

export class ZLinkRedisLocationStore implements
  ZLinkLocationChangeStampStore,
  ZLinkOwnerLeaseStore,
  ZLinkRoutingIdSlotAllocationStore {
  private readonly keys: RedisStoreKeys;
  private readonly providedClient?: RedisCommandClient;
  private client?: RedisCommandClient;

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
    return await this.write(kindMeshNode, descriptor, intent, signal);
  }

  async removeMeshNode(
    key: ZLinkMeshNodeDescriptorKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult['status']> {
    return (await this.remove(
      kindMeshNode,
      encodeMeshNodeKey(key),
      key.meshName,
      owner,
      signal
    )).status;
  }

  async listMeshNodes(
    meshName: string,
    signal?: AbortSignal
  ): Promise<readonly ZLinkMeshNodeDescriptor[]> {
    return (await this.loadAll(kindMeshNode, signal)).filter((row) => row.meshName === meshName);
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
      this.keys.leaseGenerationCounter(),
      this.keys.leaseIndex()
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
      [],
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
      this.keys.lease(token.ownerId),
      this.keys.leaseIndex()
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
      String(owner.leaseGeneration)
    ], signal));
    if (removed < 0) {
      throw new Error('Owner cleanup token is stale.');
    }
    return BigInt(removed);
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
    const raw = asArray(await this.eval(ACQUIRE_ROUTING_ID_SLOT_SCRIPT, [
      this.keys.routingIdAllocationGroup(request.groupName),
      this.keys.lease(request.ownerId),
      this.keys.leaseIndex()
    ], [
      config,
      String(request.slotCount),
      request.ownerId,
      String(Math.max(1, Math.trunc(request.leaseTtlMs))),
      this.keys.leasePrefix()
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
    const raw = asArray(await this.eval(LIST_ROUTING_ID_SLOTS_SCRIPT, [
      this.keys.routingIdAllocationGroup(groupName)
    ], [this.keys.leasePrefix()], signal));
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
      const raw = asArray(await this.eval(WRITE_SCRIPT, [
        this.keys.rowHash(kind.tag, rowKey),
        this.keys.generation(kind.tag, rowKey),
        this.keys.kindIndex(kind.tag)
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
    return client;
  }

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
