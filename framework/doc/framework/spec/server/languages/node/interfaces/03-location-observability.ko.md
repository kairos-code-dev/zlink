# Node.js Location, monitoring과 metrics 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Node.js 계약 목차](../README.ko.md)

이 문서는 ZLink Framework 11.0.0에서 `@zlink-systems/framework`와
`@zlink-systems/nestjs`가 내보내는 Location, monitoring과 metrics 관련 정확한 TypeScript declaration을 고정한다.
동작 의미는 [공통 스펙](../../../../README.ko.md)이 소유하며, 이 문서는 이름, generic, overload,
상속, member, parameter와 반환형만 정의한다.
Monitoring의 Channel, ClientServer와 placement weight는 정수 `number` `0..10000`이며 configuration과
descriptor 값을 변환 없이 제공한다.

## 1. Framework error, handler와 location event

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
    ActorCreateRejected = "actorCreateRejected",
    ObjectClientNotConfigured = "objectClientNotConfigured",
    MeshSelectionRequired = "meshSelectionRequired",
    MeshNotFound = "meshNotFound",
    InvalidConfiguration = "invalidConfiguration",
    AlreadySubmitted = "alreadySubmitted",
    ActorGenerationStale = "actorGenerationStale",
    ActorMoving = "actorMoving",
    DeadlineExceeded = "deadlineExceeded",
    PlacementCapacityExhausted = "placementCapacityExhausted",
    RoutingIdConflict = "routingIdConflict",
    SpotGenerationStale = "spotGenerationStale",
    SpotMoving = "spotMoving",
    RelocationDataLost = "relocationDataLost",
    SpotIdConflict = "spotIdConflict",
    RuntimeShutdown = "runtimeShutdown"
}

export declare const ZLINK_FRAMEWORK_ERROR_KIND_VALUES:
    Readonly<Record<ZLinkFrameworkErrorKind, number>>;

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
    addRelocationStore(store: ZLinkRelocationStore): this;
    setApplicationVersion(version: bigint): this;
    setMaintenanceWave(waveId: string): this;
    configureLocations(): ZLinkLocationOptions;
    configureNetwork(): ZLinkNetworkOptions;
    configureStreamCompression(): ZLinkStreamCompressionBuilder;
    addRouteMesh(meshName: string): ZLinkMeshNodeBuilder;
    addClientServerChannel(channelName: string): ZLinkClientServerChannelRoleBuilder;
    addFanoutChannel(name: string): ZLinkFanoutChannelBuilder;
    addStreamNode(name: string): ZLinkStreamNodeBuilder;
}

export interface ZLinkHandlerContext {
    readonly channelName?: string;
    readonly packetName: string;
    readonly contentType?: string;
    readonly connectionAborted: AbortSignal;
    readonly metadata: ZLinkMessageMetadata;
}

export type ZLinkHandlerDelegate = () => Promise<unknown>;

export interface ZLinkHandlerFilter {
    invoke(invocation: ZLinkHandlerInvocation, next: ZLinkHandlerDelegate): Promise<unknown>;
}

export declare function ZLinkHandlerGroup(groupName: string): ClassDecorator;

