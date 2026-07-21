# Node.js Spot과 Instance Spot 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Node.js 계약 목차](../README.ko.md)

이 문서는 ZLink Framework 11.0.0에서 `@zlink-systems/framework`와
`@zlink-systems/nestjs`가 내보내는 Spot과 Instance Spot 관련 정확한 TypeScript declaration을 고정한다.
동작 의미는 [공통 스펙](../../../../README.ko.md)이 소유하며, 이 문서는 이름, generic, overload,
상속, member, parameter와 반환형만 정의한다.

## 1. Spot Actor message와 lifecycle context

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

export interface ZLinkSpotCommonContext<TSpot> {
    readonly meshName: string;
    readonly spotRid: RoutingId;
    readonly nodeRid: RoutingId;
    readonly routingId: RoutingId;
    readonly outbound: ZLinkSpotOutbound;
    addTimer<THandler extends ZLinkSpotTimerHandler<TSpot>>(name: string, periodMs: number, handlerType: Type<THandler>, options?: ZLinkTimerOptions, signal?: AbortSignal): Promise<ZLinkTimer>;

    runCpuWorker<T>(work: (signal: AbortSignal) => T): ZLinkWorkerCall<T>;
    runIoWorker<T>(work: (signal: AbortSignal) => Promise<T>): ZLinkWorkerCall<T>;
}

export interface ZLinkSpotContext<TActor extends ZLinkActor = ZLinkActor, TSpot extends ZLinkSpot<TActor> = ZLinkSpot<TActor>> extends ZLinkSpotCommonContext<TSpot> {
    readonly handlers: ZLinkSpotHandlerRegistry;
    leaveActor(actor: TActor, signal?: AbortSignal): Promise<void>;
    close(signal?: AbortSignal): Promise<boolean>;
}

