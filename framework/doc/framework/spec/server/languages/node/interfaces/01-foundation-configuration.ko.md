# Node.js 기초 타입과 구성 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Node.js 계약 목차](../README.ko.md)

이 문서는 ZLink Framework 11.0.0에서 `@zlink-systems/framework`와
`@zlink-systems/nestjs`가 내보내는 기초 타입과 구성 관련 정확한 TypeScript declaration을 고정한다.
동작 의미는 [공통 스펙](../../../../README.ko.md)이 소유하며, 이 문서는 이름, generic, overload,
상속, member, parameter와 반환형만 정의한다.

## 1. 공통 식별자와 직렬화 utility

```ts
export type ActorId = string;
export type SpotRid = RoutingId;
export type ZLinkPlacementProfile = string;
export type ZLinkAffinityKey = string;

export interface ActorRef {
    readonly actorId: ActorId;
    readonly objectGeneration: bigint;
    readonly meshName: string;
    readonly nodeRid: RoutingId;
}

export interface SpotRef {
    readonly spotRid: SpotRid;
    readonly objectGeneration: bigint;
    readonly meshName: string;
    readonly nodeRid: RoutingId;
}

export declare enum ZLinkObjectRole {
    None = "none",
    Client = "client",
    Server = "server"
}

export interface ZLinkObjectPlacementOptions {
    readonly placementProfiles?: readonly ZLinkPlacementProfile[];
    readonly maxActiveObjects?: number;
    readonly maxPendingActivations?: number;
}

export declare function isZLinkFrameworkErrorRetriableByDefault(kind: ZLinkFrameworkErrorKind): boolean;

export declare function isZLinkMessage(value: unknown): value is ZLinkMessage;

export declare const MESSAGE_FLOW_MODE_RANK: Record<ZLinkMessageFlowLogMode, number>;

export declare function parseMessage<T>(_payload: ZLinkEncodedPayload, _type: Type<T>): T;

export declare function readZLinkDecoratorMetadata(target: object): readonly ZLinkDecoratorMetadata[];

export type RoutingId = string;

export declare function selectDefaultSerializer(registry?: ZLinkSerializerRegistryLike | ReadonlyMap<string, ZLinkMessageSerializer>): ZLinkMessageSerializer | undefined;

export declare function selectSerializer(value: unknown, registry?: ZLinkSerializerRegistryLike | ReadonlyMap<string, ZLinkMessageSerializer>, context?: ZLinkSerializerSelectionContext): ZLinkMessageSerializer | undefined;

export type Type<T = unknown> = new (...args: never[]) => T;
```

## 2. 등록, topology와 transfer builder

