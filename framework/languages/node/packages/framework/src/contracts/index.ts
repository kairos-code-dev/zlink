export type Type<T = unknown> = new (...args: never[]) => T;
export type RoutingId = string;
export type ZlinkStreamHeader = unknown;

export interface Message {
  data(): Buffer;
  toBytes(): Uint8Array;
  copy(): Message;
  size(): number;
  isEmpty(): boolean;
  getString(encoding?: BufferEncoding): string;
  close(): void;
}

export interface ActorRef {
  readonly nodeRid: RoutingId;
  readonly actorId: string;
  readonly generation: bigint;
}

export interface ZLinkHandlerContext {
  readonly channelName?: string;
  readonly packetName?: string;
  readonly contentType?: string;
  readonly connectionAborted?: AbortSignal;
}

export interface ZLinkRequestContext extends ZLinkHandlerContext {}
export interface ZLinkSendContext extends ZLinkHandlerContext {}
export interface ZLinkPublishContext extends ZLinkHandlerContext {
  readonly topic: string;
  readonly source?: string;
}

export interface ZLinkRouteSendContext extends ZLinkHandlerContext {
  readonly sourceNodeRid: RoutingId;
  readonly sourcePeerRid: RoutingId;
}

export interface ZLinkRouteRequestContext extends ZLinkRouteSendContext {
  readonly requestSeq: bigint;
}

export interface ZLinkSpotActorSendContext extends ZLinkHandlerContext {
  readonly metadata: ZLinkMessageMetadata;
}

export interface ZLinkSpotActorRequestContext extends ZLinkSpotActorSendContext {
  readonly reply: ZLinkSpotActorReplyOptions;
}

export interface ZLinkRequestHandler<TRequest, TResponse> {
  handle(request: TRequest, context: ZLinkRequestContext): Promise<TResponse>;
}

export interface ZLinkSendHandler<TMessage> {
  handle(message: TMessage, context: ZLinkSendContext): Promise<void>;
}

export interface ZLinkRouteSendHandler<TMessage> {
  handle(message: TMessage, context: ZLinkRouteSendContext): Promise<void>;
}

export interface ZLinkRouteRequestHandler<TRequest, TReply> {
  handle(request: TRequest, context: ZLinkRouteRequestContext): Promise<TReply>;
}

export interface ZLinkPublishHandler<TMessage> {
  handle(message: TMessage, context: ZLinkPublishContext): Promise<void>;
}

export interface ZLinkSpotPacketHandler<TSpot, TMessage> {
  handle(spot: TSpot, message: TMessage, context: ZLinkHandlerContext): Promise<void>;
}

export interface ZLinkSpotRequestHandler<TSpot, TRequest, TReply> {
  handle(spot: TSpot, request: TRequest, context: ZLinkHandlerContext): Promise<TReply>;
}

export interface ZLinkSpotSubscriptionHandler<TSpot, TEvent> {
  handle(spot: TSpot, event: TEvent, context: ZLinkPublishContext): Promise<void>;
}

export interface ZLinkSpotTimerHandler<TSpot> {
  handle(spot: TSpot, tick: ZLinkTimerTick): Promise<void>;
}

export interface ZLinkSpotActorSendHandler<TSpot, TActor extends ZLinkActor, TMessage> {
  handle(spot: TSpot, actor: TActor, context: ZLinkSpotActorSendContext, message: TMessage): Promise<void>;
}

export interface ZLinkSpotActorRequestHandler<TSpot, TActor extends ZLinkActor, TRequest, TReply> {
  handle(spot: TSpot, actor: TActor, context: ZLinkSpotActorRequestContext, request: TRequest): Promise<TReply>;
}

export interface ZLinkSpotPostActorJoinedHandler<TSpot, TActor extends ZLinkActor> {
  handle(spot: TSpot, actor: TActor, result: ZLinkSpotActorChangeResult): Promise<void>;
}

export interface ZLinkSpotActorLeftHandler<TSpot, TActor extends ZLinkActor> {
  handle(spot: TSpot, actor: TActor, result: ZLinkSpotActorChangeResult): Promise<void>;
}

export interface ZLinkSpotActorDisconnectedHandler<TSpot, TActor extends ZLinkActor> {
  handle(spot: TSpot, actor: TActor): Promise<void>;
}

export interface ZLinkEntrySpotActorSendHandler<TEntrySpot, TActor extends ZLinkActor, TMessage> {
  handle(entrySpot: TEntrySpot, actor: TActor, context: ZLinkSpotActorSendContext, message: TMessage): Promise<void>;
}

export interface ZLinkEntrySpotActorRequestHandler<TEntrySpot, TActor extends ZLinkActor, TRequest, TReply> {
  handle(entrySpot: TEntrySpot, actor: TActor, context: ZLinkSpotActorRequestContext, request: TRequest): Promise<TReply>;
}

