// SPDX-License-Identifier: MPL-2.0

import type { NativeHandle } from './binding_types';
import type {
  ActorLocationRaw,
  ActorRefRaw,
  ActorTransferPrepareOutcomeRaw,
  ActorTransferPrepareRaw,
  MeshClaimRecvRaw,
  MeshConnectPeerOptions,
  MeshDrainReadyRaw,
  MeshNodeNewOptions,
  MeshNodeStatusRaw,
  MeshOperationIdRaw,
  MeshPeerChannelsRaw,
  MeshPeerEntryRaw,
  MeshPublishDetailRaw,
  NativeReadyHandler,
  SpotGetOrNewRaw,
  SpotStatusRaw,
  StreamSessionBindingRaw,
  StreamSessionStatusRaw
} from './binding_service_types';

/** Parts payload accepted by native send/reply methods (Buffer, Message-like, or array thereof). */
export type NativeParts = unknown;

/**
 * The native addon surface for the RouteMesh 10.0.0 service layer. Every method
 * maps 1:1 to a `zlink_mesh_node_*` / `zlink_spot_*` / `zlink_actor_*` /
 * `zlink_stream_session_*` C entry point. Config/close results are thrown on
 * failure; submit results are returned as `SubmitResult` codes; request-style
 * operations return a `MeshOperationIdRaw` whose completion arrives through the
 * pull-dispatch pipeline.
 */
export interface ServiceNativeBinding {
  // --- MeshNode lifecycle -------------------------------------------------
  meshNodeNew: (ctx: NativeHandle, options: MeshNodeNewOptions) => NativeHandle;
  meshNodeSetBind: (node: NativeHandle, endpoint: string) => void;
  meshNodeStart: (node: NativeHandle) => void;
  meshNodeShutdown: (node: NativeHandle, timeoutMs: number) => number;
  meshNodeDestroy: (node: NativeHandle) => void;
  meshNodeAddChannelName: (node: NativeHandle, channelName: string) => void;
  meshNodeSetChannelWeight: (node: NativeHandle, channelName: string, weight: number) => void;

  // --- Peers --------------------------------------------------------------
  meshNodeConnectPeer: (node: NativeHandle, options: MeshConnectPeerOptions) => bigint;
  meshNodeRemovePeerConnection: (node: NativeHandle, connectionIntentId: bigint) => void;
  meshNodeDisconnectPeer: (
    node: NativeHandle,
    peerRid: Buffer,
    lifecycleGeneration: bigint
  ) => void;

  // --- Node / channel messaging ------------------------------------------
  meshNodeSendToNode: (
    node: NativeHandle,
    targetRid: Buffer,
    metadata: Buffer | null,
    parts: NativeParts,
    flags: number
  ) => number;
  meshNodeRequestToNode: (
    node: NativeHandle,
    targetRid: Buffer,
    metadata: Buffer | null,
    parts: NativeParts,
    flags: number,
    timeoutMs: number
  ) => MeshOperationIdRaw;
  meshNodeSendToChannel: (
    node: NativeHandle,
    channelName: string,
    metadata: Buffer | null,
    parts: NativeParts,
    flags: number
  ) => number;
  meshNodeRequestToChannel: (
    node: NativeHandle,
    channelName: string,
    metadata: Buffer | null,
    parts: NativeParts,
    flags: number,
    timeoutMs: number
  ) => MeshOperationIdRaw;

  // --- Options / introspection -------------------------------------------
  meshNodeSetOption: (node: NativeHandle, option: number, value: Buffer) => void;
  meshNodeGetOption: (node: NativeHandle, option: number) => Buffer;
  meshNodeStatus: (node: NativeHandle) => MeshNodeStatusRaw;
  meshNodePeers: (node: NativeHandle) => MeshPeerEntryRaw[];
  meshNodePeerChannels: (
    node: NativeHandle,
    peerRid: Buffer,
    lifecycleGeneration: bigint
  ) => MeshPeerChannelsRaw;

  // --- Publisher ----------------------------------------------------------
  meshNodePublisherNew: (node: NativeHandle) => NativeHandle;
  meshNodePublisherPublish: (
    publisher: NativeHandle,
    channelName: string,
    topic: string,
    metadata: Buffer | null,
    parts: NativeParts,
    flags: number
  ) => MeshPublishDetailRaw;
  meshNodePublisherSetOption: (publisher: NativeHandle, option: number, value: Buffer) => void;
  meshNodePublisherGetOption: (publisher: NativeHandle, option: number) => Buffer;
  meshNodePublisherDestroy: (publisher: NativeHandle) => void;

