// SPDX-License-Identifier: MPL-2.0

import { RoutingId } from '../../contracts/core';
import type { MessageLike } from '../../contracts/messaging';
import type { RequestResult, SubmitResult } from '../../contracts/errors';
import type { StreamSocket } from '../../contracts/sockets';
import type {
  ActorLocation,
  ActorRef,
  ConnectPeerOptions,
  DrainReadyResult,
  GetOrCreateSpotResult,
  MeshNode as MeshNodeContract,
  MeshNodeStatus,
  MeshOperationId,
  MeshPeerEntry,
  MessagingOptions,
  PeerChannels,
  Publisher as PublisherContract,
  ReadyBatch,
  ReadyRecord,
  ReceiveBatch,
  RequestOptions,
  Spot as SpotContract,
  StreamSessionService as StreamSessionServiceContract,
  ActorTransferPrepare,
  ActorTransferToken,
  PrepareActorTransferResult
} from '../../contracts/service';
import type {
  ActorTransferPrepareRaw,
  MeshConnectPeerOptions,
  MeshNodeNewOptions,
  MeshNodeStatusRaw,
  MeshPeerEntryRaw,
  MeshReadyRecordRaw,
  NativeReadyHandler,
  NativeServiceHandle,
  SpotGetOrNewRaw
} from '../native/binding_service_types';
import { NativeHandle, getNativeHandle } from '../handles/native_handle';
import type { RuntimeContext } from '../core/context';
import { requireNative } from '../native/native';
import { closeCall, configCall } from '../errors/native_errors';
import { normalizeRoutingId } from '../core/routing_id';
import { normalizeMessageLikePayload } from '../buffers/message_conversion';
import {
  actorLocationFromRaw,
  actorRefFromRaw,
  actorRefToRaw,
  flagsOrZero,
  maybeActorRefFromRaw,
  metadataOrNull,
  normalizeCreationParts,
  ridOrNull,
  timeoutOrZero
} from './conversions';
import { Spot } from './spot';
import { Publisher } from './publisher';
import { StreamSessionService } from './stream_session';
import { RuntimeReadyBatch, RuntimeReceiveBatch } from './dispatch';

/** Options accepted when constructing a {@link MeshNode}. */
export interface MeshNodeOptions {
  meshName?: string;
  trustProfile?: string;
}

/** Runtime mesh node: the local participant of a RouteMesh. */
export class MeshNode extends NativeHandle implements MeshNodeContract {
  /** Opaque native handle for the installed ready handler, or null when none is registered. */
  private _readyHandlerState: NativeServiceHandle | null = null;

  constructor(ctx: RuntimeContext, options?: MeshNodeOptions) {
    const nativeOptions: MeshNodeNewOptions = {
      meshName: options?.meshName,
      trustProfile: options?.trustProfile
    };
    super(requireNative().meshNodeNew(getNativeHandle(ctx), nativeOptions));
  }

  // --- Lifecycle ---------------------------------------------------------
  setBind(endpoint: string): void {
    configCall('mesh node bind failed', () => {
      requireNative().meshNodeSetBind(getNativeHandle(this), endpoint);
    });
  }

  start(): void {
    configCall('mesh node start failed', () => {
      requireNative().meshNodeStart(getNativeHandle(this));
    });
  }

  shutdown(timeoutMs: number): RequestResult {
    return requireNative().meshNodeShutdown(getNativeHandle(this), timeoutOrZero(timeoutMs)) as RequestResult;
  }

  close(): void {
    if (getNativeHandle(this) != null) {
      this.clearReadyHandler();
      closeCall('mesh node close failed', () => {
        requireNative().meshNodeDestroy(getNativeHandle(this));
      });
      this._native = null;
    }
  }

  /** Unregister and release the ready handler if one is installed. */
  private clearReadyHandler(): void {
    const state = this._readyHandlerState;
    if (state == null) {
      return;
    }
    this._readyHandlerState = null;
    configCall('mesh node ready handler unregistration failed', () => {
      requireNative().meshNodeUnsetReadyHandler(getNativeHandle(this), state);
    });
  }

  addChannelName(name: string): void {
    configCall('mesh node channel name add failed', () => {
      requireNative().meshNodeAddChannelName(getNativeHandle(this), name);
    });
  }