export interface ZLinkEntrySpotActorDisconnectedHandler<TEntrySpot, TActor extends ZLinkActor> {
  handle(entrySpot: TEntrySpot, actor: TActor, result: ZLinkSpotActorChangeResult): Promise<void>;
}

export interface ZLinkSpotActorJoinHandler<TSpot, TActor extends ZLinkActor, TRequest, TReply> {
  handle(spot: TSpot, actor: TActor, request: TRequest): Promise<TReply>;
}

export interface ZLinkSpot {
  readonly context?: ZLinkSpotContext;
  configure?(): void;
  onCreate?(createParts: readonly Message[], signal?: AbortSignal): Promise<void>;
  onInitialize?(signal?: AbortSignal): Promise<void>;
  onClosing?(signal?: AbortSignal): Promise<void>;
}

export interface ZLinkEntrySpot extends ZLinkSpot {}

export interface ZLinkActorHandlerRegistry {
  addHandler(handlerType: Type): this;
  addActorPacket(handlerType: Type, actorType: Type<ZLinkActor>, packetName?: string): this;
  addPostActorJoined(handlerType: Type, actorType: Type<ZLinkActor>): this;
  addActorLeft(handlerType: Type, actorType: Type<ZLinkActor>): this;
  addActorDisconnected(handlerType: Type, actorType: Type<ZLinkActor>): this;
}

export interface ZLinkSpotHandlerRegistry extends ZLinkActorHandlerRegistry {
  addPacket(handlerType: Type, packetName?: string): this;
  addSubscribe(handlerType: Type, topic: string): this;
  addActorJoin(handlerType: Type, actorType?: Type<ZLinkActor>): this;
  addSpotHandler(handlerType: Type): this;
}

export interface ZLinkSpotContext {
  readonly spotRid: RoutingId;
  readonly nodeRid: RoutingId;
  readonly routingId: RoutingId;
  readonly handlers: ZLinkSpotHandlerRegistry;
  readonly outbound: ZLinkSpotOutbound;
  leaveActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void>;
  addTimer<THandler extends ZLinkSpotTimerHandler<ZLinkSpot>>(
    name: string,
    periodMs: number,
    handlerType: Type<THandler>,
    options?: ZLinkTimerOptions,
    signal?: AbortSignal
  ): Promise<ZLinkTimer>;
}

export interface ZLinkEntrySpotContext extends ZLinkSpotContext {}

export enum ZLinkSpotActorChangeKind {
  Joined = 'joined',
  Left = 'left',
  Disconnected = 'disconnected'
}

export interface ZLinkSpotActorChangeResult {
  readonly kind: ZLinkSpotActorChangeKind;
  readonly actor: ActorRef;
}

export interface ZLinkSpotActorReplyOptions {
  metadata(key: string, value: string): this;
  compress(enabled?: boolean): this;
}

export interface ZLinkStream {
  readonly sessionId: string;
  readonly routingId?: RoutingId;
  readonly localAddr?: string;
  readonly remoteAddr?: string;
  write(payload: Message, flags?: number): boolean;
  close(signal?: AbortSignal): Promise<void>;
}

export enum ZLinkStreamSessionError {
  TransportError = 'transportError',
  HandshakeFailed = 'handshakeFailed'
}

export interface ZLinkStreamDiagnostic {
  readonly nativeCode?: number;
  readonly message?: string | undefined;
}

export interface ZLinkStreamError {
  readonly error: ZLinkStreamSessionError;
  readonly diagnostic?: ZLinkStreamDiagnostic;
}

export interface ZLinkSession {
  readonly context: ZLinkSessionContext;
  onConnected?(context: ZLinkSessionContext): Promise<void>;
  onDisconnected?(context: ZLinkSessionContext): Promise<void>;
  onError?(context: ZLinkSessionContext, error: ZLinkStreamError): Promise<void>;
  onDispatch?(header: ZlinkStreamHeader, payload: Message, signal?: AbortSignal): Promise<void>;
}

export interface ZLinkSessionContext {
  readonly sessionId: string;
  readonly routingId?: RoutingId;
  readonly localAddr?: string;
  readonly remoteAddr?: string;
  readonly stream: ZLinkStream;
  readonly client: ZLinkSessionClient;
  readonly actors: ZLinkSessionActors;
  close(signal?: AbortSignal): Promise<void>;
}

export interface ZLinkSessionClient {
  send<TMessage>(message: TMessage): ZLinkSessionSendCall;
  reply<TMessage>(message: TMessage): ZLinkSessionReplyCall;
}

export interface ZLinkSessionActors {
  readonly bound: readonly ZLinkSessionActor[];
  bind(actor: ZLinkActor | ActorRef, signal?: AbortSignal): Promise<ZLinkSessionActor>;
  find(actorId: string): ZLinkSessionActor | undefined;
}

export interface ZLinkSessionSendCall {
  metadata(key: string, value: string): this;
  packetName(packetName: string): this;
  compress(enabled?: boolean): this;
  submit(signal?: AbortSignal): Promise<void>;
}

