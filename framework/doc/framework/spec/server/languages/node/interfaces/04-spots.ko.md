# Node.js Spot과 Instance Spot 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Spot address와 messaging](../../../24-spot-address-messaging.ko.md) ·
[Spot·Actor membership](../../../23-spot-actor.ko.md)

이 문서는 ZLink Framework 11.0.0에서 `@zlink-systems/framework`와
`@zlink-systems/nestjs`가 내보내는 Spot 관련 정확한 TypeScript declaration을 고정한다.

## 1. Global identity와 lifecycle

`SpotRid`는 Location Store transaction domain 전체에서 유일한 logical ID다. 일반 message는 SpotRid만 받고
current authority를 resolve한다. `SpotRef`는 exact incarnation을 닫을 때 사용하는 immutable location
snapshot이다.

```ts
export declare enum ZLinkSpotKind {
    Invalid = "invalid",
    Entry = "entry",
    User = "user",
    Instance = "instance"
}

export declare enum ZLinkSpotCloseReason {
    ExplicitClose = 0,
    HostShutdown = 1,
    RelocationOut = 2
}

export interface ZLinkSpotClosingContext {
    readonly reason: ZLinkSpotCloseReason;
    readonly deadline: Date;
}

export interface ZLinkSpotAcceptRejectResponse {
    readonly accepted: boolean;
    readonly reply?: unknown;
}

export interface ZLinkSpotActorJoinResponse extends ZLinkSpotAcceptRejectResponse {}

export interface ZLinkSpotCreateResponse extends ZLinkSpotAcceptRejectResponse {}

export interface ZLinkSpotActorLifecycle<TActor extends ZLinkActor = ZLinkActor> {
    onActorJoin(actorId: string, request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse>;
    onJoinedActor(actor: TActor): Promise<void>;
    onLeaveActor(actor: TActor): Promise<void>;
    onDisconnectActor?(actor: TActor): Promise<void>;
}

export interface ZLinkSpot<TActor extends ZLinkActor = ZLinkActor>
    extends ZLinkSpotActorLifecycle<TActor> {
    readonly context: ZLinkSpotContext<TActor>;
    configure?(): void;
    onCreate?(request: ZLinkMessage): Promise<ZLinkSpotCreateResponse>;
    onInitialize?(): Promise<void>;
    onClosing?(
        context: ZLinkSpotClosingContext,
        cleanupSignal: AbortSignal): Promise<void>;
}

export interface ZLinkInstanceSpot {
    readonly context: ZLinkInstanceSpotContext;
    configure?(): void;
    onInitialize?(): Promise<void>;
    onClosing?(
        context: ZLinkSpotClosingContext,
        cleanupSignal: AbortSignal): Promise<void>;
}

export interface ZLinkSpotCommonContext<TSpot> {
    readonly meshName: string;
    readonly spotRid: SpotRid;
    readonly nodeRid: RoutingId;
    readonly outbound: ZLinkSpotOutbound;
    addTimer<THandler extends ZLinkSpotTimerHandler<TSpot>>(
        name: string,
        periodMs: number,
        handlerType: Type<THandler>,
        options?: ZLinkTimerOptions,
        signal?: AbortSignal): Promise<ZLinkTimer>;
    runCpuWorker<T>(work: (signal: AbortSignal) => T): ZLinkWorkerCall<T>;
    runIoWorker<T>(work: (signal: AbortSignal) => Promise<T>): ZLinkWorkerCall<T>;
}

export interface ZLinkSpotContext<
    TActor extends ZLinkActor = ZLinkActor,
    TSpot extends ZLinkSpot<TActor> = ZLinkSpot<TActor>>
    extends ZLinkSpotCommonContext<TSpot> {
    readonly handlers: ZLinkSpotHandlerRegistry;
    leaveActor(actor: TActor, signal?: AbortSignal): Promise<void>;
    close(signal?: AbortSignal): Promise<boolean>;
}

export interface ZLinkInstanceSpotContext
    extends ZLinkSpotCommonContext<ZLinkInstanceSpot> {
    readonly handlers: ZLinkInstanceSpotHandlerRegistry;
    close(signal?: AbortSignal): Promise<boolean>;
}
```

`SpotRid`와 `SpotRef`의 canonical declaration은
[기초 타입과 구성](01-foundation-configuration.ko.md)이 소유한다. 이 문서는 해당 타입을 다시 선언하지 않고
Spot lifecycle에서 사용하는 위치만 고정한다.

`ZLinkSpotCloseReason`의 numeric 값은 `ExplicitClose=0`, `HostShutdown=1`, `RelocationOut=2`다.
`deadline`은 closing operation의 absolute UTC instant다. Framework는 callback invocation 전에는
`cleanupSignal`을 abort하지 않고 deadline이 끝날 때 abort한다. Entry·User·Instance Spot만 callback을 받고
Actor별 closing callback은 제공하지 않는다. Host Shutdown은 Actor membership과 local instance가 유효한
상태에서 callback을 실행하고 fulfillment 뒤 scope와 authority를 정리한다. Standalone Actor relocation은 Entry
Spot을 닫지 않으므로 이 callback을 호출하지 않는다.