  setChannelWeight(name: string, weight: number): void {
    configCall('mesh node channel weight set failed', () => {
      requireNative().meshNodeSetChannelWeight(getNativeHandle(this), name, weight | 0);
    });
  }

  // --- Peers -------------------------------------------------------------
  connectPeer(options: ConnectPeerOptions): bigint {
    const nativeOptions: MeshConnectPeerOptions = {
      endpoint: options.endpoint,
      expectedRid: options.expectedRid ? normalizeRoutingId(options.expectedRid, 'expectedRid') : undefined
    };
    return configCall('mesh node connect peer failed', () =>
      requireNative().meshNodeConnectPeer(getNativeHandle(this), nativeOptions)
    );
  }

  removePeerConnection(intentId: bigint): void {
    configCall('mesh node remove peer connection failed', () => {
      requireNative().meshNodeRemovePeerConnection(getNativeHandle(this), intentId);
    });
  }

  disconnectPeer(peerRid: RoutingId, lifecycleGeneration: bigint): void {
    configCall('mesh node disconnect peer failed', () => {
      requireNative().meshNodeDisconnectPeer(
        getNativeHandle(this),
        normalizeRoutingId(peerRid, 'peerRid'),
        lifecycleGeneration
      );
    });
  }

  // --- Node / channel messaging -----------------------------------------
  sendToNode(targetRid: RoutingId, parts: MessageLike | readonly MessageLike[], options?: MessagingOptions): SubmitResult {
    return requireNative().meshNodeSendToNode(
      getNativeHandle(this),
      normalizeRoutingId(targetRid, 'targetRid'),
      metadataOrNull(options?.metadata),
      normalizeMessageLikePayload(parts),
      flagsOrZero(options?.flags)
    ) as SubmitResult;
  }

  requestToNode(targetRid: RoutingId, parts: MessageLike | readonly MessageLike[], options?: RequestOptions): MeshOperationId {
    return requireNative().meshNodeRequestToNode(
      getNativeHandle(this),
      normalizeRoutingId(targetRid, 'targetRid'),
      metadataOrNull(options?.metadata),
      normalizeMessageLikePayload(parts),
      flagsOrZero(options?.flags),
      timeoutOrZero(options?.timeoutMs)
    );
  }

  sendToChannel(channelName: string, parts: MessageLike | readonly MessageLike[], options?: MessagingOptions): SubmitResult {
    return requireNative().meshNodeSendToChannel(
      getNativeHandle(this),
      channelName,
      metadataOrNull(options?.metadata),
      normalizeMessageLikePayload(parts),
      flagsOrZero(options?.flags)
    ) as SubmitResult;
  }

  requestToChannel(channelName: string, parts: MessageLike | readonly MessageLike[], options?: RequestOptions): MeshOperationId {
    return requireNative().meshNodeRequestToChannel(
      getNativeHandle(this),
      channelName,
      metadataOrNull(options?.metadata),
      normalizeMessageLikePayload(parts),
      flagsOrZero(options?.flags),
      timeoutOrZero(options?.timeoutMs)
    );
  }

  // --- Publisher ---------------------------------------------------------
  createPublisher(): PublisherContract {
    return new Publisher(configCall('mesh node publisher creation failed', () =>
      requireNative().meshNodePublisherNew(getNativeHandle(this))
    ));
  }

  // --- Options / introspection ------------------------------------------
  setOption(option: number, value: Buffer): void {
    configCall('mesh node option set failed', () => {
      requireNative().meshNodeSetOption(getNativeHandle(this), option | 0, value);
    });
  }

  getOption(option: number): Buffer {
    return configCall('mesh node option get failed', () =>
      requireNative().meshNodeGetOption(getNativeHandle(this), option | 0)
    );
  }