export interface ZLinkSessionReplyCall {
  metadata(key: string, value: string): this;
  compress(enabled?: boolean): this;
  submit(signal?: AbortSignal): Promise<void>;
}

export interface ZLinkSessionActor {
  readonly actorId: string;
  readonly ref: ActorRef;
  relay(header: ZlinkStreamHeader, payload: Message, signal?: AbortSignal): Promise<void>;
  notifyDisconnected(signal?: AbortSignal): Promise<void>;
}

export interface ZLinkSessionPacketHandler<TSessionContext> {
  handle(context: TSessionContext, header: ZlinkStreamHeader, payload: Message): Promise<void>;
}

export interface ZLinkSessionPacketDispatcher<TSessionContext> {
  dispatch(context: TSessionContext, header: ZlinkStreamHeader, payload: Message): Promise<void>;
}

export interface ZLinkActor {
  readonly actorId: string;
  readonly context: ZLinkActorContext;
  configure?(): void;
}

export interface ZLinkActorContext {
  readonly spotRid?: RoutingId;
  readonly isJoined: boolean;
  readonly boundSession: ZLinkBoundSession;
  getSpot(): ZLinkSpot;
  getSpot<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): TSpot;
  joinSpot<TRequest = unknown>(spotRid: RoutingId, request: TRequest): ZLinkActorJoinSpotCall;
  joinEntrySpot(nodeRid: RoutingId): ZLinkActorJoinEntrySpotCall;
}

export interface ZLinkActorJoinResult<TReply> {
  readonly resultCode: number;
  readonly actor: ActorRef;
  readonly reply?: TReply;
}

export interface ZLinkActorJoinSpotCall {
  timeout(timeoutMs: number): this;
  submit<TReply>(signal?: AbortSignal): Promise<ZLinkActorJoinResult<TReply>>;
}

export interface ZLinkActorJoinEntrySpotCall {
  timeout(timeoutMs: number): this;
  submit(signal?: AbortSignal): Promise<ActorRef>;
}

export interface ZLinkActorFactory {
  create(actorId: string, context: ZLinkActorContext, signal?: AbortSignal): Promise<ZLinkActor> | ZLinkActor;
}

export interface ZLinkActorManager {
  create(actorId: string, actorType: string, signal?: AbortSignal): Promise<ZLinkActor>;
  find(actorId: string, signal?: AbortSignal): Promise<ZLinkActor | undefined>;
  getOrCreate(actorId: string, actorType: string, signal?: AbortSignal): Promise<ZLinkActor>;
}

export class ZLinkFrameworkException extends Error {
  constructor(
    public readonly kind: ZLinkFrameworkErrorKind,
    message: string,
    public readonly isRetriable = false,
    cause?: unknown
  ) {
    super(message, { cause });
    this.name = 'ZLinkFrameworkException';
  }
}

export enum ZLinkFrameworkErrorKind {
  ActorRouteNotFound = 'actorRouteNotFound',
  ActorCreateFailed = 'actorCreateFailed',
  ActorAlreadyExists = 'actorAlreadyExists',
  ActorTypeMismatch = 'actorTypeMismatch',
  SpotCreateFailed = 'spotCreateFailed',
  SpotRouteNotFound = 'spotRouteNotFound',
  SpotTypeMismatch = 'spotTypeMismatch',
  ActorSessionNotBound = 'actorSessionNotBound',
  HandlerNotFound = 'handlerNotFound',
  RouteHandlerNotFound = 'routeHandlerNotFound',
  ActorDispatchHandlerNotFound = 'actorDispatchHandlerNotFound',
  PayloadDecodeFailed = 'payloadDecodeFailed',
  RouteNotConnected = 'routeNotConnected',
  RequestTargetNotFound = 'requestTargetNotFound',
  RequestRejected = 'requestRejected',
  RequestProtocolError = 'requestProtocolError',
  RequestFailed = 'requestFailed'
}

export interface ZLinkMessageMetadata {
  readonly channelName?: string;
  readonly packetName?: string;
  readonly sourceNodeRid?: RoutingId;
  readonly sourceSessionRid?: RoutingId;
}

export interface ZLinkMessageMetadataPolicy {
  readonly forward: boolean;
}

export enum ZLinkDispatchMode {
  Inline = 'inline',
  Queued = 'queued',
  Serial = 'serial'
}

export interface ZLinkDispatchOptions {
  mode?: ZLinkDispatchMode;
  unhandled?: ZLinkUnhandledDispatchOptions;
  diagnostics?: ZLinkDiagnosticsOptions;
}

export interface ZLinkUnhandledDispatchOptions {
  action?: ZLinkUnhandledDispatchAction;
}

export interface ZLinkDiagnosticsOptions {
  messageFlowLogMode?: ZLinkMessageFlowLogMode;
}

