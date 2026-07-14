<!-- framework-adapter-nav:start -->
[문서 목록](../../../../node/README.ko.md) | [이전: 시스템 구조](01-system-structure.ko.md) | [다음: Stream Connector](../typescript/03-stream-connector.ko.md)
<!-- framework-adapter-nav:end -->

# ZLink Framework Node.js 공개 interface

## 1. 목적과 계약 소유권

이 문서는 `@zlink-systems/framework`와 `@zlink-systems/nestjs` package root가 내보내는 공개
TypeScript declaration 전체를 고정한다. 현재 분모는 framework 268개와 NestJS 66개, 합계 334개 export다.

기능의 의미와 동작 규칙은 [공통 스펙](../../README.ko.md)이 소유하고, 사용법과 예제는
[Node.js guide](../../../../node/guide/01-overview.ko.md)가 소유한다. Stream Connector의 공개 계약은
[별도 문서](../typescript/03-stream-connector.ko.md)가 소유한다. 현재 구현과 목표 계약의 차이는
[구현 차이 문서](../../90-implementation-gap.ko.md)에만 기록한다.

아래 declaration은 배포 package와 이름 집합이 양방향으로 같아야 하며, 각 이름의 overload, generic,
상속, member, parameter와 반환형도 같아야 한다. 공통 동작 설명, 사용 예제와 실제 테스트 이름은 이
문서에 두지 않는다.

## 2. 공개 declaration catalog

### 2.1 @zlink-systems/framework: ActorRef - Type

```ts
export interface ActorRef {
    readonly nodeRid: RoutingId;
    readonly actorId: string;
    readonly generation: bigint;
}

export declare function isZLinkFrameworkErrorRetriableByDefault(kind: ZLinkFrameworkErrorKind): boolean;

export declare function isZLinkMessage(value: unknown): value is ZLinkMessage;

export declare const MESSAGE_FLOW_MODE_RANK: Record<ZLinkMessageFlowLogMode, number>;

export declare function parseMessage<T>(_payload: ZLinkEncodedPayload, _type: Type<T>): T;

export declare function readZLinkDecoratorMetadata(target: object): readonly ZLinkDecoratorMetadata[];

export type RoutingId = string;

export declare function selectDefaultSerializer(registry?: ZLinkSerializerRegistryLike | ReadonlyMap<string, ZLinkMessageSerializer>): ZLinkMessageSerializer | undefined;

export declare function selectSerializer(value: unknown, registry?: ZLinkSerializerRegistryLike | ReadonlyMap<string, ZLinkMessageSerializer>, context?: ZLinkSerializerSelectionContext): ZLinkMessageSerializer | undefined;

export interface SpotHandle {
    readonly spotRid: RoutingId;
    readonly [spotHandleBrand]: never;
}

export type Type<T = unknown> = new (...args: never[]) => T;
```

### 2.2 @zlink-systems/framework: ZLINK_DECORATOR_METADATA - ZLinkActorJoinSpotCall

```ts
export declare const ZLINK_DECORATOR_METADATA: unique symbol;

export declare const ZLINK_FRAMEWORK_ERROR_KIND_VALUES: Readonly<Record<ZLinkFrameworkErrorKind, number>>;

export interface ZLinkActor {
    readonly actorId: string;
    readonly context: ZLinkActorContext;
    configure?(): void;
}

export interface ZLinkActorClient {
    sendToActor(actor: ActorRef, message: unknown): ZLinkActorSendCall;
    requestToActor(actor: ActorRef, request: unknown): ZLinkActorRequestCall;
}

export interface ZLinkActorContext {
    readonly spotRid?: RoutingId;
    readonly boundSession: ZLinkBoundSession;
    joinSpot(spotRid: RoutingId, request: unknown): ZLinkActorJoinSpotCall;
    joinEntrySpot(nodeRid: RoutingId, request: unknown): ZLinkActorJoinEntrySpotCall;
}

export interface ZLinkActorDirectory {
    find(actorId: string, signal?: AbortSignal): Promise<ActorRef | undefined>;
    ensure(actorId: string, createRequest: unknown, placement?: ZLinkActorPlacement, signal?: AbortSignal): Promise<ActorRef>;
}

export interface ZLinkActorFactory {
    create(actorId: string, context: ZLinkActorContext): Promise<ZLinkActor>;
}

export interface ZLinkActorHandlerRegistry {
    addHandler<THandler>(handlerType: Type<THandler>): this;
    addActorPacket<THandler, TActor extends ZLinkActor>(handlerType: Type<THandler>, actorType: Type<TActor>): this;
}

export interface ZLinkActorJoinCall<TSelf> {
    timeout(timeoutMs: number): TSelf;
    submit<TReply = unknown>(signal?: AbortSignal): Promise<ZLinkActorJoinResult<TReply>>;
}

export interface ZLinkActorJoinEntrySpotCall extends ZLinkActorJoinCall<ZLinkActorJoinEntrySpotCall> {
}

export type ZLinkActorJoinResult<TReply = unknown> = {
    readonly status: 'accepted';
    readonly actor: ActorRef;
    readonly reply: TReply;
} | {
    readonly status: 'rejected';
    readonly reply: TReply;
};

export interface ZLinkActorJoinSpotCall extends ZLinkActorJoinCall<ZLinkActorJoinSpotCall> {
}
```

### 2.3 @zlink-systems/framework: ZLinkActorLocation - ZLinkActorSpotHandleResolver

```ts
export interface ZLinkActorLocation {
    readonly actorId: string;
    readonly actorType?: string;
    readonly actorRef?: ActorRef;
    readonly nodeRid: RoutingId;
    readonly locationKind: ZLinkSpotKind;
    readonly spotMeshName: string;
    readonly spotRid?: RoutingId;
    readonly ownerId: string;
    readonly generation: bigint;
    readonly updatedAt: Date;
}

export interface ZLinkActorLocationFilter {
    readonly actorType?: string;
    readonly nodeRid?: RoutingId;
    readonly spotRid?: RoutingId;
    readonly locationKind?: ZLinkSpotKind;
}

export interface ZLinkActorLocationKey {

    readonly actorId: string;
}

export interface ZLinkActorLocationStore {
    updateActor(actor: ZLinkActorLocation, intent: ZLinkLocationWriteIntent, signal?: AbortSignal): Promise<ZLinkLocationWriteResult>;
    removeActor(key: ZLinkActorLocationKey, owner: ZLinkLocationOwnerToken, signal?: AbortSignal): Promise<ZLinkLocationWriteResult>;
    resolveActor(key: ZLinkActorLocationKey, signal?: AbortSignal): Promise<ZLinkActorLocation | undefined>;
    listActors(filter: ZLinkActorLocationFilter, page?: ZLinkPageRequest, signal?: AbortSignal): Promise<ZLinkLocationPage<ZLinkActorLocation>>;
}

export interface ZLinkActorManager {
    create(actorId: string, actorType: string, signal?: AbortSignal): Promise<ActorRef>;
    create(actorId: string, actorType: string, createRequest: unknown, signal?: AbortSignal): Promise<ActorRef>;
    find(actorId: string, signal?: AbortSignal): Promise<ActorRef | undefined>;
    getOrCreate(actorId: string, actorType: string, signal?: AbortSignal): Promise<ActorRef>;
    getOrCreate(actorId: string, actorType: string, createRequest: unknown, signal?: AbortSignal): Promise<ActorRef>;
}

export interface ZLinkActorPlacement {
    readonly preferredNodeRid?: RoutingId;
    readonly routeMesh?: string;
}

export interface ZLinkActorRefSnapshot {
    readonly nodeRid: RoutingId;
    readonly actorId: string;
    readonly generation: bigint;
}

export declare function zlinkActorRefSnapshotFrom(actorRef: ActorRef): ZLinkActorRefSnapshot;

export declare function zlinkActorRefSnapshotToActorRef(snapshot: ZLinkActorRefSnapshot): ActorRef;

export interface ZLinkActorRequestCall {
    metadata(key: string, value: string): this;
    timeout(timeoutMs: number): this;
    submit<TReply>(signal?: AbortSignal): Promise<TReply>;
}

export interface ZLinkActorSendCall {
    metadata(key: string, value: string): this;
    submit(): void;
}

export interface ZLinkActorSpotHandleResolver {
    resolveActorSpotHandle(actorId: string, signal?: AbortSignal): Promise<SpotHandle | undefined>;
}
```

### 2.4 @zlink-systems/framework: ZLinkActorTransferAdapter - ZLinkCodecRegistryBuilder

```ts
export interface ZLinkActorTransferAdapter<TActor extends ZLinkActor> {
    transferOut(actor: TActor): Promise<ZLinkMessage>;
    transferIn(actorId: string, state: ZLinkMessage): Promise<TActor>;
}

export interface ZLinkAutoConnectDesiredSetChange {
    readonly autoConnectType: ZLinkLocationAutoConnectType;
    readonly meshName: string;
    readonly connectedEndpoints: readonly string[];
    readonly disconnectedEndpoints: readonly string[];
}

export interface ZLinkBoundSession {
    send(message: unknown): ZLinkBoundSessionSendCall;
    disconnect(signal?: AbortSignal): Promise<void>;
}

export interface ZLinkBoundSessionSendCall {
    metadata(key: string, value: string): this;
    submit(): void;
}

export interface ZLinkChannelClient {
    sendToChannel(channelName: string, message: unknown): ZLinkSendCall;
    requestToChannel(channelName: string, request: unknown): ZLinkRequestCall;
}

export interface ZLinkChannelRuntimeOptions {
    clientServerChannel(channelName: string): ZLinkClientServerChannelRuntimeOptions;
    routeMeshChannel(channelName: string): ZLinkRouteMeshChannelRuntimeOptions;
}

export interface ZLinkClientServerChannelBuilder {
    enableServer(endpoint: string): this;
    routingId(routingId: string): this;
    configureServerSocket(): ZLinkSocketConfig;
    configureClientSocket(): ZLinkSocketConfig;
    enableClient(): this;
    enableClient(endpoint: string): this;
    clientConnections(): ZLinkEndpointConnections;
    setDefaultRequestTimeout(timeoutMs: number): this;
}

export interface ZLinkClientServerChannelOptions {
    readonly requestTimeoutMs?: number;
}

export interface ZLinkClientServerChannelRuntimeOptions {
    configureServerSocket(): ZLinkSocketConfig;
}

export interface ZLinkCodecExtension {
    register(codecs: ZLinkCodecRegistrar): void;
}

export interface ZLinkCodecRegistrar {
    addSerializer(contentType: string, serializer: ZLinkMessageSerializer): this;
    addSerializer(contentType: string, serializer: ZLinkMessageSerializer, canSerialize: (payloadType: Type) => boolean): this;
    addStreamCodec(contentType: string, codec: unknown): this;
}

export interface ZLinkCodecRegistryBuilder {
    use(extension: ZLinkCodecExtension): this;
}
```