  status(): MeshNodeStatus {
    const raw: MeshNodeStatusRaw = configCall('mesh node status failed', () =>
      requireNative().meshNodeStatus(getNativeHandle(this))
    );
    return {
      state: raw.state,
      routingId: RoutingId.from(raw.routingId),
      meshName: raw.meshName,
      localEndpoint: raw.localEndpoint,
      lifecycleGeneration: raw.lifecycleGeneration,
      descriptorRevision: raw.descriptorRevision,
      channelCount: raw.channelCount,
      configuredPeerCount: raw.configuredPeerCount,
      admittedPeerCount: raw.admittedPeerCount,
      drainingPeerCount: raw.drainingPeerCount,
      pendingApplicationMessages: raw.pendingApplicationMessages,
      pendingInfrastructureMessages: raw.pendingInfrastructureMessages,
      pendingBytes: raw.pendingBytes,
      multicastSubmitted: raw.multicastSubmitted,
      multicastDroppedTargets: raw.multicastDroppedTargets,
      lastError: raw.lastError,
      lastChangedMs: raw.lastChangedMs
    };
  }

  peers(): MeshPeerEntry[] {
    const raw: MeshPeerEntryRaw[] = configCall('mesh node peers failed', () =>
      requireNative().meshNodePeers(getNativeHandle(this))
    );
    return raw.map((entry) => ({
      connectionIntentId: entry.connectionIntentId,
      source: entry.source,
      state: entry.state,
      routingId: RoutingId.from(entry.routingId),
      lifecycleGeneration: entry.lifecycleGeneration,
      descriptorRevision: entry.descriptorRevision,
      endpoint: entry.endpoint,
      channelCount: entry.channelCount,
      lastError: entry.lastError,
      lastChangedMs: entry.lastChangedMs
    }));
  }

  peerChannels(peerRid: RoutingId, generation: bigint): PeerChannels {
    return configCall('mesh node peer channels failed', () =>
      requireNative().meshNodePeerChannels(getNativeHandle(this), normalizeRoutingId(peerRid, 'peerRid'), generation)
    );
  }

  // --- Spots -------------------------------------------------------------
  createSpot(): SpotContract {
    return new Spot(configCall('mesh node spot creation failed', () =>
      requireNative().spotNew(getNativeHandle(this))
    ));
  }

  entrySpot(): SpotContract {
    return new Spot(configCall('mesh node entry spot failed', () =>
      requireNative().meshNodeEntrySpot(getNativeHandle(this))
    ));
  }

  spotLookup(rid: RoutingId): SpotContract | null {
    const handle = configCall('mesh node spot lookup failed', () =>
      requireNative().meshNodeSpotLookup(getNativeHandle(this), normalizeRoutingId(rid, 'rid'))
    );
    return handle == null ? null : new Spot(handle);
  }

  getOrCreateSpot(rid: RoutingId): GetOrCreateSpotResult {
    const raw: SpotGetOrNewRaw = configCall('mesh node get-or-create spot failed', () =>
      requireNative().meshNodeSpotGetOrNew(getNativeHandle(this), normalizeRoutingId(rid, 'rid'))
    );
    return { spot: new Spot(raw.spot), created: raw.created };
  }

  // --- Actors ------------------------------------------------------------
  createActor(
    actorId: string,
    creationParts?: MessageLike | readonly MessageLike[],
    options?: { flags?: number; timeoutMs?: number }
  ): ActorRef {
    return actorRefFromRaw(configCall('mesh node actor creation failed', () =>
      requireNative().meshNodeActorNew(
        getNativeHandle(this),
        actorId,
        normalizeCreationParts(creationParts),
        flagsOrZero(options?.flags),
        timeoutOrZero(options?.timeoutMs)
      )
    ));
  }

  actorLookup(actorId: string): ActorLocation {
    return actorLocationFromRaw(configCall('mesh node actor lookup failed', () =>
      requireNative().meshNodeActorLookup(getNativeHandle(this), actorId)
    ));
  }

  lookupRemoteActor(targetNodeRid: RoutingId, actorId: string, timeoutMs?: number): MeshOperationId {
    return requireNative().meshNodeActorLookupRemote(
      getNativeHandle(this),
      normalizeRoutingId(targetNodeRid, 'targetNodeRid'),
      actorId,
      timeoutOrZero(timeoutMs)
    );
  }

  destroyActor(actor: ActorRef, timeoutMs?: number): MeshOperationId {
    return requireNative().meshNodeActorDestroy(
      getNativeHandle(this),
      actorRefToRaw(actor),
      timeoutOrZero(timeoutMs)
    );
  }

