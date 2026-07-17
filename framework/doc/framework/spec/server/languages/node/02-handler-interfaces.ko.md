<!-- framework-adapter-nav:start -->
[문서 목록](../../../../node/README.ko.md) | [이전: 시스템 구조](01-system-structure.ko.md) | [다음: Stream Connector](../../../stream-connector/languages/typescript/03-stream-connector.ko.md)
<!-- framework-adapter-nav:end -->

# ZLink Framework Node.js 공개 interface

## 1. 목적과 계약 소유권

이 문서는 ZLink Framework 10.0.0의 `@zlink-systems/framework`와 `@zlink-systems/nestjs`
package root가 내보내는 공개 TypeScript declaration 전체를 고정한다.
대상 독자는 두 server package의 API를 구현하거나 package declaration parity를 검토하는 개발자다.
HTTP client와 Stream Connector package는 이 문서의 범위가 아니다.

기능의 의미와 동작 규칙은 [공통 스펙](../../../README.ko.md)이 소유한다. 이 문서는 Node framework
handler의 정확한 public interface만 정의하며 사용법과 예제는 포함하지 않는다. Stream Connector의 공개 계약은
[별도 문서](../../../stream-connector/languages/typescript/03-stream-connector.ko.md)가 소유한다.

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
    readonly meshName: string;
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
    sendToActor(meshName: string, actor: ActorRef, message: unknown): ZLinkActorSendCall;
    requestToActor(meshName: string, actor: ActorRef, request: unknown): ZLinkActorRequestCall;
}

export interface ZLinkActorContext {
    readonly meshName: string;
    readonly spotRid?: RoutingId;
    readonly handlers: ZLinkActorHandlerRegistry;
    readonly boundSession: ZLinkBoundSession;
    joinSpot(spotRid: RoutingId, request: unknown): ZLinkActorJoinSpotCall;
    joinEntrySpot(nodeRid: RoutingId, request: unknown): ZLinkActorJoinEntrySpotCall;
    leaveSpot(signal?: AbortSignal): Promise<void>;
}

export interface ZLinkActorDirectory {
    find(meshName: string, actorId: string, signal?: AbortSignal): Promise<ActorRef | undefined>;
    ensure(meshName: string, actorId: string, createRequest: unknown, placement?: ZLinkActorPlacement, signal?: AbortSignal): Promise<ActorRef>;
}

export interface ZLinkActorFactory {
    create(actorId: string, context: ZLinkActorContext): Promise<ZLinkActor>;
}

export interface ZLinkActorHandlerRegistry {
    addHandler<THandler>(handlerType: Type<THandler>): this;
}

export interface ZLinkActorJoinCall<TSelf> {
    timeout(timeoutMs: number): TSelf;
    submit<TReply = unknown>(signal?: AbortSignal): Promise<ZLinkActorJoinResult<TReply>>;
    yield<TReply = unknown>(signal?: AbortSignal): Promise<ZLinkActorJoinResult<TReply>>;
}

export interface ZLinkActorJoinEntrySpotCall extends ZLinkActorJoinCall<ZLinkActorJoinEntrySpotCall> {
}

export type ZLinkActorJoinResult<TReply = unknown> = {
    readonly status: 'accepted';
    readonly actor: ActorRef;
    readonly reply: TReply;
} | {
    readonly status: 'rejected';
    readonly rejection: TReply;
};

export interface ZLinkActorJoinSpotCall extends ZLinkActorJoinCall<ZLinkActorJoinSpotCall> {
}
```

### 2.3 @zlink-systems/framework: ZLinkActorLocation - ZLinkActorSpotHandleResolver

```ts
export interface ZLinkActorLocationFilter {
    readonly actorType?: string;
    readonly nodeRid?: RoutingId;
    readonly spotRid?: RoutingId;
    readonly locationKind?: ZLinkSpotKind;
}

export interface ZLinkActorManager {
    create(meshName: string, actorId: string, actorType: string, signal?: AbortSignal): Promise<ActorRef>;
    create(meshName: string, actorId: string, actorType: string, createRequest: unknown, signal?: AbortSignal): Promise<ActorRef>;
    find(meshName: string, actorId: string, signal?: AbortSignal): Promise<ActorRef | undefined>;
    getOrCreate(meshName: string, actorId: string, actorType: string, signal?: AbortSignal): Promise<ActorRef>;
    getOrCreate(meshName: string, actorId: string, actorType: string, createRequest: unknown, signal?: AbortSignal): Promise<ActorRef>;
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
    yield<TReply>(signal?: AbortSignal): Promise<TReply>;
}

export interface ZLinkActorSendCall {
    metadata(key: string, value: string): this;
    trySubmit(): ZLinkSubmitResult;
    submit(signal?: AbortSignal): Promise<ZLinkSubmitResult>;
}

export interface ZLinkActorSpotHandleResolver {
    resolveActorSpotHandle(meshName: string, actorId: string, signal?: AbortSignal): Promise<SpotHandle | undefined>;
}
```

### 2.4 @zlink-systems/framework: ZLinkActorTransferAdapter - ZLinkCodecRegistryBuilder

```ts
export interface ZLinkActorTransferAdapter<TActor extends ZLinkActor> {
    transferOut(actor: TActor): Promise<ZLinkMessage>;
    transferIn(actorId: string, context: ZLinkActorContext, state: ZLinkMessage): Promise<TActor>;
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
    sendToChannel(meshName: string, channelName: string, message: unknown): ZLinkSendCall;
    requestToChannel(meshName: string, channelName: string, request: unknown): ZLinkRequestCall;
}

