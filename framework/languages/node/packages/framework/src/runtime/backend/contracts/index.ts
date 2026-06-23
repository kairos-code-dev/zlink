import type {
  ActorRoute,
  Received,
  RecvFlagsValue,
  RegistryServiceSummaryEntry,
  RegistryServiceSummaryFilter,
  RegistryStatus,
  RegistryTopologyEntry,
  RegistryTopologyFilter,
  RequestCallback,
  RequestResult,
  SendFlagsValue,
  SpotNodeModeValue,
  SpotNodePeerEntry,
  SpotNodeStatus,
  SpotNodeSubjectEntry,
  SpotRoute,
  TopicMessage,
  MemberPeerEntry,
  MonitorEventType,
  MessageLike
} from '@zlink-systems/zlink';
import type {
  RoutingId
} from '../../../contracts';
import type { Message } from '../../../contracts/Common/Message';

export type ZLinkBackendSendFlags = SendFlagsValue;
export type ZLinkBackendRecvFlags = RecvFlagsValue;
export type ZLinkBackendSpotNodeMode = SpotNodeModeValue;
export const ZLINK_BACKEND_SPOT_NODE_MODE_PUBSUB = 1 as ZLinkBackendSpotNodeMode;
export const ZLINK_BACKEND_SPOT_NODE_MODE_ROUTED = 2 as ZLinkBackendSpotNodeMode;
export const ZLINK_BACKEND_SPOT_NODE_MODE_ALL = 3 as ZLinkBackendSpotNodeMode;
export const ZLINK_BACKEND_SPOT_ROUTE_BRIDGE_ROUTE_ONLY = 0x00000001;
export const ZLINK_BACKEND_SPOT_ROUTE_BRIDGE_ROUTE_WITH_CHANNEL_INBOUND = 0x00000003;

export enum ZLinkBackendSpotDispatchEvent {
  SubscribeReadable = 1,
  RoutedReadable = 2,
  TimerReadable = 3,
  ChannelReplyReadable = 4,
  ActorReadable = 5,
  ActorJoinReadable = 6,
  ActorLifecycleReadable = 7
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
  readonly joinResultCode: number;
  readonly actor: ZLinkBackendActorRef;
  readonly targetNodeRid: RoutingId;
  readonly joinedSpotRid: RoutingId;
  readonly joinEpoch: bigint;
  readonly flags: number;
}

export type ZLinkBackendActorJoinCallback = (
  result: ZLinkBackendActorJoinResult,
  parts: readonly Message[]
) => void;

