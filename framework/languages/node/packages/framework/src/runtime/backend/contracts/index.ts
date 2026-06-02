import type {
  ActorRef,
  ActorRoute,
  Message,
  Received,
  RecvFlagsValue,
  Registry,
  RegistryQueryClient,
  RegistryServiceSummaryEntry,
  RegistryServiceSummaryFilter,
  RegistryStatus,
  RegistryTopologyEntry,
  RegistryTopologyFilter,
  RequestCallback,
  RequestResult,
  RoutingId,
  SendFlagsValue,
  Spot,
  SpotNode,
  SpotNodeModeValue,
  SpotNodePeerEntry,
  SpotNodeStatus,
  SpotNodeSubjectEntry,
  SpotRoute,
  TopicMessage,
  MemberPeerEntry,
  MonitorEventType
} from '@zlink-systems/zlink';

export type ZLinkBackendSendFlags = SendFlagsValue;
export type ZLinkBackendRecvFlags = RecvFlagsValue;
export type ZLinkBackendSpotNodeMode = SpotNodeModeValue;

export enum ZLinkBackendSpotDispatchEvent {
  Internal = 0,
  RouteReadable = 1,
  ChannelReplyReadable = 2,
  ActorJoinReadable = 3,
  ActorReadable = 4
}

export interface ZLinkBackendActorRef {
  readonly nodeRid: RoutingId;
  readonly actorId: string;
  readonly generation: bigint;
}

export interface ZLinkBackendActorJoinResult {
  readonly result: RequestResult;
  readonly joinResultCode: number;
  readonly actor: ZLinkBackendActorRef;
  readonly joinedSpotRid: RoutingId;
  readonly joinEpoch: bigint;
  readonly flags: number;
}

export interface ZLinkBackendActorJoinEntrySpotResult {
  readonly result: RequestResult;
  readonly actor: ZLinkBackendActorRef;
  readonly targetNodeRid: RoutingId;
  readonly joinEpoch: bigint;
  readonly flags: number;
}

export type ZLinkBackendActorJoinCallback = (
  result: ZLinkBackendActorJoinResult,
  parts: readonly Message[]
) => void;

export type ZLinkBackendActorJoinEntrySpotCallback = (
  result: ZLinkBackendActorJoinEntrySpotResult
) => void;

export interface ZLinkBackendDiscoveryRoute {
  readonly ownerRoutingId: RoutingId;
  readonly value: Message;
  dispose(): Promise<void>;
}

export interface ZLinkBackendActorPart {
  readonly actor: ZLinkBackendActorRef;
  readonly sourceNodeRid: RoutingId;
  readonly sourceSessionRid: RoutingId;
  readonly message: Message;
  readonly more: boolean;
}

export interface ZLinkBackendActorJoinRequest {
  readonly sourceActor: ZLinkBackendActorRef;
  readonly targetActor: ZLinkBackendActorRef;
  readonly sourceNodeRid: RoutingId;
  readonly targetSpotRid: RoutingId;
  readonly joinEpoch: bigint;
  readonly message: Message;
  readonly parts: readonly Message[];
  readonly nativeRequest?: unknown;
}

export interface ZLinkBackendSpotDispatchInfo {
  readonly event: ZLinkBackendSpotDispatchEvent;
  readonly drainChannelReply?: () => void;
  readonly actorParts?: readonly ZLinkBackendActorPart[];
  readonly routedMessages?: readonly Received[];
}

export interface ZLinkBackendSocketMonitorEvent {
  readonly nativeEvent: MonitorEventType;
  readonly routingId?: RoutingId;
  readonly localAddr: string;
  readonly remoteAddr: string;
  readonly value: number;
}

export interface ZLinkBackendObject {
  readonly nativeInstance: unknown;
}

export interface ZLinkBackendContext extends ZLinkBackendObject {
  shutdown(): void;
  dispose(): Promise<void>;
}