export interface ZLinkMeshPeerConnection {
    readonly endpoint: string;
    readonly expectedRoutingId?: RoutingId;
}

export interface ZLinkMeshPeerConnections {
    connect(endpoint: string): void;
    connect(expectedRoutingId: RoutingId, endpoint: string): void;
    disconnect(endpoint: string): void;
    listConnections(): readonly ZLinkMeshPeerConnection[];
}

export interface ZLinkMeshChannelBuilder {
    setWeight(weight: number): this;
    addSendHandler<TMessage>(handlerType: Type<ZLinkSendHandler<TMessage>>): this;
    addRequestHandler<TRequest, TReply>(handlerType: Type<ZLinkRequestHandler<TRequest, TReply>>): this;
}

export interface ZLinkMeshNodeSocketConfig {
    maxMessageSize: number;
    sendHighWaterMark: number;
    receiveHighWaterMark: number;
    receiveTimeoutMs?: number;
    sendTimeoutMs?: number;
}

export interface ZLinkSpotPublisherConfig {
    noDrop: boolean;
}

export interface ZLinkMeshNodeBuilder {
    channelName(channelName: string): ZLinkMeshChannelBuilder;
    listen(endpoint: string): this;
    routingId(routingId: RoutingId): this;
    useAllocatedRoutingId(slotCount: number, routingIdPrefix?: string): this;
    setRoutingIdAllocationGroup(groupName: string): this;
    configureRouterSocket(): ZLinkMeshNodeSocketConfig;
    configureSpotPublisher(): ZLinkSpotPublisherConfig;
    peerConnections(): ZLinkMeshPeerConnections;
    setDefaultRequestTimeout(timeoutMs: number): this;
    addRouteSendHandler<TMessage>(handlerType: Type<ZLinkRouteSendHandler<TMessage>>): this;
    addRouteRequestHandler<TRequest, TReply>(handlerType: Type<ZLinkRouteRequestHandler<TRequest, TReply>>): this;
    configureEntrySpot(options: ZLinkEntrySpotOptions): this;
    addEntrySpot<TEntrySpot extends ZLinkEntrySpot>(entrySpotType: Type<TEntrySpot>): this;
    addSpotFactory<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): this;
    actorFactory(actorType: string, factoryType: Type): this;
    addActorTransferAdapter<TActor extends ZLinkActor>(
        actorType: string,
        adapterType: Type<ZLinkActorTransferAdapter<TActor>>): this;
    useDrainPolicy(policy: ZLinkMeshNodeDrainPolicy): this;
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
    readonly meshName?: string;
    readonly topic?: string;
}

export interface ZLinkLocationOptionValues {
    readonly heartbeatIntervalMs: number;
    readonly ownerLeaseTtlMs: number;
    readonly pollingIntervalMs: number;
    readonly storeFailureGraceMs: number;
    readonly routingIdFencingMarginMs: number;
    readonly ownerLeaseRenewTimeoutMs: number;
}

export declare const zlinkDefaultLocationOptions: Readonly<ZLinkLocationOptionValues>;

export interface ZLinkDiagnosticsOptions {
    messageFlow: ZLinkMessageFlowLogMode;
    sampleRate: number;
    includeMessageSizes: boolean;

    logFile?: string;

    label?: string;
}

export type ZLinkMessageSurface =
    | "node" | "channel" | "spot" | "logical_multicast"
    | "actor" | "stream" | "classic_fanout" | "actor_transfer";
export type ZLinkMessageKind =
    | "send" | "request" | "response" | "error" | "publish" | "control";
export type ZLinkMessageFlowOutcome =
    | "succeeded" | "failed" | "backpressured" | "dropped" | "cancelled" | "shutdown";
export type ZLinkDispatchErrorReason =
    | "no_handler" | "decode_error" | "handler_exception" | "invalid_frame"
    | "reply_path_missing" | "unexpected_reply" | "backpressure" | "stale_target" | "shutdown";
export type ZLinkDispatchErrorAction = "reply_error" | "fail_caller" | "drop";

export interface ZLinkDispatchOptions {
    readonly unhandled: ZLinkUnhandledDispatchOptions;
    readonly diagnostics: ZLinkDiagnosticsOptions;
}