export interface ZLinkHandlerInvocation {
    readonly ownerKind: string;
    readonly channelName?: string;
    readonly packetName: string;
    readonly metadata: ZLinkMessageMetadata;
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
    RouteMesh = 1
}
```

`ZLINK_FRAMEWORK_ERROR_KIND_VALUES`는 위 순서대로 `ActorRouteNotFound=0`부터
`ActorCreateRejected=21`, `ObjectClientNotConfigured=22`, `MeshSelectionRequired=23`,
`MeshNotFound=24`, `InvalidConfiguration=25`, `AlreadySubmitted=26`,
`ActorGenerationStale=27`, `ActorMoving=28`, `DeadlineExceeded=29`,
`PlacementCapacityExhausted=30`, `RoutingIdConflict=31`, `SpotGenerationStale=32`,
`SpotMoving=33`, `RelocationDataLost=34`, `SpotIdConflict=35`, `RuntimeShutdown=36`을 반환한다.
`RelocationDataLost`는 Location authority가 공개한 Relocation
payload가 영구적으로 없거나 checksum·inventory digest가 일치하지 않을 때 반환하며 이전 owner로 rollback하지
않는다. 기본 retriable kind는 `RouteNotConnected`, `ActorLocationStale`,
`ActorMoving`, `DeadlineExceeded`, `PlacementCapacityExhausted`, `SpotMoving`이다.
`RuntimeShutdown`은 runtime이 신규 admission을 받지 않는 terminal state에서 사용한다.

## 2. Location change

```ts
export interface ZLinkLocationChanged {
    readonly kind: ZLinkLocationKind;
    readonly key: ZLinkLocationKey;
    readonly changeType: ZLinkLocationChangeType;
    readonly generation: bigint;
    readonly updatedAt: Date;
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
} | {
    readonly kind: ZLinkLocationKind.ClientServer;
    readonly key: ZLinkClientServerServerDescriptorKey;
};

export declare enum ZLinkLocationKind {
    Invalid = 0,
    Peer = 1,
    Spot = 2,
    Actor = 3,
    Route = 4,
    ClientServer = 5
}

