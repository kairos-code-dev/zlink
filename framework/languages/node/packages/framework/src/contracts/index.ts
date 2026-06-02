import type { ActorRef as BindingActorRef, Message as BindingMessage } from '@zlink-systems/zlink';

export type Type<T = unknown> = new (...args: never[]) => T;
export type RoutingId = string;
export type Message = BindingMessage;
export type ZlinkStreamHeader = unknown;
export type ActorRef = BindingActorRef;

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
  metadata(policy: ZLinkMessageMetadataPolicy): this;
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
  Internal = 'internal',
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
  send<TMessage>(targetNodeRid: RoutingId, message: TMessage): ZLinkSendCall;
  request<TRequest>(targetNodeRid: RoutingId, request: TRequest): ZLinkRequestCall;
}

export interface ZLinkSpotPublisherClient {
  publish<TEvent>(topic: string, event: TEvent): ZLinkPublishCall;
}

export interface ZLinkFanoutClient {
  publish<TEvent>(topic: string, event: TEvent): ZLinkPublishCall;
  publishToChannel<TEvent>(channelName: string, topic: string, event: TEvent): ZLinkPublishCall;
}

export interface ZLinkBoundSession {
  send<TMessage>(message: TMessage): ZLinkBoundSessionSendCall;
  disconnect(signal?: AbortSignal): Promise<void>;
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
  Entry = 'entry',
  User = 'user'
}

export interface ZLinkSpotRemoteAddress {
  readonly routerChannelId: string;
  readonly targetNodeRid: RoutingId;
  readonly spotRid: RoutingId;
  readonly spotKind: ZLinkSpotKind;
}

export interface ZLinkFrameworkOptions {
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
}

export interface ZLinkSpotMeshNodeBuilder extends ZLinkSpotNodeBuilder {}

export interface ZLinkSpotMeshBuilder {
  node(name: string): ZLinkSpotMeshNodeBuilder;
}

export interface SpotRouterCapabilityBuilder {
  bind(endpoint: string): this;
}

export interface SpotPubSubCapabilityBuilder {
  bind(endpoint: string): this;
}

export interface SpotPublisherClientCapabilityBuilder {
  connect(endpoint: string): this;
}

export interface SpotChannelClientCapabilityBuilder {
  connect(endpoint: string): this;
}

export interface ZLinkSpotRouteChannelAcceptanceBuilder {
  accept(channelName: string): this;
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
  topology(filter?: ZLinkRegistryTopologyFilter): readonly ZLinkRegistryTopologyEntry[];
  serviceSummary(filter?: ZLinkRegistryServiceSummaryFilter): readonly ZLinkRegistryServiceSummaryEntry[];
}

export interface ZLinkRegistryQueryClient {
  topology(filter?: ZLinkRegistryTopologyFilter): Promise<readonly ZLinkRegistryTopologyEntry[]>;
}

export interface ZLinkRegistryQueryClientOptions {
  endpoint: string;
}

export interface ZLinkRegistryOptions {
  pubEndpoint?: string;
  routerEndpoint?: string;
}

export interface ZLinkMonitoringOptions {
  enabled?: boolean;
}

export interface ZLinkRuntimeEvent {
  readonly timestampMs: number;
}

export interface ZLinkRuntimeEventHandler<TEvent extends ZLinkRuntimeEvent> {
  handle(event: TEvent): Promise<void>;
}

export interface ZLinkRuntimeEventPublisher {
  publish<TEvent extends ZLinkRuntimeEvent>(event: TEvent): Promise<void>;
}

export enum ZLinkSocketEventKind {
  Connected = 'connected',
  Disconnected = 'disconnected',
  Error = 'error'
}

export enum ZLinkSocketNativeEventType {
  Connected = 'connected',
  ConnectDelayed = 'connectDelayed',
  ConnectRetried = 'connectRetried',
  Listening = 'listening',
  BindFailed = 'bindFailed',
  Accepted = 'accepted',
  AcceptFailed = 'acceptFailed',
  Closed = 'closed',
  CloseFailed = 'closeFailed',
  Disconnected = 'disconnected',
  MonitorStopped = 'monitorStopped'
}

