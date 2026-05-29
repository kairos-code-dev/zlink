// SPDX-License-Identifier: MPL-2.0

type NativeFn = (...args: unknown[]) => unknown;
type NativeHandle = unknown;
type NativeBuffer = Buffer;
type NullableNativeHandle = NativeHandle | null;
type NativeVersion = [number, number, number];
type NativeVoidFn = (...args: unknown[]) => void;
type NativeHandleFn = (...args: unknown[]) => NativeHandle;

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
  dealerRequest: NativeFn;
  handleGetRoutingId: NativeFn;
  handleSetRoutingId: NativeFn;
  monitorOpen: NativeFn;
  routerRecvMessage: NativeFn;
  routerRecvMessageNoWait: NativeFn;
  routerReply: NativeFn;
  routerRequest: NativeFn;
  routerSpotReply: NativeFn;
  routerSpotRequest: NativeFn;
  routerSpotSend: NativeFn;
  socketAttachDiscovery: NativeFn;
  socketBind: NativeFn;
  socketClose: NativeFn;
  socketConnect: NativeFn;
  socketDisconnect: NativeFn;
  socketDisconnectRid: NativeFn;
  socketGetChannelName: NativeFn;
  socketGetOpt: NativeFn;
  socketNew: NativeFn;
  socketPublish: NativeFn;
  socketRecv: NativeFn;
  socketRecvMessage: NativeFn;
  socketRecvMessageNoWait: NativeFn;
  socketSend: NativeFn;
  socketSendNoWaitResult: NativeFn;
  socketSendNoWaitResultParts: NativeFn;
  socketSendParts: NativeFn;
  socketSendReadyHandler: NativeFn;
  socketSendRouting: NativeFn;
  socketSendRoutingNoWaitResult: NativeFn;
  socketSendRoutingNoWaitResultParts: NativeFn;
  socketSetChannelName: NativeFn;
  socketSetOpt: NativeFn;
  socketSetTlsClient: NativeFn;
  socketSetTlsServer: NativeFn;
  socketStreamAttach: NativeFn;
  socketSubscribeMessage: NativeFn;
  socketSubscriptionEvent: NativeFn;
  socketTryPublish: NativeFn;
  socketTrySubscribeMessage: NativeFn;
  socketTrySubscriptionEvent: NativeFn;
  socketUnbind: NativeFn;
  streamAttachActorGateway: NativeFn;
  streamBindActor: NativeFn;
  streamBoundActors: NativeFn;
  streamSendBoundActorPart: NativeFn;
  streamUnbindActor: NativeFn;
  subscriptionAt: NativeFn;
}

interface EventingNativeBinding {
  atomicCounterDec: NativeFn;
  atomicCounterDestroy: NativeFn;
  atomicCounterInc: NativeFn;
  atomicCounterNew: NativeHandleFn;
  atomicCounterSet: NativeFn;
  atomicCounterValue: NativeFn;
  monitorClose: NativeVoidFn;
  monitorHandler: NativeFn;
  monitorRecv: NativeFn;
  monitorRecvNoWait: NativeFn;
  monitorStatus: NativeFn;
  pollEventsDestroy: NativeFn;
  pollEventsFd: NativeFn;
  pollEventsNew: NativeFn;
  pollEventsRevents: NativeFn;
  pollEventsSlot: NativeFn;
  pollEventsSourceKind: NativeFn;
  pollerAdd: NativeFn;
  pollerAddFd: NativeFn;
  pollerAddTimer: NativeFn;
  pollerDestroy: NativeFn;
  pollerModify: NativeFn;
  pollerModifyFd: NativeFn;
  pollerNew: NativeFn;
  pollerRemove: NativeFn;
  pollerRemoveFd: NativeFn;
  pollerRemoveTimer: NativeFn;
  pollerSize: NativeFn;
  pollerWait: NativeFn;
  pollerWaitInto: NativeFn;
  poll: NativeFn;
  spotTimerNew: NativeFn;
  stopwatchIntermediate: NativeFn;
  stopwatchStart: NativeFn;
  stopwatchStop: NativeFn;
  timerDestroy: NativeFn;
  timerHandler: NativeFn;
  timerNew: NativeFn;
  timerRecv: NativeFn;
  timerStart: NativeFn;
  timerStop: NativeFn;
}