export interface ZLinkLocationMonitoringRegistration {
    readonly sourceName: string;
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

## 3. Location runtime 상태와 event

```ts
export interface ZLinkLocationReadiness {
    isPeerReady(meshName: string, role: ZLinkLocationRole, nodeRid?: RoutingId, signal?: AbortSignal): Promise<boolean>;
}

export declare enum ZLinkLocationRole {
    Invalid = 0,
    Spot = 2,
    Router = 3,
    Dealer = 4
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

`ZLinkLocationAutoConnectType`, `ZLinkLocationRole`과 `ZLinkPeerLocation` 계열은 RouteMesh peer만 표현한다.
Classic fanout 자동 연결은 `ZLinkFanoutPublisherDescriptor`와 `ZLinkFanoutLocationStore`를 사용하므로 이
generic enum에 fanout 전용 값이나 publisher/subscriber role을 추가하지 않는다.

## 4. Location topology와 message-flow event

```ts
export interface ZLinkLocationTopologyEntry {
    readonly kind: ZLinkLocationKind;
    readonly meshName?: string;
    readonly role?: ZLinkLocationRole;
    readonly nodeRid?: RoutingId;
    readonly spotId?: SpotId;
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
    readonly reason?: ZLinkMessageFlowReason | ZLinkDispatchErrorReason;
    readonly action?: ZLinkDispatchErrorAction;
    readonly packetName?: string;
    readonly meshName?: string;
    readonly channelName?: string;
    readonly channelRouteKind?: "routeMesh" | "clientServer";
    readonly topic?: string;
    readonly correlationId?: string;
    readonly sourceRid?: RoutingId;
    readonly targetRid?: RoutingId;
    readonly serverRid?: RoutingId;
    readonly flowId?: string;
    readonly flowOrigin?: import('../Eventing/Contracts').ZLinkFlowOrigin;
    readonly spotId?: SpotId;
    readonly instanceSpotType?: string;
    readonly activationState?: ZLinkSpotActivationState;
    readonly actorId?: string;
    readonly messageSizeBytes?: number;
    readonly durationSeconds?: number;
    readonly remoteSnapshotCount?: bigint;
    readonly remoteAdmittedCount?: bigint;
    readonly remoteDroppedCount?: bigint;
    readonly remoteUnreachableCount?: bigint;
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

## 5. Message metadata, observer와 meter

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

## 6. Host와 topology runtime

```ts
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
    Starting = 0,
    Serving = 1,
    Draining = 2,
    Drained = 3,
    ForceStopping = 4,
    Stopped = 5,
    Faulted = 6
}

export declare enum ZLinkFrameworkRuntimeState {
    Preparing = 0,
    Serving = 1,
    Retiring = 2,
    Draining = 3,
    Stopped = 4,
    Error = 5
}

export declare enum ZLinkTerminationIntent {
    Retire = 0,
    Shutdown = 1
}

export declare enum ZLinkTerminationOutcome {
    Stopped = 0,
    Blocked = 1,
    ForceStopped = 2
}

export declare enum ZLinkTerminationReason {
    None = 0,
    TargetUnavailable = 1,
    StoreUnavailable = 2,
    RelocationDisabled = 3,
    StateIncompatible = 4,
    DeadlineExceeded = 5,
    RelocationFailed = 6,
    TeardownFailed = 7,
    RuntimeNotReady = 8
}

export interface ZLinkTerminationResult {
    readonly effectiveIntent: ZLinkTerminationIntent;
    readonly outcome: ZLinkTerminationOutcome;
    readonly reason: ZLinkTerminationReason;
}

export interface ZLinkTerminationOptions {
    readonly deadlineMs?: number;
    readonly signal?: AbortSignal;
}

export interface ZLinkMeshChannelSnapshot {
    readonly channelName: string;
    readonly localWeight: number;
    readonly readyMemberCount: bigint;
    readonly selectable: boolean;
}

export interface ZLinkLogicalMulticastSnapshot {
    readonly submitted: bigint;
    readonly backpressured: bigint;
    readonly dropped: bigint;
    readonly remoteSnapshotCount: bigint;
    readonly remoteAdmittedCount: bigint;
    readonly remoteDroppedCount: bigint;
    readonly remoteUnreachableCount: bigint;
    readonly localSnapshotCount: bigint;
    readonly localAdmittedCount: bigint;
    readonly localDroppedCount: bigint;
}

export interface ZLinkMeshClaimSnapshot {
    readonly applicationActive: boolean;
    readonly pendingApplicationWork: bigint;
    readonly infrastructureActive: boolean;
    readonly pendingInfrastructureWork: bigint;
}

export interface ZLinkInstanceSpotTypeSnapshot {
    readonly instanceSpotType: string;
    readonly activeCount: bigint;
    readonly activatingCount: bigint;
    readonly closingCount: bigint;
    readonly pendingMessageCount: bigint;
    readonly pendingByteCount: bigint;
    readonly lastActivationOutcome?: string;
}

export interface ZLinkLocationRuntimeSnapshot {
    readonly state: string;
    readonly lastSuccessAt?: Date;
    readonly lastFailureAt?: Date;
}

export interface ZLinkFrameworkRuntimeSnapshot {
    readonly state: ZLinkFrameworkRuntimeState;
    readonly effectiveIntent?: ZLinkTerminationIntent;
    readonly deadline?: Date;
    readonly workSealed: boolean;
    readonly blockerReason?: ZLinkTerminationReason;
    readonly pendingRequestCount: bigint;
    readonly pendingRelocationCount: bigint;
    readonly pendingStreamBarrierCount: bigint;
    readonly terminalResult?: ZLinkTerminationResult;
    readonly sequence: bigint;
    readonly observedAt: Date;
}

export interface ZLinkFrameworkRuntimeEvent {
    readonly identifier: "zlink.runtime.host.termination_changed";
    readonly sequence: bigint;
    readonly timestamp: Date;
    readonly state: ZLinkFrameworkRuntimeState;
    readonly effectiveIntent?: ZLinkTerminationIntent;
    readonly outcome?: ZLinkTerminationOutcome;
    readonly reason?: ZLinkTerminationReason;
}

export interface ZLinkFrameworkRuntime {
    readonly state: ZLinkFrameworkRuntimeState;
    readonly isReady: boolean;
    snapshot(): ZLinkFrameworkRuntimeSnapshot;
    observe(capacity?: number, signal?: AbortSignal): AsyncIterable<ZLinkFrameworkRuntimeEvent>;
    retire(options?: ZLinkTerminationOptions): Promise<ZLinkTerminationResult>;
    shutdown(options?: ZLinkTerminationOptions): Promise<ZLinkTerminationResult>;
}

export interface ZLinkMeshNodeSnapshot {
    readonly meshName: string;
    readonly rid: RoutingId;
    readonly lifecycleGeneration: bigint;
    readonly descriptorRevision: bigint;
    readonly endpoint: string;
    readonly objectRole: ZLinkObjectRole;
    readonly placementWeight: number;
    readonly populationCapacity: {
        readonly actors: ZLinkPopulationCapacity;
        readonly spots: ZLinkPopulationCapacity;
        readonly spotTypes: readonly ZLinkSpotTypeCapacity[];
    };
    readonly activationConcurrency: {
        readonly active: number;
        readonly limit: number;
    };
    readonly applicationVersion: bigint;
    readonly placementReservationFailureCount: bigint;
    readonly lastPlacementReservationFailure?: string;
    readonly objectCapabilities: readonly ZLinkObjectCapability[];
    readonly state: ZLinkMeshNodeState;
    readonly sequence: bigint;
    readonly observedAt: Date;
    readonly descriptorSources: readonly string[];
    readonly peers: readonly ZLinkMeshPeerSnapshot[];
    readonly channels: readonly ZLinkMeshChannelSnapshot[];
    readonly multicast: ZLinkLogicalMulticastSnapshot;
    readonly instanceSpots: readonly ZLinkInstanceSpotTypeSnapshot[];
    readonly claims: ZLinkMeshClaimSnapshot;
    readonly location: ZLinkLocationRuntimeSnapshot;
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
    readonly mailboxDomain?: "application" | "infrastructure";
    readonly messageKind?: string;
    readonly remoteSnapshotCount?: bigint;
    readonly remoteAdmittedCount?: bigint;
    readonly remoteDroppedCount?: bigint;
    readonly remoteUnreachableCount?: bigint;
    readonly localSnapshotCount?: bigint;
    readonly localAdmittedCount?: bigint;
    readonly localDroppedCount?: bigint;
    readonly placementOutcome?: string;
    readonly capacity?: ZLinkCapacityVector;
    readonly populationCapacity?: {
        readonly actors: ZLinkPopulationCapacity;
        readonly spots: ZLinkPopulationCapacity;
        readonly spotTypes: readonly ZLinkSpotTypeCapacity[];
    };
    readonly activationConcurrency?: {
        readonly active: number;
        readonly limit: number;
    };
    readonly reason?: string;
    readonly state?: ZLinkMeshNodeState;
}

export interface ZLinkRouteMeshRuntime {
    snapshot(meshName: string): ZLinkMeshNodeSnapshot;
    observe(meshName: string, capacity?: number, signal?: AbortSignal): AsyncIterable<ZLinkMeshRuntimeEvent>;
    isReady(meshName: string): boolean;
}

export type ZLinkClientServerRole = "client" | "server" | "clientAndServer";
export type ZLinkClientServerServerState =
    | "configured"
    | "connecting"
    | "ready"
    | "draining"
    | "disconnected"
    | "rejected";

export interface ZLinkClientServerServerSnapshot {
    readonly serverRid: RoutingId;
    readonly lifecycleGeneration: bigint;
    readonly descriptorRevision: bigint;
    readonly endpoint: string;
    readonly weight: number;
    readonly ready: boolean;
    readonly state: ZLinkClientServerServerState;
    readonly descriptorSource: string;
    readonly lastFailure?: string;
}

export interface ZLinkClientServerChannelSnapshot {
    readonly channelName: string;
    readonly localRole: ZLinkClientServerRole;
    readonly selectable: boolean;
    readonly readyServerCount: number;
    readonly connectionIntentCount: number;
    readonly pendingRequestCount: number;
    readonly sequence: bigint;
    readonly observedAt: Date;
    readonly servers: readonly ZLinkClientServerServerSnapshot[];
    readonly location: ZLinkLocationRuntimeSnapshot;
}

export interface ZLinkClientServerRuntimeEvent {
    readonly identifier: string;
    readonly sequence: bigint;
    readonly timestamp: Date;
    readonly channelName: string;
    readonly serverRid?: RoutingId;
    readonly lifecycleGeneration?: bigint;
    readonly descriptorRevision?: bigint;
    readonly weight?: number;
    readonly ready?: boolean;
    readonly state?: ZLinkClientServerServerState;
    readonly reason?: string;
}

export interface ZLinkClientServerRuntime {
    snapshot(channelName: string): ZLinkClientServerChannelSnapshot;
    observe(channelName: string, capacity?: number, signal?: AbortSignal):
        AsyncIterable<ZLinkClientServerRuntimeEvent>;
    isReady(channelName: string): boolean;
}

export interface ZLinkFanoutPublisherConnectionSnapshot {
    readonly publisherRid: RoutingId;
    readonly lifecycleGeneration: bigint;
    readonly descriptorRevision: bigint;
    readonly endpoint: string;
    readonly connectionIntent: boolean;
    readonly ready: boolean;
    readonly state: ZLinkFanoutPublisherConnectionState;
    readonly lastFailure?: string;
}

export type ZLinkFanoutPublisherConnectionState =
    | "connecting"
    | "ready"
    | "disconnected"
    | "reconnecting"
    | "excluded_draining"
    | "excluded_stale";

export interface ZLinkFanoutChannelSnapshot {
    readonly channelName: string;
    readonly connectionIntentCount: number;
    readonly readyConnectionCount: number;
    readonly sequence: bigint;
    readonly observedAt: Date;
    readonly publishers: readonly ZLinkFanoutPublisherConnectionSnapshot[];
    readonly location: ZLinkLocationRuntimeSnapshot;
}

export type ZLinkFanoutRuntimeEvent =
    | {
        readonly identifier: "zlink.runtime.fanout.publisher_changed";
        readonly sequence: bigint;
        readonly timestamp: Date;
        readonly channelName: string;
        readonly entry: ZLinkFanoutPublisherConnectionSnapshot;
    }
    | {
        readonly identifier: "zlink.runtime.location.store_changed";
        readonly sequence: bigint;
        readonly timestamp: Date;
        readonly channelName: string;
        readonly location: ZLinkLocationRuntimeSnapshot;
    };

export interface ZLinkFanoutRuntime {
    snapshot(channelName: string): ZLinkFanoutChannelSnapshot;
    observe(channelName: string, capacity?: number, signal?: AbortSignal):
        AsyncIterable<ZLinkFanoutRuntimeEvent>;
}

```

`populationCapacity`는 Actor 전체, Spot 전체와 등록한 User·Instance Spot type별
active·reserved·limit을 구분한다. Limit `0`은 제한 없음이다. Entry Spot 자체는 Spot count에서 제외하고
Entry Spot의 Actor는 Actor 전체 count에 포함한다. `activationConcurrency`의 active·limit은 population
reservation과 별도로 제공한다. Placement event의 `capacity`는 해당 operation의 typed vector이고
`populationCapacity`는 관찰 시점의 node aggregate다.

같은 ChannelName에 Client와 Server를 함께 등록한 snapshot의 `localRole`은
`"clientAndServer"`다. 이 값은 `(ChannelName, Role)`의 별도 registration 두 개가 하나의 ClientServer
topology를 공유한다는 aggregate projection이다. Builder role이나 registration key로 사용할 수 없다.

Host `retire()`에서 deadline이 모든 target의 `Prepared`와 host `Draining` descriptor publication 전에 끝나면
relocation reference와 reservation을 정리하고 reversible seal을 해제한 뒤 `Blocked/DeadlineExceeded`로 끝난다.
모든 target이 `Prepared`이고 `Draining` publication이 성공한 뒤에는 `Blocked`로 돌아가지 않는다. 이 경계 뒤의
deadline은 admission을 닫은 채 recovery handoff와 bounded teardown을 수행하고
`ForceStopped/DeadlineExceeded`로 끝낸다. 두 결과는 같은 reason을 사용하지만 phase와 side effect가 다르며
별도 enum을 추가하지 않는다.