export enum ZLinkUnhandledDispatchAction {
  Ignore = 'ignore',
  Warn = 'warn',
  Throw = 'throw'
}

export enum ZLinkMessageFlowLogMode {
  Off = 'off',
  Metadata = 'metadata',
  Verbose = 'verbose'
}

export interface ZLinkMessageSerializer {
  serialize<T>(value: T): Message;
  deserialize<T>(message: Message, type: Type<T>): T;
}

export function parseMessage<T>(_message: Message, _type: Type<T>): T {
  throw new Error('No ZLinkMessageSerializer is registered.');
}

export interface ZLinkCodecRegistryBuilder {
  addSerializer(contentType: string, serializer: ZLinkMessageSerializer): this;
  addJson(): this;
  addMessagePack(): this;
  addProtobuf(): this;
}

export interface ZLinkSendCall {
  packetName(packetName: string): this;
  submit(signal?: AbortSignal): Promise<void>;
}

export interface ZLinkRequestCall {
  packetName(packetName: string): this;
  timeout(timeoutMs: number): this;
  submit<TReply>(signal?: AbortSignal): Promise<TReply>;
}

export interface ZLinkPublishCall {
  packetName(packetName: string): this;
  submit(signal?: AbortSignal): Promise<void>;
}

export interface ZLinkChannelClient {
  send<TMessage>(message: TMessage): ZLinkSendCall;
  request<TRequest>(request: TRequest): ZLinkRequestCall;
  sendToChannel<TMessage>(channelName: string, message: TMessage): ZLinkSendCall;
  requestToChannel<TRequest>(channelName: string, request: TRequest): ZLinkRequestCall;
}

export interface ZLinkSpotOutbound {
  sendToSpot<TMessage>(spotRid: RoutingId, message: TMessage): ZLinkSendCall;
  requestToSpot<TRequest>(spotRid: RoutingId, request: TRequest): ZLinkRequestCall;
  publish<TEvent>(topic: string, event: TEvent): ZLinkPublishCall;
  sendToChannel<TMessage>(channelName: string, message: TMessage): ZLinkSendCall;
  requestToChannel<TRequest>(channelName: string, request: TRequest): ZLinkRequestCall;
}

export interface ZLinkRouteClient {
  send<TMessage>(routerChannelId: string, targetNodeRid: RoutingId, message: TMessage): ZLinkSendCall;
  request<TRequest>(routerChannelId: string, targetNodeRid: RoutingId, request: TRequest): ZLinkRequestCall;
}

export interface ZLinkSpotPublisherClient {
  publishSpot<TEvent>(channelName: string, topic: string, event: TEvent): ZLinkPublishCall;
}

export interface ZLinkFanoutClient {
  publish<TEvent>(topic: string, event: TEvent): ZLinkPublishCall;
  publishToChannel<TEvent>(channelName: string, topic: string, event: TEvent): ZLinkPublishCall;
}

export interface ZLinkBoundSession {
  send<TMessage>(message: TMessage): ZLinkBoundSessionSendCall;
  disconnect(signal?: AbortSignal): Promise<void>;
}

export interface ZLinkBoundSessionFactory {
  create(actorId: string): ZLinkBoundSession;
}

export interface ZLinkBoundSessionSendCall {
  metadata(key: string, value: string): this;
  packetName(packetName: string): this;
  submit(signal?: AbortSignal): Promise<void>;
}

export interface ZLinkSpotRemoteAddressResolver {
  resolve(spotRid: RoutingId, signal?: AbortSignal): Promise<ZLinkSpotRemoteAddress>;
}

export enum ZLinkSpotKind {
  Invalid = 0,
  Entry = 1,
  User = 2
}

export interface ZLinkSpotRemoteAddress {
  readonly routerChannelId: string;
  readonly targetNodeRid: RoutingId;
  readonly spotRid: RoutingId;
  readonly spotKind: ZLinkSpotKind;
}

export interface ZLinkFrameworkOptions {
  useDiscovery(): ZLinkDiscoveryBuilder;
  spotFactory<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): this;
  addSpotMesh(channelName: string): ZLinkSpotMeshBuilder;
  clientServerChannel(name: string): ZLinkClientServerChannelBuilder;
  fanoutChannel(name: string): ZLinkFanoutChannelBuilder;
  dealerMeshChannel(name: string): ZLinkDealerMeshChannelBuilder;
  routeChannel(name: string): ZLinkRouteChannelBuilder;
  routeMeshChannel(name: string): ZLinkRouteMeshChannelBuilder;
  streamNode(name: string): ZLinkStreamNodeBuilder;
  spotNode(name: string): ZLinkSpotNodeBuilder;
}

export interface ZLinkRegistrySpotRemoteAddressesOptions {
  registryEndpoint: string;
}

export interface ZLinkDiscoveryBuilder {
  connectRegistry(endpoint: string): this;
}

export interface ZLinkMetadataPolicyBuilder {
  forward(enabled?: boolean): this;
}

