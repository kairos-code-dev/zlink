# Node.js Location 운영 조회와 observability 공개 interface

이 문서는 application이 상태를 조회하고 event를 처리할 때 사용하는 공개 interface만 정의한다. 저장 행 변화 감시, runtime event 발행, serializer 선택과 handler 호출 wrapper는 Framework 내부 책임이다.

## 1. Handler filter

Filter는 message 정보와 공개 dispatch 종류만 포함하는 전용 context를 받는다. Socket, endpoint,
내부 owner 종류와 decoded message는 공개하지 않는다. `AbortSignal`은 dispatch가 취소될 때 전달된다.

```ts
export interface ZLinkMessageContext {
  readonly meshName?: string;
  readonly channelName?: string;
  readonly packetName: string;
  readonly contentType?: string;
  readonly metadata: ZLinkMessageMetadata;
  readonly correlationId?: string;
}

export enum ZLinkHandlerDispatchKind {
  NodeDirectSend = 'nodeDirectSend',
  NodeDirectRequest = 'nodeDirectRequest',
  ChannelSend = 'channelSend',
  ChannelRequest = 'channelRequest',
  ClassicFanout = 'classicFanout'
}

export interface ZLinkHandlerFilterContext extends ZLinkMessageContext {
  readonly dispatchKind: ZLinkHandlerDispatchKind;
}

export type ZLinkHandlerDelegate = () => Promise<void>;

export interface ZLinkHandlerFilter {
  invoke(
    context: ZLinkHandlerFilterContext,
    next: ZLinkHandlerDelegate,
    signal?: AbortSignal
  ): Promise<void>;
}
```

`ChannelSend`와 `ChannelRequest`는 RouteMesh와 ClientServer Channel을 함께 나타낸다. RouteMesh와
Node direct는 MeshName을 제공한다. ClientServer와 classic fanout은 MeshName을 제공하지 않는다.

Filter는 `next()`를 최대 한 번 호출한다. 두 번째 호출은
`ZLinkFrameworkErrorKind.InvalidOperation`으로 실패하며 handler를 다시 실행하지 않는다. Request에서
`next()`를 호출하지 않으면 `ZLinkFrameworkErrorKind.RequestRejected` reply를 보낸다. Filter의 반환값으로
업무 reply를 만들거나 바꾸지 않는다.

`ZLinkHandlerInvocation`은 public contract가 아니다. Filter는 Node direct send/request, Channel
send/request와 classic fanout 구독 handler에만 적용한다. Spot·Actor·Logical Multicast·STREAM
handler에는 적용하지 않는다.

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

## 4. Host relocation과 termination runtime

Object relocation과 host 종료는 `ZLinkFrameworkRuntime`의 `relocate(options)`와 `shutdown()`으로 각각
시작한다.
RouteMesh topology runtime은 상태 조회만 제공하며 host lifecycle을 변경하지 않는다.

```ts
export enum ZLinkFrameworkRuntimeState {
  Preparing = 0,
  Serving = 1,
  Relocating = 2,
  Relocated = 3,
  Draining = 4,
  Stopped = 5,
  Error = 6
}

export enum ZLinkFrameworkRelocationOutcome {
  Relocated = 0,
  Blocked = 1
}

export enum ZLinkFrameworkRelocationMode {
  PlannedMaintenance = 0,
  RollingUpdate = 1
}

export enum ZLinkFrameworkRelocationReason {
  None = 0,
  TargetUnavailable = 1,
  StoreUnavailable = 2,
  RelocationDisabled = 3,
  StateIncompatible = 4,
  DeadlineExceeded = 5,
  RelocationFailed = 6,
  RuntimeNotReady = 7,
  ManualTopologyUnsupported = 8,
  ShutdownRequested = 9,
  OperationInProgress = 10
}

export interface ZLinkFrameworkRelocationOptions {
  readonly mode: ZLinkFrameworkRelocationMode;
  readonly targetApplicationVersion?: bigint;
  readonly deadlineMs?: number;
  readonly signal?: AbortSignal;
}

export interface ZLinkFrameworkRelocationResult {
  readonly mode: ZLinkFrameworkRelocationMode;
  readonly effectiveTargetApplicationVersion: bigint;
  readonly outcome: ZLinkFrameworkRelocationOutcome;
  readonly reason: ZLinkFrameworkRelocationReason;
}

export enum ZLinkFrameworkTerminationOutcome {
  Stopped = 0,
  ForceStopped = 1
}

export enum ZLinkFrameworkTerminationReason {
  None = 0,
  DeadlineExceeded = 1,
  TeardownFailed = 2
}

export interface ZLinkFrameworkTerminationResult {
  readonly outcome: ZLinkFrameworkTerminationOutcome;
  readonly reason: ZLinkFrameworkTerminationReason;
}

export interface ZLinkFrameworkLifecycleOptions {
  readonly deadlineMs?: number;
  readonly signal?: AbortSignal;
}

export interface ZLinkFrameworkRuntimeStatus {
  readonly state: ZLinkFrameworkRuntimeState;
  readonly isReady: boolean;
  readonly acceptingWork: boolean;
  readonly deadline?: Date;
  readonly relocationResult?: ZLinkFrameworkRelocationResult;
  readonly terminationResult?: ZLinkFrameworkTerminationResult;
  readonly sequence: bigint;
  readonly observedAt: Date;
}

export interface ZLinkFrameworkRuntime {
  readonly status: ZLinkFrameworkRuntimeStatus;
  observe(signal?: AbortSignal): AsyncIterable<ZLinkFrameworkRuntimeStatus>;
  relocate(options: ZLinkFrameworkRelocationOptions): Promise<ZLinkFrameworkRelocationResult>;
  shutdown(options?: ZLinkFrameworkLifecycleOptions): Promise<ZLinkFrameworkTerminationResult>;
}
```