export interface ZLinkBackendDiscovery extends ZLinkBackendObject {
  spotOwnerSyncEnabled: boolean;
  actorRouteSyncEnabled: boolean;
  connectRegistry(endpoint: string): void;
  memberPeers(): readonly MemberPeerEntry[];
  resolveSpot(spotRid: RoutingId): SpotRoute;
  resolveActor(actorId: string): ActorRoute;
  bindRoute(kind: number, key: Buffer, value: Buffer): void;
  unbindRoute(kind: number, key: Buffer): void;
  resolveRoute(kind: number, key: Buffer): ZLinkBackendDiscoveryRoute;
  dispose(): Promise<void>;
}

export interface ZLinkBackendSocket extends ZLinkBackendObject {
  bind(endpoint: string): void;
  setChannelName(channelName: string): void;
  dispose(): Promise<void>;
}

export interface ZLinkBackendConnectableSocket extends ZLinkBackendSocket {
  connect(endpoint: string): void;
  disconnect(endpoint: string): void;
}

export interface ZLinkBackendDealerSocket extends ZLinkBackendConnectableSocket {
  attachDiscovery(discovery: ZLinkBackendDiscovery): void;
  onSendReady(handler: () => void): void;
  send(message: Message | readonly Message[], flags: ZLinkBackendSendFlags): boolean;
  request(
    message: Message | readonly Message[],
    callback: RequestCallback,
    flags: ZLinkBackendSendFlags,
    timeoutMs?: number
  ): boolean;
  recv(flags?: ZLinkBackendRecvFlags): Received | undefined;
}

export interface ZLinkBackendRouterSocket extends ZLinkBackendConnectableSocket {
  attachDiscovery(discovery: ZLinkBackendDiscovery): void;
  onSendReady(handler: () => void): void;
  setRoutingId(routingId: RoutingId): void;
  recv(flags?: ZLinkBackendRecvFlags): Received | undefined;
  send(
    routingId: RoutingId,
    message: Message | readonly Message[],
    flags: ZLinkBackendSendFlags
  ): boolean;
  request(
    routingId: RoutingId,
    message: Message | readonly Message[],
    callback: RequestCallback,
    flags: ZLinkBackendSendFlags,
    timeoutMs?: number
  ): boolean;
  sendToSpot(
    targetNodeRid: RoutingId,
    targetSpotRid: RoutingId,
    parts: readonly Message[],
    flags: ZLinkBackendSendFlags
  ): boolean;
  requestToSpot(
    targetNodeRid: RoutingId,
    targetSpotRid: RoutingId,
    parts: readonly Message[],
    callback: RequestCallback,
    flags: ZLinkBackendSendFlags,
    timeoutMs?: number
  ): boolean;
  reply(routingId: RoutingId, requestSeq: bigint, message: Message | readonly Message[]): void;
}

export interface ZLinkBackendPublisherSocket extends ZLinkBackendSocket {
  attachDiscovery(discovery: ZLinkBackendDiscovery): void;
  onSendReady(handler: () => void): void;
  publish(topic: string, message: Message | readonly Message[], flags: ZLinkBackendSendFlags): boolean;
}

export interface ZLinkBackendSubscriberSocket extends ZLinkBackendConnectableSocket {
  attachDiscovery(discovery: ZLinkBackendDiscovery): void;
  setSubscription(topic: string): void;
  subscribe(result: TopicMessage, flags?: ZLinkBackendRecvFlags): boolean;
}

export interface ZLinkBackendStreamSocket extends ZLinkBackendSocket {
  onFramedPacket(handler: (peer: string, header: Message, payload: Message) => void): void;
  send(routingId: RoutingId, payload: Message | readonly Message[], flags: ZLinkBackendSendFlags): boolean;
  disconnectPeer(routingId: RoutingId): void;
  attachActorGateway(node: ZLinkBackendSpotNode): void;
  bindActor(
    sessionRid: RoutingId,
    actor: ZLinkBackendActorRef,
    timeoutMs: number,
    signal?: AbortSignal
  ): Promise<void>;
  unbindActor(
    sessionRid: RoutingId,
    actorId: string,
    timeoutMs: number,
    signal?: AbortSignal
  ): Promise<void>;
  sendBoundActor(
    sessionRid: RoutingId,
    actorId: string,
    parts: readonly Message[],
    flags: ZLinkBackendSendFlags
  ): boolean;
}