  joinActorSpot(
    actor: ActorRef,
    targetNodeRid: RoutingId,
    targetSpotRid: RoutingId,
    targetSpotGeneration: bigint,
    creationParts?: MessageLike | readonly MessageLike[],
    timeoutMs?: number
  ): MeshOperationId {
    return requireNative().meshNodeActorJoinSpot(
      getNativeHandle(this),
      actorRefToRaw(actor),
      normalizeRoutingId(targetNodeRid, 'targetNodeRid'),
      normalizeRoutingId(targetSpotRid, 'targetSpotRid'),
      targetSpotGeneration,
      normalizeCreationParts(creationParts),
      timeoutOrZero(timeoutMs)
    );
  }

  joinActorEntrySpot(
    actor: ActorRef,
    targetNodeRid: RoutingId,
    creationParts?: MessageLike | readonly MessageLike[],
    timeoutMs?: number
  ): MeshOperationId {
    return requireNative().meshNodeActorJoinEntrySpot(
      getNativeHandle(this),
      actorRefToRaw(actor),
      normalizeRoutingId(targetNodeRid, 'targetNodeRid'),
      normalizeCreationParts(creationParts),
      timeoutOrZero(timeoutMs)
    );
  }

  leaveActor(actor: ActorRef, expectedMembershipEpoch: bigint, timeoutMs?: number): MeshOperationId {
    return requireNative().meshNodeActorLeaveSpot(
      getNativeHandle(this),
      actorRefToRaw(actor),
      expectedMembershipEpoch,
      timeoutOrZero(timeoutMs)
    );
  }

  sendToActor(actor: ActorRef, parts: MessageLike | readonly MessageLike[], options?: MessagingOptions): SubmitResult {
    return requireNative().meshNodeSendToActor(
      getNativeHandle(this),
      actorRefToRaw(actor),
      metadataOrNull(options?.metadata),
      normalizeMessageLikePayload(parts),
      flagsOrZero(options?.flags)
    ) as SubmitResult;
  }

  requestToActor(actor: ActorRef, parts: MessageLike | readonly MessageLike[], options?: RequestOptions): MeshOperationId {
    return requireNative().meshNodeRequestToActor(
      getNativeHandle(this),
      actorRefToRaw(actor),
      metadataOrNull(options?.metadata),
      normalizeMessageLikePayload(parts),
      flagsOrZero(options?.flags),
      timeoutOrZero(options?.timeoutMs)
    );
  }

  actorSendToActor(
    source: ActorRef,
    target: ActorRef,
    parts: MessageLike | readonly MessageLike[],
    options?: MessagingOptions
  ): SubmitResult {
    return requireNative().actorSendToActor(
      getNativeHandle(this),
      actorRefToRaw(source),
      actorRefToRaw(target),
      metadataOrNull(options?.metadata),
      normalizeMessageLikePayload(parts),
      flagsOrZero(options?.flags)
    ) as SubmitResult;
  }

  actorRequestToActor(
    source: ActorRef,
    target: ActorRef,
    parts: MessageLike | readonly MessageLike[],
    options?: RequestOptions
  ): MeshOperationId {
    return requireNative().actorRequestToActor(
      getNativeHandle(this),
      actorRefToRaw(source),
      actorRefToRaw(target),
      metadataOrNull(options?.metadata),
      normalizeMessageLikePayload(parts),
      flagsOrZero(options?.flags),
      timeoutOrZero(options?.timeoutMs)
    );
  }

  sendActorBoundSession(actor: ActorRef, parts: MessageLike | readonly MessageLike[], flags?: number): SubmitResult {
    return requireNative().meshNodeActorSendBoundSession(
      getNativeHandle(this),
      actorRefToRaw(actor),
      normalizeMessageLikePayload(parts),
      flagsOrZero(flags)
    ) as SubmitResult;
  }

  closeActorBoundSession(actor: ActorRef, expectedBindingGeneration: bigint, timeoutMs?: number): MeshOperationId {
    return requireNative().meshNodeActorCloseBoundSession(
      getNativeHandle(this),
      actorRefToRaw(actor),
      expectedBindingGeneration,
      timeoutOrZero(timeoutMs)
    );
  }