export interface ChannelServerCapabilityBuilder {
  bind(endpoint: string): this;
}

export interface ChannelClientCapabilityBuilder {
  connect(endpoint: string): this;
}

export interface DealerMeshChannelClientCapabilityBuilder extends ChannelClientCapabilityBuilder {}

export interface ChannelPublisherCapabilityBuilder {
  bind(endpoint: string): this;
}

export interface ChannelSubscriberCapabilityBuilder {
  connect(endpoint: string): this;
}

export interface ZLinkClientServerChannelBuilder {
  server(): ChannelServerCapabilityBuilder;
  client(): ChannelClientCapabilityBuilder;
}

export interface ZLinkFanoutChannelBuilder {
  publisher(): ChannelPublisherCapabilityBuilder;
  subscriber(): ChannelSubscriberCapabilityBuilder;
}

export interface ZLinkDealerMeshChannelBuilder {
  client(): DealerMeshChannelClientCapabilityBuilder;
}

export interface ZLinkRouteChannelBuilder {
  router(): ChannelServerCapabilityBuilder;
  dealer(): ChannelClientCapabilityBuilder;
}

export interface ZLinkRouteMeshChannelBuilder extends ZLinkRouteChannelBuilder {}

export interface ZLinkStreamNodeBuilder {
  bind(endpoint: string): this;
  attachActorGateway(spotNodeName: string): this;
  registerSession<TSession extends ZLinkSession>(sessionType: Type<TSession>): this;
}

export interface ZLinkEndpointConnections {
  connect(endpoint: string): this;
  bind(endpoint: string): this;
}

export interface ZLinkSpotCreateResult {
  readonly spotRid: RoutingId;
  readonly created: boolean;
}

export interface ZLinkSpotInfo {
  readonly spotRid: RoutingId;
}