### 2.5 @zlink-systems/framework: ZLinkDecoratorMetadata - ZLinkDrainEvent

```ts
export interface ZLinkDecoratorMetadata {
    readonly kind: string;
    readonly packetName?: string;
    readonly groupName?: string;
    readonly methodName?: string;
    readonly spotNodeName?: string;
    readonly topic?: string;
}

export declare const zlinkDefaultLocationOptions: Required<ZLinkLocationOptions>;

export interface ZLinkDiagnosticsOptions {
    messageFlow: ZLinkMessageFlowLogMode;
    sampleRate: number;
    includeMessageSizes: boolean;
    includeNativeDiagnostics: boolean;

    logFile?: string;

    label?: string;
}

export declare enum ZLinkDispatchErrorAction {
    ReplyError = "replyError",
    FailCaller = "failCaller",
    Drop = "drop"
}

export declare enum ZLinkDispatchErrorReason {
    HandlerMissing = "handlerMissing",
    PayloadDecodeFailed = "payloadDecodeFailed",
    HandlerException = "handlerException",
    InvalidFrame = "invalidFrame",
    ReplyPathMissing = "replyPathMissing",
    UnexpectedReply = "unexpectedReply"
}

export declare enum ZLinkDispatchErrorSurface {
    Channel = "channel",
    RouteMeshChannel = "routeMeshChannel",
    SpotRoute = "spotRoute",
    SpotSubscription = "spotSubscription",
    SpotActor = "spotActor",
    StreamSession = "streamSession"
}

export interface ZLinkDispatchFailure {
    readonly surface: ZLinkDispatchErrorSurface;
    readonly messageKind: ZLinkDispatchMessageKind;
    readonly reason: ZLinkDispatchErrorReason;
    readonly action: ZLinkDispatchErrorAction;
    readonly packetName?: string;
    readonly channelName?: string;
    readonly topic?: string;
    readonly spotRid?: string;
    readonly actorId?: string;
    readonly sourceRid?: string;
    readonly correlationId?: string;
    readonly flowId?: string;
    readonly flowOrigin?: import('../Eventing/Contracts').ZLinkFlowOrigin;
    readonly errorType?: string;
    readonly errorMessage?: string;
}

export declare enum ZLinkDispatchMessageKind {
    Request = "request",
    Send = "send",
    Publish = "publish",
    Response = "response",
    Error = "error",
    ActorRequest = "actorRequest",
    ActorSend = "actorSend"
}

export interface ZLinkDispatchOptions {
    readonly unhandled: ZLinkUnhandledDispatchOptions;
    readonly diagnostics: ZLinkDiagnosticsOptions;
}

export interface ZLinkDispatchOptionsBuilder {
    setMessageFlowObserver(observerType: Type<ZLinkMessageFlowObserver>): this;

    messageFlow(mode: ZLinkMessageFlowLogMode): this;
    traceSampleRate(rate: number): this;
    includeMessageSizes(include: boolean): this;

    traceLogFile(path: string): this;

    traceLabel(label: string): this;
}

export interface ZLinkDrainControl {
    drain(deadlineMs?: number, signal?: AbortSignal): Promise<ZLinkDrainResult>;
    awaitDrained(signal?: AbortSignal): Promise<ZLinkDrainResult>;
    isReady(): boolean;
}

export interface ZLinkDrainEvent extends ZLinkRuntimeEvent {
    readonly state: ZLinkDrainState;
    readonly result?: ZLinkDrainResult;
}
```

### 2.6 @zlink-systems/framework: ZLinkDrainForceReason - ZLinkFanoutClient

```ts
export type ZLinkDrainForceReason = 'DeadlineExceeded' | 'DrainingStatePublishFailed' | 'OwnerCleanupFailed' | 'TeardownFailed';

export type ZLinkDrainResult = {
    readonly kind: 'drained';
} | {
    readonly kind: 'force-stopped';
    readonly reason: ZLinkDrainForceReason;
};

export type ZLinkDrainState = 'Serving' | 'Draining' | 'Drained' | 'ForceStopping';

export declare class ZLinkEncodedPayload {
    private readonly payload;
    private constructor();
    static from(bytes: Uint8Array): ZLinkEncodedPayload;
    data(): Uint8Array;
    toBytes(): Uint8Array;
    copy(): ZLinkEncodedPayload;
    size(): number;
    isEmpty(): boolean;
    getString(encoding?: BufferEncoding): string;
    close(): void;
}

export interface ZLinkEndpointConnections {
    connect(endpoint: string): void;
    disconnect(endpoint: string): void;
    listConnections(): readonly string[];
}

export interface ZLinkEntrySpot<TActor extends ZLinkActor = ZLinkActor> extends ZLinkSpotActorLifecycle<TActor> {
    readonly context: ZLinkEntrySpotContext<TActor>;
    configure?(): void;
    onInitialize?(): Promise<void>;
    onClosing?(): Promise<void>;
    onCreateActor?(actor: TActor, createRequest: ZLinkMessage): Promise<void>;
}

export interface ZLinkEntrySpotActorRequestHandler<TEntrySpot, TActor extends ZLinkActor, TRequest, TReply> {
    handle(entrySpot: TEntrySpot, actor: TActor, context: ZLinkSpotActorRequestContext, request: TRequest): Promise<TReply>;
}

export interface ZLinkEntrySpotActorSendHandler<TEntrySpot, TActor extends ZLinkActor, TMessage> {
    handle(entrySpot: TEntrySpot, actor: TActor, context: ZLinkSpotActorSendContext, message: TMessage): Promise<void>;
}

export interface ZLinkEntrySpotContext<TActor extends ZLinkActor = ZLinkActor, TEntrySpot extends ZLinkEntrySpot<TActor> = ZLinkEntrySpot<TActor>> extends ZLinkSpotCommonContext<TActor, TEntrySpot> {

    runWorker<T>(work: (signal: AbortSignal) => T | Promise<T>): ZLinkWorkerCall<T>;
    destroyActor(actor: TActor, signal?: AbortSignal): Promise<void>;
}

export interface ZLinkEntrySpotOptions {
    routingId?: RoutingId;
}

export interface ZLinkFanoutChannelBuilder {
    enablePublisher(endpoint: string): this;
    enableSubscriber(): this;
    enableSubscriber(endpoint: string): this;
    subscriberConnections(): ZLinkEndpointConnections;
}

export interface ZLinkFanoutClient {
    publish(channelName: string, topic: string, event: unknown): ZLinkPublishCall;
}
```

### 2.7 @zlink-systems/framework: ZLinkFlowOrigin - ZLinkLocationAutoConnectType

```ts
export type ZLinkFlowOrigin = 'Inbound' | 'Timer' | 'Application' | 'Lifecycle';

export declare enum ZLinkFrameworkErrorKind {
    ActorRouteNotFound = "actorRouteNotFound",
    ActorCreateFailed = "actorCreateFailed",
    ActorAlreadyExists = "actorAlreadyExists",
    ActorTypeMismatch = "actorTypeMismatch",
    SpotCreateFailed = "spotCreateFailed",
    SpotRouteNotFound = "spotRouteNotFound",
    SpotTypeMismatch = "spotTypeMismatch",
    ActorSessionNotBound = "actorSessionNotBound",
    HandlerNotFound = "handlerNotFound",
    RouteHandlerNotFound = "routeHandlerNotFound",
    ActorDispatchHandlerNotFound = "actorDispatchHandlerNotFound",
    PayloadDecodeFailed = "payloadDecodeFailed",
    RouteNotConnected = "routeNotConnected",
    RequestTargetNotFound = "requestTargetNotFound",
    RequestRejected = "requestRejected",
    RequestProtocolError = "requestProtocolError",
    RequestFailed = "requestFailed",
    WorkerQueueFull = "workerQueueFull",
    WorkerTimedOut = "workerTimedOut",
    WorkerFailed = "workerFailed",
    ActorLocationStale = "actorLocationStale",
    ActorCreateRejected = "actorCreateRejected"
}

export declare class ZLinkFrameworkException extends Error {
    readonly kind: ZLinkFrameworkErrorKind;
    constructor(kind: ZLinkFrameworkErrorKind, message: string, isRetriable?: boolean, cause?: unknown);
    readonly isRetriable: boolean;
}

export interface ZLinkFrameworkOptions {
    codecs(): ZLinkCodecRegistryBuilder;

    configureWorker(options: ZLinkWorkerOptions): this;
    configureDispatch(): ZLinkDispatchOptionsBuilder;
    useInMemoryLocationStores(): this;
    addLocationStore(store: ZLinkLocationStore): this;
    addActorTransferAdapter<TActor extends ZLinkActor>(actorType: Type<TActor>, adapterType: Type<ZLinkActorTransferAdapter<TActor>>): this;

    setActorTransferForwardWindow(timeoutMs: number): this;
    configureLocations(): ZLinkLocationOptions;
    configureStreamCompression(): ZLinkStreamCompressionBuilder;
    addSpotFactory<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): this;
    addSpotMesh(channelName: string): ZLinkSpotMeshBuilder;
    addClientServerChannel(name: string): ZLinkClientServerChannelBuilder;
    addFanoutChannel(name: string): ZLinkFanoutChannelBuilder;
    addRouteMeshChannel(name: string): ZLinkRouteMeshChannelBuilder;
    addStreamNode(name: string): ZLinkStreamNodeBuilder;
}

export interface ZLinkHandlerContext {
    readonly channelName?: string;
    readonly packetName?: string;
    readonly contentType?: string;
    readonly connectionAborted?: AbortSignal;
}

export type ZLinkHandlerDelegate = () => Promise<unknown>;

export interface ZLinkHandlerFilter {
    invoke(invocation: ZLinkHandlerInvocation, next: ZLinkHandlerDelegate): Promise<unknown>;
}

export declare function ZLinkHandlerGroup(groupName: string): ClassDecorator;

export interface ZLinkHandlerInvocation {
    readonly message: unknown;
    readonly context: ZLinkHandlerContext;
}

export type ZLinkLocationActorEvent = ZLinkRuntimeEvent & ({
    readonly event: ZLinkLocationActorEventKind.RowUpdated;
    readonly key: ZLinkActorLocationKey;
    readonly actor: ZLinkActorLocation;
} | {
    readonly event: ZLinkLocationActorEventKind.RowRemoved | ZLinkLocationActorEventKind.ResolveMiss;
    readonly key: ZLinkActorLocationKey;
});

export declare enum ZLinkLocationActorEventKind {
    RowUpdated = 0,
    RowRemoved = 1,
    ResolveMiss = 2
}

export declare enum ZLinkLocationAutoConnectType {
    Invalid = 0,
    RouteMesh = 1,
    ClientServer = 2,
    DealerMesh = 3,
    Fanout = 4,
    SpotMesh = 5
}
```

