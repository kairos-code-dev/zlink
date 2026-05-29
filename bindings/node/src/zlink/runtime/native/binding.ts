// SPDX-License-Identifier: MPL-2.0

import type {
  NativeReceivedRaw,
  NativeTopicMessageRaw
} from '../messaging/message_materializer';
import type {
  MonitorEventValueRaw,
  MonitorStatusRaw
} from '../../contracts/eventing';
import type {
  RegistryServiceSummaryFilter,
  RegistryTopologyFilter,
  SubscriptionEntry
} from '../../contracts/service';
import type {
  MemberPeerEntryRaw,
  RegistryServiceSummaryEntryRaw,
  RegistryStatusRaw,
  RegistryTopologyEntryRaw
} from '../service/registry/registry_support';
import type {
  DiscoveryActorRouteRaw,
  DiscoverySpotRouteRaw
} from '../service/discovery/discovery';
import type {
  ActorJoinEntrySpotResultRaw,
  ActorJoinResultRaw,
  ActorLookupResultRaw,
  ActorPartRaw,
  ActorRefRaw,
  SpotNodeActorEntryRaw,
  SpotNodeSpotEntryRaw
} from '../service/spot/actor_models';
import type {
  SpotActorJoinRecvRaw,
  SpotActorLifecycleRaw,
  SpotDispatchRaw,
  SpotNodePeerEntryRaw,
  SpotNodeSocketEntryRaw,
  SpotNodeSpotGetOrNewRaw,
  SpotNodeStatusRaw,
  SpotNodeSubjectEntryRaw,
  SpotRoutedRaw
} from '../service/spot/spot_raw_models';

type NativeHandle = unknown;
type NativeBuffer = Buffer;
type NullableNativeHandle = NativeHandle | null;
type NativeVersion = [number, number, number];
type NativeRequestCallback = (result: number, replyParts: Buffer[] | null) => void;
type NativeActorJoinCallback = (result: ActorJoinResultRaw | null, replyParts: Buffer[] | null) => void;
type NativeActorEntryJoinCallback = (result: ActorJoinEntrySpotResultRaw | null) => void;
type NativeActorLookupCallback = (result: ActorLookupResultRaw) => void;

interface CoreNativeBinding {
  errno: () => number;
  has: (capability: string) => boolean;
  proxy: (
    frontend: NativeHandle,
    backend: NativeHandle,
    capture: NullableNativeHandle
  ) => void;
  proxySteerable: (
    frontend: NativeHandle,
    backend: NativeHandle,
    capture: NullableNativeHandle,
    control: NativeHandle
  ) => void;
  sleep: (seconds: number) => void;
  strerror: (code: number) => string;
  version: () => NativeVersion;
}

interface ContextNativeBinding {
  ctxGetOpt: (ctx: NativeHandle, option: number) => number;
  ctxNew: () => NativeHandle;
  ctxRecalculateAutoHwm: (ctx: NativeHandle) => void;
  ctxSetOpt: (ctx: NativeHandle, option: number, value: number | NativeBuffer) => void;
  ctxShutdown: (ctx: NativeHandle) => void;
  ctxTerm: (ctx: NativeHandle) => void;
}