export type ZLinkBackendActorJoinEntrySpotCallback = (
  result: ZLinkBackendActorJoinEntrySpotResult,
  parts: readonly Message[]
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

export interface ZLinkBackendActorJoinInfo {
  readonly sourceActor: ZLinkBackendActorRef;
  readonly targetActor: ZLinkBackendActorRef;
  readonly sourceNodeRid: RoutingId;
  readonly sourceSpotRid: RoutingId;
  readonly targetNodeRid: RoutingId;
  readonly targetSpotRid: RoutingId;
  readonly joinEpoch: bigint;
  readonly flags: number;
}

export interface ZLinkBackendActorJoinRequest {
  readonly info: ZLinkBackendActorJoinInfo;
  readonly message: Message;
}

export interface ZLinkBackendActorJoinReplyOperation {
  message(message: Message): ZLinkBackendActorJoinReplyOperation;
  submit(): void;
}

export interface ZLinkBackendSpotDispatchInfo {
  readonly event: ZLinkBackendSpotDispatchEvent;
  readonly subjectKind?: number;
  readonly subjectHandle?: bigint;
  readonly routed?: Received | null;
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

export interface ZLinkBackendReplyOperation {
  message(message: unknown): ZLinkBackendReplyOperation;
  submit(): void;
  flags(flags: ZLinkBackendSendFlags): { submit(): void };
}

export interface ZLinkBackendSendOperation {
  message(message: MessageLike): ZLinkBackendSendOperation;
  flags(flags: ZLinkBackendSendFlags): ZLinkBackendSendOperation;
  submit(): boolean;
}

export interface ZLinkBackendRequestOperation {
  message(message: MessageLike): ZLinkBackendRequestOperation;
  timeout(timeoutMs: number): ZLinkBackendRequestOperation;
  flags(flags: ZLinkBackendSendFlags): ZLinkBackendRequestOperation;
  submit(callback: RequestCallback): boolean;
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
  reply(routingId: RoutingId, requestSeq: bigint): ZLinkBackendReplyOperation;
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

export interface ZLinkBackendReadablePoller {
  wait(timeoutMs: number): boolean;
  dispose(): void;
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

export interface ZLinkBackendSpotRouteBridge extends ZLinkBackendObject {
  attachDealerChannel(
    channelName: string,
    dealer: ZLinkBackendDealerSocket,
    options?: { readonly capabilities?: number }
  ): void;
  attachRouterChannel(
    channelName: string,
    router: ZLinkBackendRouterSocket,
    options?: { readonly capabilities?: number }
  ): void;
  setTargetNode(channelName: string, targetNodeRid: RoutingId): void;
  send(channelName: string, targetSpotRid: RoutingId): ZLinkBackendSendOperation;
  request(channelName: string, targetSpotRid: RoutingId): ZLinkBackendRequestOperation;
  handleRouterReceived(
    channelName: string,
    sourceNodeRid: RoutingId,
    parts: readonly MessageLike[],
    requestSeq?: bigint | number
  ): boolean;
  dispose(): Promise<void>;
}

export interface ZLinkBackendSpotNode extends ZLinkBackendObject {
  readonly routingId: RoutingId;
  setRoutingId(routingId: RoutingId): void;
  setRouterBind(endpoint: string): void;
  setPubBind(endpoint: string): void;
  attachDiscovery(discovery: ZLinkBackendDiscovery): void;
  connectPeer(endpoint: string): void;
  connectPeerRid(targetNodeRid: RoutingId, endpoint: string): void;
  disconnectPeer(endpoint: string): void;
  createSpot(): ZLinkBackendSpot;
  getOrCreateSpot(spotRid: RoutingId): { readonly spot: ZLinkBackendSpot; readonly created: boolean };
  status(): SpotNodeStatus;
  peers(): readonly SpotNodePeerEntry[];
  subjects(): readonly SpotNodeSubjectEntry[];
  createRouteBridge(): ZLinkBackendSpotRouteBridge;
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
    request: Message,
    callback: ZLinkBackendActorJoinEntrySpotCallback,
    timeoutMs?: number
  ): boolean;
  destroyActor(actor: ZLinkBackendActorRef, timeoutMs: number, signal?: AbortSignal): Promise<void>;
  sendActorBoundSession(
    actor: ZLinkBackendActorRef,
    parts: readonly Message[],
    flags: ZLinkBackendSendFlags
  ): boolean;
  bindRemoteActorSession(
    actor: ZLinkBackendActorRef,
    sourceNodeRid: RoutingId,
    sourceSessionRid: RoutingId
  ): void;
  closeActorBoundSession(actor: ZLinkBackendActorRef, timeoutMs: number, signal?: AbortSignal): Promise<void>;
  dispose(): Promise<void>;
}

export interface ZLinkBackendSpot extends ZLinkBackendObject {
  readonly routingId: RoutingId;
  setRoutingId(routingId: RoutingId): void;
  setSubscription(topic: string): void;
  subscribe(result: TopicMessage, flags: ZLinkBackendRecvFlags): boolean;
  recvActorLifecycle(flags: ZLinkBackendRecvFlags): unknown | null;
  drainReply(): number;
  drainChannelReply(subjectHandle: bigint): number;
  recvRoute(result: Received, flags: ZLinkBackendRecvFlags): boolean;
  setDispatchHandler(handler: (info: ZLinkBackendSpotDispatchInfo) => void): void;
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
  recvActorJoin(flags: ZLinkBackendRecvFlags): ZLinkBackendActorJoinRequest | null;
  replyActorJoin(
    request: ZLinkBackendActorJoinRequest,
    joinResultCode: number
  ): ZLinkBackendActorJoinReplyOperation;
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
  createTopicMessage(): TopicMessage;
  createDiscovery(
    context: ZLinkBackendContext,
    autoConnectType: number,
    channelName: string
  ): ZLinkBackendDiscovery;
  createDealerSocket(context: ZLinkBackendContext): ZLinkBackendDealerSocket;
  createRouterSocket(context: ZLinkBackendContext): ZLinkBackendRouterSocket;
  createPublisherSocket(context: ZLinkBackendContext): ZLinkBackendPublisherSocket;
  createSubscriberSocket(context: ZLinkBackendContext): ZLinkBackendSubscriberSocket;
  createReadablePoller(socket: ZLinkBackendConnectableSocket): ZLinkBackendReadablePoller;
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