  // --- Pull dispatch ------------------------------------------------------
  /** Install the ready handler; returns an opaque handle used to unregister it. */
  meshNodeSetReadyHandler: (node: NativeHandle, handler: NativeReadyHandler) => NativeHandle;
  /** Unregister a previously installed ready handler and release its resources. */
  meshNodeUnsetReadyHandler: (node: NativeHandle, handlerState: NativeHandle) => void;
  meshReadyBatchNew: (capacity: number) => NativeHandle;
  meshReadyBatchReset: (batch: NativeHandle) => void;
  meshReadyBatchDestroy: (batch: NativeHandle) => void;
  meshNodeDrainReady: (
    node: NativeHandle,
    domains: number,
    batch: NativeHandle,
    flags: number
  ) => MeshDrainReadyRaw;
  meshReadyBatchTakeClaim: (batch: NativeHandle, index: number) => NativeHandle;
  meshClaimRelease: (claim: NativeHandle) => void;
  meshReceiveBatchNew: (
    messageCapacity: number,
    partCapacity: number,
    byteCapacity: number
  ) => NativeHandle;
  meshReceiveBatchReset: (batch: NativeHandle) => void;
  meshReceiveBatchDestroy: (batch: NativeHandle) => void;
  meshClaimRecvBatch: (
    claim: NativeHandle,
    batch: NativeHandle,
    flags: number
  ) => MeshClaimRecvRaw;
  meshReply: (replyToken: Buffer, parts: NativeParts, flags: number) => number;

  // --- Spot ---------------------------------------------------------------
  spotNew: (node: NativeHandle) => NativeHandle;
  meshNodeEntrySpot: (node: NativeHandle) => NativeHandle;
  meshNodeSpotLookup: (node: NativeHandle, spotRid: Buffer) => NativeHandle | null;
  meshNodeSpotGetOrNew: (node: NativeHandle, spotRid: Buffer) => SpotGetOrNewRaw;
  spotDestroy: (spot: NativeHandle) => void;
  spotStatus: (spot: NativeHandle) => SpotStatusRaw;
  spotSendToChannel: (
    spot: NativeHandle,
    channelName: string,
    metadata: Buffer | null,
    parts: NativeParts,
    flags: number
  ) => number;
  spotRequestToChannel: (
    spot: NativeHandle,
    channelName: string,
    metadata: Buffer | null,
    parts: NativeParts,
    flags: number,
    timeoutMs: number
  ) => MeshOperationIdRaw;
  spotSendToSpot: (
    spot: NativeHandle,
    targetNodeRid: Buffer,
    targetSpotRid: Buffer,
    targetSpotGeneration: bigint,
    metadata: Buffer | null,
    parts: NativeParts,
    flags: number
  ) => number;
  spotRequestToSpot: (
    spot: NativeHandle,
    targetNodeRid: Buffer,
    targetSpotRid: Buffer,
    targetSpotGeneration: bigint,
    metadata: Buffer | null,
    parts: NativeParts,
    flags: number,
    timeoutMs: number
  ) => MeshOperationIdRaw;
  spotPublish: (
    spot: NativeHandle,
    channelName: string,
    topic: string,
    metadata: Buffer | null,
    parts: NativeParts,
    flags: number
  ) => MeshPublishDetailRaw;
  spotSetPublishOption: (spot: NativeHandle, option: number, value: Buffer) => void;
  spotGetPublishOption: (spot: NativeHandle, option: number) => Buffer;
  spotSetSubscription: (
    spot: NativeHandle,
    channelName: string,
    topicFilter: string,
    kind: number
  ) => void;
  spotUnsetSubscription: (
    spot: NativeHandle,
    channelName: string,
    topicFilter: string,
    kind: number
  ) => void;