export interface ZLinkBackendSocketMonitor extends ZLinkBackendObject {
  onEvent(handler: (event: ZLinkBackendSocketMonitorEvent) => void): void;
  recv(): ZLinkBackendSocketMonitorEvent;
  dispose(): Promise<void>;
}

export interface ZLinkBackendSpotNode extends ZLinkBackendObject {
  readonly routingId: RoutingId;
  setRoutingId(routingId: RoutingId): void;
  setRouterBind(endpoint: string): void;
  setPubBind(endpoint: string): void;
  attachDiscovery(discovery: ZLinkBackendDiscovery): void;
  connectPeer(endpoint: string): void;
  disconnectPeer(endpoint: string): void;
  connectRouterChannelPeer(channelName: string, endpoint: string): void;
  connectRouterChannelPeerRid(channelName: string, peerRid: RoutingId, endpoint: string): void;
  disconnectRouterChannelPeer(channelName: string, endpoint: string): void;
  disconnectRouterChannelPeerRid(channelName: string, peerRid: RoutingId): void;
  attachSpotRouteChannelDiscovery(channelName: string, discovery: ZLinkBackendDiscovery): void;
  createSpot(): ZLinkBackendSpot;
  getOrCreateSpot(spotRid: RoutingId): { readonly spot: ZLinkBackendSpot; readonly created: boolean };
  status(): SpotNodeStatus;
  peers(): readonly SpotNodePeerEntry[];
  subjects(): readonly SpotNodeSubjectEntry[];
  attachChannelDealer(discovery: ZLinkBackendDiscovery, dealer: ZLinkBackendDealerSocket): void;
  attachChannelDealerManual(channelName: string, dealer: ZLinkBackendDealerSocket): void;
  entrySpot(): ZLinkBackendSpot;
  createActor(actorId: string): ZLinkBackendActorRef;
  actorLookup(actorId: string): ZLinkBackendActorRef | undefined;
  joinActor(
    actor: ZLinkBackendActorRef,
    destNodeRid: RoutingId,
    destSpotRid: RoutingId,
    payload: Message | readonly Message[],
    callback: RequestCallback | ZLinkBackendActorJoinCallback,
    timeoutMs?: number
  ): boolean;
  joinActorEntrySpot(
    actor: ZLinkBackendActorRef,
    destNodeRid: RoutingId,
    callback: ZLinkBackendActorJoinEntrySpotCallback,
    timeoutMs?: number
  ): boolean;
  destroyActor(actor: ZLinkBackendActorRef, timeoutMs: number, signal?: AbortSignal): Promise<void>;
  sendActorBoundSession(
    actor: ZLinkBackendActorRef,
    parts: readonly Message[],
    flags: ZLinkBackendSendFlags
  ): boolean;
  closeActorBoundSession(actor: ZLinkBackendActorRef, timeoutMs: number, signal?: AbortSignal): Promise<void>;
  dispose(): Promise<void>;
}

