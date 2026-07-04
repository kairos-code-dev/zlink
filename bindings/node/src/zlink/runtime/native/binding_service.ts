// SPDX-License-Identifier: MPL-2.0

import type {
  ActorPartRaw,
  ActorRefRaw,
  NativeActorEntryJoinCallback,
  NativeActorJoinCallback,
  NativeActorLookupCallback,
  NativeHandle,
  NativeRequestCallback,
  NativeTopicMessageRaw,
  SpotActorJoinRecvRaw,
  SpotActorLifecycleRaw,
  SpotDispatchRaw,
  SpotNodeActorEntryRaw,
  SpotNodePeerEntryRaw,
  SpotNodeSocketEntryRaw,
  SpotNodeSpotEntryRaw,
  SpotNodeSpotGetOrNewRaw,
  SpotNodeStatusRaw,
  SpotNodeSubjectEntryRaw,
  SpotRoutedRaw
} from './binding_types';

export interface ServiceNativeBinding {
  remoteActorGetRef: (
    node: NativeHandle,
    targetNodeRid: Buffer,
    actorId: string,
    callback: NativeActorLookupCallback,
    timeoutMs: number
  ) => void;
  spotActorJoinRecv: (spot: NativeHandle, flags: number) => SpotActorJoinRecvRaw | null;
  spotActorJoinReply: (
    spot: NativeHandle,
    info: Record<string, unknown>,
    joinResultCode: number,
    parts: unknown
  ) => void;
  spotActors: (spot: NativeHandle) => ActorRefRaw[];
  spotDestroy: (spot: NativeHandle) => void;
  spotDispatchEventHandler: (
    spot: NativeHandle,
    node: NativeHandle,
    handler: (event: SpotDispatchRaw) => void
  ) => void;
  spotGetOption: (spot: NativeHandle, option: number) => Buffer;
  spotNew: (node: NativeHandle) => NativeHandle;
  spotNodeActorCloseBoundSession: (
    node: NativeHandle,
    actor: ActorRefRaw,
    timeoutMs: number
  ) => void;
  spotNodeActorDestroy: (
    node: NativeHandle,
    actor: ActorRefRaw,
    callbackOrTimeout: NativeRequestCallback | number,
    timeoutMs?: number
  ) => void;
  spotNodeActorJoinEntrySpot: (
    node: NativeHandle,
    actor: ActorRefRaw,
    destNodeRid: Buffer,
    parts: unknown,
    callback: NativeActorEntryJoinCallback,
    flags: number,
    timeoutMs: number
  ) => void;
  spotNodeActorJoinSpot: (
    node: NativeHandle,
    actor: ActorRefRaw,
    destNodeRid: Buffer,
    destSpotRid: Buffer,
    parts: unknown,
    callback: NativeActorJoinCallback,
    flags: number,
    timeoutMs: number
  ) => void;
  spotNodeActorLeaveSpot: (
    node: NativeHandle,
    actor: ActorRefRaw,
    currentSpotRid: Buffer,
    callback: NativeRequestCallback,
    timeoutMs: number
  ) => void;
  spotNodeActorLookup: (node: NativeHandle, actorId: string) => ActorRefRaw;
  spotNodeActorNew: (node: NativeHandle, actorId: string, request?: unknown) => ActorRefRaw;
  spotNodeActorRecvPart: (
    node: NativeHandle,
    actor: ActorRefRaw,
    flags: number
  ) => ActorPartRaw | null;
  spotNodeActorSendBoundSessionMsg: (
    node: NativeHandle,
    actor: ActorRefRaw,
    parts: unknown,
    flags: number
  ) => void;
  spotNodeSendToActor: (
    node: NativeHandle,
    actor: ActorRefRaw,
    parts: unknown,
    flags: number,
    timeoutMs: number,
    callback?: NativeRequestCallback
  ) => void;
  spotNodeRequestToActor: (
    node: NativeHandle,
    actor: ActorRefRaw,
    parts: unknown,
    callback: NativeRequestCallback,
    flags: number,
    timeoutMs: number
  ) => void;
  spotNodeActorReplyNoBind: (
    node: NativeHandle,
    info: unknown,
    parts: unknown,
    result: number
  ) => void;
  spotNodeActors: (node: NativeHandle) => SpotNodeActorEntryRaw[];
  spotRouteBridgeNew: (ctx: NativeHandle, node: NativeHandle) => NativeHandle;
  spotRouteBridgeClose: (bridge: NativeHandle) => void;
  spotRouteBridgeAttachRouterChannel: (
    bridge: NativeHandle,
    channelName: string,
    router: NativeHandle,
    capabilities: number
  ) => void;
  spotRouteBridgeSend: (
    bridge: NativeHandle,
    channelName: string,
    targetNodeRid: Buffer,
    targetSpotRid: Buffer,
    parts: unknown,
    flags: number
  ) => boolean;
  spotRouteBridgeRequest: (
    bridge: NativeHandle,
    channelName: string,
    targetNodeRid: Buffer,
    targetSpotRid: Buffer,
    parts: unknown,
    callback: NativeRequestCallback,
    flags: number,
    timeoutMs: number
  ) => void;
  spotRouteBridgeHandleRouterReceived: (
    bridge: NativeHandle,
    channelName: string,
    sourceNodeRid: Buffer,
    requestSeq: bigint,
    parts: unknown
  ) => boolean;
  spotRouteBridgeDrain: (bridge: NativeHandle) => number;
  spotNodePublisherNew: (node: NativeHandle) => NativeHandle;
  spotNodePublisherPublish: (
    publisher: NativeHandle,
    topic: string,
    parts: unknown,
    flags: number
  ) => boolean;
  spotNodePublisherClose: (publisher: NativeHandle) => void;
  spotNodeConnectPeerPub: (node: NativeHandle, endpoint: string) => void;
  spotNodeConnectPeerRidPub: (
    node: NativeHandle,
    targetNodeRid: Buffer,
    endpoint: string
  ) => void;
  spotNodeDestroy: (node: NativeHandle) => void;
  spotNodeDisconnectPeerPub: (node: NativeHandle, endpoint: string) => void;
  spotNodeDisconnectPeerRidPub: (node: NativeHandle, targetNodeRid: Buffer) => void;
  spotNodeEntrySpot: (node: NativeHandle) => NativeHandle;
  spotNodeGetOption: (node: NativeHandle, option: number) => Buffer;
  spotNodeInternalSockets: (node: NativeHandle, filter?: unknown) => SpotNodeSocketEntryRaw[];
  spotNodeNew: (ctx: NativeHandle, options: { mode: number }) => NativeHandle;
  spotNodePeers: (node: NativeHandle) => SpotNodePeerEntryRaw[];
  spotNodePeersQuery: (node: NativeHandle, filter?: unknown) => SpotNodePeerEntryRaw[];
  spotNodeProcessRoutedRouter: (node: NativeHandle) => void;
  spotNodeTryProcessRoutedRouterParts: (node: NativeHandle, parts: unknown) => boolean;
  spotNodeSetOption: (node: NativeHandle, option: number, value: Buffer) => void;
  spotNodeSetPubBind: (node: NativeHandle, endpoint: string) => void;
  spotNodeSetPubRoutingId: (node: NativeHandle, routingId: Buffer) => void;
  spotNodeSetRouterBind: (node: NativeHandle, endpoint: string) => void;
  spotNodeSetSubRoutingId: (node: NativeHandle, routingId: Buffer) => void;
  spotNodeSetTlsClient: (
    node: NativeHandle,
    ca: string,
    hostname: string,
    trustSystem: number
  ) => void;
  spotNodeSetTlsServer: (
    node: NativeHandle,
    cert: string,
    key: string,
    requireClientCert: number
  ) => void;
  spotNodeSpotGetOrNew: (
    node: NativeHandle,
    spotRid: Buffer
  ) => SpotNodeSpotGetOrNewRaw;
  spotNodeSpotLookup: (node: NativeHandle, spotRid: Buffer) => NativeHandle | null;
  spotNodeSpots: (node: NativeHandle) => SpotNodeSpotEntryRaw[];
  spotNodeStatus: (node: NativeHandle) => SpotNodeStatusRaw;
  spotNodeSubjects: (node: NativeHandle, filter?: unknown) => SpotNodeSubjectEntryRaw[];
  spotPublish: (
    spot: NativeHandle,
    topic: string,
    parts: unknown,
    flags: number
  ) => void;
  spotRecv: (spot: NativeHandle, flags: number) => NativeTopicMessageRaw | null;
  spotRecvActorLifecycle: (spot: NativeHandle, flags: number) => SpotActorLifecycleRaw | null;
  spotRecvNoWait: (spot: NativeHandle) => NativeTopicMessageRaw | null;
  spotRecvRouted: (spot: NativeHandle, flags: number) => SpotRoutedRaw | null;
  spotRecvRoutedNoWait: (spot: NativeHandle) => SpotRoutedRaw | null;
  spotRecvRoutedMetricLatency: (
    spot: NativeHandle,
    runId: number,
    msgSize: number,
    expectedSize: number,
    activeStartNs: bigint,
    activeStopNs: bigint
  ) => number | false | null;
  spotReplyRouter: (
    spot: NativeHandle,
    peerRid: Buffer,
    requestSeq: bigint,
    parts: unknown
  ) => void;
  spotDrainReply: (spot: NativeHandle) => number;
  spotDrainChannelReply: (spot: NativeHandle, subjectHandle: bigint) => number;
  spotReplySpot: (
    spot: NativeHandle,
    destNodeRid: Buffer,
    destSpotRid: Buffer,
    requestSeq: bigint,
    parts: unknown
  ) => void;
  spotRequestChannel: (
    spot: NativeHandle,
    channelName: string,
    parts: unknown,
    callback: NativeRequestCallback,
    flags: number,
    timeoutMs: number
  ) => void;
  spotRequestRouter: (
    spot: NativeHandle,
    peerRid: Buffer,
    parts: unknown,
    callback: NativeRequestCallback,
    flags: number,
    timeoutMs: number
  ) => void;
  spotRequestSpot: (
    spot: NativeHandle,
    destNodeRid: Buffer,
    destSpotRid: Buffer,
    parts: unknown,
    callback: NativeRequestCallback,
    flags: number,
    timeoutMs: number
  ) => void;
  spotSendChannel: (
    spot: NativeHandle,
    channelName: string,
    parts: unknown,
    flags: number
  ) => void;
  spotSendReadyHandler: (spot: NativeHandle, handler: unknown) => void;
  spotSendToSpot: (
    spot: NativeHandle,
    destNodeRid: Buffer,
    destSpotRid: Buffer,
    parts: unknown,
    flags: number
  ) => void;
  spotSendToSpotNoWaitResult: (
    spot: NativeHandle,
    destNodeRid: Buffer,
    destSpotRid: Buffer,
    parts: unknown
  ) => number;
  spotSetOption: (spot: NativeHandle, option: number, value: Buffer) => void;
  spotSubscribe: (spot: NativeHandle, topic: string) => void;
  spotUnsubscribe: (spot: NativeHandle, topic: string) => void;
}