interface SocketNativeBinding {
  dealerRequest: (
    socket: NativeHandle,
    parts: readonly unknown[],
    callback: unknown,
    flags: number,
    timeoutMs: number
  ) => void;
  handleGetRoutingId: (handle: NativeHandle) => Buffer;
  handleSetRoutingId: (handle: NativeHandle, routingId: Buffer) => void;
  monitorOpen: (socket: NativeHandle, eventMask: number) => NativeHandle;
  routerRecvMessage: (socket: NativeHandle, flags: number) => NativeReceivedRaw | null;
  routerRecvMessageNoWait: (socket: NativeHandle) => NativeReceivedRaw | null;
  routerReply: (
    socket: NativeHandle,
    peerRid: Buffer,
    requestSeq: bigint,
    parts: readonly unknown[]
  ) => void;
  routerRequest: (
    socket: NativeHandle,
    peerRid: Buffer,
    parts: readonly unknown[],
    callback: NativeRequestCallback,
    flags: number,
    timeoutMs: number
  ) => void;
  routerSpotReply: (
    socket: NativeHandle,
    destNodeRid: Buffer,
    destSpotRid: Buffer,
    requestSeq: bigint,
    parts: readonly unknown[]
  ) => void;
  routerSpotRequest: (
    socket: NativeHandle,
    destNodeRid: Buffer,
    destSpotRid: Buffer,
    parts: readonly unknown[],
    callback: NativeRequestCallback,
    flags: number,
    timeoutMs: number
  ) => void;
  routerSpotSend: (
    socket: NativeHandle,
    destNodeRid: Buffer,
    destSpotRid: Buffer,
    parts: readonly unknown[],
    flags: number
  ) => void;
  socketAttachDiscovery: (socket: NativeHandle, discovery: NativeHandle) => void;
  socketBind: (socket: NativeHandle, endpoint: string) => void;
  socketClose: (socket: NativeHandle) => void;
  socketConnect: (socket: NativeHandle, endpoint: string) => void;
  socketDisconnect: (socket: NativeHandle, endpoint: string) => void;
  socketDisconnectRid: (socket: NativeHandle, routingId: Buffer) => void;
  socketGetChannelName: (socket: NativeHandle) => string;
  socketGetOpt: (socket: NativeHandle, option: number) => Buffer;
  socketNew: (ctx: NativeHandle, type: number) => NativeHandle;
  socketPublish: (
    socket: NativeHandle,
    topic: string,
    payload: unknown,
    flags: number
  ) => number;
  socketRecvMessage: (socket: NativeHandle, flags: number) => NativeReceivedRaw | null;
  socketRecvMessageNoWait: (socket: NativeHandle) => NativeReceivedRaw | null;
  socketSend: (socket: NativeHandle, payload: unknown, flags: number) => void;
  socketSendNoWaitResult: (socket: NativeHandle, payload: unknown) => number;
  socketSendNoWaitResultParts: (
    socket: NativeHandle,
    parts: readonly unknown[]
  ) => number;
  socketSendParts: (
    socket: NativeHandle,
    parts: readonly unknown[],
    flags: number
  ) => void;
  socketSendReadyHandler: (socket: NativeHandle, handler: unknown) => void;
  socketSendRouting: (
    socket: NativeHandle,
    routingId: Buffer,
    payload: unknown,
    flags: number
  ) => void;
  socketSendRoutingNoWaitResult: (
    socket: NativeHandle,
    routingId: Buffer,
    payload: unknown
  ) => number;
  socketSendRoutingNoWaitResultParts: (
    socket: NativeHandle,
    routingId: Buffer,
    parts: readonly unknown[]
  ) => number;
  socketSetChannelName: (socket: NativeHandle, channelName: string) => void;
  socketSetOpt: (socket: NativeHandle, option: number, value: Buffer) => void;
  socketSetSubscription: (socket: NativeHandle, topic: string) => void;
  socketSetTlsClient: (
    socket: NativeHandle,
    ca: string,
    hostname: string,
    trustSystem: number
  ) => void;
  socketSetTlsServer: (
    socket: NativeHandle,
    cert: string,
    key: string,
    requireClientCert: number
  ) => void;
  socketStreamAttach: (
    socket: NativeHandle,
    handler: (routingId: Buffer | null, packets: Buffer[]) => number,
    packetCount: number
  ) => void;
  socketSubscribeMessage: (
    socket: NativeHandle,
    flags: number
  ) => NativeTopicMessageRaw | null;
  socketSubscriptionEvent: (
    socket: NativeHandle,
    flags: number
  ) => { routingId?: Buffer | null; topic: string; subscribed: boolean } | null;
  socketTryPublish: (
    socket: NativeHandle,
    topic: string,
    payload: unknown
  ) => number;
  socketTrySubscribeMessage: (socket: NativeHandle) => NativeTopicMessageRaw | null;
  socketTrySubscriptionEvent: (
    socket: NativeHandle
  ) => { routingId?: Buffer | null; topic: string; subscribed: boolean } | null;
  socketUnbind: (socket: NativeHandle, endpoint: string) => void;
  socketUnsetSubscription: (socket: NativeHandle, topic: string) => void;
  streamAttachActorGateway: (socket: NativeHandle, node: NativeHandle) => void;
  streamBindActor: (
    stream: NativeHandle,
    sessionRid: Buffer,
    actor: ActorRefRaw,
    callback: NativeRequestCallback,
    timeoutMs: number
  ) => void;
  streamBoundActors: (
    stream: NativeHandle,
    sessionRid: Buffer
  ) => ActorRefRaw[];
  streamSendBoundActorPart: (
    stream: NativeHandle,
    sessionRid: Buffer,
    actorId: string,
    parts: readonly unknown[],
    flags: number
  ) => void;
  streamUnbindActor: (
    stream: NativeHandle,
    sessionRid: Buffer,
    actorId: string,
    callback: NativeRequestCallback,
    timeoutMs: number
  ) => void;
  subscriptionAt: (socket: NativeHandle, index: number) => SubscriptionEntry | null;
}