export interface ZLinkInstanceSpotContext
    extends ZLinkSpotCommonContext<ZLinkInstanceSpot> {
    readonly handlers: ZLinkInstanceSpotHandlerRegistry;
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

```

## 2. Spot 상태, handle과 handler registry

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
    addSubscribe<THandler>(handlerType: Type<THandler>, channelName: string, topic: string): this;
}

export interface ZLinkInstanceSpotHandlerRegistry {
    addPacket<THandler>(handlerType: Type<THandler>): this;
}

export interface ZLinkSpotInfo {
    readonly spotRid: RoutingId;
}

export declare enum ZLinkSpotKind {
    Invalid = "invalid",
    Entry = "entry",
    User = "user",
    Instance = "instance"
}

export interface ZLinkSpotLocationFilter {
    readonly meshName?: string;
    readonly spotType?: string;
    readonly nodeRid?: RoutingId;
    readonly spotKind?: ZLinkSpotKind;
}

```

## 3. Spot manager와 outbound messaging

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

export interface ZLinkSpotOutbound {
    sendToSpot(spot: SpotHandle, message: unknown): ZLinkSendCall;
    requestToSpot(spot: SpotHandle, request: unknown): ZLinkRequestCall;
    sendToSpot(target: InstanceSpotAddress, message: unknown): ZLinkSendCall;
    requestToSpot(target: InstanceSpotAddress, request: unknown): ZLinkRequestCall;
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

`InstanceSpotAddress.meshName`과 `instanceSpotType`은 비어 있을 수 없고 UTF-8 byte 기준 각각 255 이하여야
하며, `spotRid`는 빈 RID일 수 없다. Equality와 hash는 세 값을 모두 사용한다. Location row의 유일성은
`(meshName, spotRid)`가 소유하므로 같은 key에 다른 Instance type이나 User Spot을 함께 등록할 수 없다.
Wire number와 public enum 사이의 변환은 runtime codec이 소유하며 package export에 포함하지 않는다.

`ZLinkSpot<TActor>`는 message를 받는 User Spot의 context와 create callback, 일반 lifecycle,
Actor membership lifecycle을 하나의 interface로 제공한다. `ZLinkInstanceSpot`은 Actor membership과
create callback을 제공하지 않고 `context`, `configure()`, `onInitialize()`, `onClosing()`만 제공한다.
Instance Spot은
direct packet과 timer handler만 등록할 수 있다. Instance context가 제공하는 registry에는 Actor handler와
Logical Multicast subscription 등록 member가 존재하지 않는다.

Activation은 scope와 Instance를 만든 뒤 `configure()`, `onInitialize()` 순서로 진행한다. 빈
`ZLinkMessage`를 create callback에 넘기지 않는다. Location `Ready` commit이 성공한 뒤에만 Node.js
Framework runtime이 소유한 activation barrier를 연다. 실패, close와 stale owner fencing에서는 provider
scope와 type별 active slot을 한 번만 정리한다.

Store-backed dynamic User Spot은 internal `creating` row를 `newObject` CAS로 만든 뒤 factory, `configure()`,
`onInitialize()`와 `ready` CAS를 수행한다. Resolve와 remote messaging은 `ready`만 사용한다. 실패하면 exact
fence로 delete하고 read로 reconcile하며 확인 전 같은 typed failure와 hidden retry 0을 적용한다. `missing` 뒤
다음 caller만 새 create를 시작한다. User Spot `close()`는 active Actor membership이나 missing이면 `false`다.
Active membership에서는 state·admission·authority를 바꾸지 않고 `onClosing()`이나 hidden leave·destroy를
실행하지 않는다. Caller가 명시적으로 leave·destroy한 뒤 다시 close하며 Host Shutdown·Retire는 Actor barrier
뒤 Spot을 cleanup한다.

`ZLinkMeshNodeSnapshot.instanceSpots`는 이 MeshNode가 startup에서 등록한 Instance type마다 집계한 immutable
snapshot이다. 각 entry는 active, activating, closing 수와 activation barrier 앞에서 대기하는 message·byte
수를 제공한다. `lastActivationOutcome`은 아직 terminal activation을 관찰하지 않았으면 존재하지 않으며, 값이
있으면 `ready`, `rejected`, `conflict`, `timed_out`, `shutdown`, `store_failure`, `fenced` 가운데 하나다. Spot
RID, owner ID, `ObjectGeneration`, `AuthorityOwnerGeneration`, `StoreVersion`과 owner lease fence는 entry나
별도 목록으로 노출하지 않는다.

생략한 option field에는 type별 active Instance `4096`과 activation timeout `3000`밀리초를 적용한다.
`maxActiveInstances`의 `0`은 거부하고 생략하면 `4096`을 적용한다.
`activationTimeoutMs`의 `0`도 거부하고 생략하면 `3000`밀리초를 적용한다. 즉, `0`을 기본값 sentinel이나 무제한으로
해석하지 않는다. 같은
MeshNode에서 stable `instanceSpotType` 또는
같은 provider class를 User Spot factory와 Instance factory에 중복 등록해도 startup이 실패한다.

`InstanceSpotAddress` overload는 location resolve가 필요할 수 있으므로 cache 상태와 관계없이
`ZLinkSendCall.submit(signal?)`으로만 one-way admission을 시작한다. Source는 location resolve, eligible target
선택과 `"coldActivating"` CAS claim을 outbound보다 먼저 같은 send deadline 안에서 완료한다. Target은 source가
확정한 token과 generation을 다시 검증하고 factory activation과 `"ready"` CAS만 수행하며 target-side claim을
시작하지 않는다. `submit()`은 source local outbound admission까지 기다리지만 target factory 실행, activation
queue 수락과 `"ready"`는 기다리지 않는다. 동기 즉시 제출 terminator는 제공하지 않는다. Request는 `ZLinkRequestCall`의 timeout과
`submit(...)`·`yield(...)`를 사용한다.
Caller는 target node, owner token, internal authority fields나 retry option을 받거나 전달하지 않는다.
SpotHandle overload와 Spot manager의 create·get-or-create·find·list·close는 User Spot과 local existing
Spot 계약을 유지하며 `ZLinkInstanceSpot`을 생성하거나 숨은 remote activation을 시작하지 않는다.

Cold Instance factory·initialize failure는 durable public `failed` state를 만들지 않는다. Runtime은 local failed
barrier와 exact fenced delete/read reconcile을 사용한다. Delete 확인 전 같은 typed failure와 hidden retry 0을
적용하고 `missing` 확인 뒤 다음 caller만 새 `coldActivating`을 시작한다. Public recovery API는 없다.

`ZLinkFanoutRuntime`은 endpoint 없이 등록한 automatic subscriber ChannelName만 받는다. Snapshot의
`publishers`와 publisher changed variant의 `entry`는 Publisher RID, lifecycle generation, descriptor
revision과 endpoint를 하나의 immutable identity로 보존한다. Location changed variant의 `location`은
publisher가 0개여도 store degraded·recovered 상태를 전달한다. Discriminated union의 두 variant는 서로의
payload를 optional field로 섞지 않는다. `state`의 닫힌 값과 event identifier는
[Runtime monitoring](../../../50-runtime-monitoring.ko.md)의 lowercase identifier를 그대로 사용한다. 이
runtime은 읽기 전용이며 manual
subscriber의 `ZLinkEndpointConnections`를 대신하거나 그 endpoint 집합을 변경하지 않는다. Manual
subscriber ChannelName을 조회하면 `ZLinkConfigurationError`가 발생한다.

`observe(...)`의 `AbortSignal`은 해당 `AsyncIterable` 소비 하나만 종료한다. Abort를 인식하면
아직 소비하지 않은 event를 폐기하고 대기 중인 또는 다음 iterator operation을 `AbortError`로
종료한다. 이미 실행을 시작한 소비 코드는 반환할 수 있지만 abort를 인식한 뒤에 새 event를
전달하지 않는다. 다른 iterator, automatic connection과 manual endpoint 집합은 영향을 받지 않는다.

`connectionIntent=true`는 automatic planner가 endpoint 연결을 요청했다는 뜻이고 transport readiness가
아니다. `ready=true`, `readyConnectionCount`와 publisher changed variant의 `ready` state는 publisher 전용
SUB socket의 native-ready와 같은 socket의 첫 valid application record 또는 liveness beacon 수신을 모두
반영한다. `disconnected`는 native disconnect 또는 15초 inbound timeout을 반영한다. `connect` 반환,
native-ready 하나와 내부 active target 수로 이 값을 먼저 바꾸지 않는다.
