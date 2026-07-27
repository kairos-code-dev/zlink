# Node.js Location 운영 조회와 observability 공개 interface

이 문서는 application이 상태를 조회하고 event를 처리할 때 사용하는 공개 interface만 정의한다. 저장 행 변화 감시, runtime event 발행, serializer 선택과 handler 호출 wrapper는 Framework 내부 책임이다.

## 1. Handler filter

Filter는 message context와 다음 delegate를 직접 받는다. owner 종류나 decoded message를 별도 wrapper로 공개하지 않는다. `AbortSignal`은 dispatch가 취소될 때 전달된다.

```ts
export interface ZLinkMessageContext {
  readonly meshName?: string;
  readonly channelName?: string;
  readonly packetName: string;
  readonly contentType?: string;
  readonly metadata: ZLinkMessageMetadata;
  readonly correlationId?: string;
}

export type ZLinkHandlerDelegate = () => Promise<unknown>;

export interface ZLinkHandlerFilter {
  invoke(
    context: ZLinkMessageContext,
    next: ZLinkHandlerDelegate,
    signal?: AbortSignal
  ): Promise<unknown>;
}
```

`ZLinkHandlerInvocation`은 public contract가 아니다. Filter는 `context`를 검사하고 필요하면 `next()`를 한 번 호출한다.

## 2. Location 운영 조회

```ts
export interface ZLinkLocationRuntimeQuery {
  getStatus(signal?: AbortSignal): Promise<ZLinkLocationRuntimeStatus>;
  listTopology(
    filter: ZLinkLocationTopologyFilter,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkLocationTopologyEntry>>;
  listServiceSummaries(
    filter: ZLinkLocationServiceSummaryFilter,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkLocationServiceSummary>>;
}

export interface ZLinkLocationTopologyFilter {
  readonly meshName?: string;
  readonly nodeRid?: RoutingId;
  readonly state?: ZLinkLocationTopologyState;
}

export interface ZLinkLocationTopologyEntry {
  readonly meshName: string;
  readonly nodeRid: RoutingId;
  readonly endpoint: string;
  readonly draining: boolean;
  readonly state: ZLinkLocationTopologyState;
  readonly updatedAt: Date;
}

export interface ZLinkLocationServiceSummaryFilter {
  readonly meshName?: string;
}

export interface ZLinkLocationServiceSummary {
  readonly meshName: string;
  readonly totalCount: number;
  readonly readyCount: number;
  readonly errorCount: number;
  readonly stoppedCount: number;
  readonly lastUpdatedAt: Date;
}

export interface ZLinkLocationReadiness {
  isPeerReady(
    meshName: string,
    role: ZLinkLocationRole,
    nodeRid?: RoutingId,
    signal?: AbortSignal
  ): Promise<boolean>;
}
```

Spot·Actor·route 저장 행 query, 저장 key, `ZLinkLocationAutoConnectType`, watch store와 change stamp는 runtime 내부 계약이다. Application은 aggregate topology와 service summary를 조회한다.

## 3. Monitoring event

```ts
export interface ZLinkMonitoringOptions {
  socket?: ZLinkSocketMonitoringRegistration[];
  locationRuntime?: ZLinkPollingMonitoringRegistration[];
}

export interface ZLinkRuntimeEvent {
  readonly sourceName: string;
  readonly timestamp: Date;
}

export interface ZLinkRuntimeEventHandler<TEvent extends ZLinkRuntimeEvent> {
  handle(event: TEvent): Promise<void>;
}

export interface ZLinkSocketEvent extends ZLinkRuntimeEvent {
  readonly event: ZLinkSocketEventKind;
  readonly routingId?: RoutingId;
  readonly localAddr: string;
  readonly remoteAddr: string;
}

export type ZLinkLocationRuntimeEvent =
  | (ZLinkRuntimeEvent & {
      readonly event: ZLinkLocationRuntimeEventKind.StatusChanged;
      readonly status: ZLinkLocationRuntimeStatus;
    })
  | (ZLinkRuntimeEvent & {
      readonly event: ZLinkLocationRuntimeEventKind.TopologyChanged;
      readonly topology: readonly ZLinkLocationTopologyEntry[];
    })
  | (ZLinkRuntimeEvent & {
      readonly event: ZLinkLocationRuntimeEventKind.ServiceSummaryChanged;
      readonly serviceSummary: readonly ZLinkLocationServiceSummary[];
    })
  | (ZLinkRuntimeEvent & {
      readonly event:
        | ZLinkLocationRuntimeEventKind.StoreFailure
        | ZLinkLocationRuntimeEventKind.StoreRecovered;
    });

export enum ZLinkSpotEventKind {
  TimerHandlerFailed = 'timerHandlerFailed',
  TimerStoppedAfterUnhandledException = 'timerStoppedAfterUnhandledException'
}

export interface ZLinkSpotTimerDiagnostic {
  readonly spotId: SpotId;
  readonly isEntrySpot: boolean;
  readonly timerName: string;
  readonly handlerType: string;
  readonly deliveryIndex: bigint;
  readonly scheduledIndex: bigint;
  readonly exceptionType: string;
  readonly exceptionMessage: string;
}

export type ZLinkSpotEvent = ZLinkRuntimeEvent & {
  readonly event:
    | ZLinkSpotEventKind.TimerHandlerFailed
    | ZLinkSpotEventKind.TimerStoppedAfterUnhandledException;
  readonly timerDiagnostic: ZLinkSpotTimerDiagnostic;
};
```

Application은 handler를 등록한다. `ZLinkRuntimeEventPublisher`, native socket event number·diagnostic, location row별 event emitter는 runtime composition에만 사용하므로 package root에서 공개하지 않는다.