  // --- Actor transfer fence ---------------------------------------------
  prepareActorTransfer(prepare: ActorTransferPrepare, timeoutMs?: number): PrepareActorTransferResult {
    const raw: ActorTransferPrepareRaw = {
      role: prepare.role | 0,
      transferId: { high: prepare.transferId.high, low: prepare.transferId.low },
      actor: actorRefToRaw(prepare.actor),
      expectedMembershipEpoch: prepare.expectedMembershipEpoch,
      peerNodeRid: normalizeRoutingId(prepare.peerNodeRid, 'peerNodeRid'),
      finalSequence: prepare.finalSequence,
      reserveMessageCount: prepare.reserveMessageCount,
      reserveByteCount: prepare.reserveByteCount
    };
    const outcome = configCall('mesh node actor transfer prepare failed', () =>
      requireNative().meshNodeActorTransferPrepare(getNativeHandle(this), raw, timeoutOrZero(timeoutMs))
    );
    return {
      token: { opaque: outcome.token },
      result: {
        role: outcome.result.role,
        transferId: { high: outcome.result.transferId.high, low: outcome.result.transferId.low },
        actor: actorRefFromRaw(outcome.result.actor),
        finalSequence: outcome.result.finalSequence,
        reserveMessageCount: outcome.result.reserveMessageCount,
        reserveByteCount: outcome.result.reserveByteCount
      }
    };
  }

  commitActorTransfer(token: ActorTransferToken, newMembershipEpoch: bigint): void {
    configCall('mesh node actor transfer commit failed', () => {
      requireNative().meshNodeActorTransferCommit(token.opaque, newMembershipEpoch);
    });
  }

  activateActorTransfer(token: ActorTransferToken): void {
    configCall('mesh node actor transfer activate failed', () => {
      requireNative().meshNodeActorTransferActivate(token.opaque);
    });
  }

  abortActorTransfer(token: ActorTransferToken): void {
    configCall('mesh node actor transfer abort failed', () => {
      requireNative().meshNodeActorTransferAbort(token.opaque);
    });
  }

  // --- Pull dispatch -----------------------------------------------------
  setReadyHandler(handler: (readyDomains: number) => number): void {
    const nativeHandler: NativeReadyHandler = handler;
    // Replacing an existing handler releases the previous native registration
    // first so its threadsafe function is not leaked.
    this.clearReadyHandler();
    this._readyHandlerState = configCall('mesh node ready handler registration failed', () =>
      requireNative().meshNodeSetReadyHandler(getNativeHandle(this), nativeHandler)
    );
  }

  createReadyBatch(capacity: number): ReadyBatch {
    return new RuntimeReadyBatch(configCall('ready batch creation failed', () =>
      requireNative().meshReadyBatchNew(capacity | 0)
    ));
  }

  createReceiveBatch(messageCapacity: number, partCapacity: number, byteCapacity: number): ReceiveBatch {
    return new RuntimeReceiveBatch(configCall('receive batch creation failed', () =>
      requireNative().meshReceiveBatchNew(messageCapacity | 0, partCapacity | 0, byteCapacity | 0)
    ));
  }

  drainReady(domains: number, batch: ReadyBatch, flags?: number): DrainReadyResult {
    const raw = requireNative().meshNodeDrainReady(
      getNativeHandle(this),
      domains | 0,
      getNativeHandle(batch as unknown as NativeHandle),
      flagsOrZero(flags)
    );
    const records: ReadyRecord[] = raw.records.map((record: MeshReadyRecordRaw) => ({
      ownerKind: record.ownerKind,
      domain: record.domain,
      spotRid: ridOrNull(record.spotRid),
      actor: maybeActorRefFromRaw(record.actor)
    }));
    (batch as RuntimeReadyBatch).records = records;
    return { ok: raw.ok, hasResidue: raw.hasResidue, records };
  }

  // --- Stream session service -------------------------------------------
  createStreamSessionService(stream: StreamSocket): StreamSessionServiceContract {
    return new StreamSessionService(configCall('stream session service creation failed', () =>
      requireNative().streamSessionServiceNew(
        getNativeHandle(this),
        getNativeHandle(stream as unknown as NativeHandle)
      )
    ));
  }
}