### 2.8 @zlink-systems/framework: ZLinkLocationChanged - ZLinkLocationPeerEventKind

```ts
export interface ZLinkLocationChanged {
    readonly kind: ZLinkLocationKind;
    readonly key: ZLinkLocationKey;
    readonly changeType: ZLinkLocationChangeType;
    readonly generation: bigint;
    readonly updatedAt: Date;
}

export interface ZLinkLocationChangeStampScope {
    readonly kind: ZLinkLocationKind;
    readonly meshName?: string;
}

export interface ZLinkLocationChangeStampStore {
    getChangeStamp(scope: ZLinkLocationChangeStampScope, signal?: AbortSignal): Promise<bigint>;
}

export declare enum ZLinkLocationChangeType {
    Upserted = "upserted",
    Removed = "removed",
    Expired = "expired"
}

export type ZLinkLocationKey = {
    readonly kind: ZLinkLocationKind.Peer;
    readonly key: ZLinkPeerLocationKey;
} | {
    readonly kind: ZLinkLocationKind.Spot;
    readonly key: ZLinkSpotLocationKey;
} | {
    readonly kind: ZLinkLocationKind.Actor;
    readonly key: ZLinkActorLocationKey;
} | {
    readonly kind: ZLinkLocationKind.Route;
    readonly key: ZLinkRouteLocationKey;
};

export declare enum ZLinkLocationKind {
    Invalid = 0,
    Peer = 1,
    Spot = 2,
    Actor = 3,
    Route = 4
}

export interface ZLinkLocationMonitoringRegistration {
    readonly sourceName: string;
}

export interface ZLinkLocationOptions {
    readonly heartbeatIntervalMs?: number;
    readonly ownerLeaseTtlMs?: number;
    readonly pollingIntervalMs?: number;
    readonly listPageSize?: number;
    readonly storeFailureGraceMs?: number;
}

export interface ZLinkLocationOwnerToken {
    readonly ownerId: string;
    readonly generation: bigint;
}

export interface ZLinkLocationPage<T> {
    readonly items: readonly T[];
    readonly continuationToken?: string;
}

export type ZLinkLocationPeerEvent = (ZLinkRuntimeEvent & {
    readonly event: ZLinkLocationPeerEventKind.RowUpdated;
    readonly key: string;
    readonly peer: ZLinkPeerLocation;
}) | (ZLinkRuntimeEvent & {
    readonly event: ZLinkLocationPeerEventKind.RowRemoved;
    readonly key: string;
}) | (ZLinkRuntimeEvent & {
    readonly event: ZLinkLocationPeerEventKind.DesiredSetChanged;
    readonly desiredSetChange: ZLinkAutoConnectDesiredSetChange;
});

export declare enum ZLinkLocationPeerEventKind {
    RowUpdated = 0,
    RowRemoved = 1,
    DesiredSetChanged = 2
}
```

### 2.9 @zlink-systems/framework: ZLinkLocationReadiness - ZLinkLocationSpotEventKind

```ts
export interface ZLinkLocationReadiness {
    isPeerReady(meshName: string, role: ZLinkLocationRole, nodeRid?: RoutingId, signal?: AbortSignal): Promise<boolean>;
}

export declare enum ZLinkLocationRole {
    Invalid = 0,
    Spot = 2,
    Router = 3,
    Dealer = 4,
    Pub = 5,
    Sub = 6
}

export type ZLinkLocationRouteEvent = ZLinkRuntimeEvent & ({
    readonly event: ZLinkLocationRouteEventKind.RowUpdated;
    readonly key: ZLinkRouteLocationKey;
    readonly route: ZLinkRouteLocation;
} | {
    readonly event: ZLinkLocationRouteEventKind.RowRemoved | ZLinkLocationRouteEventKind.ResolveMiss;
    readonly key: ZLinkRouteLocationKey;
});

export declare enum ZLinkLocationRouteEventKind {
    RowUpdated = 0,
    RowRemoved = 1,
    ResolveMiss = 2
}

export type ZLinkLocationRuntimeEvent = (ZLinkRuntimeEvent & {
    readonly event: ZLinkLocationRuntimeEventKind.StatusChanged;
    readonly status: ZLinkLocationRuntimeStatus;
}) | (ZLinkRuntimeEvent & {
    readonly event: ZLinkLocationRuntimeEventKind.TopologyChanged;
    readonly topology: readonly ZLinkLocationTopologyEntry[];
    readonly topologyFilter?: ZLinkLocationTopologyFilter;
}) | (ZLinkRuntimeEvent & {
    readonly event: ZLinkLocationRuntimeEventKind.ServiceSummaryChanged;
    readonly serviceSummary: readonly ZLinkLocationServiceSummary[];
    readonly serviceSummaryFilter?: ZLinkLocationServiceSummaryFilter;
}) | (ZLinkRuntimeEvent & {
    readonly event: ZLinkLocationRuntimeEventKind.StoreUnavailable | ZLinkLocationRuntimeEventKind.StoreRecovered;
});

export declare enum ZLinkLocationRuntimeEventKind {
    StatusChanged = 0,
    TopologyChanged = 1,
    ServiceSummaryChanged = 2,
    StoreUnavailable = 3,
    StoreRecovered = 4
}

export interface ZLinkLocationRuntimeQuery {
    getStatus(signal?: AbortSignal): Promise<ZLinkLocationRuntimeStatus>;
    listPeerLocations(filter: ZLinkPeerLocationFilter, signal?: AbortSignal): Promise<readonly ZLinkPeerLocation[]>;
    listSpotLocations(filter: ZLinkSpotLocationFilter, page?: ZLinkPageRequest, signal?: AbortSignal): Promise<ZLinkLocationPage<ZLinkSpotLocation>>;
    listActorLocations(filter: ZLinkActorLocationFilter, page?: ZLinkPageRequest, signal?: AbortSignal): Promise<ZLinkLocationPage<ZLinkActorLocation>>;
    listRouteLocations(filter: ZLinkRouteLocationFilter, page?: ZLinkPageRequest, signal?: AbortSignal): Promise<ZLinkLocationPage<ZLinkRouteLocation>>;
    listTopology(filter: ZLinkLocationTopologyFilter, page?: ZLinkPageRequest, signal?: AbortSignal): Promise<ZLinkLocationPage<ZLinkLocationTopologyEntry>>;
    listServiceSummaries(filter: ZLinkLocationServiceSummaryFilter, signal?: AbortSignal): Promise<readonly ZLinkLocationServiceSummary[]>;
}

export interface ZLinkLocationRuntimeStatus {
    readonly storeHealthy: boolean;
    readonly watchEnabled: boolean;
    readonly pollingIntervalMs: number;
    readonly lastRefreshAt?: Date;
    readonly lastError?: string;
    readonly ownerLeaseHealthy: boolean;
    readonly ownerLeaseRenewedAt?: Date;
}

export interface ZLinkLocationServiceSummary {
    readonly meshName: string;
    readonly autoConnectType: ZLinkLocationAutoConnectType;
    readonly role: ZLinkLocationRole;
    readonly totalCount: number;
    readonly readyCount: number;
    readonly errorCount: number;
    readonly stoppedCount: number;
    readonly updatedAt: Date;
}

export interface ZLinkLocationServiceSummaryFilter {
    readonly meshName?: string;
    readonly autoConnectType?: ZLinkLocationAutoConnectType;
    readonly role?: ZLinkLocationRole;
}

export type ZLinkLocationSpotEvent = ZLinkRuntimeEvent & ({
    readonly event: ZLinkLocationSpotEventKind.RowUpdated;
    readonly key: ZLinkSpotLocationKey;
    readonly spot: ZLinkSpotLocation;
} | {
    readonly event: ZLinkLocationSpotEventKind.RowRemoved | ZLinkLocationSpotEventKind.ResolveMiss;
    readonly key: ZLinkSpotLocationKey;
});

export declare enum ZLinkLocationSpotEventKind {
    RowUpdated = 0,
    RowRemoved = 1,
    ResolveMiss = 2
}
```

### 2.10 @zlink-systems/framework: ZLinkLocationStore - ZLinkMessageFlowEvent