## 2. Handler와 outbound

```ts
export interface ZLinkSpotHandlerRegistry {
    addPacket<THandler>(handlerType: Type<THandler>): this;
    addSubscribe<THandler>(handlerType: Type<THandler>, channelName: string, topic: string): this;
}

export interface ZLinkInstanceSpotHandlerRegistry {
    addPacket<THandler>(handlerType: Type<THandler>): this;
}

export interface ZLinkSpotOutbound {
    sendToSpot(spotRid: SpotRid, message: unknown): ZLinkSpotSendCall;
    requestToSpot(spotRid: SpotRid, request: unknown): ZLinkSpotRequestCall;
    publish(channelName: string, topic: string, event: unknown): ZLinkPublishCall;
    sendToChannel(channelName: string, message: unknown): ZLinkSendCall;
    requestToChannel(channelName: string, request: unknown): ZLinkRequestCall;
}

export interface ZLinkSpotSendCall {
    metadata(key: string, value: string): this;
    metadata(metadata: ZLinkMessageMetadata): this;
    instanceSpot(): this;
    instanceSpot(instanceSpotType: string): this;
    inMesh(meshName: string): this;
    placementProfile(placementProfile: ZLinkPlacementProfile): this;
    affinityKey(affinityKey: ZLinkAffinityKey): this;
    submit(signal?: AbortSignal): Promise<ZLinkSubmitResult>;
}

export interface ZLinkSpotRequestCall {
    metadata(key: string, value: string): this;
    metadata(metadata: ZLinkMessageMetadata): this;
    instanceSpot(): this;
    instanceSpot(instanceSpotType: string): this;
    inMesh(meshName: string): this;
    placementProfile(placementProfile: ZLinkPlacementProfile): this;
    affinityKey(affinityKey: ZLinkAffinityKey): this;
    timeout(timeoutMs: number): this;
    submit<TReply>(signal?: AbortSignal): Promise<TReply>;
    yield<TReply>(signal?: AbortSignal): Promise<TReply>;
}

export interface ZLinkSpotPacketHandler<TSpot, TMessage> {
    handle(spot: TSpot, message: TMessage, context: ZLinkHandlerContext): Promise<void>;
}

export declare function ZLinkSpotActorRequest(packetName?: string): MethodDecorator;
export declare function ZLinkSpotActorSend(packetName?: string): MethodDecorator;

export interface ZLinkSpotActorSendContext extends ZLinkHandlerContext {
    readonly metadata: ZLinkMessageMetadata;
}

export interface ZLinkSpotActorRequestContext extends ZLinkSpotActorSendContext {
    readonly reply: ZLinkSpotActorReplyOptions;
}

export interface ZLinkSpotActorSendHandler<TActor extends ZLinkActor, TMessage> {
    handle(actor: TActor, context: ZLinkSpotActorSendContext, message: TMessage): Promise<void>;
}

export interface ZLinkSpotActorRequestHandler<TActor extends ZLinkActor, TRequest, TReply> {
    handle(actor: TActor, context: ZLinkSpotActorRequestContext, request: TRequest): Promise<TReply>;
}
```

## 3. Manager와 single-use create call

```ts
export interface ZLinkSpotCreateResult {
    readonly spot: SpotRef;
    readonly state: ZLinkSpotCreateState;
    readonly reply?: unknown;
}

export declare enum ZLinkSpotCreateState {
    Existing = "existing",
    Created = "created",
    Rejected = "rejected"
}

export interface ZLinkSpotManager {
    create(spotType: string): ZLinkSpotCreateCall;
    getOrCreate(
        spotRid: SpotRid,
        spotType: string): ZLinkSpotGetOrCreateCall;
    find(spotRid: SpotRid, signal?: AbortSignal): Promise<SpotRef | undefined>;
    close(spot: SpotRef, signal?: AbortSignal): Promise<boolean>;
}

export interface ZLinkSpotCreateCall {
    inMesh(meshName: string): this;
    request(request: unknown): this;
    placementProfile(placementProfile: ZLinkPlacementProfile): this;
    affinityKey(affinityKey: ZLinkAffinityKey): this;
    timeout(timeoutMs: number): this;
    submit(signal?: AbortSignal): Promise<ZLinkSpotCreateResult>;
}

export interface ZLinkSpotGetOrCreateCall {
    inMesh(meshName: string): this;
    request(request: unknown): this;
    placementProfile(placementProfile: ZLinkPlacementProfile): this;
    affinityKey(affinityKey: ZLinkAffinityKey): this;
    timeout(timeoutMs: number): this;
    submit(signal?: AbortSignal): Promise<ZLinkSpotCreateResult>;
}
```

User·Instance SpotRid는 global key다. Stable type은 UTF-8 1..255 bytes이며 case-sensitive exact value로
비교하고 normalization하지 않는다. Object generation은 positive signed-63-bit 값이다. MeshName과 NodeRid는
조회 시점의 route snapshot이며 identity key에 포함하지 않는다.