interface EventingNativeBinding {
  atomicCounterDec: (counter: NativeHandle) => number;
  atomicCounterDestroy: (counter: NativeHandle) => void;
  atomicCounterInc: (counter: NativeHandle) => number;
  atomicCounterNew: () => NativeHandle;
  atomicCounterSet: (counter: NativeHandle, value: number) => void;
  atomicCounterValue: (counter: NativeHandle) => number;
  monitorClose: (monitor: NativeHandle) => void;
  monitorHandler: (
    monitor: NativeHandle,
    handler: (event: MonitorEventValueRaw) => void
  ) => void;
  monitorRecv: (monitor: NativeHandle) => MonitorEventValueRaw;
  monitorRecvNoWait: (monitor: NativeHandle) => MonitorEventValueRaw | null;
  monitorStatus: (monitor: NativeHandle) => MonitorStatusRaw;
  pollEventsDestroy: (events: NativeHandle) => void;
  pollEventsFd: (events: NativeHandle, index: number) => number;
  pollEventsNew: (capacity: number) => NativeHandle;
  pollEventsRevents: (events: NativeHandle, index: number) => number;
  pollEventsSlot: (events: NativeHandle, index: number) => number;
  pollEventsSourceKind: (events: NativeHandle, index: number) => number;
  pollerAdd: (
    poller: NativeHandle,
    handle: NativeHandle,
    slot: bigint | null,
    events: number
  ) => void;
  pollerAddFd: (
    poller: NativeHandle,
    fd: number,
    slot: bigint,
    events: number
  ) => void;
  pollerAddTimer: (poller: NativeHandle, timer: NativeHandle, slot: bigint) => void;
  pollerDestroy: (poller: NativeHandle) => void;
  pollerModify: (poller: NativeHandle, handle: NativeHandle, events: number) => void;
  pollerModifyFd: (poller: NativeHandle, fd: number, events: number) => void;
  pollerNew: () => NativeHandle;
  pollerRemove: (poller: NativeHandle, handle: NativeHandle) => void;
  pollerRemoveFd: (poller: NativeHandle, fd: number) => void;
  pollerRemoveTimer: (poller: NativeHandle, timer: NativeHandle) => void;
  pollerSize: (poller: NativeHandle) => number;
  pollerWait: (poller: NativeHandle, timeoutMs: number) => number;
  pollerWaitInto: (
    poller: NativeHandle,
    events: NativeHandle,
    capacity: number,
    timeoutMs: number
  ) => number;
  spotTimerNew: (spot: NativeHandle) => NativeHandle;
  stopwatchIntermediate: (watch: NativeHandle) => number;
  stopwatchStart: () => NativeHandle;
  stopwatchStop: (watch: NativeHandle) => number;
  timerDestroy: (timer: NativeHandle) => void;
  timerHandler: (
    timer: NativeHandle,
    handler: (fireCount: bigint) => void
  ) => void;
  timerNew: () => NativeHandle;
  timerRecv: (timer: NativeHandle, flags: number) => bigint | null;
  timerStart: (timer: NativeHandle, intervalNs: bigint, repeatCount: bigint) => void;
  timerStop: (timer: NativeHandle) => void;
}