```ts
export interface ZLinkLocationStore extends ZLinkPeerLocationStore, ZLinkSpotLocationStore, ZLinkActorLocationStore, ZLinkRouteLocationStore, ZLinkOwnerLeaseStore {
    removeAllByOwner(ownerId: string, signal?: AbortSignal): Promise<number>;
}

export interface ZLinkLocationTopologyEntry {
    readonly kind: ZLinkLocationKind;
    readonly meshName?: string;
    readonly role?: ZLinkLocationRole;
    readonly nodeRid?: RoutingId;
    readonly spotRid?: RoutingId;
    readonly actorId?: string;
    readonly endpoint?: string;
    readonly state: ZLinkLocationTopologyState;
    readonly desiredCount: number;
    readonly readyCount: number;
    readonly errorCode: number;
    readonly updatedAt: Date;
}

export interface ZLinkLocationTopologyFilter {
    readonly kind?: ZLinkLocationKind;
    readonly meshName?: string;
    readonly role?: ZLinkLocationRole;
    readonly nodeRid?: RoutingId;
    readonly state?: ZLinkLocationTopologyState;
}

export declare enum ZLinkLocationTopologyState {
    Discovered = 1,
    Connecting = 2,
    Ready = 3,
    Lost = 4,
    Error = 5,
    Stopped = 6
}

export interface ZLinkLocationWatchFilter {
    readonly kind: ZLinkLocationKind;
    readonly meshName?: string;
    readonly routeKind?: ZLinkRouteKind;
}

export interface ZLinkLocationWatchStore {
    watch(filter: ZLinkLocationWatchFilter, signal?: AbortSignal): AsyncIterable<ZLinkLocationChanged>;
}

export declare enum ZLinkLocationWriteIntent {
    NewClaim = 1,
    Renew = 2,
    Takeover = 3
}

export interface ZLinkLocationWriteResult {
    readonly status: ZLinkLocationWriteStatus;
    readonly generation: bigint;
    readonly updatedAt: Date;
}

export declare enum ZLinkLocationWriteStatus {
    Stored = "stored",
    IgnoredStale = "ignoredStale",
    RejectedConflict = "rejectedConflict"
}

export declare class ZLinkMessage<TValue = unknown> {
    private readonly value;
    private readonly encoded;
    private readonly registry;
    private constructor();
    static from<T>(value: T): ZLinkMessage<T>;
    static fromEncoded(payload: ZLinkEncodedPayload, registry?: ZLinkSerializerRegistryLike | ReadonlyMap<string, ZLinkMessageSerializer>): ZLinkMessage;
    decode<T>(type?: Type<T>): T;
    toEncodedPayload(registry?: ZLinkSerializerRegistryLike | ReadonlyMap<string, ZLinkMessageSerializer>, context?: ZLinkSerializerSelectionContext): ZLinkEncodedPayload;
    isEncoded(): boolean;
}

export interface ZLinkMessageFlowControl {
    setMessageFlowMode(mode: ZLinkMessageFlowLogMode): void;
    messageFlowMode(): ZLinkMessageFlowLogMode;
}

export interface ZLinkMessageFlowEvent {
    readonly outcome: ZLinkMessageFlowOutcome;
    readonly surface: ZLinkDispatchErrorSurface;
    readonly messageKind: ZLinkDispatchMessageKind;
    readonly packetName?: string;
    readonly channelName?: string;
    readonly topic?: string;
    readonly correlationId?: string;
    readonly sourceRid?: string;
    readonly peerRid?: string;
    readonly socketRole?: string;
    readonly effectiveMode: ZLinkMessageFlowLogMode;
    readonly flowId: string;
    readonly flowOrigin: import('../Eventing/Contracts').ZLinkFlowOrigin;
    readonly spotRid?: string;
    readonly actorId?: string;
    readonly messageSize?: number;
    readonly errorReason?: ZLinkDispatchErrorReason;
    readonly errorAction?: ZLinkDispatchErrorAction;
    readonly errorType?: string;
    readonly errorMessage?: string;
}
```

### 2.11 @zlink-systems/framework: ZLinkMessageFlowLogMode - ZLinkMeters

```ts
export declare enum ZLinkMessageFlowLogMode {
    Off = "off",
    ErrorsOnly = "errorsOnly",
    KeyTransitions = "keyTransitions",
    Verbose = "verbose",
    Diagnostic = "diagnostic"
}

export interface ZLinkMessageFlowObserver {
    onMessageFlow(flow: ZLinkMessageFlowEvent): Promise<void> | void;
}

export declare enum ZLinkMessageFlowOutcome {
    Received = "received",
    Dispatched = "dispatched",
    Replied = "replied",
    Dropped = "dropped",
    Sent = "sent",
    ReplyReceived = "replyReceived",
    Error = "error"
}

export declare function zlinkMessageMetadata(values: ReadonlyMap<string, string> | Readonly<Record<string, string>>): ZLinkMessageMetadata;

export interface ZLinkMessageMetadata {
    readonly values: ReadonlyMap<string, string>;
    find(key: string): string | undefined;
}

export declare const ZLinkMessageMetadataEmpty: ZLinkMessageMetadata;

export interface ZLinkMessageMetadataPolicy {
    canForward(key: string): boolean;
}

export interface ZLinkMessageSerializer {
    canSerialize?(value: unknown, context: ZLinkSerializerSelectionContext): boolean;
    serialize<T>(value: T): ZLinkEncodedPayload;
    deserialize<T>(payload: ZLinkEncodedPayload, type: Type<T>): T;
}

export interface ZLinkMetadataPolicyBuilder {
    forward(enabled?: boolean): this;
}

export interface ZLinkMeter {
    createCounter(name: string, options?: {
        readonly unit?: string;
    }): ZLinkMetricInstrument;
    createUpDownCounter(name: string, options?: {
        readonly unit?: string;
    }): ZLinkMetricInstrument;
    createHistogram(name: string, options?: {
        readonly unit?: string;
    }): ZLinkMetricHistogram;
}

export interface ZLinkMeterProvider {
    getMeter(name: string): ZLinkMeter;
}

export declare const ZLinkMeters: Readonly<{
    readonly Framework: "zlink.framework";
}>;
```

### 2.12 @zlink-systems/framework: ZLinkMetricAttributes - ZLinkPacket

```ts
export interface ZLinkMetricAttributes {
    readonly [name: string]: string | number | boolean;
}

export interface ZLinkMetricHistogram {
    record(value: number, attributes?: ZLinkMetricAttributes): void;
}

export interface ZLinkMetricInstrument {
    add(value: number, attributes?: ZLinkMetricAttributes): void;
}

export interface ZLinkMetricsOptions {
    readonly meterProvider?: ZLinkMeterProvider;
}

export interface ZLinkMonitoringOptions {
    socket?: ZLinkSocketMonitoringRegistration[];
    spot?: ZLinkPollingMonitoringRegistration[];
    locationRuntime?: ZLinkPollingMonitoringRegistration[];
    locationPeer?: ZLinkLocationMonitoringRegistration[];
    locationSpot?: ZLinkLocationMonitoringRegistration[];
    locationActor?: ZLinkLocationMonitoringRegistration[];
    locationRoute?: ZLinkLocationMonitoringRegistration[];
}

export interface ZLinkOutboundRouteConfig {
    targetNodeRid: RoutingId;
    endpoint: string;
}

export interface ZLinkOwnerLease {
    readonly ownerId: string;
    readonly nodeRid: RoutingId;
    readonly leaseExpiresAt: Date;
    readonly updatedAt: Date;
}

export interface ZLinkOwnerLeaseRenewal {
    readonly leaseExpiresAt: Date;
    readonly storeNow: Date;
}

export interface ZLinkOwnerLeaseRenewalRequest {
    readonly ownerId: string;
    readonly nodeRid: RoutingId;
    readonly leaseTtlMs: number;
}

export interface ZLinkOwnerLeaseSnapshot {
    readonly leases: readonly ZLinkOwnerLease[];
    readonly storeNow: Date;
}

export interface ZLinkOwnerLeaseStore {
    renewOwnerLease(ownerId: string, nodeRid: RoutingId, leaseTtlMs: number, signal?: AbortSignal): Promise<ZLinkOwnerLeaseRenewal>;
    removeOwnerLease(ownerId: string, signal?: AbortSignal): Promise<boolean>;
    listOwnerLeases(signal?: AbortSignal): Promise<ZLinkOwnerLeaseSnapshot>;
}

export declare function ZLinkPacket(packetName: string): ClassDecorator;
```

### 2.13 @zlink-systems/framework: ZLinkPageRequest - ZLinkPublishHandler

```ts
export interface ZLinkPageRequest {
    readonly pageSize?: number;
    readonly continuationToken?: string;
}

export interface ZLinkPeerLocation {
    readonly autoConnectType: ZLinkLocationAutoConnectType;
    readonly meshName: string;
    readonly nodeRid?: RoutingId;
    readonly role: ZLinkLocationRole;
    readonly endpoint: string;
    readonly weight: number;
    readonly draining: boolean;
    readonly value: bigint;
    readonly metadata?: Readonly<Record<string, string>>;
    readonly capabilities?: readonly string[];
    readonly ownerId: string;
    readonly generation: bigint;
    readonly updatedAt: Date;
}

export interface ZLinkPeerLocationFilter {
    readonly autoConnectType?: ZLinkLocationAutoConnectType;
    readonly meshName?: string;
    readonly role?: ZLinkLocationRole;
    readonly nodeRid?: RoutingId;
    readonly endpoint?: string;
}

export interface ZLinkPeerLocationKey {
    readonly autoConnectType: ZLinkLocationAutoConnectType;
    readonly meshName: string;
    readonly role: ZLinkLocationRole;
    readonly nodeRid?: RoutingId;
    readonly endpoint?: string;
}

export interface ZLinkPeerLocationResolver {
    listLivePeers(filter: ZLinkPeerLocationFilter, signal?: AbortSignal): Promise<readonly ZLinkPeerLocation[]>;
}

export interface ZLinkPeerLocationStore {
    updatePeer(peer: ZLinkPeerLocation, intent: ZLinkLocationWriteIntent, signal?: AbortSignal): Promise<ZLinkLocationWriteResult>;
    removePeer(key: ZLinkPeerLocationKey, owner: ZLinkLocationOwnerToken, signal?: AbortSignal): Promise<ZLinkLocationWriteResult>;
    listPeers(filter: ZLinkPeerLocationFilter, signal?: AbortSignal): Promise<readonly ZLinkPeerLocation[]>;
}

export interface ZLinkPollingMonitoringRegistration {
    readonly sourceName: string;
    readonly intervalMs: number;
}

export interface ZLinkProviderResolver {
    get?<T>(type: Type<T>): T | undefined;
    create?<T>(type: Type<T>): T | Promise<T>;
}

export declare function ZLinkPublish(packetName?: string): MethodDecorator;

export interface ZLinkPublishCall {
    submit(): void;
}

export interface ZLinkPublishContext extends ZLinkHandlerContext {
    readonly topic: string;
    readonly source?: string;
}

export interface ZLinkPublishHandler<TMessage> {
    handle(message: TMessage, context: ZLinkPublishContext): Promise<void>;
}
```

