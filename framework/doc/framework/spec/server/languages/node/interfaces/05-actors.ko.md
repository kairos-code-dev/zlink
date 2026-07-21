# Node.js Actor와 session binding 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Node.js 계약 목차](../README.ko.md)

이 문서는 ZLink Framework 11.0.0에서 `@zlink-systems/framework`와
`@zlink-systems/nestjs`가 내보내는 Actor와 session binding 관련 정확한 TypeScript declaration을 고정한다.
동작 의미는 [공통 스펙](../../../../README.ko.md)이 소유하며, 이 문서는 이름, generic, overload,
상속, member, parameter와 반환형만 정의한다.

Source는 connection-bound one-way를 포함해 admission한 모든 connection-bound work가 terminal accounting에
도달한 뒤에만 `captured`를 commit한다. Durable accepted journal은 exact owner lease가 있는 source에서만
사용한다. Pre-`captured` drain deadline 실패는 abort와 `Blocked/TransferDisabled`이며 미완료 one-way capture
예외는 없다.

Transferable Actor는 source Entry Spot member여야 한다. User Spot member이면 Retire preflight가
`Blocked/TransferDisabled`이고 authority와 admission을 바꾸지 않는다. `newOwner` CAS는 owner,
authorityOwnerGeneration과 current Spot을 target Entry identity로 원자적으로 바꾼다. Target factory·restore,
target `onJoinedActor`, journal replay 뒤 source `onLeaveActor`와 old Entry removal을 durable cleanup으로
수행한다. Callback은 retry-safe해야 하며 at-least-once 호출될 수 있다. Public phase API는 없다.

새 distributed Actor는 internal `creating` row의 `newObject` CAS, final `ActorRef.generation`, factory, initial
Entry membership과 initialize 뒤 `ready` CAS를 거친다. Resolver와 remote messaging은 `ready`만 사용한다.
실패하면 exact fence로 delete하고 read로 reconcile하며 확인 전 같은 typed failure와 hidden retry 0을 적용한다.
`missing` 뒤 다음 caller만 새 create를 시작한다. Entry initialization도 Host `Serving` publication보다 먼저
완료한다. 이 barrier를 위한 public API는 없다.

## 1. Actor factory, context와 join

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
    readonly boundSession: ZLinkBoundSession;
    joinSpot(spotRid: RoutingId, request: unknown): ZLinkActorJoinSpotCall;
    joinEntrySpot(nodeRid: RoutingId, request: unknown): ZLinkActorJoinEntrySpotCall;
}

export interface ZLinkActorDirectory {
    find(meshName: string, actorId: string, signal?: AbortSignal): Promise<ActorRef | undefined>;
}

export interface ZLinkActorFactory<TActor extends ZLinkActor = ZLinkActor> {
    create(actorId: string, context: ZLinkActorContext): Promise<TActor>;
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

## 2. Actor manager, address와 call

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
    getOrCreate(meshName: string, actorId: string, actorType: string, signal?: AbortSignal): Promise<ActorRef>;
    getOrCreate(meshName: string, actorId: string, actorType: string, createRequest: unknown, signal?: AbortSignal): Promise<ActorRef>;
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
    submit(signal?: AbortSignal): Promise<ZLinkSubmitResult>;
}

export interface ZLinkActorSpotHandleResolver {
    resolveActorSpotHandle(meshName: string, actorId: string, signal?: AbortSignal): Promise<SpotHandle | undefined>;
}
```

Canonical logical identity는 `(MeshName, ActorId)`다. Actor type은 create에서 factory를 선택한 뒤 authority
payload에 고정하는 immutable lifecycle attribute이며 `ActorRef`나 directory key에 반복하지 않는다. 같은
MeshName과 Actor ID에는 active type 하나만 존재한다. `getOrCreate`에 전달한 type이 existing authority의 type과
다르면 type conflict로 실패한다.

`ZLinkActorDirectory`는 MeshName과 Actor ID로 이미 존재하는 logical Actor만 조회한다. Missing Actor를
생성하거나 remote MeshNode를 선택하지 않는다. Local create와 get-or-create는 `ZLinkActorManager`가 actor
type을 명시해서 수행한다. MeshName은 현재 host에 등록된 local MeshNode를 선택한다. Existing Actor가 remote
owner에 있으면 조회 결과를 반환할 수 있지만, missing Actor를 remote owner에 생성하거나 hidden forwarding으로
만들지 않는다. Actor handler 등록과 membership leave는 Actor context가 아니라 owner Spot context가 소유한다.

## 3. Actor lifecycle, session send와 socket monitoring

```ts
export interface ZLinkSessionSendCall {
    metadata(key: string, value: string): this;
    compress(enabled?: boolean): this;
    submit(signal?: AbortSignal): Promise<ZLinkSubmitResult>;
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

export interface ZLinkSpot<TActor extends ZLinkActor = ZLinkActor>
    extends ZLinkSpotActorLifecycle<TActor> {
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
    compress(enabled?: boolean): this;
}
```