interface ServiceNativeBinding {
  discoveryConnectRegistry: NativeFn;
  discoveryDestroy: NativeFn;
  discoveryGetProviders: NativeFn;
  discoveryGetValue: NativeFn;
  discoveryNew: NativeFn;
  discoveryResolveActor: NativeFn;
  discoveryResolveSpot: NativeFn;
  discoverySetTlsClient: NativeFn;
  discoverySetValue: NativeFn;
  registryAddPeer: NativeFn;
  registryDestroy: NativeFn;
  registryMemberPeers: NativeFn;
  registryNew: NativeFn;
  registryQueryClientConnect: NativeFn;
  registryQueryClientNew: NativeFn;
  registryQueryDestroy: NativeFn;
  registryQueryTopology: NativeFn;
  registryServiceSummary: NativeFn;
  registrySetBroadcastInterval: NativeFn;
  registrySetEndpoints: NativeFn;
  registrySetHeartbeat: NativeFn;
  registrySetId: NativeFn;
  registrySetTlsClient: NativeFn;
  registrySetTlsServer: NativeFn;
  registryStatus: NativeFn;
  registryTopology: NativeFn;
  remoteActorGetRef: NativeFn;
  spotActorJoinRecv: NativeFn;
  spotActorJoinReply: NativeFn;
  spotActors: NativeFn;
  spotDestroy: NativeFn;
  spotDispatchEventHandler: NativeFn;
  spotGetOption: NativeFn;
  spotNew: NativeFn;
  spotNodeActorCloseBoundSession: NativeFn;
  spotNodeActorDestroy: NativeFn;
  spotNodeActorJoinEntrySpot: NativeFn;
  spotNodeActorJoinSpot: NativeFn;
  spotNodeActorLeaveSpot: NativeFn;
  spotNodeActorLookup: NativeFn;
  spotNodeActorNew: NativeFn;
  spotNodeActorRecvPart: NativeFn;
  spotNodeActorSendBoundSessionMsg: NativeFn;
  spotNodeActors: NativeFn;
  spotNodeAttachChannelDealer: NativeFn;
  spotNodeAttachChannelDealerManual: NativeFn;
  spotNodeAttachPubIngress: NativeFn;
  spotNodeAttachRouterChannelDiscovery: NativeFn;
  spotNodeConnectPeerPub: NativeFn;
  spotNodeConnectRouterChannelPeer: NativeFn;
  spotNodeDestroy: NativeFn;
  spotNodeDisconnectPeerPub: NativeFn;
  spotNodeDisconnectPeerRidPub: NativeFn;
  spotNodeDisconnectRouterChannelPeer: NativeFn;
  spotNodeDisconnectRouterChannelPeerRid: NativeFn;
  spotNodeEntrySpot: NativeFn;
  spotNodeGetOption: NativeFn;
  spotNodeInternalSockets: NativeFn;
  spotNodeNew: NativeFn;
  spotNodePeers: NativeFn;
  spotNodePeersQuery: NativeFn;
  spotNodeSetDiscovery: NativeFn;
  spotNodeSetOption: NativeFn;
  spotNodeSetPubBind: NativeFn;
  spotNodeSetRouterBind: NativeFn;
  spotNodeSetTlsClient: NativeFn;
  spotNodeSetTlsServer: NativeFn;
  spotNodeSpotGetOrNew: NativeFn;
  spotNodeSpotLookup: NativeFn;
  spotNodeSpots: NativeFn;
  spotNodeStatus: NativeFn;
  spotNodeSubjects: NativeFn;
  spotPublish: NativeFn;
  spotRecv: NativeFn;
  spotRecvActorLifecycle: NativeFn;
  spotRecvNoWait: NativeFn;
  spotRecvRouted: NativeFn;
  spotReplyRouter: NativeFn;
  spotReplySpot: NativeFn;
  spotRequestChannel: NativeFn;
  spotRequestRouter: NativeFn;
  spotRequestSpot: NativeFn;
  spotSendChannel: NativeFn;
  spotSendReadyHandler: NativeFn;
  spotSendToSpot: NativeFn;
  spotSetOption: NativeFn;
  spotSubscribe: NativeFn;
  spotUnsubscribe: NativeFn;
}

export interface NativeBinding
  extends CoreNativeBinding,
    ContextNativeBinding,
    SocketNativeBinding,
    EventingNativeBinding,
    ServiceNativeBinding {}