### 2.14 @zlink-systems/framework: ZLinkRequest - ZLinkRouteMeshChannelBuilder

```ts
export declare function ZLinkRequest(packetName?: string): MethodDecorator;

export interface ZLinkRequestCall {
    timeout(timeoutMs: number): this;
    submit<TReply>(signal?: AbortSignal): Promise<TReply>;
}

export interface ZLinkRequestContext extends ZLinkHandlerContext {
}

export interface ZLinkRequestHandler<TRequest, TResponse> {
    handle(request: TRequest, context: ZLinkRequestContext): Promise<TResponse>;
}

export interface ZLinkRouteClient {
    sendToNode(routerChannelId: string, targetNodeRid: RoutingId, message: unknown): ZLinkSendCall;
    requestToNode(routerChannelId: string, targetNodeRid: RoutingId, request: unknown): ZLinkRequestCall;
}

export interface ZLinkRouteConfig {
    channelName: string;
    endpoint: string;
}

export declare enum ZLinkRouteKind {
    Invalid = 0,
    ActorSession = 1,
    SpotName = 2,
    FrameworkRoute = 3
}

export interface ZLinkRouteLocation {
    readonly routeKind: ZLinkRouteKind;
    readonly routeKey: string;
    readonly ownerNodeRid: RoutingId;
    readonly ownerId: string;
    readonly generation: bigint;

    readonly value: Uint8Array;
    readonly updatedAt: Date;
}

export interface ZLinkRouteLocationFilter {
    readonly routeKind?: ZLinkRouteKind;
    readonly ownerNodeRid?: RoutingId;
    readonly ownerId?: string;
}

export interface ZLinkRouteLocationKey {
    readonly routeKind: ZLinkRouteKind;
    readonly routeKey: string;
}

export interface ZLinkRouteLocationStore {
    updateRoute(route: ZLinkRouteLocation, intent: ZLinkLocationWriteIntent, signal?: AbortSignal): Promise<ZLinkLocationWriteResult>;
    removeRoute(key: ZLinkRouteLocationKey, owner: ZLinkLocationOwnerToken, signal?: AbortSignal): Promise<ZLinkLocationWriteResult>;
    resolveRoute(key: ZLinkRouteLocationKey, signal?: AbortSignal): Promise<ZLinkRouteLocation | undefined>;
    listRoutes(filter: ZLinkRouteLocationFilter, page?: ZLinkPageRequest, signal?: AbortSignal): Promise<ZLinkLocationPage<ZLinkRouteLocation>>;
}

export interface ZLinkRouteMeshChannelBuilder {
    enableServer(endpoint: string): this;
    enableClient(): this;
    enableClient(endpoint: string): this;
    clientConnections(): ZLinkEndpointConnections;
    configureSocket(): ZLinkSocketConfig;
    setDefaultRequestTimeout(timeoutMs: number): this;
}
```

### 2.15 @zlink-systems/framework: ZLinkRouteMeshChannelRuntimeOptions - ZLinkSendHandler

```ts
export interface ZLinkRouteMeshChannelRuntimeOptions {
    configureSocket(): ZLinkSocketConfig;
}

export interface ZLinkRouteRequestContext extends ZLinkRouteSendContext {
}

export interface ZLinkRouteRequestHandler<TRequest, TReply> {
    handle(request: TRequest, context: ZLinkRouteRequestContext): Promise<TReply>;
}

export interface ZLinkRouteSendContext extends ZLinkHandlerContext {
    readonly routerChannelId: string;
    readonly sourceNodeRid: RoutingId;
}

export interface ZLinkRouteSendHandler<TMessage> {
    handle(message: TMessage, context: ZLinkRouteSendContext): Promise<void>;
}

export interface ZLinkRuntimeEvent {
    readonly sourceName: string;
    readonly timestamp: Date;
}

export interface ZLinkRuntimeEventHandler<TEvent extends ZLinkRuntimeEvent> {
    handle(event: TEvent): Promise<void>;
}

export interface ZLinkRuntimeEventPublisher {
    register<TEvent extends ZLinkRuntimeEvent>(handler: ZLinkRuntimeEventHandler<TEvent>): void;
    publish<TEvent extends ZLinkRuntimeEvent>(event: TEvent): Promise<void>;
}

export declare function ZLinkSend(packetName?: string): MethodDecorator;

export interface ZLinkSendCall {
    submit(): void;
}

export interface ZLinkSendContext extends ZLinkHandlerContext {
}

export interface ZLinkSendHandler<TMessage> {
    handle(message: TMessage, context: ZLinkSendContext): Promise<void>;
}
```

### 2.16 @zlink-systems/framework: ZLinkSerializerRegistryLike - ZLinkSessionReplyCall

```ts
export interface ZLinkSerializerRegistryLike {
    readonly serializers: ReadonlyMap<string, ZLinkMessageSerializer>;
}

export interface ZLinkSerializerSelectionContext {
    readonly messageType?: Type<unknown>;
    readonly packetName?: string;
}

export interface ZLinkSession {
    readonly context: ZLinkSessionContext;
    onConnected?(context: ZLinkSessionContext): Promise<void>;
    onDisconnected?(context: ZLinkSessionContext): Promise<void>;
    onError?(context: ZLinkSessionContext, error: ZLinkStreamError): Promise<void>;
    onDispatch?(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage): Promise<void>;
}

export interface ZLinkSessionActor {
    readonly actorId: string;
    readonly ref: ActorRef;
    relay(payload: ZLinkMessage, signal?: AbortSignal): Promise<void>;
    notifyDisconnected(signal?: AbortSignal): Promise<void>;
}

export interface ZLinkSessionActors {
    readonly bound: readonly ZLinkSessionActor[];
    bind(actor: ZLinkActor | ActorRef, signal?: AbortSignal): Promise<ZLinkSessionActor>;
    bindOrGet(actor: ActorRef, signal?: AbortSignal): Promise<ZLinkSessionActor>;
    find(actorId: string): ZLinkSessionActor | undefined;
}

export interface ZLinkSessionClient {
    send(message: unknown): ZLinkSessionSendCall;
    reply(message: unknown): ZLinkSessionReplyCall;
}

export interface ZLinkSessionContext {
    readonly sessionId: string;
    readonly routingId?: RoutingId;
    readonly localAddr?: string;
    readonly remoteAddr?: string;
    readonly client: ZLinkSessionClient;
    readonly actors: ZLinkSessionActors;
    readonly handlers: ZLinkSessionHandlerRegistry;
    close(signal?: AbortSignal): Promise<void>;
}

export interface ZLinkSessionDispatchContext {
    readonly packetName: string;
    readonly metadata: ReadonlyMap<string, string>;
    readonly canReply: boolean;
}

export interface ZLinkSessionFactory<TSession extends ZLinkSession = ZLinkSession> {
    create(context: ZLinkSessionContext): Promise<TSession>;
}

export interface ZLinkSessionHandlerRegistry {
    addHandler<THandler>(handlerType: Type<THandler>): this;
    tryHandle(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage): Promise<boolean>;
}

export interface ZLinkSessionPacketHandler<TSessionContext, TMessage = ZLinkMessage> {
    handle(context: TSessionContext, dispatch: ZLinkSessionDispatchContext, message: TMessage): Promise<void>;
}

export interface ZLinkSessionReplyCall {
    metadata(key: string, value: string): this;
    compress(enabled?: boolean): this;
    submit(): void;
}
```

### 2.17 @zlink-systems/framework: ZLinkSessionSendCall - ZLinkSpotActorReplyOptions

```ts
export interface ZLinkSessionSendCall {
    metadata(key: string, value: string): this;
    compress(enabled?: boolean): this;
    submit(): void;
}

export interface ZLinkSocketConfig {
    bind?: string;
    connect?: string;
    channelName?: string;
    weight?: number;
    sendHighWaterMark?: number;
    receiveHighWaterMark?: number;
    sendTimeoutMs?: number;
    maxMessageSize?: number;
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

export declare enum ZLinkSocketEventKind {
    Connected = "connected",
    ConnectionReady = "connectionReady",
    Disconnected = "disconnected",
    HandshakeFailed = "handshakeFailed",
    PeerAdmissionChanged = "peerAdmissionChanged",
    Closed = "closed",
    Internal = "internal"
}

export interface ZLinkSocketMonitoringRegistration {
    readonly sourceName: string;
    readonly events?: readonly ZLinkSocketEventKind[];
}

export declare enum ZLinkSocketNativeEventType {
    Connected = 1,
    ConnectDelayed = 2,
    ConnectRetried = 4,
    Listening = 8,
    BindFailed = 16,
    Accepted = 32,
    AcceptFailed = 64,
    Closed = 128,
    CloseFailed = 256,
    Disconnected = 512,
    MonitorStopped = 1024,
    HandshakeFailedNoDetail = 2048,
    ConnectionReady = 4096,
    HandshakeFailedProtocol = 8192,
    HandshakeFailedAuth = 16384,
    PeerAdmissionChanged = 32768
}

export interface ZLinkSpot<TActor extends ZLinkActor = ZLinkActor> extends ZLinkSpotActorLifecycle<TActor> {
    readonly context: ZLinkSpotContext<TActor>;
    configure?(): void;
    onCreate?(request: ZLinkMessage): Promise<ZLinkSpotCreateResponse>;
    onInitialize?(): Promise<void>;
    onClosing?(): Promise<void>;
}

export interface ZLinkSpotAcceptRejectResponse {
    readonly accepted: boolean;
    readonly reply?: unknown;
}

export interface ZLinkSpotActorJoinResponse extends ZLinkSpotAcceptRejectResponse {
}

export interface ZLinkSpotActorLifecycle<TActor extends ZLinkActor = ZLinkActor> {
    onActorJoin(actorId: string, request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse>;
    onJoinedActor(actor: TActor): Promise<void>;
    onLeaveActor(actor: TActor): Promise<void>;
    onDisconnectActor?(actor: TActor): Promise<void>;
}

export interface ZLinkSpotActorReplyOptions {
    metadata(key: string, value: string): this;
    compress(enabled?: boolean): this;
}
```