interface ServiceNativeBinding {
  discoveryConnectRegistry: (discovery: NativeHandle, endpoint: string) => void;
  discoveryDestroy: (discovery: NativeHandle) => void;
  discoveryGetProviders: (discovery: NativeHandle) => MemberPeerEntryRaw[];
  discoveryGetValue: (discovery: NativeHandle) => number;
  discoveryNew: (
    ctx: NativeHandle,
    autoConnectType: number,
    channelName: string
  ) => NativeHandle;
  discoveryResolveActor: (discovery: NativeHandle, actorId: string) => DiscoveryActorRouteRaw;
  discoveryResolveSpot: (discovery: NativeHandle, spotRid: Buffer) => DiscoverySpotRouteRaw;
  discoverySetTlsClient: (
    discovery: NativeHandle,
    ca: string,
    hostname: string,
    trustSystem: number
  ) => void;
  discoverySetValue: (discovery: NativeHandle, value: number) => void;
  registryAddPeer: (registry: NativeHandle, pubEndpoint: string) => void;
  registryDestroy: (registry: NativeHandle) => void;
  registryMemberPeers: (registry: NativeHandle, channelName: string) => MemberPeerEntryRaw[];
  registryNew: (ctx: NativeHandle) => NativeHandle;
  registryQueryClientConnect: (client: NativeHandle, endpoint: string) => void;
  registryQueryClientNew: (ctx: NativeHandle) => NativeHandle;
  registryQueryDestroy: (client: NativeHandle) => void;
  registryQueryTopology: (
    client: NativeHandle,
    filter?: RegistryTopologyFilter
  ) => RegistryTopologyEntryRaw[];
  registryServiceSummary: (
    registry: NativeHandle,
    filter?: RegistryServiceSummaryFilter
  ) => RegistryServiceSummaryEntryRaw[];
  registrySetBroadcastInterval: (registry: NativeHandle, intervalMs: number) => void;
  registrySetEndpoints: (
    registry: NativeHandle,
    pubEndpoint: string,
    routerEndpoint: string
  ) => void;
  registrySetHeartbeat: (
    registry: NativeHandle,
    intervalMs: number,
    timeoutMs: number
  ) => void;
  registrySetId: (registry: NativeHandle, id: number) => void;
  registrySetTlsClient: (
    registry: NativeHandle,
    ca: string,
    hostname: string,
    trustSystem: number
  ) => void;
  registrySetTlsServer: (
    registry: NativeHandle,
    cert: string,
    key: string,
    requireClientCert: number
  ) => void;
  registryStatus: (registry: NativeHandle) => RegistryStatusRaw;
  registryTopology: (
    registry: NativeHandle,
    filter?: RegistryTopologyFilter
  ) => RegistryTopologyEntryRaw[];
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
    parts: readonly unknown[]
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
    callback: NativeActorEntryJoinCallback,
    timeoutMs: number
  ) => void;
  spotNodeActorJoinSpot: (
    node: NativeHandle,
    actor: ActorRefRaw,
    destNodeRid: Buffer,
    destSpotRid: Buffer,
    parts: readonly unknown[],
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
  spotNodeActorNew: (node: NativeHandle, actorId: string) => ActorRefRaw;
  spotNodeActorRecvPart: (
    node: NativeHandle,
    actor: ActorRefRaw,
    flags: number
  ) => ActorPartRaw | null;
  spotNodeActorSendBoundSessionMsg: (
    node: NativeHandle,
    actor: ActorRefRaw,
    parts: readonly unknown[],
    flags: number
  ) => void;
  spotNodeActors: (node: NativeHandle) => SpotNodeActorEntryRaw[];
  spotNodeAttachChannelDealer: (
    node: NativeHandle,
    discovery: NativeHandle,
    dealer: NativeHandle
  ) => void;
  spotNodeAttachChannelDealerManual: (
    node: NativeHandle,
    channelName: string,
    dealer: NativeHandle
  ) => void;
  spotNodeAttachPubIngress: (node: NativeHandle, pub: NativeHandle) => void;
  spotNodeAttachRouterChannelDiscovery: (
    node: NativeHandle,
    channelName: string,
    discovery: NativeHandle
  ) => void;
  spotNodeConnectPeerPub: (node: NativeHandle, endpoint: string) => void;
  spotNodeConnectRouterChannelPeer: (
    node: NativeHandle,
    channelName: string,
    endpoint: string
  ) => void;
  spotNodeDestroy: (node: NativeHandle) => void;
  spotNodeDisconnectPeerPub: (node: NativeHandle, endpoint: string) => void;
  spotNodeDisconnectPeerRidPub: (node: NativeHandle, targetNodeRid: Buffer) => void;
  spotNodeDisconnectRouterChannelPeer: (
    node: NativeHandle,
    channelName: string,
    endpoint: string
  ) => void;
  spotNodeDisconnectRouterChannelPeerRid: (
    node: NativeHandle,
    channelName: string,
    peerRid: Buffer
  ) => void;
  spotNodeEntrySpot: (node: NativeHandle) => NativeHandle;
  spotNodeGetOption: (node: NativeHandle, option: number) => Buffer;
  spotNodeInternalSockets: (node: NativeHandle, filter?: unknown) => SpotNodeSocketEntryRaw[];
  spotNodeNew: (ctx: NativeHandle, options: { mode: number }) => NativeHandle;
  spotNodePeers: (node: NativeHandle) => SpotNodePeerEntryRaw[];
  spotNodePeersQuery: (node: NativeHandle, filter?: unknown) => SpotNodePeerEntryRaw[];
  spotNodeSetDiscovery: (node: NativeHandle, discovery: NativeHandle) => void;
  spotNodeSetOption: (node: NativeHandle, option: number, value: Buffer) => void;
  spotNodeSetPubBind: (node: NativeHandle, endpoint: string) => void;
  spotNodeSetRouterBind: (node: NativeHandle, endpoint: string) => void;
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
    parts: readonly unknown[],
    flags: number
  ) => void;
  spotRecv: (spot: NativeHandle, flags: number) => NativeTopicMessageRaw | null;
  spotRecvActorLifecycle: (spot: NativeHandle, flags: number) => SpotActorLifecycleRaw | null;
  spotRecvNoWait: (spot: NativeHandle) => NativeTopicMessageRaw | null;
  spotRecvRouted: (spot: NativeHandle, flags: number) => SpotRoutedRaw | null;
  spotReplyRouter: (
    spot: NativeHandle,
    peerRid: Buffer,
    requestSeq: bigint,
    parts: readonly Buffer[]
  ) => void;
  spotReplySpot: (
    spot: NativeHandle,
    destNodeRid: Buffer,
    destSpotRid: Buffer,
    requestSeq: bigint,
    parts: readonly Buffer[]
  ) => void;
  spotRequestChannel: (
    spot: NativeHandle,
    channelName: string,
    parts: readonly unknown[],
    callback: NativeRequestCallback,
    flags: number,
    timeoutMs: number
  ) => void;
  spotRequestRouter: (
    spot: NativeHandle,
    peerRid: Buffer,
    parts: readonly unknown[],
    callback: NativeRequestCallback,
    flags: number,
    timeoutMs: number
  ) => void;
  spotRequestSpot: (
    spot: NativeHandle,
    destNodeRid: Buffer,
    destSpotRid: Buffer,
    parts: readonly unknown[],
    callback: NativeRequestCallback,
    flags: number,
    timeoutMs: number
  ) => void;
  spotSendChannel: (
    spot: NativeHandle,
    channelName: string,
    parts: readonly unknown[],
    flags: number
  ) => void;
  spotSendReadyHandler: (spot: NativeHandle, handler: unknown) => void;
  spotSendToSpot: (
    spot: NativeHandle,
    destNodeRid: Buffer,
    destSpotRid: Buffer,
    parts: readonly unknown[],
    flags: number
  ) => void;
  spotSetOption: (spot: NativeHandle, option: number, value: Buffer) => void;
  spotSubscribe: (spot: NativeHandle, topic: string) => void;
  spotUnsubscribe: (spot: NativeHandle, topic: string) => void;
}

export interface NativeBinding
  extends CoreNativeBinding,
    ContextNativeBinding,
    SocketNativeBinding,
    EventingNativeBinding,
    ServiceNativeBinding {}
