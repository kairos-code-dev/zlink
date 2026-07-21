# Node.js Location, monitoring과 metrics 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Node.js 계약 목차](../README.ko.md)

이 문서는 ZLink Framework 11.0.0에서 `@zlink-systems/framework`와
`@zlink-systems/nestjs`가 내보내는 Location, monitoring과 metrics 관련 정확한 TypeScript declaration을 고정한다.
동작 의미는 [공통 스펙](../../../../README.ko.md)이 소유하며, 이 문서는 이름, generic, overload,
상속, member, parameter와 반환형만 정의한다.

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
    addCheckpointStore(store: ZLinkCheckpointStore): this;
    setApplicationVersion(version: bigint): this;
    setMaintenanceWave(waveId: string): this;
    setActorTransferForwardWindow(timeoutMs: number): this;
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
    RouteMesh = 1
}
```

## 2. Instance Spot 구성과 location change

```ts
export interface ZLinkInstanceSpotFactoryOptions {
    readonly maxActiveInstances?: number;
    readonly activationTimeoutMs?: number;
}

export interface ZLinkInstanceSpot {
    readonly context: ZLinkInstanceSpotContext;
    configure?(): void;
    onInitialize?(): Promise<void>;
    onClosing?(): Promise<void>;
}

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
    readonly spotRid?: RoutingId;
    readonly instanceSpotType?: string;
    readonly activationState?: ZLinkSpotActivationState;
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
    Starting = 1,
    Serving = 2,
    Draining = 3,
    Drained = 4,
    ForceStopping = 5,
    Stopped = 6,
    Faulted = 7
}

export declare enum ZLinkFrameworkRuntimeState {
    Preparing = 0,
    Serving = 1,
    Draining = 2,
    Stopped = 3,
    Error = 4
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
    TransferDisabled = 3,
    StateIncompatible = 4,
    DeadlineExceeded = 5,
    TransferFailed = 6,
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
    readonly pendingTransferCount: bigint;
    readonly pendingStreamBarrierCount: bigint;
    readonly terminalResult?: ZLinkTerminationResult;
    readonly sequence: bigint;
    readonly observedAt: Date;
}

export interface ZLinkFrameworkRuntimeEvent {
    readonly identifier: "zlink.runtime.host.termination_changed";
    readonly sequence: bigint;
    readonly timestamp: Date;
    readonly runtime: ZLinkFrameworkRuntimeSnapshot;
}

export interface ZLinkFrameworkRuntime {
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
    readonly localSnapshotCount?: bigint;
    readonly localAdmittedCount?: bigint;
    readonly localDroppedCount?: bigint;
    readonly reason?: string;
    readonly state?: ZLinkMeshNodeState;
}

export interface ZLinkRouteMeshRuntime {
    snapshot(meshName: string): ZLinkMeshNodeSnapshot;
    observe(meshName: string, capacity?: number, signal?: AbortSignal): AsyncIterable<ZLinkMeshRuntimeEvent>;
    isReady(meshName: string): boolean;
}

export type ZLinkClientServerRole = "client" | "server";
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

Host `retire()`에서 `Blocked/DeadlineExceeded`는 seal과 첫 `captured` commit 전 preflight timeout이다. 이
경우 host state와 admission은 그대로 유지한다. Seal 뒤 bounded teardown timeout은
`ForceStopped/DeadlineExceeded`다. 두 결과는 같은 reason을 사용하지만 phase와 side effect가 다르며 enum을
추가하지 않는다.