### 2.18 @zlink-systems/framework: ZLinkSpotActorRequest - ZLinkSpotDrainPolicy

```ts
export declare function ZLinkSpotActorRequest(packetName?: string): MethodDecorator;

export interface ZLinkSpotActorRequestContext extends ZLinkSpotActorSendContext {
    readonly reply: ZLinkSpotActorReplyOptions;
}

export interface ZLinkSpotActorRequestHandler<TSpot, TActor extends ZLinkActor, TRequest, TReply> {
    handle(spot: TSpot, actor: TActor, context: ZLinkSpotActorRequestContext, request: TRequest): Promise<TReply>;
}

export declare function ZLinkSpotActorSend(packetName?: string): MethodDecorator;

export interface ZLinkSpotActorSendContext extends ZLinkHandlerContext {
    readonly metadata: ZLinkMessageMetadata;
}

export interface ZLinkSpotActorSendHandler<TSpot, TActor extends ZLinkActor, TMessage> {
    handle(spot: TSpot, actor: TActor, context: ZLinkSpotActorSendContext, message: TMessage): Promise<void>;
}

export interface ZLinkSpotCommonContext<TActor extends ZLinkActor = ZLinkActor, TSpot = ZLinkSpot<TActor>> {
    readonly spotRid: RoutingId;
    readonly nodeRid: RoutingId;
    readonly routingId: RoutingId;
    readonly handlers: ZLinkSpotHandlerRegistry;
    readonly outbound: ZLinkSpotOutbound;
    addTimer<THandler extends ZLinkSpotTimerHandler<TSpot>>(name: string, periodMs: number, handlerType: Type<THandler>, options?: ZLinkTimerOptions, signal?: AbortSignal): Promise<ZLinkTimer>;

    runWorker<T>(work: (signal: AbortSignal) => T | Promise<T>): ZLinkWorkerCall<T>;
}

export interface ZLinkSpotContext<TActor extends ZLinkActor = ZLinkActor, TSpot extends ZLinkSpot<TActor> = ZLinkSpot<TActor>> extends ZLinkSpotCommonContext<TActor, TSpot> {

    runWorker<T>(work: (signal: AbortSignal) => T | Promise<T>): ZLinkWorkerCall<T>;
    leaveActor(actor: TActor, signal?: AbortSignal): Promise<void>;
    close(signal?: AbortSignal): Promise<boolean>;
}

export interface ZLinkSpotCreateResponse extends ZLinkSpotAcceptRejectResponse {
}

export interface ZLinkSpotCreateResult {
    readonly spotRid: RoutingId;
    readonly state: ZLinkSpotCreateState;
    readonly reply?: unknown;
}

export declare enum ZLinkSpotCreateState {
    Existing = "existing",
    Created = "created",
    Rejected = "rejected"
}

export type ZLinkSpotDrainPolicy = 'DrainNatural' | 'ReleaseAndRecreate';
```

### 2.19 @zlink-systems/framework: ZLinkSpotEvent - ZLinkSpotLocationStore

```ts
export type ZLinkSpotEvent = (ZLinkRuntimeEvent & {
    readonly event: ZLinkSpotEventKind.StatusChanged;
    readonly status: ZLinkSpotNodeStatus;
}) | (ZLinkRuntimeEvent & {
    readonly event: ZLinkSpotEventKind.PeersChanged;
    readonly peers: readonly ZLinkSpotNodePeerEntry[];
}) | (ZLinkRuntimeEvent & {
    readonly event: ZLinkSpotEventKind.SubjectsChanged;
    readonly subjects: readonly ZLinkSpotNodeSubjectEntry[];
}) | (ZLinkRuntimeEvent & {
    readonly event: ZLinkSpotEventKind.TimerHandlerFailed | ZLinkSpotEventKind.TimerStoppedAfterUnhandledException;
    readonly timerDiagnostic: ZLinkSpotTimerDiagnostic;
});

export declare enum ZLinkSpotEventKind {
    StatusChanged = "statusChanged",
    PeersChanged = "peersChanged",
    SubjectsChanged = "subjectsChanged",
    TimerHandlerFailed = "timerHandlerFailed",
    TimerStoppedAfterUnhandledException = "timerStoppedAfterUnhandledException"
}

export interface ZLinkSpotHandleResolver {
    resolveSpotHandle(spotRid: RoutingId, signal?: AbortSignal): Promise<SpotHandle | undefined>;
}

export interface ZLinkSpotHandlerRegistry extends ZLinkActorHandlerRegistry {
    addPacket<THandler>(handlerType: Type<THandler>): this;
    addSubscribe<THandler>(handlerType: Type<THandler>, topic: string): this;
}

export interface ZLinkSpotInfo {
    readonly spotRid: RoutingId;
}

export declare enum ZLinkSpotKind {
    Invalid = "invalid",
    Entry = "entry",
    User = "user"
}

export declare function zlinkSpotKindFromWire(value: number): ZLinkSpotKind;

export declare function zlinkSpotKindToWire(kind: ZLinkSpotKind): number;

export interface ZLinkSpotLocation {
    readonly meshName: string;
    readonly spotRid: RoutingId;
    readonly spotType?: string;
    readonly nodeRid: RoutingId;
    readonly spotKind: ZLinkSpotKind;
    readonly routeEndpoint?: string;
    readonly ownerId: string;
    readonly generation: bigint;
    readonly updatedAt: Date;
}

export interface ZLinkSpotLocationFilter {
    readonly meshName?: string;
    readonly spotType?: string;
    readonly nodeRid?: RoutingId;
    readonly spotKind?: ZLinkSpotKind;
}

export interface ZLinkSpotLocationKey {
    readonly meshName: string;
    readonly spotRid: RoutingId;
}

export interface ZLinkSpotLocationStore {
    updateSpot(spot: ZLinkSpotLocation, intent: ZLinkLocationWriteIntent, signal?: AbortSignal): Promise<ZLinkLocationWriteResult>;
    removeSpot(key: ZLinkSpotLocationKey, owner: ZLinkLocationOwnerToken, signal?: AbortSignal): Promise<ZLinkLocationWriteResult>;
    resolveSpot(key: ZLinkSpotLocationKey, signal?: AbortSignal): Promise<ZLinkSpotLocation | undefined>;
    listSpots(filter: ZLinkSpotLocationFilter, page?: ZLinkPageRequest, signal?: AbortSignal): Promise<ZLinkLocationPage<ZLinkSpotLocation>>;
}
```

### 2.20 @zlink-systems/framework: ZLinkSpotManager - ZLinkSpotPeerSource

```ts
export interface ZLinkSpotManager {
    create<TSpot extends ZLinkSpot>(spotType: Type<TSpot>, signal?: AbortSignal): Promise<ZLinkSpotCreateResult>;
    create<TSpot extends ZLinkSpot>(spotType: Type<TSpot>, request: ZLinkMessage, signal?: AbortSignal): Promise<ZLinkSpotCreateResult>;
    create<TSpot extends ZLinkSpot, TRequest>(spotType: Type<TSpot>, request: TRequest, signal?: AbortSignal): Promise<ZLinkSpotCreateResult>;
    getOrCreate<TSpot extends ZLinkSpot>(spotType: Type<TSpot>, spotRid: RoutingId, signal?: AbortSignal): Promise<ZLinkSpotCreateResult>;
    getOrCreate<TSpot extends ZLinkSpot>(spotType: Type<TSpot>, spotRid: RoutingId, request: ZLinkMessage, signal?: AbortSignal): Promise<ZLinkSpotCreateResult>;
    getOrCreate<TSpot extends ZLinkSpot, TRequest>(spotType: Type<TSpot>, spotRid: RoutingId, request: TRequest, signal?: AbortSignal): Promise<ZLinkSpotCreateResult>;
    find(spotRid: RoutingId, signal?: AbortSignal): Promise<ZLinkSpotInfo | null>;
    list(signal?: AbortSignal): Promise<readonly ZLinkSpotInfo[]>;
    close(spotRid: RoutingId, signal?: AbortSignal): Promise<boolean>;
}

export interface ZLinkSpotMeshBuilder extends ZLinkSpotNodeBuilder {
}

export interface ZLinkSpotMeshNodeBuilder extends ZLinkSpotNodeBuilder {
}

export interface ZLinkSpotNodeBuilder {
    routingId(routingId: RoutingId): this;
    enableRouter(endpoint: string, routingId?: RoutingId, connect?: string | readonly string[]): this;
    connectRouter(endpoint: string): this;
    connectRouter(peerRid: RoutingId, endpoint: string): this;
    enablePubSub(endpoint: string, routingId?: RoutingId, connect?: string | readonly string[]): this;
    connectPeerPub(endpoint: string): this;
    configureEntrySpot(options: ZLinkEntrySpotOptions): this;
    addEntrySpot<TEntrySpot extends ZLinkEntrySpot>(entrySpotType: Type<TEntrySpot>): this;
    addSpotFactory<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): this;
    actorFactory(actorType: string, factoryType: Type): this;
    useDrainPolicy(policy: ZLinkSpotDrainPolicy): this;
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

export declare enum ZLinkSpotNodeState {
    Idle = 1,
    Connecting = 2,
    PartialReady = 3,
    Ready = 4,
    Error = 5
}

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

export interface ZLinkSpotNodeSubjectEntry {
    readonly role: ZLinkSpotRole;
    readonly subject: string;
    readonly subjectKind: ZLinkSubjectKind;
    readonly readyPeerCount: number;
    readonly activePeerCount: number;
    readonly lastChangedMs: bigint;
}

export interface ZLinkSpotOutbound {
    sendToSpot(spot: SpotHandle, message: unknown): ZLinkSendCall;
    requestToSpot(spot: SpotHandle, request: unknown): ZLinkRequestCall;
    publish(topic: string, event: unknown): ZLinkPublishCall;
    sendToChannel(channelName: string, message: unknown): ZLinkSendCall;
    requestToChannel(channelName: string, request: unknown): ZLinkRequestCall;
}

export interface ZLinkSpotPacketHandler<TSpot, TMessage> {
    handle(spot: TSpot, message: TMessage, context: ZLinkHandlerContext): Promise<void>;
}

export declare enum ZLinkSpotPeerKind {
    SpotMesh = 1,
    RouterChannel = 2
}

export declare enum ZLinkSpotPeerSource {
    Manual = 1,
    Discovery = 2,
    Mixed = 3
}
```