export interface ZLinkSpotManager {
  create<TSpot extends ZLinkSpot>(
    spotType: Type<TSpot>,
    createParts?: readonly Message[],
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult>;
  getOrCreate<TSpot extends ZLinkSpot>(
    spotType: Type<TSpot>,
    spotRid: RoutingId,
    createParts?: readonly Message[],
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult>;
  find(spotRid: RoutingId, signal?: AbortSignal): Promise<ZLinkSpotInfo | null>;
  list(signal?: AbortSignal): Promise<readonly ZLinkSpotInfo[]>;
  remove(spotRid: RoutingId, signal?: AbortSignal): Promise<boolean>;
}

export interface ZLinkSpotNodeBuilder {
  router(): SpotRouterCapabilityBuilder;
  pubSub(): SpotPubSubCapabilityBuilder;
  configureEntrySpot(options: ZLinkEntrySpotOptions): this;
  addEntrySpot<TEntrySpot extends ZLinkEntrySpot>(entrySpotType: Type<TEntrySpot>): this;
  addSpotFactory<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): this;
  attachChannelClient(channelName: string): SpotChannelClientCapabilityBuilder;
  attachSpotPublisherClient(channelName: string): SpotPublisherClientCapabilityBuilder;
  acceptSpotRoutesFromChannel(channelName: string): ZLinkSpotRouteChannelAcceptanceBuilder;
}

export interface ZLinkSpotMeshNodeBuilder extends ZLinkSpotNodeBuilder {}

export interface ZLinkSpotMeshBuilder {
  useDiscovery(): ZLinkDiscoveryBuilder;
  node(name: string): ZLinkSpotMeshNodeBuilder;
}

export interface SpotRouterCapabilityBuilder {
  bind(endpoint: string): this;
  routingId(routingId: RoutingId): this;
  connect(endpoint: string): this;
}

export interface SpotPubSubCapabilityBuilder {
  bind(endpoint: string): this;
  routingId(routingId: RoutingId): this;
  connect(endpoint: string): this;
}

export interface SpotPublisherClientCapabilityBuilder {
  connect(endpoint: string): this;
}

export interface SpotChannelClientCapabilityBuilder {
  connect(endpoint: string): this;
}

export interface ZLinkSpotRouteChannelAcceptanceBuilder {
  connect(endpoint: string): this;
}

export interface ZLinkSocketConfig {
  bind?: string;
  connect?: string;
  channelName?: string;
}

export interface ZLinkRouteConfig {
  channelName: string;
  endpoint: string;
}

export interface ZLinkOutboundRouteConfig {
  targetNodeRid: RoutingId;
  endpoint: string;
}

export interface ZLinkSpotPublisherConfig {
  topic: string;
}

export interface ZLinkSpotSubscriberConfig {
  topic: string;
}

export interface ZLinkEntrySpotOptions {
  routingId?: RoutingId;
}

export interface ZLinkTimer {
  readonly isDisposed: boolean;
  cancel(signal?: AbortSignal): Promise<void>;
  dispose(): Promise<void>;
}

export interface ZLinkTimerOptions {
  overrunPolicy?: ZLinkTimerOverrunPolicy;
  maxCatchUpTicks?: number;
  stopOnUnhandledException?: boolean;
}

export enum ZLinkTimerOverrunPolicy {
  SkipLateTicks = 'skipLateTicks',
  CatchUpBounded = 'catchUpBounded',
  DelayNextTick = 'delayNextTick'
}

export interface ZLinkTimerTick {
  readonly name: string;
  readonly deliveryIndex: bigint;
  readonly scheduledIndex: bigint;
  readonly periodMs: number;
  readonly scheduledAt: Date;
  readonly startedAt: Date;
  readonly scheduledElapsedMs: number;
  readonly startedElapsedMs: number;
  readonly delayMs: number;
  readonly skippedTicks: bigint;
}

export type ZLinkHandlerDelegate = () => Promise<unknown>;

export interface ZLinkHandlerInvocation {
  readonly context: ZLinkHandlerContext;
  readonly handler: unknown;
}

export interface ZLinkHandlerFilter {
  invoke(invocation: ZLinkHandlerInvocation, next: ZLinkHandlerDelegate): Promise<unknown>;
}

export interface ZLinkRegistryQuery {
  statusAsync(signal?: AbortSignal): Promise<ZLinkRegistryStatus>;
  serviceSummaryAsync(
    filter?: ZLinkRegistryServiceSummaryFilter,
    signal?: AbortSignal
  ): Promise<readonly ZLinkRegistryServiceSummaryEntry[]>;
  topologyAsync(
    filter?: ZLinkRegistryTopologyFilter,
    signal?: AbortSignal
  ): Promise<readonly ZLinkRegistryTopologyEntry[]>;
  memberPeersAsync(channelName: string, signal?: AbortSignal): Promise<readonly ZLinkMemberPeerEntry[]>;
}

export interface ZLinkRegistryQueryClient {
  topologyAsync(
    filter?: ZLinkRegistryTopologyFilter,
    signal?: AbortSignal
  ): Promise<readonly ZLinkRegistryTopologyEntry[]>;
}

export interface ZLinkRegistryQueryClientOptions {
  endpoint: string;
}

export interface ZLinkRegistryOptions {
  pubEndpoint: string;
  routerEndpoint: string;
  registryId?: number;
  heartbeatIntervalMs?: number;
  heartbeatTimeoutMs?: number;
  broadcastIntervalMs?: number;
  peers?: readonly string[];
}

export interface ZLinkMonitoringOptions {
  socket?: ZLinkSocketMonitoringRegistration[];
  registry?: ZLinkPollingMonitoringRegistration[];
  spot?: ZLinkPollingMonitoringRegistration[];
}

export interface ZLinkSocketMonitoringRegistration {
  readonly sourceName: string;
  readonly events?: readonly ZLinkSocketEventKind[];
}

export interface ZLinkPollingMonitoringRegistration {
  readonly sourceName: string;
  readonly intervalMs: number;
}

export interface ZLinkRuntimeEvent {
  readonly sourceName: string;
  readonly timestamp: Date;
}

export interface ZLinkRuntimeEventHandler<TEvent extends ZLinkRuntimeEvent> {
  handle(event: TEvent): Promise<void>;
}

export interface ZLinkRuntimeEventPublisher {
  publish<TEvent extends ZLinkRuntimeEvent>(event: TEvent): Promise<void>;
}

export enum ZLinkSocketEventKind {
  Connected = 0,
  ConnectionReady = 1,
  Disconnected = 2,
  HandshakeFailed = 3,
  PeerAdmissionChanged = 4,
  Closed = 5,
  Internal = 6
}

export enum ZLinkSocketNativeEventType {
  Connected = 0x0001,
  ConnectDelayed = 0x0002,
  ConnectRetried = 0x0004,
  Listening = 0x0008,
  BindFailed = 0x0010,
  Accepted = 0x0020,
  AcceptFailed = 0x0040,
  Closed = 0x0080,
  CloseFailed = 0x0100,
  Disconnected = 0x0200,
  MonitorStopped = 0x0400,
  HandshakeFailedNoDetail = 0x0800,
  ConnectionReady = 0x1000,
  HandshakeFailedProtocol = 0x2000,
  HandshakeFailedAuth = 0x4000,
  PeerAdmissionChanged = 0x8000
}

export interface ZLinkSocketDiagnostic {
  readonly nativeEvent: ZLinkSocketNativeEventType;
  readonly nativeValue: number;
}

export interface ZLinkSocketEvent extends ZLinkRuntimeEvent {
  readonly event: ZLinkSocketEventKind;
  readonly routingId?: RoutingId;
  readonly localAddr: string;
  readonly remoteAddr: string;
  readonly diagnostic?: ZLinkSocketDiagnostic;
}

export enum ZLinkRegistryEventKind {
  StatusChanged = 0,
  TopologyChanged = 1,
  ServiceSummaryChanged = 2
}

export interface ZLinkRegistryEvent extends ZLinkRuntimeEvent {
  readonly event: ZLinkRegistryEventKind;
  readonly status?: ZLinkRegistryStatus;
  readonly topology?: readonly ZLinkRegistryTopologyEntry[];
  readonly serviceSummary?: readonly ZLinkRegistryServiceSummaryEntry[];
}

export enum ZLinkSpotEventKind {
  StatusChanged = 0,
  PeersChanged = 1,
  SubjectsChanged = 2,
  TimerHandlerFailed = 3,
  TimerStoppedAfterUnhandledException = 4
}

export interface ZLinkSpotTimerDiagnostic {
  readonly spotRid: RoutingId;
  readonly isEntrySpot: boolean;
  readonly timerName: string;
  readonly handlerType: string;
  readonly deliveryIndex: bigint;
  readonly scheduledIndex: bigint;
  readonly exceptionType: string;
  readonly exceptionMessage: string;
}

export interface ZLinkSpotEvent extends ZLinkRuntimeEvent {
  readonly event: ZLinkSpotEventKind;
  readonly status?: ZLinkSpotNodeStatus;
  readonly peers?: readonly ZLinkSpotNodePeerEntry[];
  readonly subjects?: readonly ZLinkSpotNodeSubjectEntry[];
  readonly timerDiagnostic?: ZLinkSpotTimerDiagnostic;
}

export enum ZLinkAutoConnectType {
  Invalid = 0,
  RouteMesh = 1,
  ClientServer = 2,
  DealerMesh = 3,
  Fanout = 4,
  SpotMesh = 5
}

export enum ZLinkServiceKind { Discovery = 1, SpotSub = 3, SpotPub = 4, Socket = 5 }
export enum ZLinkServiceRole { Invalid = 0, Spot = 2, Router = 3, Dealer = 4, Pub = 5, Sub = 6 }
export enum ZLinkRegistryState { Idle = 1, Active = 2, Degraded = 3, Error = 4 }
export enum ZLinkTopologySource { Manual = 1, Discovery = 2, Registry = 3 }
export enum ZLinkTopologyState { Discovered = 1, Connecting = 2, Ready = 3, Lost = 4, Error = 5, Stopped = 6 }
export enum ZLinkAdmissionState { Serving = 1, Draining = 2 }

export interface ZLinkRegistryServiceSummaryFilter {
  readonly autoConnectType?: ZLinkAutoConnectType;
  readonly serviceRole?: ZLinkServiceRole;
  readonly channelName?: string;
}

export interface ZLinkRegistryTopologyFilter {
  readonly autoConnectType?: ZLinkAutoConnectType;
  readonly serviceKind?: ZLinkServiceKind;
  readonly serviceRole?: ZLinkServiceRole;
  readonly channelName?: string;
  readonly routingId?: RoutingId;
  readonly state?: ZLinkTopologyState;
  readonly source?: ZLinkTopologySource;
}

export interface ZLinkRegistryStatus {
  readonly registryId: number;
  readonly bindEndpoint: string;
  readonly state: ZLinkRegistryState;
  readonly topologyEntryCount: number;
  readonly peerRegistryCount: number;
  readonly connectedPeerRegistryCount: number;
  readonly listSeq: bigint;
  readonly lastError: number;
  readonly lastChangedMs: bigint;
}

export interface ZLinkRegistryServiceSummaryEntry {
  readonly autoConnectType: ZLinkAutoConnectType;
  readonly serviceRole: ZLinkServiceRole;
  readonly channelName: string;
  readonly totalCount: number;
  readonly connectingCount: number;
  readonly readyCount: number;
  readonly errorCount: number;
  readonly stoppedCount: number;
  readonly lastReportedMs: bigint;
}

export interface ZLinkRegistryTopologyEntry {
  readonly autoConnectType: ZLinkAutoConnectType;
  readonly routingId?: RoutingId;
  readonly serviceKind: ZLinkServiceKind;
  readonly serviceRole: ZLinkServiceRole;
  readonly channelName: string;
  readonly endpoint: string;
  readonly source: ZLinkTopologySource;
  readonly state: ZLinkTopologyState;
  readonly desiredCount: number;
  readonly readyCount: number;
  readonly errorCode: number;
  readonly lastReportedMs: bigint;
  readonly spotKind: ZLinkSpotKind;
}

export interface ZLinkMemberPeerEntry {
  readonly autoConnectType: ZLinkAutoConnectType;
  readonly serviceRole: ZLinkServiceRole;
  readonly channelName: string;
  readonly endpoint: string;
  readonly routingId?: RoutingId;
  readonly value: bigint;
  readonly weight: number;
}

export enum ZLinkSpotNodeState { Idle = 1, Connecting = 2, PartialReady = 3, Ready = 4, Error = 5 }
export enum ZLinkSpotPeerSource { Manual = 1, Discovery = 2, Mixed = 3 }
export enum ZLinkSpotPeerKind { SpotMesh = 1, RouterChannel = 2 }
export enum ZLinkSpotPeerState { Configured = 1, Connecting = 2, Connected = 3 }
export enum ZLinkSubjectKind { None = 0, Topic = 1, Pattern = 2 }
export enum ZLinkSpotRole { Pub = 1, Sub = 2 }

export interface ZLinkSpotNodeStatus {
  readonly channelName: string;
  readonly localEndpoint: string;
  readonly nodeRoutingId?: RoutingId;
  readonly state: ZLinkSpotNodeState;
  readonly configuredPeerCount: number;
  readonly activePeerCount: number;
  readonly connectedPeerCount: number;
  readonly subjectCount: number;
  readonly readySubjectCount: number;
  readonly lastError: number;
  readonly lastChangedMs: bigint;
}

export interface ZLinkSpotNodePeerEntry {
  readonly channelName: string;
  readonly localEndpoint: string;
  readonly peerEndpoint: string;
  readonly source: ZLinkSpotPeerSource;
  readonly kind: ZLinkSpotPeerKind;
  readonly state: ZLinkSpotPeerState;
  readonly weight: number;
  readonly connectedSinceMs: bigint;
  readonly lastChangedMs: bigint;
}

export interface ZLinkSpotNodeSubjectEntry {
  readonly role: ZLinkSpotRole;
  readonly subject: string;
  readonly subjectKind: ZLinkSubjectKind;
  readonly readyPeerCount: number;
  readonly activePeerCount: number;
  readonly lastChangedMs: bigint;
}

export const ZLINK_DECORATOR_METADATA = Symbol.for('@zlink-systems/framework:decorator');

export interface ZLinkDecoratorMetadata {
  readonly kind: string;
  readonly packetName?: string;
  readonly groupName?: string;
  readonly spotNodeName?: string;
  readonly topic?: string;
}

export function ZLinkHandlerGroup(groupName: string): ClassDecorator {
  return classDecorator({ kind: 'handlerGroup', groupName });
}

export function ZLinkRequest(packetName?: string): MethodDecorator {
  return methodDecorator({ kind: 'request', packetName });
}

export function ZLinkSend(packetName?: string): MethodDecorator {
  return methodDecorator({ kind: 'send', packetName });
}

export function ZLinkPublish(packetName?: string): MethodDecorator {
  return methodDecorator({ kind: 'publish', packetName });
}

export function ZLinkPacket(packetName: string): ClassDecorator {
  return classDecorator({ kind: 'packet', packetName });
}

export function ZLinkSpotRequest(packetName?: string): MethodDecorator {
  return methodDecorator({ kind: 'spotRequest', packetName });
}

export function ZLinkSpotSubscription(spotNodeName: string, topic: string): MethodDecorator {
  return methodDecorator({ kind: 'spotSubscription', spotNodeName, topic });
}

export function ZLinkSpotActorSend(packetName?: string): MethodDecorator {
  return methodDecorator({ kind: 'spotActorSend', packetName });
}

export function ZLinkSpotActorRequest(packetName?: string): MethodDecorator {
  return methodDecorator({ kind: 'spotActorRequest', packetName });
}

export function ZLinkSpotActorJoin(): MethodDecorator {
  return methodDecorator({ kind: 'spotActorJoin' });
}

export function ZLinkSpotPostActorJoined(): MethodDecorator {
  return methodDecorator({ kind: 'spotPostActorJoined' });
}

export function ZLinkSpotActorLeft(): MethodDecorator {
  return methodDecorator({ kind: 'spotActorLeft' });
}

export function ZLinkSpotActorDisconnected(): MethodDecorator {
  return methodDecorator({ kind: 'spotActorDisconnected' });
}

export function ZLinkStreamPacket(): MethodDecorator {
  return methodDecorator({ kind: 'streamPacket' });
}

export function ZLinkStreamRaw(): MethodDecorator {
  return methodDecorator({ kind: 'streamRaw' });
}

function classDecorator(metadata: ZLinkDecoratorMetadata): ClassDecorator {
  return (target) => appendMetadata(target, metadata);
}

function methodDecorator(metadata: ZLinkDecoratorMetadata): MethodDecorator {
  return (target) => appendMetadata(target.constructor, metadata);
}

function appendMetadata(target: object, metadata: ZLinkDecoratorMetadata): void {
  const current = readZLinkDecoratorMetadata(target);
  Object.defineProperty(target, ZLINK_DECORATOR_METADATA, {
    configurable: true,
    enumerable: false,
    value: [...current, metadata],
    writable: false
  });
}

export function readZLinkDecoratorMetadata(target: object): readonly ZLinkDecoratorMetadata[] {
  return ((target as Record<symbol, unknown>)[ZLINK_DECORATOR_METADATA] as readonly ZLinkDecoratorMetadata[] | undefined) ?? [];
}