export interface ZLinkBackendSpot extends ZLinkBackendObject {
  readonly routingId: RoutingId;
  setRoutingId(routingId: RoutingId): void;
  setSubscription(topic: string): void;
  subscribe(result: TopicMessage, flags: ZLinkBackendRecvFlags): boolean;
  recvRoute(result: Received, flags: ZLinkBackendRecvFlags): boolean;
  onDispatchEvent(handler: (info: ZLinkBackendSpotDispatchInfo) => void): void;
  onSendReady(handler: () => void): void;
  requestToChannel(
    channelName: string,
    payload: Message | readonly Message[],
    callback: RequestCallback,
    flags: ZLinkBackendSendFlags,
    timeoutMs?: number
  ): boolean;
  sendToChannel(channelName: string, payload: Message | readonly Message[], flags: ZLinkBackendSendFlags): boolean;
  publish(topic: string, payload: Message | readonly Message[], flags: ZLinkBackendSendFlags): boolean;
  sendToSpot(
    targetRid: RoutingId,
    spotRid: RoutingId,
    payload: Message | readonly Message[],
    flags: ZLinkBackendSendFlags
  ): boolean;
  requestToSpot(
    targetRid: RoutingId,
    spotRid: RoutingId,
    payload: Message | readonly Message[],
    callback: RequestCallback,
    flags: ZLinkBackendSendFlags,
    timeoutMs?: number
  ): boolean;
  recvActorJoin(flags: ZLinkBackendRecvFlags): ZLinkBackendActorJoinRequest | undefined;
  replyActorJoin(
    request: ZLinkBackendActorJoinRequest,
    joinResultCode: number,
    reply: Message | readonly Message[]
  ): void;
  dispose(): Promise<void>;
}

export interface ZLinkBackendRegistry extends ZLinkBackendObject {
  setId(registryId: number): void;
  setHeartbeat(intervalMs: number, timeoutMs: number): void;
  setBroadcastInterval(intervalMs: number): void;
  addPeer(endpoint: string): void;
  bind(pubEndpoint: string, routerEndpoint: string): void;
  status(): RegistryStatus;
  serviceSummary(filter?: RegistryServiceSummaryFilter): readonly RegistryServiceSummaryEntry[];
  topology(filter?: RegistryTopologyFilter): readonly RegistryTopologyEntry[];
  memberPeers(channelName: string): readonly MemberPeerEntry[];
  dispose(): Promise<void>;
}

export interface ZLinkBackendRegistryQueryClient extends ZLinkBackendObject {
  connect(endpoint: string): void;
  topology(filter?: RegistryTopologyFilter): readonly RegistryTopologyEntry[];
  dispose(): Promise<void>;
}

export interface ZLinkChannelBackendAdapter {
  createContext(): ZLinkBackendContext;
  createDiscovery(
    context: ZLinkBackendContext,
    autoConnectType: number,
    channelName: string
  ): ZLinkBackendDiscovery;
  createDealerSocket(context: ZLinkBackendContext): ZLinkBackendDealerSocket;
  createRouterSocket(context: ZLinkBackendContext): ZLinkBackendRouterSocket;
  createPublisherSocket(context: ZLinkBackendContext): ZLinkBackendPublisherSocket;
  createSubscriberSocket(context: ZLinkBackendContext): ZLinkBackendSubscriberSocket;
}

export interface ZLinkSpotBackendAdapter {
  createSpotNode(context: ZLinkBackendContext, mode: ZLinkBackendSpotNodeMode): ZLinkBackendSpotNode;
}

export interface ZLinkStreamBackendAdapter {
  createStreamSocket(context: ZLinkBackendContext): ZLinkBackendStreamSocket;
}

export interface ZLinkRegistryBackendAdapter {
  createRegistry(context: ZLinkBackendContext): ZLinkBackendRegistry;
  createRegistryQueryClient(context: ZLinkBackendContext): ZLinkBackendRegistryQueryClient;
}

export interface ZLinkMonitoringBackendAdapter {
  openSocketMonitor(socket: ZLinkBackendSocket): ZLinkBackendSocketMonitor;
}

export interface ZLinkBackendAdapterFactory {
  createChannelAdapter(): ZLinkChannelBackendAdapter;
  createSpotAdapter(): ZLinkSpotBackendAdapter;
  createStreamAdapter(): ZLinkStreamBackendAdapter;
  createRegistryAdapter(): ZLinkRegistryBackendAdapter;
  createMonitoringAdapter(): ZLinkMonitoringBackendAdapter;
}

export type ZLinkNativeContext = unknown;
export type ZLinkNativeRegistry = Registry;
export type ZLinkNativeRegistryQueryClient = RegistryQueryClient;
export type ZLinkNativeSpotNode = SpotNode;
export type ZLinkNativeSpot = Spot;
export type ZLinkNativeActorRef = ActorRef;