Create와 GetOrCreate call은 single-use다. 같은 option을 두 번 설정하면 `InvalidConfiguration`, terminal
`submit(...)`을 두 번 호출하면 `AlreadySubmitted`다. User Spot `create`는 Framework가 새 global RID를 발급한다.
`getOrCreate`는 같은 User kind·stable type의 ready 또는 creating attempt에 합류한다. Kind나 type이 다르면
`SpotTypeMismatch`, deadline 안에 terminal state가 되지 않으면 `DeadlineExceeded`다.

`close(spotRef)`는 exact incarnation만 닫는다. Generation이 다르면 `SpotGenerationStale`, 이동 중이면
retriable `SpotMoving`이다. Framework는 current ref를 다시 찾아 다른 incarnation을 닫지 않는다.

Instance Spot에는 manager create·get-or-create를 제공하지 않는다. `sendToSpot`과 `requestToSpot`은 SpotRid만
받고 Spot 전용 call을 반환한다. Instance intent를 설정하지 않은 call은 existing-only이며 Missing에서
target-not-found다. `instanceSpot()`은 선택한 Mesh의 serving descriptor에 distinct Instance type이 하나일 때
그 type을 자동 선택하고, 여러 type이면 stable type을 받는 overload를 요구한다. 같은 type을 여러 MeshNode가
등록한 것은 distinct type 하나다. `inMesh`, placement profile과 affinity key는 Missing cold activation의 최초
placement에만 사용하고 existing owner의 current Mesh를 제한하지 않는다.

선택한 Mesh에 Instance type이 없으면 send는 `TargetNotFound`, request는 `RequestTargetNotFound`로 끝난다.
Distinct type이 여러 개인데 type을 생략하면 `InvalidConfiguration`이다. Ready Instance authority가 있으면 저장된
stable type을 사용하므로 caller가 type을 다시 제공하지 않아도 된다. Instance marker를 사용했는데 existing
authority가 User Spot이거나 명시한 type과 authority type이 다르면 `SpotTypeMismatch`다.

Terminal call에서 source는 `Ready` authority가 있으면 current owner에게 일반 message를 보낸다. Missing
authority와 Instance intent가 있으면 eligible target을 선택하고 SpotRid, stable type, creation intent와 first
message를 포함한 activation envelope를 그 target에 보낸다. Source는 generic Store reservation을 만들지 않는다.
Activation envelope는 `Ready` 전에도 target transport로 전달할 수 있는 Framework infrastructure message이며
application handler로 dispatch하지 않는다.

Target runtime은 metadata presence·frame을 포함한 complete envelope를 Relocation Store에 immutable recovery root로 먼저 저장하고 local exact
instance를 확인한다. Instance가 없을 때만 자신을 owner로 Creating row와 pending capacity를 Reserve하며 Pending
snapshot은 provider가 발급한 reservation fence와 recovery root receipt를 반환한다. CAS winner가 factory,
initialize와 durable activation inbox first record 확정을 수행한다. CAS loser는 factory를 시작하지 않고 current
authority를 읽어 owner에게 reroute하거나 진행 중인 attempt에 합류한다. Commit은 handler barrier를 유지한 채
recovery root·cursor와 Ready, active capacity를 게시한다. Runtime은 first record를 local queue head로 복원한
뒤 barrier를 열며 source는 Ready 뒤 같은 message를 다시 전송하지 않는다. Authority와 일치하지 않는 local-only
instance는 message를 처리하지 못하도록 fence한다. Existing User kind나 다른 Instance type은
`SpotTypeMismatch`다.
Recovery pointer는 첫 handler terminal completion을 durable하게 기록하고 replay cursor를 inbox sequence까지
갱신한 뒤에만 Preserve CAS로 제거한다. Queue admission만으로 제거하지 않는다.

User Spot의 `close(spotRef)`는 active Actor membership이 남아 있으면 `false`를 반환한다. Framework는 Actor를
자동으로 leave·destroy하지 않는다. One-way Spot message는 local outbound admission까지만 기다리며, target
queue admission 이후 실패한 operation을 새 owner에게 hidden retry하지 않는다.

Instance Spot factory는 actor-free lifecycle만 만든다. Actor handler, Actor membership과 Logical Multicast
subscription을 등록할 수 없으며 direct packet과 timer만 처리한다. Instance Spot은 handler나 timer가 자신의
context `close(...)`를 호출해 종료한다. 일반 message는 missing RID에 새 intent를 만들거나 factory를 직접
시작하지 않는다.

User·Instance Spot relocation에서는 Framework가 `addTimer(...)`로 만든 logical timer registration, 마지막 완료
tick sequence, 다음 예정 시각과 아직 실행하지 않은 pending tick을 relocation payload에 포함한다. Target은 새
native timer handle을 만들며 application이 timer를 다시 등록하지 않는다. 현재 실행 중인 timer handler만 source에서
완료하고 target Ready 전에는 복원한 tick을 실행하지 않는다.

Public trace category는 `spot-instance`, `actor-relocation`다. 의미와 검증 기준은
[Spot address와 messaging](../../../24-spot-address-messaging.ko.md)과
[Spot·Actor membership](../../../23-spot-actor.ko.md)이 소유한다.