MeshNode readiness와 peer 상태는 `ZLinkRouteMeshRuntime.snapshot()`과 `observe()`로 확인한다. Monitoring
option은 같은 정보를 Spot polling event로 중복 발행하지 않는다. Spot event는 application이 대응해야 하는
timer handler 실패 두 종류만 제공한다.

## 4. Host termination runtime

Host 전체의 `Retire`와 `Shutdown`은 `ZLinkFrameworkRuntime`에서 시작한다. RouteMesh topology runtime은
상태 조회만 제공하며 host lifecycle을 변경하지 않는다.

```ts
export enum ZLinkFrameworkRuntimeState {
  Preparing = 0,
  Serving = 1,
  Retiring = 2,
  Draining = 3,
  Stopped = 4,
  Error = 5
}

export enum ZLinkTerminationIntent {
  Retire = 0,
  Shutdown = 1
}

export enum ZLinkTerminationOutcome {
  Stopped = 0,
  Blocked = 1,
  ForceStopped = 2
}

export enum ZLinkTerminationReason {
  None = 0,
  TargetUnavailable = 1,
  StoreUnavailable = 2,
  RelocationDisabled = 3,
  StateIncompatible = 4,
  DeadlineExceeded = 5,
  RelocationFailed = 6,
  TeardownFailed = 7,
  RuntimeNotReady = 8,
  ManualTopologyUnsupported = 9
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
  readonly identifier: 'zlink.runtime.host.termination_changed';
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
```

## 5. RouteMesh runtime 상태와 readiness

`meshName`은 조회할 RouteMesh를 지정한다. 등록되지 않은 이름은 새 상태를 만들지 않고 typed route error로
실패한다. `isReady(...)`는 해당 MeshNode가 `Serving` 상태이고 runtime에 연결된 경우에만 `true`다.

```ts
export enum ZLinkMeshNodeState {
  Starting = 0,
  Serving = 1,
  Draining = 2,
  Drained = 3,
  ForceStopping = 4,
  Stopped = 5,
  Faulted = 6
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

export interface ZLinkMeshChannelSnapshot {
  readonly channelName: string;
  readonly localWeight: number;
  readonly readyMemberCount: bigint;
  readonly selectable: boolean;
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

export interface ZLinkInstanceSpotTypeSnapshot {
  readonly instanceSpotType: string;
  readonly activeCount: bigint;
  readonly activatingCount: bigint;
  readonly closingCount: bigint;
  readonly pendingMessageCount: bigint;
  readonly pendingByteCount: bigint;
  readonly lastActivationOutcome?: string;
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
  readonly messageKind?: string;
  readonly placementOutcome?: string;
  readonly capacity?: {
    readonly actorSlots: number;
    readonly spotSlots: number;
    readonly spotTypes: readonly {
      readonly objectKind: 'user_spot' | 'instance_spot';
      readonly stableType: string;
      readonly slots: number;
    }[];
  };
  readonly populationCapacity?: ZLinkMeshNodeSnapshot['populationCapacity'];
  readonly activationConcurrency?: ZLinkMeshNodeSnapshot['activationConcurrency'];
  readonly reason?: string;
  readonly state?: ZLinkMeshNodeState;
}

export interface ZLinkRouteMeshRuntime {
  snapshot(meshName: string): ZLinkMeshNodeSnapshot;
  observe(
    meshName: string,
    capacity?: number,
    signal?: AbortSignal
  ): AsyncIterable<ZLinkMeshRuntimeEvent>;
  isReady(meshName: string): boolean;
}
```

## 6. ClientServer와 fanout runtime 상태

같은 process의 endpoint도 remote endpoint와 같은 후보 선택 및 연결 상태 계약을 따른다. 이 view는
상태를 조회하고 event를 구독할 뿐 topology나 host lifecycle을 변경하지 않는다.

```ts
export type ZLinkClientServerRole = 'client' | 'server' | 'clientAndServer';
export type ZLinkClientServerServerState =
  | 'configured' | 'connecting' | 'ready' | 'draining' | 'disconnected' | 'rejected';

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
  observe(
    channelName: string,
    capacity?: number,
    signal?: AbortSignal
  ): AsyncIterable<ZLinkClientServerRuntimeEvent>;
  isReady(channelName: string): boolean;
}

export type ZLinkFanoutPublisherConnectionState =
  | 'connecting' | 'ready' | 'disconnected' | 'reconnecting'
  | 'excluded_draining' | 'excluded_stale';

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
      readonly identifier: 'zlink.runtime.fanout.publisher_changed';
      readonly sequence: bigint;
      readonly timestamp: Date;
      readonly channelName: string;
      readonly entry: ZLinkFanoutPublisherConnectionSnapshot;
    }
  | {
      readonly identifier: 'zlink.runtime.location.store_changed';
      readonly sequence: bigint;
      readonly timestamp: Date;
      readonly channelName: string;
      readonly location: ZLinkLocationRuntimeSnapshot;
    };

export interface ZLinkFanoutRuntime {
  snapshot(channelName: string): ZLinkFanoutChannelSnapshot;
  observe(
    channelName: string,
    capacity?: number,
    signal?: AbortSignal
  ): AsyncIterable<ZLinkFanoutRuntimeEvent>;
}
```

## 7. Message wrapper

```ts
export declare class ZLinkMessage<TValue = unknown> {
  private constructor();
  static from<T>(value: T): ZLinkMessage<T>;
  static fromEncoded(payload: ZLinkEncodedPayload): ZLinkMessage;
  decode<T>(type?: Type<T>): T;
  toEncodedPayload(): ZLinkEncodedPayload;
  isEncoded(): boolean;
}

```

Serializer registry 선택과 default serializer 결정 helper는 runtime 내부에 둔다. Application은 codec을 Framework 구성에 등록하고 message별 selector나 registry를 전달하지 않는다.