### 2.21 @zlink-systems/framework: ZLinkSpotPeerState - ZLinkStream

```ts
export declare enum ZLinkSpotPeerState {
    Configured = 1,
    Connecting = 2,
    Connected = 3
}

export interface ZLinkSpotPublisherClient {
    publish(channelName: string, topic: string, event: unknown): ZLinkPublishCall;
}

export interface ZLinkSpotPublisherConfig {
    topic: string;
}

export declare function ZLinkSpotRequest(packetName?: string): MethodDecorator;

export interface ZLinkSpotRequestHandler<TSpot, TRequest, TReply> {
    handle(spot: TSpot, request: TRequest, context: ZLinkHandlerContext): Promise<TReply>;
}

export declare enum ZLinkSpotRole {
    Pub = 1,
    Sub = 2
}

export interface ZLinkSpotSubscriberConfig {
    topic: string;
}

export declare function ZLinkSpotSubscription(spotNodeName: string, topic: string): MethodDecorator;

export interface ZLinkSpotSubscriptionHandler<TSpot, TEvent> {
    handle(spot: TSpot, event: TEvent, context: ZLinkPublishContext): Promise<void>;
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

export interface ZLinkSpotTimerHandler<TSpot> {
    handle(spot: TSpot, tick: ZLinkTimerTick): Promise<void>;
}

export interface ZLinkStream {
    readonly sessionId: string;
    readonly routingId?: RoutingId;
    readonly localAddr?: string;
    readonly remoteAddr?: string;
    write(payload: ZLinkMessage, flags?: number): boolean;
    close(signal?: AbortSignal): Promise<void>;
}
```

### 2.22 @zlink-systems/framework: ZLinkStreamCompressionBuilder - ZLinkTimerOptions

```ts
export interface ZLinkStreamCompressionBuilder {
    useDefault(): this;
    useLz4(): this;
    use(codec: ZLinkStreamCompressionCodec): this;
    disable(): this;
}

export interface ZLinkStreamCompressionCodec {
    compress(payload: Uint8Array): Uint8Array;
    decompress(payload: Uint8Array, maxDecompressedSize: number): Uint8Array;
}

export interface ZLinkStreamCompressionOptions {
    readonly disabled?: boolean;
    readonly codec?: ZLinkStreamCompressionCodec;
}

export interface ZLinkStreamDiagnostic {
    readonly nativeCode?: number;
    readonly message?: string | undefined;
}

export interface ZLinkStreamError {
    readonly error: ZLinkStreamSessionError;
    readonly diagnostic?: ZLinkStreamDiagnostic;
}

export interface ZLinkStreamNodeBuilder {
    bind(endpoint: string): this;
    setTlsServer(certificatePath: string, keyPath: string, requireClientCertificate?: boolean): this;
    registerSession<TSession extends ZLinkSession>(sessionType: Type<TSession> | Type<ZLinkSessionFactory<TSession>>): this;
}

export declare function ZLinkStreamPacket(): MethodDecorator;

export declare function ZLinkStreamRaw(): MethodDecorator;

export declare enum ZLinkStreamSessionError {
    TransportError = "transportError",
    HandshakeFailed = "handshakeFailed"
}

export declare enum ZLinkSubjectKind {
    None = 0,
    Topic = 1,
    Pattern = 2
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
```

### 2.23 @zlink-systems/framework: ZLinkTimerOverrunPolicy - ZLinkWorkerCall

```ts
export declare enum ZLinkTimerOverrunPolicy {
    SkipLateTicks = "skipLateTicks",
    CatchUpBounded = "catchUpBounded",
    DelayNextTick = "delayNextTick"
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

export declare enum ZLinkUnhandledDispatchAction {
    ReplyError = "replyError",
    LogAndDrop = "logAndDrop",
    Drop = "drop",
    Throw = "throw"
}

export interface ZLinkUnhandledDispatchOptions {
    request: ZLinkUnhandledDispatchAction;
    send: ZLinkUnhandledDispatchAction;
    publish: ZLinkUnhandledDispatchAction;
}

export interface ZLinkWorkerCall<T> {
    timeoutMs(durationMs: number): ZLinkWorkerCall<T>;
    submit(signal?: AbortSignal): Promise<T>;
}
```

### 2.24 @zlink-systems/nestjs: createZLinkDynamicModule - ZLINK_LOCATION_RUNTIME_QUERY

```ts
export declare function createZLinkDynamicModule(registration: ZLinkFrameworkRegistration): DynamicModule;

export declare const ZLINK_ACTOR_CLIENT: unique symbol;

export declare const ZLINK_ACTOR_MANAGER: unique symbol;

export declare const ZLINK_ACTOR_SPOT_HANDLE_RESOLVER: unique symbol;

export declare const ZLINK_BOUND_SESSION_FACTORY: unique symbol;

export declare const ZLINK_CHANNEL_CLIENT: unique symbol;

export declare const ZLINK_CHANNEL_RUNTIME_OPTIONS: unique symbol;

export declare const ZLINK_DRAIN_CONTROL: unique symbol;

export declare const ZLINK_FANOUT_CLIENT: unique symbol;

export declare const ZLINK_FRAMEWORK_REGISTRATION: unique symbol;

export declare const ZLINK_FRAMEWORK_RUNTIME: unique symbol;

export declare const ZLINK_LOCATION_RUNTIME_QUERY: unique symbol;
```

### 2.25 @zlink-systems/nestjs: ZLINK_MESSAGE_METADATA_POLICY - zlinkEntrySpotActorSendHandler

```ts
export declare const ZLINK_MESSAGE_METADATA_POLICY: unique symbol;

export declare const ZLINK_NEST_HANDLER_GROUP: unique symbol;

export declare const ZLINK_ROUTE_CLIENT: unique symbol;

export declare const ZLINK_RUNTIME_EVENT_PUBLISHER: unique symbol;

export declare const ZLINK_SPOT_HANDLE_RESOLVER: unique symbol;

export declare const ZLINK_SPOT_MANAGER: unique symbol;

export declare const ZLINK_SPOT_OUTBOUND: unique symbol;

export declare const ZLINK_SPOT_PUBLISHER_CLIENT: unique symbol;

export declare function zlinkDiscoverProviders(rootDir: string, options?: ZLinkNestProviderDiscoveryOptions): Provider[];

export declare class ZLinkDrainHealthIndicator {
    private readonly drain;
    constructor(drain: ZLinkDrainControl);
    isHealthy(key?: string): Promise<Record<string, {
        readonly status: 'up';
    }>>;
}

export declare function zlinkEntrySpotActorRequestHandler<TEntrySpot extends ZLinkEntrySpot, TActor extends ZLinkActor>(options: ZLinkNestEntrySpotActorRequestHandlerOptions<TEntrySpot, TActor>): ClassDecorator;

export declare function zlinkEntrySpotActorSendHandler<TEntrySpot extends ZLinkEntrySpot, TActor extends ZLinkActor>(options: ZLinkNestEntrySpotActorSendHandlerOptions<TEntrySpot, TActor>): ClassDecorator;
```

### 2.26 @zlink-systems/nestjs: zlinkEntrySpotPacketHandler - ZLinkNestEntrySpotActorRequestHandlerOptions

```ts
export declare function zlinkEntrySpotPacketHandler<TEntrySpot extends ZLinkEntrySpot>(options: ZLinkNestEntrySpotPacketHandlerOptions<TEntrySpot>): ClassDecorator;

export declare function zlinkEntrySpotSubscriptionHandler<TEntrySpot extends ZLinkEntrySpot>(options: ZLinkNestEntrySpotSubscriptionHandlerOptions<TEntrySpot>): ClassDecorator;

export declare function zlinkFramework(): ZLinkNestFrameworkOptionsBuilder;

export declare function zlinkHandler(groupName: string, kind: ZLinkNestHandlerKind, packetName?: string, options?: ZLinkNestHandlerOptions): ClassDecorator;

export declare function zlinkModule(metadata: ZLinkNestModuleMetadata): ClassDecorator;
export declare function zlinkModule(roleRoot: ZLinkNestModuleRoleRoot, metadata: ModuleMetadata): ClassDecorator;

export declare class ZLinkModule {
    static forRoot(options?: ZLinkModuleOptions): DynamicModule;
    static forRootFactory(options: ZLinkModuleFactoryOptions): DynamicModule;
}

export interface ZLinkModuleFactoryOptions {
    readonly useFactory: (...args: unknown[]) => ZLinkModuleOptions | Promise<ZLinkModuleOptions>;
    readonly inject?: readonly InjectionToken[];
    readonly imports?: ModuleMetadata['imports'];
}

export interface ZLinkModuleOptions {
    readonly [ZLINK_MODULE_OPTIONS_BRAND]: true;
}

export declare class ZLinkMonitoringModule {
    static forRoot(): DynamicModule;
}

export interface ZLinkNestClientServerChannelBuilder extends ZLinkNestFrameworkOptionsBuilder {
    enableServer(bind: string | undefined): this;
    routingId(routingId: string | undefined): this;
    configureServerSocket(): ZLinkSocketConfig;
    configureClientSocket(): ZLinkSocketConfig;
    enableClient(endpoint?: string | readonly string[]): this;
    addRequestHandler(packetName: string, handlerType: Type): this;
    addSendHandler(packetName: string, handlerType: Type): this;
    addHandlerGroup(groupName: string): this;
}

export interface ZLinkNestCodecRegistryBuilder extends ZLinkNestFrameworkOptionsBuilder {
    use(extension: ZLinkCodecExtension): this;
}

export interface ZLinkNestEntrySpotActorRequestHandlerOptions<TEntrySpot extends ZLinkEntrySpot, TActor extends ZLinkActor> {
    readonly entrySpot: ZLinkNestTypeResolver<TEntrySpot>;
    readonly actor: ZLinkNestTypeResolver<TActor>;
    readonly packetName: string;
    readonly methodName?: string;
}
```

