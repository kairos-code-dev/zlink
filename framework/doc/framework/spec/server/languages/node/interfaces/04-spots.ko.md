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

export type ZLinkCreatableSpotKind = ZLinkSpotKind.User | ZLinkSpotKind.Instance;

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
    onClosing?(): Promise<void>;
}

export interface ZLinkInstanceSpot {
    readonly context: ZLinkInstanceSpotContext;
    configure?(): void;
    onInitialize?(): Promise<void>;
    onClosing?(): Promise<void>;
}

export interface ZLinkSpotCommonContext<TSpot> {
    readonly meshName: string;
    readonly spotRid: SpotRid;
    readonly nodeRid: RoutingId;
    readonly routingId: RoutingId;
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
    sendToSpot(spotRid: SpotRid, message: unknown): ZLinkSendCall;
    requestToSpot(spotRid: SpotRid, request: unknown): ZLinkRequestCall;
    publish(channelName: string, topic: string, event: unknown): ZLinkPublishCall;
    sendToChannel(channelName: string, message: unknown): ZLinkSendCall;
    requestToChannel(channelName: string, request: unknown): ZLinkRequestCall;
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
    readonly ref: SpotRef;
    readonly state: ZLinkSpotCreateState;
    readonly reply?: unknown;
}

export declare enum ZLinkSpotCreateState {
    Existing = "existing",
    Created = "created",
    Rejected = "rejected"
}

export interface ZLinkSpotManager {
    create(kind: ZLinkCreatableSpotKind, spotType: string): ZLinkSpotCreateCall;
    getOrCreate(
        spotRid: SpotRid,
        kind: ZLinkCreatableSpotKind,
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
`submit(...)`을 두 번 호출하면 `AlreadySubmitted`다. `create`는 Framework가 새 global RID를 발급한다.
`getOrCreate`는 같은 kind·stable type의 ready 또는 creating attempt에 합류한다. Kind나 type이 다르면
`SpotTypeMismatch`, deadline 안에 terminal state가 되지 않으면 `DeadlineExceeded`다.

`close(spotRef)`는 exact incarnation만 닫는다. Generation이 다르면 `SpotGenerationStale`, 이동 중이면
retriable `SpotMoving`이다. Framework는 current ref를 다시 찾아 다른 incarnation을 닫지 않는다.

Instance Spot의 최초 create intent만 kind, stable type과 initial Mesh를 기록한다. Ready 이후 message와 owner
loss 뒤 reactivation은 SpotRid와 저장된 intent를 사용한다. Public address, handle, resolver와 unbounded list는
제공하지 않는다. Generic Store reservation이 Creating row와 pending capacity를 원자적으로 확보하고, 성공하면
Ready와 active capacity로 commit하며 실패하면 abort한다.

User Spot의 `close(spotRef)`는 active Actor membership이 남아 있으면 `false`를 반환한다. Framework는 Actor를
자동으로 leave·destroy하지 않는다. One-way Spot message는 local outbound admission까지만 기다리며, target
queue admission 이후 실패한 operation을 새 owner에게 hidden retry하지 않는다.

Instance Spot factory는 actor-free lifecycle만 만든다. Actor handler, Actor membership과 Logical Multicast
subscription을 등록할 수 없으며 direct packet과 timer만 처리한다. Cold Instance activation은 manager가 먼저
기록한 durable creation intent를 사용한다. 일반 message는 missing RID에 새 intent를 만들거나 factory를 직접
시작하지 않는다.

Public trace category는 `spot-instance`, `actor-transfer`다. 의미와 검증 기준은
[Spot address와 messaging](../../../24-spot-address-messaging.ko.md)과
[Spot·Actor membership](../../../23-spot-actor.ko.md)이 소유한다.