export interface ZLinkSocketDiagnostic {
  readonly nativeEvent?: ZLinkSocketNativeEventType;
  readonly value?: number;
}

export interface ZLinkSocketEvent extends ZLinkRuntimeEvent {
  readonly kind: ZLinkSocketEventKind;
  readonly diagnostic?: ZLinkSocketDiagnostic;
}

export enum ZLinkRegistryEventKind {
  Started = 'started',
  Stopped = 'stopped',
  TopologyChanged = 'topologyChanged'
}

export interface ZLinkRegistryEvent extends ZLinkRuntimeEvent {
  readonly kind: ZLinkRegistryEventKind;
}

export enum ZLinkSpotEventKind {
  Started = 'started',
  Stopped = 'stopped',
  TimerOverrun = 'timerOverrun'
}

export interface ZLinkSpotTimerDiagnostic {
  readonly overrunMs: number;
}

export interface ZLinkSpotEvent extends ZLinkRuntimeEvent {
  readonly kind: ZLinkSpotEventKind;
  readonly timer?: ZLinkSpotTimerDiagnostic;
}

export enum ZLinkAutoConnectType {
  RouteMesh = 'routeMesh',
  ClientServer = 'clientServer',
  DealerMesh = 'dealerMesh',
  Fanout = 'fanout',
  SpotMesh = 'spotMesh'
}

export enum ZLinkServiceKind { Discovery = 'discovery', SpotSub = 'spotSub', SpotPub = 'spotPub', Socket = 'socket' }
export enum ZLinkServiceRole { Invalid = 'invalid', Spot = 'spot', Router = 'router', Dealer = 'dealer', Pub = 'pub', Sub = 'sub' }
export enum ZLinkRegistryState { Idle = 'idle', Active = 'active', Degraded = 'degraded', Error = 'error' }
export enum ZLinkTopologySource { Manual = 'manual', Discovery = 'discovery', Registry = 'registry' }
export enum ZLinkTopologyState { Configured = 'configured', Connected = 'connected', Missing = 'missing' }
export enum ZLinkAdmissionState { Serving = 'serving', Draining = 'draining' }

export interface ZLinkRegistryServiceSummaryFilter {
  readonly serviceName?: string;
}

export interface ZLinkRegistryTopologyFilter {
  readonly channelName?: string;
}

export interface ZLinkRegistryStatus {
  readonly state: ZLinkRegistryState;
}

export interface ZLinkRegistryServiceSummaryEntry {
  readonly serviceName: string;
}

export interface ZLinkRegistryTopologyEntry {
  readonly channelName: string;
}

export interface ZLinkMemberPeerEntry {
  readonly channelName: string;
  readonly endpoint: string;
}

export enum ZLinkSpotNodeState { Idle = 'idle', Connecting = 'connecting', PartialReady = 'partialReady', Ready = 'ready', Error = 'error' }
export enum ZLinkSpotPeerSource { Manual = 'manual', Discovery = 'discovery', Mixed = 'mixed' }
export enum ZLinkSpotPeerKind { SpotMesh = 'spotMesh', RouterChannel = 'routerChannel' }
export enum ZLinkSpotPeerState { Configured = 'configured', Connecting = 'connecting', Connected = 'connected' }
export enum ZLinkSubjectKind { None = 'none', Topic = 'topic', Pattern = 'pattern' }
export enum ZLinkSpotRole { Pub = 'pub', Sub = 'sub' }

export interface ZLinkSpotNodeStatus {
  readonly state: ZLinkSpotNodeState;
}

export interface ZLinkSpotNodePeerEntry {
  readonly endpoint: string;
}

export interface ZLinkSpotNodeSubjectEntry {
  readonly subject: string;
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