  // --- Actor --------------------------------------------------------------
  meshNodeActorNew: (
    node: NativeHandle,
    actorId: string,
    creationParts: NativeParts,
    flags: number,
    timeoutMs: number
  ) => ActorRefRaw;
  meshNodeActorLookup: (node: NativeHandle, actorId: string) => ActorLocationRaw;
  meshNodeActorLookupRemote: (
    node: NativeHandle,
    targetNodeRid: Buffer,
    actorId: string,
    timeoutMs: number
  ) => MeshOperationIdRaw;
  meshNodeActorDestroy: (
    node: NativeHandle,
    actor: ActorRefRaw,
    timeoutMs: number
  ) => MeshOperationIdRaw;
  meshNodeActorJoinSpot: (
    node: NativeHandle,
    actor: ActorRefRaw,
    targetNodeRid: Buffer,
    targetSpotRid: Buffer,
    targetSpotGeneration: bigint,
    creationParts: NativeParts,
    timeoutMs: number
  ) => MeshOperationIdRaw;
  meshNodeActorJoinEntrySpot: (
    node: NativeHandle,
    actor: ActorRefRaw,
    targetNodeRid: Buffer,
    creationParts: NativeParts,
    timeoutMs: number
  ) => MeshOperationIdRaw;
  meshNodeActorLeaveSpot: (
    node: NativeHandle,
    actor: ActorRefRaw,
    expectedMembershipEpoch: bigint,
    timeoutMs: number
  ) => MeshOperationIdRaw;
  actorJoinReply: (
    replyToken: Buffer,
    joinResult: number,
    parts: NativeParts,
    flags: number
  ) => number;
  meshNodeSendToActor: (
    node: NativeHandle,
    actor: ActorRefRaw,
    metadata: Buffer | null,
    parts: NativeParts,
    flags: number
  ) => number;
  meshNodeRequestToActor: (
    node: NativeHandle,
    actor: ActorRefRaw,
    metadata: Buffer | null,
    parts: NativeParts,
    flags: number,
    timeoutMs: number
  ) => MeshOperationIdRaw;
  actorSendToActor: (
    node: NativeHandle,
    sourceActor: ActorRefRaw,
    targetActor: ActorRefRaw,
    metadata: Buffer | null,
    parts: NativeParts,
    flags: number
  ) => number;
  actorRequestToActor: (
    node: NativeHandle,
    sourceActor: ActorRefRaw,
    targetActor: ActorRefRaw,
    metadata: Buffer | null,
    parts: NativeParts,
    flags: number,
    timeoutMs: number
  ) => MeshOperationIdRaw;

  // --- Actor transfer fence ----------------------------------------------
  meshNodeActorTransferPrepare: (
    node: NativeHandle,
    prepare: ActorTransferPrepareRaw,
    timeoutMs: number
  ) => ActorTransferPrepareOutcomeRaw;
  meshNodeActorTransferCommit: (token: Buffer, newMembershipEpoch: bigint) => void;
  meshNodeActorTransferActivate: (token: Buffer) => void;
  meshNodeActorTransferAbort: (token: Buffer) => void;

  // --- Stream session service --------------------------------------------
  streamSessionServiceNew: (node: NativeHandle, stream: NativeHandle) => NativeHandle;
  streamSessionServiceStart: (service: NativeHandle) => void;
  streamSessionServiceShutdown: (service: NativeHandle, timeoutMs: number) => number;
  streamSessionServiceDestroy: (service: NativeHandle) => void;
  streamSessionServiceStatus: (service: NativeHandle) => StreamSessionStatusRaw;
  streamSessionBindActor: (
    service: NativeHandle,
    sessionRid: Buffer,
    actor: ActorRefRaw,
    timeoutMs: number
  ) => MeshOperationIdRaw;
  streamSessionUnbindActor: (
    service: NativeHandle,
    sessionRid: Buffer,
    actor: ActorRefRaw,
    expectedBindingGeneration: bigint,
    timeoutMs: number
  ) => MeshOperationIdRaw;
  streamSessionBindings: (
    service: NativeHandle,
    sessionRid: Buffer
  ) => StreamSessionBindingRaw[];
  streamSessionSendToActor: (
    service: NativeHandle,
    sessionRid: Buffer,
    actor: ActorRefRaw,
    metadata: Buffer | null,
    parts: NativeParts,
    flags: number
  ) => number;
  streamSessionRequestToActor: (
    service: NativeHandle,
    sessionRid: Buffer,
    actor: ActorRefRaw,
    metadata: Buffer | null,
    parts: NativeParts,
    flags: number,
    timeoutMs: number
  ) => MeshOperationIdRaw;
  meshNodeActorSendBoundSession: (
    node: NativeHandle,
    actor: ActorRefRaw,
    parts: NativeParts,
    flags: number
  ) => number;
  meshNodeActorCloseBoundSession: (
    node: NativeHandle,
    actor: ActorRefRaw,
    expectedBindingGeneration: bigint,
    timeoutMs: number
  ) => MeshOperationIdRaw;
}