`relocate(options)`가 성공하면 runtime은 `Relocated` 상태가 되고 process와 infrastructure connection은 유지된다.
호출자는 결과가 `Relocated`인지 확인한 뒤 `shutdown()`을 호출할 수 있으며, relocation이 필요하지 않으면
`shutdown()`만 호출한다. `Relocating`에서 `shutdown()`을 호출하면 실행 중인 atomic relocation unit만
terminal 상태까지 확정하고 나머지 relocation을 중단한다. 이때 relocation waiter는
`Blocked/ShutdownRequested`를 받는다.
`signal`은 해당 Promise의 대기만 취소한다. 이미 시작된 shared relocation 또는 shutdown operation과
다른 waiter에는 영향을 주지 않는다.

호출자는 relocation mode를 생략할 수 없다. `PlannedMaintenance`는 같은 application version을 유지하는
node 점검이나 재부팅에 사용한다. 이 mode에서 `targetApplicationVersion`을 지정하면 Promise는 application
admission을 변경하기 전에 `TypeError`로 reject된다. 유효한 호출의
`effectiveTargetApplicationVersion`은 source host의 application version이다.

`RollingUpdate`는 `targetApplicationVersion`이 필수이며 source version보다 커야 한다. 값이 없거나 source
version 이하이면 같은 방식으로 `TypeError`로 reject된다. Framework는 지정한 version과 정확히 같은 node만
후보로 사용하고 중간 version이나 더 높은 다른 version으로 대체하지 않는다.

Target 후보는 다음 순서로 줄인다.

1. 같은 Mesh에서 `Serving` 상태인 Object Server를 찾는다.
2. Planned maintenance이면 source version, rolling update이면 지정한 target version과 정확히 같은
   node만 남긴다.
3. Source와 같은 maintenance wave에 속한 node를 제외한다.
4. stable type, relocation policy와 adapter capability가 맞는지 확인한다.
5. population capacity와 reservation 가능 여부를 확인한다.
6. 남은 후보에 node-wide placement weight를 적용한다.

Version filter를 capability·capacity·weight보다 먼저 적용하므로 다른 version으로 fallback하지 않는다.
조건을 만족하는 Ready target이 없으면 `Blocked/TargetUnavailable`이다.

같은 shared relocation이 실행 중일 때 mode와 effective target version이 같은 호출은 기존 operation에
참여하고 같은 terminal result를 받는다. 첫 호출의 `deadlineMs`가 shared operation deadline을 고정하며
뒤에 참여한 호출은 이를 변경하지 않는다. Mode 또는 target version이 다른 호출은 실행 중인 operation을
변경하거나 대기열에 넣지 않고 `Blocked/OperationInProgress`를 반환한다. 이 결과에는 거부된 호출이
요청한 mode와 effective target version을 기록한다.

## 5. RouteMesh runtime 상태와 readiness

`meshName`은 조회할 RouteMesh를 지정한다. 등록되지 않은 이름은 새 상태를 만들지 않고 typed route error로
실패한다. `isReady(...)`는 host가 `Serving`이고 해당 RouteMesh topology가 `Ready`일 때만 `true`다.

```ts
export enum ZLinkTopologyState {
  Starting = 0,
  Ready = 1,
  Degraded = 2,
  Stopping = 3,
  Stopped = 4,
  Failed = 5
}

export enum ZLinkPeerState {
  Connecting = 0,
  Ready = 1,
  Draining = 2,
  NotConnected = 3,
  NotRequired = 4
}

export interface ZLinkMeshPeerSnapshot {
  readonly rid: RoutingId;
  readonly lifecycleGeneration: bigint;
  readonly descriptorRevision: bigint;
  readonly endpoint: string;
  readonly state: ZLinkPeerState;
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
  readonly state: ZLinkTopologyState;
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
  readonly state?: ZLinkTopologyState;
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

`NotConnected`는 topology상 연결이 필요하지만 ready connection이 없는 상태다.
`NotRequired`는 두 Object Client 모두 RouteMesh Channel Server membership이 없어 연결이 필요하지 않은
정상 상태다. Channel Client membership만 등록한 경우도 같다. 어느 한쪽에라도 weight `0`을 포함한
Channel Server membership이 있으면 연결 부재는 `NotConnected`다. 두 상태 모두 ready peer 수에서
제외하지만 `NotRequired`는 liveness·health failure 집계에 포함하지 않는다.

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