```ts
export interface ZLinkTransferStateAdapter<TInstance, TState> {
    capture(instance: TInstance, signal: AbortSignal): Promise<TState>;
    restore(instance: TInstance, state: TState, signal: AbortSignal): Promise<void>;
}

declare const zlinkTransferPolicyBrand: unique symbol;

export interface ZLinkDisabledTransferPolicy<TInstance> {
    readonly [zlinkTransferPolicyBrand]: TInstance;
    readonly kind: "disabled";
}

export interface ZLinkRecreateTransferPolicy<TInstance> {
    readonly [zlinkTransferPolicyBrand]: TInstance;
    readonly kind: "recreate";
}

export interface ZLinkSnapshotTransferPolicy<TInstance> {
    readonly [zlinkTransferPolicyBrand]: TInstance;
    readonly kind: "snapshot";
}

export type ZLinkTransferPolicy<TInstance> =
    | ZLinkDisabledTransferPolicy<TInstance>
    | ZLinkRecreateTransferPolicy<TInstance>
    | ZLinkSnapshotTransferPolicy<TInstance>;

export declare function zlinkDisabledTransfer<T>(): ZLinkDisabledTransferPolicy<T>;
export declare function zlinkRecreateTransfer<T>(): ZLinkRecreateTransferPolicy<T>;
export declare function zlinkSnapshotTransfer<T, TState>(
    stateContractId: string,
    stateType: Type<TState>,
    adapterType: Type<ZLinkTransferStateAdapter<T, TState>>): ZLinkSnapshotTransferPolicy<T>;

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
    submit(signal?: AbortSignal): Promise<ZLinkSubmitResult>;
}

export interface ZLinkChannelClient {
    sendToChannel(channelName: string, message: unknown): ZLinkSendCall;
    requestToChannel(channelName: string, request: unknown): ZLinkRequestCall;
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
    client(): ZLinkMeshChannelClientBuilder;
    server(): ZLinkMeshChannelServerBuilder;
}

export interface ZLinkMeshChannelClientBuilder {
}

export interface ZLinkMeshChannelServerBuilder {
    setWeight(weight: number): this;
    addHandlerGroup(groupName: string): this;
    addSendHandler<TMessage>(handlerType: Type<ZLinkSendHandler<TMessage>>): this;
    addRequestHandler<TRequest, TReply>(handlerType: Type<ZLinkRequestHandler<TRequest, TReply>>): this;
}

export interface ZLinkMeshNodeSocketConfig {
    maxMessageSize: number;
    sendHighWaterMark: number;
    receiveHighWaterMark: number;
    mailboxMessageBudget: number;
    mailboxByteBudget: number;
    receiveTimeoutMs?: number;
    sendTimeoutMs?: number;
}

export interface ZLinkSpotPublisherConfig {
    sendHighWaterMark: number;
    sendTimeoutMs?: number;
    lingerMs?: number;
}

export interface ZLinkMeshNodeBuilder {
    channel(channelName: string): ZLinkMeshChannelBuilder;
    listen(endpoint: string): this;
    listen(port?: number): this;
    setBindHost(bindHost: string): this;
    setAdvertiseHost(advertiseHost: string): this;
    routingId(routingId: RoutingId): this;
    setRoutingIdPrefix(prefix: string): this;
    setPlacementWeight(weight: number): this;
    setObjectCapacity(maxActiveObjects: number, maxPendingActivations: number): this;
    objects(): ZLinkMeshObjectRoleBuilder;
    configureRouterSocket(): ZLinkMeshNodeSocketConfig;
    configureSpotPublisher(): ZLinkSpotPublisherConfig;
    peerConnections(): ZLinkMeshPeerConnections;
    setDefaultRequestTimeout(timeoutMs: number): this;
    addRouteSendHandler<TMessage>(handlerType: Type<ZLinkRouteSendHandler<TMessage>>): this;
    addRouteRequestHandler<TRequest, TReply>(handlerType: Type<ZLinkRouteRequestHandler<TRequest, TReply>>): this;
}

export interface ZLinkMeshObjectRoleBuilder {
    client(): ZLinkMeshObjectClientBuilder;
    server(): ZLinkMeshObjectServerBuilder;
}

export interface ZLinkMeshObjectClientBuilder {
}

export interface ZLinkMeshObjectServerBuilder {
    addEntrySpot<TEntrySpot extends ZLinkEntrySpot>(entrySpotType: Type<TEntrySpot>): this;
    addSpotFactory<TSpot extends ZLinkSpot>(
        spotType: string,
        implementation: Type<TSpot>,
        placement: ZLinkObjectPlacementOptions | undefined,
        transfer: ZLinkTransferPolicy<TSpot>): this;
    addInstanceSpotFactory<TSpot extends ZLinkInstanceSpot>(
        instanceSpotType: string,
        implementation: Type<TSpot>,
        placement: ZLinkObjectPlacementOptions | undefined,
        transfer: ZLinkTransferPolicy<TSpot>): this;
    addActorFactory<TActor extends ZLinkActor>(
        actorType: string,
        factoryType: Type<ZLinkActorFactory<TActor>>,
        placement: ZLinkObjectPlacementOptions | undefined,
        transfer: ZLinkTransferPolicy<TActor>): this;
}

export interface ZLinkNetworkOptions {
    bindHost: string;
    advertiseHost?: string;
}

export interface ZLinkClientServerChannelRoleBuilder {
    client(): ZLinkClientServerChannelClientBuilder;
    server(): ZLinkClientServerChannelServerBuilder;
}

export interface ZLinkClientServerChannelClientBuilder {
    connect(endpoint: string): this;
}

export interface ZLinkClientServerChannelServerBuilder {
    listen(port?: number): this;
    setBindHost(bindHost: string): this;
    setAdvertiseHost(advertiseHost: string): this;
    setWeight(weight: number): this;
    addHandlerGroup(groupName: string): this;
    addSendHandler<TMessage>(handlerType: Type<ZLinkSendHandler<TMessage>>): this;
    addRequestHandler<TRequest, TReply>(handlerType: Type<ZLinkRequestHandler<TRequest, TReply>>): this;
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

`channel(channelName)` 뒤에는 `client()` 또는 `server()`를 정확히 한 번 호출한다. Client builder에는
역할별 추가 설정이 없고 Server builder만 `setWeight(...)`와 handler 등록을 제공한다. 따라서 잘못된 역할
설정을 runtime validation까지 미루지 않고 TypeScript type 단계에서 막는다. Server membership이 없는
MeshNode도 시작할 수 있다. `addClientServerChannel(channelName)`의
client는 send/request를 시작하고
server는 수신한 send/request 처리와 reply만 수행한다.

Actor와 Instance Spot의 maintenance 정책은 factory 등록과 함께 전달한다. 별도 Actor transfer adapter
registry나 operation별 state adapter는 제공하지 않는다. Snapshot policy만 typed
`ZLinkTransferStateAdapter<TInstance, TState>`를 사용한다.

같은 `stateContractId`를 사용하는 source와 target adapter는 `frameworkJsonV1` semantic profile로 호환되어야
한다. 이 profile은 enum을 string, 64-bit integer를 decimal string, binary를 padded base64로 표현하고 unknown
field는 무시한다. Duplicate field와 required field 누락은 거부한다. Application state의 JSON byte 배열 자체는
canonical하지 않으며 Transfer Store에는 opaque bytes로 보관한다. Canonical byte identity는 Framework 내부
root manifest, chunk와 envelope에만 적용한다. Message별 codec 등록이나 transfer 전용 codec API는 제공하지
않는다.

Target이 `"activated"`에 도달해도 application과 session ingress는 sealed 상태를 유지하고 restore, accepted
journal replay와 bound-session route는 staged 상태로만 준비한다. Source cleanup이 terminal 상태에 도달하고
authority의 `"completed"` CAS가 성공한 뒤에만 target을 `"ready"`로 열고 transfer fence를 해제한다.
`"completed"` 뒤의 target failure는 ordinary owner loss로 처리하며 이전 transfer payload를 transparent replay하지
않는다. 이 barrier를 조작하는 public phase API는 제공하지 않는다.

Target replacement가 발생하면 stable transfer 안의 각 attempt가 factory와 `restore(...)`를 at-least-once
호출할 수 있고 중단된 stale attempt callback이 successor와 겹칠 수 있다. `capture(...)`도 immutable transfer
root가 authority에 연결되기 전까지 반복될 수 있다. Current exact owner와 attempt fence만 completion을 commit하고
admission을 열 수 있다. Callback에는 transfer ID를 추가하지 않으므로 application restore와 capture는 retry-safe해야
하며 exactly-once external side effect를 보장하지 않는다.

Transferred terminal reply accounting은 internal command ID 46 `replyRelayAck`를 사용한다. 이 command는 stable
transfer ID, operation ID, exact request-source fence(owner ID, lease generation, node RID, node generation)와
status만 가지며 payload와 metadata를 싣지 않는다. Physical connection close는 terminal 증거가 아니다. ACK 또는
accepted record에 저장한 exact request-source lease expiry만 terminal accounting을 완료하며 public ACK API는 없다.

`mailboxMessageBudget`와 `mailboxByteBudget`은 owner별 application mailbox의 메시지 수와 payload byte 수
상한이며 startup 전에만 설정한다. `0`은 unlimited가 아니라 Framework profile의 유한 기본값을 선택한다.
음수, 정수가 아닌 값과 안전 정수 범위를 벗어난 값은 startup 설정 오류다. Logical Multicast의 local target도
이 용량 제한으로 admission을 판단한다.

Framework가 모든 registration에서 만든 fully encoded MeshNode descriptor는 1 MiB 이하여야 한다.
Spot type과 stateful object capability collection은 각각 최대 1024개이고, capability 하나의 readable state
contract ID도 최대 1024개다. Runtime은 완성된 descriptor를 socket bind 전에 한 번에 검증한다. Bound를 넘으면
startup을 실패시키며 collection을 truncate·split하거나 descriptor 일부를 게시하지 않는다.

`configureNetwork()`의 기본 BindHost는 `127.0.0.1`이다. AdvertiseHost를 생략하면 wildcard가
아닌 BindHost를 사용하고, wildcard BindHost에서는 AdvertiseHost를 반드시 명시한다.
Automatic discovery listener의 port를 생략하거나 listener 호출을 생략하면 port `0`을
사용한다. Listener별 host 설정은 root 기본값보다 우선한다.

MeshNode의 기본 object role은 `ZLinkObjectRole.None`이다. `objects().client()`는 global Actor·Spot client와
manager를 제공하고 `objects().server()`는 그 기능과 factory·Entry Spot hosting을 함께 제공한다. Role을 두 번
선택하거나 factory를 Server builder 밖에서 등록하면 `InvalidConfiguration`이다. Client 또는 Server role은
Location Store가 필요하다.

모든 User·Instance Spot과 Actor factory는 transfer policy를 명시해야 한다. 생략을 Disabled로 해석하지 않는다.
Placement profile은 UTF-8 1..255 bytes이며 factory별 capacity가 없으면 MeshNode의 object capacity를 사용한다.
Placement weight는 양수이고 active limit은 양수, pending limit은 0 이상이다.

## 3. Handler metadata와 dispatch option

```ts
export interface ZLinkDecoratorMetadata {
    readonly kind: string;
    readonly packetName?: string;
    readonly groupName?: string;
    readonly methodName?: string;
    readonly meshName?: string;
    readonly channelName?: string;
    readonly topic?: string;
}

export interface ZLinkLocationOptionValues {
    readonly ownerLeaseRenewIntervalMs: number;
    readonly ownerLeaseTtlMs: number;
    readonly pollingIntervalMs: number;
    readonly storeFailureGraceMs: number;
    readonly ownerLeaseFencingMarginMs: number;
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
    | "node" | "channel" | "spot" | "instance_spot" | "logical_multicast"
    | "actor" | "stream" | "classic_fanout" | "actor_transfer";
export type ZLinkMessageKind =
    | "send" | "request" | "response" | "error" | "publish" | "control";
export type ZLinkMessageFlowOutcome =
    | "succeeded" | "failed" | "backpressured" | "dropped" | "cancelled" | "shutdown";
export type ZLinkMessageFlowReason =
    | "backpressure" | "stale_target" | "target_closed" | "shutdown"
    | "location_unavailable" | "activation_rejected" | "activation_timeout";
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

```