### 2.27 @zlink-systems/nestjs: ZLinkNestEntrySpotActorSendHandlerOptions - ZLinkNestProviderDiscoveryRoot

```ts
export interface ZLinkNestEntrySpotActorSendHandlerOptions<TEntrySpot extends ZLinkEntrySpot, TActor extends ZLinkActor> {
    readonly entrySpot: ZLinkNestTypeResolver<TEntrySpot>;
    readonly actor: ZLinkNestTypeResolver<TActor>;
    readonly packetName: string;
    readonly methodName?: string;
}

export interface ZLinkNestEntrySpotPacketHandlerOptions<TEntrySpot extends ZLinkEntrySpot> {
    readonly entrySpot: ZLinkNestTypeResolver<TEntrySpot>;
    readonly packetName?: string;
}

export interface ZLinkNestEntrySpotSubscriptionHandlerOptions<TEntrySpot extends ZLinkEntrySpot> {
    readonly entrySpot: ZLinkNestTypeResolver<TEntrySpot>;
    readonly topic: string;
}

export interface ZLinkNestFanoutChannelBuilder extends ZLinkNestFrameworkOptionsBuilder {
    enablePublisher(bind: string | undefined): this;
    enableSubscriber(endpoint?: string | readonly string[]): this;
    addPublishHandler(packetName: string, handlerType: Type): this;
    addHandlerGroup(groupName: string): this;
}

export type ZLinkNestFrameworkAdditionalOptions = Omit<ZLinkFrameworkRegistrationOptions, 'channels' | 'routeChannels' | 'streamNodes' | 'spotNodes' | 'codecs'>;

export interface ZLinkNestFrameworkOptionsBuilder {
    options(options: ZLinkNestFrameworkAdditionalOptions): this;
    codecs(): ZLinkNestCodecRegistryBuilder;
    configureDispatch(): ZLinkDispatchOptionsBuilder;
    useInMemoryLocationStores(): this;
    addLocationStore(store: ZLinkLocationStore): this;
    addActorTransferAdapter<TActor extends ZLinkActor>(actorType: Type<TActor>, adapterType: Type<ZLinkActorTransferAdapter<TActor>>): this;
    setActorTransferForwardWindow(timeoutMs: number): this;
    configureStreamCompression(): ZLinkStreamCompressionBuilder;
    configureLocations(): ZLinkLocationOptions;
    addClientServerChannel(name: string): ZLinkNestClientServerChannelBuilder;
    addFanoutChannel(name: string): ZLinkNestFanoutChannelBuilder;
    addRouteMeshChannel(name: string): ZLinkNestRouterMeshBuilder;
    addSpotMesh(name: string): ZLinkNestSpotNodeBuilder;
    addStreamNode(name: string): ZLinkNestStreamNodeBuilder;
    build(): ZLinkModuleOptions;
}

export type ZLinkNestHandlerKind = 'request' | 'send' | 'publish';

export interface ZLinkNestHandlerOptions {
    readonly methodName?: string;
    readonly decodePayload?: (payload: Buffer, context: ZLinkRequestContext | ZLinkSendContext | ZLinkRouteRequestContext | ZLinkRouteSendContext | ZLinkPublishContext) => unknown;
    readonly encodeResult?: (result: unknown, context: ZLinkRequestContext | ZLinkRouteRequestContext) => unknown;
}

export interface ZLinkNestModuleMetadata extends ModuleMetadata {
    readonly providerDiscovery?: readonly ZLinkNestProviderDiscoveryRoot[];
}

export type ZLinkNestModuleRoleRoot = string;

export interface ZLinkNestProviderDiscoveryOptions {
    readonly recursive?: boolean;
}

export type ZLinkNestProviderDiscoveryRoot = string | {
    readonly rootDir: string;
    readonly options?: ZLinkNestProviderDiscoveryOptions;
};
```

### 2.28 @zlink-systems/nestjs: ZLinkNestRouterMeshBuilder - zlinkRuntimeEventHandler

```ts
export interface ZLinkNestRouterMeshBuilder extends ZLinkNestFrameworkOptionsBuilder {
    enableRouter(endpoint: string | undefined): this;
    enableClient(): this;
    routingId(routingId: string | undefined): this;
    configureSocket(): ZLinkSocketConfig;
    connect(endpoint: string | readonly string[] | undefined): this;
    addSendHandler(packetName: string, handlerType: Type): this;
    addRequestHandler(packetName: string, handlerType: Type): this;
    addHandlerGroup(groupName: string): this;
}

export interface ZLinkNestSpotActorRequestHandlerOptions<TSpot extends ZLinkSpot, TActor extends ZLinkActor> {
    readonly spot: ZLinkNestTypeResolver<TSpot>;
    readonly actor: ZLinkNestTypeResolver<TActor>;
    readonly packetName: string;
    readonly methodName?: string;
}

export interface ZLinkNestSpotActorSendHandlerOptions<TSpot extends ZLinkSpot, TActor extends ZLinkActor> {
    readonly spot: ZLinkNestTypeResolver<TSpot>;
    readonly actor: ZLinkNestTypeResolver<TActor>;
    readonly packetName: string;
    readonly methodName?: string;
}

export interface ZLinkNestSpotNodeBuilder extends ZLinkNestFrameworkOptionsBuilder {
    routingId(routingId: string | undefined): this;
    enableRouter(bind: string | undefined, routingId?: string, connect?: string | readonly string[]): this;
    connectRouter(endpoint: string): this;
    connectRouter(peerRid: string, endpoint: string): this;
    enablePubSub(bind: string | undefined, routingId?: string, connect?: string | readonly string[]): this;
    connectPeerPub(endpoint: string): this;
    configureEntrySpot(options: ZLinkEntrySpotOptions): this;
    addEntrySpot<TEntrySpot extends ZLinkEntrySpot>(entrySpotType: Type<TEntrySpot>): this;
    addSpotFactory<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): this;
    actorFactory(actorType: string, factoryType: Type): this;
    useDrainPolicy(policy: import('@zlink-systems/framework').ZLinkSpotDrainPolicy): this;
}

export interface ZLinkNestSpotPacketHandlerOptions<TSpot extends ZLinkSpot> {
    readonly spot: ZLinkNestTypeResolver<TSpot>;
    readonly packetName?: string;
}

export interface ZLinkNestSpotSubscriptionHandlerOptions<TSpot extends ZLinkSpot> {
    readonly spot: ZLinkNestTypeResolver<TSpot>;
    readonly topic: string;
}

export interface ZLinkNestSpotTimerHandlerOptions<TSpot extends ZLinkSpot = ZLinkSpot> {
    readonly spot?: ZLinkNestTypeResolver<TSpot>;
    readonly entrySpot?: ZLinkNestTypeResolver<ZLinkEntrySpot>;
    readonly name?: string;
    readonly periodMs?: number;
    readonly options?: ZLinkTimerOptions;
}

export interface ZLinkNestStreamNodeBuilder extends ZLinkNestFrameworkOptionsBuilder {
    bind(endpoint: string | undefined): this;
    setTlsServer(certificatePath: string, keyPath: string, requireClientCertificate?: boolean): this;
    registerSession<TSession extends ZLinkSession>(sessionType: Type<TSession> | Type<ZLinkSessionFactory<TSession>>): this;
}

export type ZLinkNestTypeResolver<T> = Type<T> | (() => Type<T>);

export declare function zlinkPublishHandler(groupName: string, packetName?: string, options?: ZLinkNestHandlerOptions): ClassDecorator;

export declare function zlinkRequestHandler(groupName: string, packetName?: string, options?: ZLinkNestHandlerOptions): ClassDecorator;

export declare function zlinkRuntimeEventHandler(): ClassDecorator;
```

### 2.29 @zlink-systems/nestjs: zlinkSendHandler - zlinkSpotTimerHandler

```ts
export declare function zlinkSendHandler(groupName: string, packetName?: string, options?: ZLinkNestHandlerOptions): ClassDecorator;

export declare function zlinkSpotActorRequestHandler<TSpot extends ZLinkSpot, TActor extends ZLinkActor>(options: ZLinkNestSpotActorRequestHandlerOptions<TSpot, TActor>): ClassDecorator;

export declare function zlinkSpotActorSendHandler<TSpot extends ZLinkSpot, TActor extends ZLinkActor>(options: ZLinkNestSpotActorSendHandlerOptions<TSpot, TActor>): ClassDecorator;

export declare function zlinkSpotPacketHandler<TSpot extends ZLinkSpot>(options: ZLinkNestSpotPacketHandlerOptions<TSpot>): ClassDecorator;

export declare function zlinkSpotSubscriptionHandler<TSpot extends ZLinkSpot>(options: ZLinkNestSpotSubscriptionHandlerOptions<TSpot>): ClassDecorator;

export declare function zlinkSpotTimerHandler<TSpot extends ZLinkSpot = ZLinkSpot>(options?: ZLinkNestSpotTimerHandlerOptions<TSpot>): ClassDecorator;
```

## 3. 검증

검증 범위는 [Node.js 회귀 검증 matrix](../../../../node/internals/regression-test-matrix.ko.md)가
소유한다. 배포 package와 이 catalog의 export 이름 집합 및 declaration 차이는 문서 회귀 검증에서
AST로 비교한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../node/README.ko.md) | [이전: 시스템 구조](01-system-structure.ko.md) | [다음: Stream Connector](../typescript/03-stream-connector.ko.md)
<!-- framework-adapter-nav:bottom:end -->