export interface ZLinkDispatchOptionsBuilder {
    setMessageFlowObserver(observerType: Type<ZLinkMessageFlowObserver>): this;
    setRuntimeErrorSink(sinkType: Type<ZLinkRuntimeErrorSink>): this;

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

export interface ZLinkEntrySpot<TActor extends ZLinkActor = ZLinkActor> extends ZLinkSpotActorLifecycle {
    readonly context: ZLinkEntrySpotContext<TActor>;
    configure?(): void;
    onInitialize?(): Promise<void>;
    onClosing?(): Promise<void>;
    onCreateActor?(actor: ZLinkActorMembership, createRequest: ZLinkMessage): Promise<void>;
}

export interface ZLinkEntrySpotActorRequestHandler<TActor extends ZLinkActor, TRequest, TReply> {
    handle(actor: TActor, context: ZLinkSpotActorRequestContext, request: TRequest): Promise<TReply>;
}

export interface ZLinkEntrySpotActorSendHandler<TActor extends ZLinkActor, TMessage> {
    handle(actor: TActor, context: ZLinkSpotActorSendContext, message: TMessage): Promise<void>;
}

export interface ZLinkEntrySpotContext<TActor extends ZLinkActor = ZLinkActor, TEntrySpot extends ZLinkEntrySpot<TActor> = ZLinkEntrySpot<TActor>> extends ZLinkSpotCommonContext<TActor, TEntrySpot> {
    destroyActor(actor: ActorRef, signal?: AbortSignal): Promise<void>;
}

export interface ZLinkEntrySpotOptions {
    routingId?: RoutingId;
}

export interface ZLinkFanoutChannelBuilder {
    enablePublisher(endpoint: string): this;
    routingId(routingId: string): this;
    useAllocatedRoutingId(slotCount: number, routingIdPrefix?: string): this;
    setRoutingIdAllocationGroup(groupName: string): this;
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
    readonly code: number;
    constructor(kind: ZLinkFrameworkErrorKind, message: string, isRetriable?: boolean, cause?: unknown);
    readonly isRetriable: boolean;
}

export type ZLinkRequestFailureReason = 'timeout' | 'cancelled' | 'shutdown';

export declare class ZLinkRequestFailureError extends Error {
    readonly reason: ZLinkRequestFailureReason;
    constructor(reason: ZLinkRequestFailureReason, message: string, cause?: unknown);
}

export interface ZLinkFrameworkOptions {
    codecs(): ZLinkCodecRegistryBuilder;

    configureWorker(options: ZLinkWorkerOptions): this;
    configureDispatch(): ZLinkDispatchOptionsBuilder;
    addLocationStore(store: ZLinkLocationStore): this;
    setActorTransferTimeout(timeoutMs: number): this;
    setActorTransferForwardWindow(timeoutMs: number): this;
    configureLocations(): ZLinkLocationOptions;
    configureStreamCompression(): ZLinkStreamCompressionBuilder;
    addRouteMesh(meshName: string): ZLinkMeshNodeBuilder;
    addFanoutChannel(name: string): ZLinkFanoutChannelBuilder;
    addStreamNode(name: string): ZLinkStreamNodeBuilder;
}

export interface ZLinkHandlerContext {
    readonly channelName?: string;
    readonly packetName?: string;
    readonly contentType?: string;
    readonly connectionAborted?: AbortSignal;
    readonly metadata: ZLinkMessageMetadata;
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
    Fanout = 2
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

export interface ZLinkAllocatedRoutingId {
    readonly groupName: string;
    readonly slot: number;
    readonly memberRoutingIds: ReadonlyMap<string, RoutingId>;
}

export interface ZLinkAllocatedRoutingIdProvider {
    waitForReadyAllocation(groupName: string, signal?: AbortSignal): Promise<ZLinkAllocatedRoutingId>;
}

export declare const ZLINK_ALLOCATED_ROUTING_ID_PROVIDER: unique symbol;

export interface ZLinkRoutingIdSlotAllocationMember {
    readonly meshName: string;
    readonly routingIdPrefix: string;
}

export interface ZLinkRoutingIdSlotAcquireRequest {
    readonly groupName: string;
    readonly members: readonly ZLinkRoutingIdSlotAllocationMember[];
    readonly slotCount: number;
    readonly ownerId: string;
    readonly leaseTtlMs: number;
}

export interface ZLinkRoutingIdSlotAllocation {
    readonly slot: number;
    readonly owner: ZLinkLocationOwnerToken;
    readonly leaseExpiresAt: Date;
    readonly storeNow: Date;
}

export type ZLinkRoutingIdSlotAcquireResult =
    | { readonly kind: 'acquired'; readonly allocation: ZLinkRoutingIdSlotAllocation }
    | { readonly kind: 'groupExhausted' }
    | {
        readonly kind: 'groupConfigurationMismatch';
        readonly expectedMembers: readonly ZLinkRoutingIdSlotAllocationMember[];
        readonly expectedSlotCount: number;
        readonly actualMembers: readonly ZLinkRoutingIdSlotAllocationMember[];
        readonly actualSlotCount: number;
      }
    | { readonly kind: 'identityModeConflict' };

export type ZLinkRoutingIdSlotReleaseResult = 'released' | 'ignoredStale';

export interface ZLinkRoutingIdSlotAllocationSnapshot {
    readonly groupName: string;
    readonly members: readonly ZLinkRoutingIdSlotAllocationMember[];
    readonly slotCount: number;
    readonly allocations: readonly ZLinkRoutingIdSlotAllocation[];
    readonly storeNow: Date;
}

export interface ZLinkRoutingIdSlotAllocationStore {
    acquireRoutingIdSlot(request: ZLinkRoutingIdSlotAcquireRequest, signal?: AbortSignal): Promise<ZLinkRoutingIdSlotAcquireResult>;
    releaseRoutingIdSlot(groupName: string, slot: number, owner: ZLinkLocationOwnerToken, signal?: AbortSignal): Promise<ZLinkRoutingIdSlotReleaseResult>;
    listRoutingIdSlots(groupName: string, signal?: AbortSignal): Promise<ZLinkRoutingIdSlotAllocationSnapshot>;
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
    readonly event: ZLinkLocationRuntimeEventKind.StoreFailure | ZLinkLocationRuntimeEventKind.StoreRecovered;
});

export declare enum ZLinkLocationRuntimeEventKind {
    StatusChanged = 0,
    TopologyChanged = 1,
    ServiceSummaryChanged = 2,
    StoreFailure = 3,
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

### 2.10 @zlink-systems/framework: ZLinkLocationTopologyEntry - ZLinkMessageFlowEvent

```ts
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
    readonly eventId: "zlink.message_flow" | "zlink.dispatch_error";
    readonly timestamp: Date;
    readonly phase?: ZLinkMessageFlowPhase;
    readonly outcome: ZLinkMessageFlowOutcome;
    readonly surface: ZLinkMessageSurface;
    readonly messageKind: ZLinkMessageKind;
    readonly reason?: ZLinkDispatchErrorReason;
    readonly action?: ZLinkDispatchErrorAction;
    readonly packetName?: string;
    readonly meshName?: string;
    readonly channelName?: string;
    readonly topic?: string;
    readonly correlationId?: string;
    readonly sourceRid?: RoutingId;
    readonly targetRid?: RoutingId;
    readonly flowId?: string;
    readonly flowOrigin?: import('../Eventing/Contracts').ZLinkFlowOrigin;
    readonly spotRid?: RoutingId;
    readonly actorId?: string;
    readonly messageSizeBytes?: number;
    readonly durationSeconds?: number;
    readonly remoteSnapshotCount?: bigint;
    readonly remoteAdmittedCount?: bigint;
    readonly remoteDroppedCount?: bigint;
    readonly localSnapshotCount?: bigint;
    readonly localAdmittedCount?: bigint;
    readonly localDroppedCount?: bigint;
    readonly targetCount?: bigint;
    readonly dropCount?: bigint;
}

export interface ZLinkRuntimeErrorEvent {
    readonly eventId: "zlink.runtime_error";
    readonly timestamp: Date;
    readonly kind: "observer_failed";
    readonly source: "message_flow_observer";
    readonly reason: string;
}

export interface ZLinkRuntimeErrorSink {
    onRuntimeError(error: ZLinkRuntimeErrorEvent): Promise<void> | void;
}
```

### 2.11 @zlink-systems/framework: ZLinkMessageFlowLogMode - ZLinkMeters

```ts
export declare enum ZLinkMessageFlowLogMode {
    Off = "off",
    ErrorsOnly = "errorsOnly",
    KeyTransitions = "keyTransitions",
    Verbose = "verbose"
}

export interface ZLinkMessageFlowObserver {
    onMessageFlow(flow: ZLinkMessageFlowEvent): Promise<void> | void;
}

export declare enum ZLinkMessageFlowPhase {
    Received = "received",
    Admitted = "admitted",
    Dispatched = "dispatched",
    Completed = "completed",
    Replied = "replied",
    Sent = "sent",
    ReplyReceived = "reply_received",
    Backpressured = "backpressured",
    Dropped = "dropped"
}

export declare function zlinkMessageMetadata(values: ReadonlyMap<string, string> | Readonly<Record<string, string>>): ZLinkMessageMetadata;

export interface ZLinkMessageMetadata {
    readonly values: ReadonlyMap<string, string>;
    find(key: string): string | undefined;
}

export declare const ZLinkMessageMetadataEmpty: ZLinkMessageMetadata;

export interface ZLinkMessageMetadataPolicy {
    canForwardSessionToActor(key: string): boolean;
    canForwardActorToSession(key: string): boolean;
}

export interface ZLinkMessageSerializer {
    canSerialize?(value: unknown, context: ZLinkSerializerSelectionContext): boolean;
    serialize<T>(value: T): ZLinkEncodedPayload;
    deserialize<T>(payload: ZLinkEncodedPayload, type: Type<T>): T;
}

export interface ZLinkMetadataPolicyBuilder {
    allowSessionToActor(key: string): this;
    allowActorToSession(key: string): this;
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

export interface ZLinkOwnerLeaseRenewalRequest {
    readonly ownerId: string;
    readonly nodeRid: RoutingId;
    readonly leaseTtlMs: number;
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
    metadata(key: string, value: string): this;
    metadata(metadata: ZLinkMessageMetadata): this;
    trySubmit(): ZLinkPublishResult;
    submit(signal?: AbortSignal): Promise<ZLinkPublishResult>;
}

export interface ZLinkLogicalMulticastDetail {
    readonly snapshotRemoteNodeCount: bigint;
    readonly admittedRemoteNodeCount: bigint;
    readonly droppedRemoteNodeCount: bigint;
    readonly snapshotLocalSpotCount: bigint;
    readonly admittedLocalSpotCount: bigint;
    readonly droppedLocalSpotCount: bigint;
}

export interface ZLinkPublishResult {
    readonly status: ZLinkSubmitStatus;
    readonly detail: ZLinkLogicalMulticastDetail;
}

export interface ZLinkPublishContext extends ZLinkHandlerContext {
    readonly topic: string;
    readonly source?: string;
}

export interface ZLinkPublishHandler<TMessage> {
    handle(message: TMessage, context: ZLinkPublishContext): Promise<void>;
}
```

### 2.14 @zlink-systems/framework: ZLinkRequest - ZLinkRouteMeshRuntimeOptions

```ts
export declare function ZLinkRequest(packetName?: string): MethodDecorator;

export interface ZLinkRequestCall {
    metadata(key: string, value: string): this;
    metadata(metadata: ZLinkMessageMetadata): this;
    timeout(timeoutMs: number): this;
    submit<TReply>(signal?: AbortSignal): Promise<TReply>;
    yield<TReply>(signal?: AbortSignal): Promise<TReply>;
}

export interface ZLinkRequestContext extends ZLinkHandlerContext {
}

export interface ZLinkRequestHandler<TRequest, TResponse> {
    handle(request: TRequest, context: ZLinkRequestContext): Promise<TResponse>;
}

export interface ZLinkRouteClient {
    sendToNode(meshName: string, targetNodeRid: RoutingId, message: unknown): ZLinkSendCall;
    requestToNode(meshName: string, targetNodeRid: RoutingId, request: unknown): ZLinkRequestCall;
    sendToChannel(meshName: string, channelName: string, message: unknown): ZLinkSendCall;
    requestToChannel(meshName: string, channelName: string, request: unknown): ZLinkRequestCall;
    sendToSpot(spot: SpotHandle, message: unknown): ZLinkSendCall;
    requestToSpot(spot: SpotHandle, request: unknown): ZLinkRequestCall;
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

export interface ZLinkRouteMeshRuntimeOptions {
    meshNode(meshName: string): ZLinkMeshNodeRuntimeOptions;
    channel(meshName: string, channelName: string): ZLinkMeshChannelRuntimeOptions;
}

export interface ZLinkMeshNodeRuntimeOptions {
    maxMessageSize: number;
}

export interface ZLinkMeshChannelRuntimeOptions {
    weight: number;
}
```

`maxMessageSize = 0`은 framework 상한 없음이다. Adapter는 이를 Core의
`ZLINK_OPT_MAXMSGSIZE = -1`로 변환하고 음수는 startup 설정 오류로 거부한다.

### 2.15 @zlink-systems/framework: ZLinkRouteRequestContext - ZLinkSendHandler

```ts
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
    metadata(key: string, value: string): this;
    metadata(metadata: ZLinkMessageMetadata): this;
    trySubmit(): ZLinkSubmitResult;
    submit(signal?: AbortSignal): Promise<ZLinkSubmitResult>;
}

export declare enum ZLinkSubmitStatus {
    Submitted = "submitted",
    Backpressured = "backpressured",
    TimedOut = "timedOut",
    TargetNotFound = "targetNotFound",
    RouteNotConnected = "routeNotConnected",
    Shutdown = "shutdown"
}

export interface ZLinkSubmitResult {
    readonly status: ZLinkSubmitStatus;
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
    Closed = "closed"
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

export interface ZLinkSpot<TActor extends ZLinkActor = ZLinkActor> extends ZLinkSpotActorLifecycle {
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

export interface ZLinkSpotActorLifecycle {
    onActorJoin(actor: ZLinkActorJoinRequest, request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse>;
    onJoinedActor(actor: ZLinkActorMembership): Promise<void>;
    onLeaveActor(actor: ZLinkActorMembership): Promise<void>;
    onDisconnectActor(actor: ZLinkActorMembership): Promise<void>;
}

export interface ZLinkActorMembership {
    readonly actor: ActorRef;
    readonly actorType: string;
    readonly membershipEpoch: bigint;
}

export interface ZLinkActorJoinRequest {
    readonly actor: ActorRef;
    readonly actorType: string;
    readonly expectedMembershipEpoch: bigint;
}

export interface ZLinkSpotActorReplyOptions {
    compress(enabled?: boolean): this;
}
```

### 2.18 @zlink-systems/framework: ZLinkSpotActorRequest - ZLinkMeshNodeDrainPolicy

```ts
export declare function ZLinkSpotActorRequest(packetName?: string): MethodDecorator;

export interface ZLinkSpotActorRequestContext extends ZLinkSpotActorSendContext {
    readonly reply: ZLinkSpotActorReplyOptions;
}

export interface ZLinkSpotActorRequestHandler<TActor extends ZLinkActor, TRequest, TReply> {
    handle(actor: TActor, context: ZLinkSpotActorRequestContext, request: TRequest): Promise<TReply>;
}

export declare function ZLinkSpotActorSend(packetName?: string): MethodDecorator;

export interface ZLinkSpotActorSendContext extends ZLinkHandlerContext {
    readonly metadata: ZLinkMessageMetadata;
}

export interface ZLinkSpotActorSendHandler<TActor extends ZLinkActor, TMessage> {
    handle(actor: TActor, context: ZLinkSpotActorSendContext, message: TMessage): Promise<void>;
}

export interface ZLinkSpotCommonContext<TActor extends ZLinkActor = ZLinkActor, TSpot = ZLinkSpot<TActor>> {
    readonly meshName: string;
    readonly spotRid: RoutingId;
    readonly nodeRid: RoutingId;
    readonly routingId: RoutingId;
    readonly handlers: ZLinkSpotHandlerRegistry;
    readonly outbound: ZLinkSpotOutbound;
    addTimer<THandler extends ZLinkSpotTimerHandler<TSpot>>(name: string, periodMs: number, handlerType: Type<THandler>, options?: ZLinkTimerOptions, signal?: AbortSignal): Promise<ZLinkTimer>;

    runCpuWorker<T>(work: (signal: AbortSignal) => T): ZLinkWorkerCall<T>;
    runIoWorker<T>(work: (signal: AbortSignal) => Promise<T>): ZLinkWorkerCall<T>;
}

export interface ZLinkSpotContext<TActor extends ZLinkActor = ZLinkActor, TSpot extends ZLinkSpot<TActor> = ZLinkSpot<TActor>> extends ZLinkSpotCommonContext<TActor, TSpot> {
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

export type ZLinkMeshNodeDrainPolicy = 'DrainNatural' | 'ReleaseAndRecreate';
```

### 2.19 @zlink-systems/framework: ZLinkSpotEvent - ZLinkSpotLocationStore

```ts
export type ZLinkSpotEvent = (ZLinkRuntimeEvent & {
    readonly event: ZLinkSpotEventKind.StatusChanged;
    readonly status: ZLinkMeshNodeSnapshot;
}) | (ZLinkRuntimeEvent & {
    readonly event: ZLinkSpotEventKind.PeersChanged;
    readonly peers: readonly ZLinkMeshPeerSnapshot[];
}) | (ZLinkRuntimeEvent & {
    readonly event: ZLinkSpotEventKind.TimerHandlerFailed | ZLinkSpotEventKind.TimerStoppedAfterUnhandledException;
    readonly timerDiagnostic: ZLinkSpotTimerDiagnostic;
});

export declare enum ZLinkSpotEventKind {
    StatusChanged = "statusChanged",
    PeersChanged = "peersChanged",
    TimerHandlerFailed = "timerHandlerFailed",
    TimerStoppedAfterUnhandledException = "timerStoppedAfterUnhandledException"
}

export interface ZLinkSpotHandleResolver {
    resolveSpotHandle(meshName: string, spotRid: RoutingId, signal?: AbortSignal): Promise<SpotHandle | undefined>;
}

export interface ZLinkSpotHandlerRegistry {
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

export interface ZLinkSpotLocationFilter {
    readonly meshName?: string;
    readonly spotType?: string;
    readonly nodeRid?: RoutingId;
    readonly spotKind?: ZLinkSpotKind;
}

```

### 2.20 @zlink-systems/framework: ZLinkSpotManager - ZLinkSpotPeerSource

```ts
export interface ZLinkSpotManager {
    create<TSpot extends ZLinkSpot>(meshName: string, spotType: Type<TSpot>, signal?: AbortSignal): Promise<ZLinkSpotCreateResult>;
    create<TSpot extends ZLinkSpot>(meshName: string, spotType: Type<TSpot>, request: ZLinkMessage, signal?: AbortSignal): Promise<ZLinkSpotCreateResult>;
    create<TSpot extends ZLinkSpot, TRequest>(meshName: string, spotType: Type<TSpot>, request: TRequest, signal?: AbortSignal): Promise<ZLinkSpotCreateResult>;
    getOrCreate<TSpot extends ZLinkSpot>(meshName: string, spotType: Type<TSpot>, spotRid: RoutingId, signal?: AbortSignal): Promise<ZLinkSpotCreateResult>;
    getOrCreate<TSpot extends ZLinkSpot>(meshName: string, spotType: Type<TSpot>, spotRid: RoutingId, request: ZLinkMessage, signal?: AbortSignal): Promise<ZLinkSpotCreateResult>;
    getOrCreate<TSpot extends ZLinkSpot, TRequest>(meshName: string, spotType: Type<TSpot>, spotRid: RoutingId, request: TRequest, signal?: AbortSignal): Promise<ZLinkSpotCreateResult>;
    find(meshName: string, spotRid: RoutingId, signal?: AbortSignal): Promise<ZLinkSpotInfo | null>;
    list(meshName: string, signal?: AbortSignal): Promise<readonly ZLinkSpotInfo[]>;
    close(meshName: string, spotRid: RoutingId, signal?: AbortSignal): Promise<boolean>;
}

export interface ZLinkMeshPeerSnapshot {
    readonly rid: RoutingId;
    readonly lifecycleGeneration: bigint;
    readonly descriptorRevision: bigint;
    readonly endpoint: string;
    readonly admissionState: string;
    readonly ready: boolean;
    readonly drainState: string;
    readonly channelNames: readonly string[];
    readonly lastFailure?: string;
}

export declare enum ZLinkMeshNodeState {
    Starting = 1,
    Serving = 2,
    Draining = 3,
    Drained = 4,
    ForceStopping = 5,
    Stopped = 6,
    Faulted = 7
}

export interface ZLinkMeshChannelSnapshot {
    readonly channelName: string;
    readonly localWeight: number;
    readonly readyMemberCount: bigint;
    readonly selectable: boolean;
}

export interface ZLinkLogicalMulticastSnapshot {
    readonly noDrop: boolean;
    readonly submitted: bigint;
    readonly backpressured: bigint;
    readonly dropped: bigint;
    readonly remoteSnapshotCount: bigint;
    readonly remoteAdmittedCount: bigint;
    readonly remoteDroppedCount: bigint;
    readonly localSnapshotCount: bigint;
    readonly localAdmittedCount: bigint;
    readonly localDroppedCount: bigint;
    readonly pendingAdmissionCount: bigint;
}

export interface ZLinkMeshClaimSnapshot {
    readonly applicationActive: boolean;
    readonly pendingApplicationWork: bigint;
    readonly infrastructureActive: boolean;
    readonly pendingInfrastructureWork: bigint;
}

export interface ZLinkLocationRuntimeSnapshot {
    readonly state: string;
    readonly lastSuccessAt?: Date;
    readonly lastFailureAt?: Date;
}

export interface ZLinkMeshDrainSnapshot {
    readonly state: ZLinkMeshNodeState;
    readonly deadline?: Date;
    readonly workSealed: boolean;
    readonly pendingRequestCount: bigint;
    readonly pendingTransferCount: bigint;
    readonly pendingStreamBarrierCount: bigint;
}

export interface ZLinkMeshNodeSnapshot {
    readonly meshName: string;
    readonly rid: RoutingId;
    readonly lifecycleGeneration: bigint;
    readonly descriptorRevision: bigint;
    readonly endpoint: string;
    readonly state: ZLinkMeshNodeState;
    readonly sequence: bigint;
    readonly observedAt: Date;
    readonly descriptorSources: readonly string[];
    readonly peers: readonly ZLinkMeshPeerSnapshot[];
    readonly channels: readonly ZLinkMeshChannelSnapshot[];
    readonly multicast: ZLinkLogicalMulticastSnapshot;
    readonly claims: ZLinkMeshClaimSnapshot;
    readonly location: ZLinkLocationRuntimeSnapshot;
    readonly drain: ZLinkMeshDrainSnapshot;
}

export interface ZLinkMeshRuntimeEvent {
    readonly identifier: string;
    readonly sequence: bigint;
    readonly timestamp: Date;
    readonly meshName: string;
    readonly sourceRid: RoutingId;
    readonly peerRid?: RoutingId;
    readonly lifecycleGeneration?: bigint;
    readonly descriptorRevision?: bigint;
    readonly channelName?: string;
    readonly claimDomain?: string;
    readonly messageKind?: string;
    readonly remoteSnapshotCount?: bigint;
    readonly remoteAdmittedCount?: bigint;
    readonly remoteDroppedCount?: bigint;
    readonly localSnapshotCount?: bigint;
    readonly localAdmittedCount?: bigint;
    readonly localDroppedCount?: bigint;
    readonly reason?: string;
    readonly state?: ZLinkMeshNodeState;
}

export type ZLinkMeshDrainResult =
    | { readonly kind: "drained" }
    | { readonly kind: "forceStopped"; readonly reason: string };

export interface ZLinkRouteMeshRuntime {
    snapshot(meshName: string): ZLinkMeshNodeSnapshot;
    observe(meshName: string, capacity?: number, signal?: AbortSignal): AsyncIterable<ZLinkMeshRuntimeEvent>;
    isReady(meshName: string): boolean;
    drain(meshName: string, deadlineMs?: number, signal?: AbortSignal): Promise<ZLinkMeshDrainResult>;
    awaitDrained(meshName: string, signal?: AbortSignal): Promise<ZLinkMeshDrainResult>;
}

export interface ZLinkSpotOutbound {
    sendToSpot(spot: SpotHandle, message: unknown): ZLinkSendCall;
    requestToSpot(spot: SpotHandle, request: unknown): ZLinkRequestCall;
    publish(channelName: string, topic: string, event: unknown): ZLinkPublishCall;
    sendToChannel(channelName: string, message: unknown): ZLinkSendCall;
    requestToChannel(channelName: string, request: unknown): ZLinkRequestCall;
}

export interface ZLinkSpotPacketHandler<TSpot, TMessage> {
    handle(spot: TSpot, message: TMessage, context: ZLinkHandlerContext): Promise<void>;
}

export declare enum ZLinkSpotPeerKind {
    RouteMesh = 1
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
    publish(meshName: string, channelName: string, topic: string, event: unknown): ZLinkPublishCall;
}

export declare function ZLinkSpotRequest(packetName?: string): MethodDecorator;

export interface ZLinkSpotRequestHandler<TSpot, TRequest, TReply> {
    handle(spot: TSpot, request: TRequest, context: ZLinkHandlerContext): Promise<TReply>;
}

export declare function ZLinkSpotSubscription(channelName: string, topic: string): MethodDecorator;

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
    enableActorDispatch(meshName: string): this;
    setTlsServer(certificatePath: string, keyPath: string, requireClientCertificate?: boolean): this;
    registerSession<TSession extends ZLinkSession>(sessionType: Type<TSession> | Type<ZLinkSessionFactory<TSession>>): this;
}

export declare function ZLinkStreamPacket(): MethodDecorator;

export declare function ZLinkStreamRaw(): MethodDecorator;

export declare enum ZLinkStreamSessionError {
    Internal = "internal",
    TransportError = "transportError"
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
    submit(signal?: AbortSignal): void;
    async(signal?: AbortSignal): Promise<T>;
    yield(signal?: AbortSignal): Promise<T>;
}

export interface ZLinkWorkerOptions {
    readonly minThreads: number;
    readonly maxThreads: number;
    readonly idleTimeoutMs: number;
    readonly maxQueueLength: number;
}
```

Request와 join의 result-bearing `submit()`은 공통 `Async` 의미이며 terminal reply 또는 결과까지 현재
owner turn을 유지한다. Worker call은 결과를 기다리지 않는 `submit()`, 결과까지 현재 turn을 유지하는
`async()`, 현재 turn을 반납하는 `yield()`를 별도로 제공한다.

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
    routingId(routingId: string | undefined): this;
    useAllocatedRoutingId(slotCount: number, routingIdPrefix?: string): this;
    setRoutingIdAllocationGroup(groupName: string): this;
    enableSubscriber(endpoint?: string | readonly string[]): this;
    addPublishHandler(packetName: string, handlerType: Type): this;
    addHandlerGroup(groupName: string): this;
}

export interface ZLinkNestFrameworkAdditionalOptions {
    readonly requestTimeoutMs?: number;
    readonly filters?: readonly Type<ZLinkHandlerFilter>[];
    readonly worker?: ZLinkWorkerOptions;
    readonly dispatch?: ZLinkDispatchOptions;
    readonly monitoring?: ZLinkMonitoringOptions;
    readonly metrics?: ZLinkMetricsOptions;
}

export interface ZLinkNestFrameworkOptionsBuilder {
    options(options: ZLinkNestFrameworkAdditionalOptions): this;
    codecs(): ZLinkNestCodecRegistryBuilder;
    configureDispatch(): ZLinkDispatchOptionsBuilder;
    addLocationStore(store: ZLinkLocationStore): this;
    setActorTransferTimeout(timeoutMs: number): this;
    setActorTransferForwardWindow(timeoutMs: number): this;
    configureStreamCompression(): ZLinkStreamCompressionBuilder;
    configureLocations(): ZLinkLocationOptions;
    addRouteMesh(name: string): ZLinkNestMeshNodeBuilder;
    addFanoutChannel(name: string): ZLinkNestFanoutChannelBuilder;
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

### 2.28 @zlink-systems/nestjs: ZLinkNestMeshNodeBuilder - zlinkRuntimeEventHandler

```ts
export interface ZLinkNestMeshNodeBuilder extends ZLinkNestFrameworkOptionsBuilder {
    channelName(name: string): ZLinkNestMeshChannelBuilder;
    listen(endpoint: string): this;
    routingId(routingId: string | undefined): this;
    useAllocatedRoutingId(slotCount: number, routingIdPrefix?: string): this;
    setRoutingIdAllocationGroup(groupName: string): this;
    configureRouterSocket(): ZLinkMeshNodeSocketConfig;
    configureSpotPublisher(): ZLinkSpotPublisherConfig;
    peerConnections(): ZLinkMeshPeerConnections;
    addSendHandler(packetName: string, handlerType: Type): this;
    addRequestHandler(packetName: string, handlerType: Type): this;
    configureEntrySpot(options: ZLinkEntrySpotOptions): this;
    addEntrySpot<TEntrySpot extends ZLinkEntrySpot>(entrySpotType: Type<TEntrySpot>): this;
    addSpotFactory<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): this;
    actorFactory(actorType: string, factoryType: Type): this;
    addActorTransferAdapter<TActor extends ZLinkActor>(
        actorType: string,
        adapterType: Type<ZLinkActorTransferAdapter<TActor>>): this;
    useDrainPolicy(policy: ZLinkMeshNodeDrainPolicy): this;
}

export interface ZLinkNestMeshChannelBuilder extends ZLinkNestFrameworkOptionsBuilder {
    setWeight(weight: number): this;
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
    enableActorDispatch(meshName: string): this;
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

### 2.30 @zlink-systems/nestjs: HTTP client 통합

서버 handler에서 HTTP 요청을 실행할 때는 이름으로 등록한 client를 주입받는다. 실행 scheduler는
framework가 소유하므로 handler는 명시적으로 `yield()`를 선택할 수 있고, 등록한 client는 Nest module의
수명과 함께 정리된다.

```ts
export interface ZLinkNamedHttpClientOptions {
    readonly name: string;
    readonly baseUrl: string;
    readonly configure?: (builder: ZLinkHttpClientBuilder) => void;
}

export interface ZLinkHttpClientModuleOptions {
    readonly imports: ModuleMetadata['imports'];
    readonly clients: readonly ZLinkNamedHttpClientOptions[];
}

export interface ZLinkServerHttpRequestBuilder extends ZLinkHttpRequestBuilder {
    submit(): void;
    yield<T>(): Promise<HttpResponse<T>>;
}

export interface ZLinkServerHttpClient extends Omit<ZLinkHttpClient, 'get' | 'post' | 'put' | 'delete' | 'patch' | 'head' | 'options'> {
    get(path: string): ZLinkServerHttpRequestBuilder;
    post(path: string): ZLinkServerHttpRequestBuilder;
    put(path: string): ZLinkServerHttpRequestBuilder;
    delete(path: string): ZLinkServerHttpRequestBuilder;
    patch(path: string): ZLinkServerHttpRequestBuilder;
    head(path: string): ZLinkServerHttpRequestBuilder;
    options(path: string): ZLinkServerHttpRequestBuilder;
}

export declare function zlinkHttpClientToken(name: string): InjectionToken;

export declare class ZLinkHttpClientModule {
    static forRoot(options: ZLinkHttpClientModuleOptions): DynamicModule;
}
```

## 2.31 목표 계약 적용 추적

정식 계약은 위 시그니처다. Source와 package 적용이 남은 항목은 gap 문서가 추적하며 계약을 축소하지 않는다.

| gap | 적용 작업 |
|---|---|
| [90 §12.28](../../../90-implementation-gap.ko.md) | `ZLinkStreamNodeBuilder.enableActorDispatch(meshName)`과 MeshName별 startup 검증이 없다. |
| [90 §12.33](../../../90-implementation-gap.ko.md) | `addRouteMesh(meshName)`과 MeshNode builder가 source·package에 없고 기존 분리 builder가 남아 있다. |

## 3. 검증

검증 범위는 [Node.js 회귀 검증 matrix](../../../../node/internals/regression-test-matrix.ko.md)가
소유한다. 배포 package와 이 catalog의 export 이름 집합 및 declaration 차이는 문서 회귀 검증에서
AST로 비교한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../node/README.ko.md) | [이전: 시스템 구조](01-system-structure.ko.md) | [다음: Stream Connector](../../../stream-connector/languages/typescript/03-stream-connector.ko.md)
<!-- framework-adapter-nav:bottom:end -->
