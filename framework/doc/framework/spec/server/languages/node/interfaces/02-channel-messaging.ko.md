# Node.js Channel, request와 routing 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Node.js 계약 목차](../README.ko.md)

이 문서는 ZLink Framework 11.0.0에서 `@zlink-systems/framework`와
`@zlink-systems/nestjs`가 내보내는 Channel, request와 routing 관련 정확한 TypeScript declaration을 고정한다.
동작 의미는 [공통 스펙](../../../../README.ko.md)이 소유하며, 이 문서는 이름, generic, overload,
상속, member, parameter와 반환형만 정의한다.

## 1. Entry Spot과 classic fanout

```ts
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

export interface ZLinkEntrySpot<TActor extends ZLinkActor = ZLinkActor> extends ZLinkSpotActorLifecycle<TActor> {
    readonly context: ZLinkEntrySpotContext<TActor>;
    configure?(): void;
    onInitialize?(): Promise<void>;
    onClosing?(
        context: ZLinkSpotClosingContext,
        cleanupSignal: AbortSignal): Promise<void>;
    onCreateActor?(actor: TActor, createRequest: ZLinkMessage): Promise<void>;
    onActorRelocated?(actor: TActor): Promise<void>;
}

export interface ZLinkEntrySpotActorRequestHandler<TActor extends ZLinkActor, TRequest, TReply> {
    handle(actor: TActor, context: ZLinkSpotActorRequestContext, request: TRequest): Promise<TReply>;
}

export interface ZLinkEntrySpotActorSendHandler<TActor extends ZLinkActor, TMessage> {
    handle(actor: TActor, context: ZLinkSpotActorSendContext, message: TMessage): Promise<void>;
}

export interface ZLinkEntrySpotContext<TActor extends ZLinkActor = ZLinkActor, TEntrySpot extends ZLinkEntrySpot<TActor> = ZLinkEntrySpot<TActor>> extends ZLinkSpotCommonContext<TEntrySpot> {
    readonly handlers: ZLinkSpotHandlerRegistry;
    destroyActor(actor: TActor, signal?: AbortSignal): Promise<void>;
}

export interface ZLinkFanoutChannelBuilder {
    enablePublisher(endpoint: string): this;
    enablePublisher(port?: number): this;
    setBindHost(bindHost: string): this;
    setAdvertiseHost(advertiseHost: string): this;
    routingId(publisherRoutingId: RoutingId): this;
    setRoutingIdPrefix(prefix: string): this;
    enableSubscriber(): this;
    enableSubscriber(endpoint: string): this;
    subscriberConnections(): ZLinkEndpointConnections;
}

export interface ZLinkFanoutClient {
    publish(channelName: string, event: unknown): ZLinkFanoutPublishCall;
    publish(channelName: string, topic: string, event: unknown): ZLinkFanoutPublishCall;
}

export interface ZLinkFanoutPublishCall {
    submit(signal?: AbortSignal): Promise<ZLinkSubmitResult>;
}
```

Entry Spot의 RID는 Framework가 MeshNode startup에서 발급한다. 애플리케이션은 Entry Spot RID를 구성값으로
제공하지 않는다. Actor create는 선택한 owner MeshNode의 Entry Spot membership과 Actor Ready barrier를 같은
lifecycle에서 완료한다. 이후 one-way 업무 message는 Actor queue로 직접 전달되며 Entry Spot callback을
경유하지 않는다.

`onActorRelocated?(actor)`는 default no-op이 가능한 maintenance 전용 async Entry Spot callback이다. Maintenance가
Actor를 target Entry Spot에 materialize할 때 Snapshot은 Actor adapter `restore(...)`를 먼저 완료하고 Recreate는
payload restore 없이 factory materialization을 완료한다. Accepted journal은 checksum, 순서와 fence만 검증해
application handler가 실행하지 않는 target staging queue에 준비한 뒤 Prepared CAS와 Location
authority·Entry membership commit을 수행한다. Commit 뒤 target `onActorRelocated(...)`와 source
`onLeaveActor(...)`를 완료하고 old Entry membership의 durable cleanup을 끝낸 다음 accepted journal을 replay한다.
Source process가 종료되면 exact source fence의 durable cleanup terminal이 source callback 완료를 대신한다.
두 callback과 old Entry membership cleanup, replay, 남은 source resource cleanup, Completed CAS, route ACK와
steady normalization을 모두 완료한 뒤 Actor dispatch admission을 연다. 두 callback
중 하나가 throw하거나 rejected Promise로 끝나도 authority를 source로 rollback하지 않고 target을 sealed 상태로
유지한 채 exact relocation fence로 retry한다. 두 callback은 at-least-once 호출될 수 있으므로 retry-safe해야 한다.

일반 same-node·remote User·Entry Spot join은 기존 admission·joined callback과 source leave callback을 사용하며
`onActorRelocated(...)`를 호출하지 않는다. Maintenance relocation에서는 target의 일반 join callback을 호출하지 않지만
실제 source Entry membership을 끝내므로 source `onLeaveActor(...)`는 commit 뒤 호출한다. Whole User Spot aggregate
relocation에서는 membership이 유지되므로 member Actor에 대한 Entry Spot 또는 User Spot membership callback을
모두 호출하지 않는다. Disabled operation에서도 `onActorRelocated(...)`를 호출하지 않는다.

`ZLinkFanoutClient.publish(...)`는 typed event의 packet name을 topic으로 사용하는 호출과 topic을 명시하는
호출을 함께 제공한다. `ZLinkFanoutPublishCall`은 local publisher transport의 bounded admission만
`ZLinkSubmitResult`로 반환한다. Logical Multicast의 `ZLinkPublishCall`과 target count를 포함한
`ZLinkPublishResult`는 classic fanout에 사용하지 않는다. Subscriber가 0개여도 publisher local queue가
event를 수락하면 `Submitted`다.

Topic을 명시하는 overload에 내부 liveness용 exact byte `01 5A 4C 46 31`을 전달하면 transport를 시작하지
않고 `ZLinkConfigurationException`을 발생시킨다. Topic을 생략한 overload는 typed event의 packet name을
사용하므로 이 내부 topic을 만들지 않는다.

Location store를 등록한 fanout publisher는 고정 Publisher RID와 자동 할당 중 하나를 startup 전에
선택하고 전용 descriptor를 게시한다. Store가 없는 publisher는 listener endpoint를 수동으로 전달하는
대상으로 사용할 수 있지만 RID allocation과 automatic discovery 등록은 수행하지 않는다. 인자 없는
`enableSubscriber()`는 같은 ChannelName의 유효한 publisher descriptor를 location store에서 조회해 모두
연결한다. Endpoint를 받는 overload는 명시한 endpoint만 사용하는 manual subscriber를 구성한다. 한
channel에서 두 subscriber mode를 함께 설정하면 startup이 실패한다. Automatic subscriber는 location
store가 필요하고, manual publisher와 manual subscriber만 사용하는 host에는 필요하지 않다.
Publisher는 descriptor만 게시하고 subscriber endpoint로 outbound connect를 시작하지 않는다. Subscriber만
publisher endpoint로 connect하며 automatic subscriber는 Publisher RID와 lifecycle generation마다 connection
intent 하나를 만든다.

## 2. Metrics, monitoring과 packet

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

export declare function ZLinkPacket(packetName: string): ClassDecorator;
```

Node runtime은 Instance Spot 관측값도 `ZLinkMeter`로 기록한다. 이 언어에서 사용하는 Instance Spot
계기 이름 카탈로그는 다음 여섯 값이며, 이름·종류·단위와 attribute 제한은
[runtime-metrics](../../../51-runtime-metrics.ko.md)가 소유한다.

- `zlink.instance_spot.activations`
- `zlink.instance_spot.activation.duration`
- `zlink.instance_spot.pending.messages`
- `zlink.instance_spot.pending.bytes`
- `zlink.instance_spot.claim.conflicts`
- `zlink.instance_spot.takeovers`

One-way placement·activation 실패는 `zlink.mesh_node.messages.dropped`에
`surface=instance_spot`을 붙여 기록한다. `ZLinkMessageFlowEvent`도 별도 event ID를 추가하지 않고
`eventId=zlink.message_flow`, 같은 surface와 `outcome=dropped`를 사용한다. `instanceSpotType`에는 startup에
등록한 bounded type만 기록하며 Spot RID, owner ID와 internal authority fields는 metric attribute로 사용하지
않는다. `eventId=zlink.message_flow`의 reason은 `ZLinkMessageFlowReason`,
`eventId=zlink.dispatch_error`의 reason은 `ZLinkDispatchErrorReason` 값만 사용한다.

## 3. Location peer와 Logical Multicast

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
    readonly leaseGeneration: bigint;
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
    submit(signal?: AbortSignal): Promise<ZLinkPublishResult>;
}

export interface ZLinkLogicalMulticastDetail {
    readonly snapshotRemoteNodeCount: bigint;
    readonly admittedRemoteNodeCount: bigint;
    readonly droppedRemoteNodeCount: bigint;
    readonly unreachableRemoteNodeCount: bigint;
    readonly snapshotLocalSpotCount: bigint;
    readonly admittedLocalSpotCount: bigint;
    readonly droppedLocalSpotCount: bigint;
}

export interface ZLinkPublishResult {
    readonly status: ZLinkSubmitStatus;
    readonly detail: ZLinkLogicalMulticastDetail;
}

export interface ZLinkPublishContext extends ZLinkHandlerContext {
    readonly channelName: string;
    readonly topic: string;
    readonly source?: string;
}

export interface ZLinkPublishHandler<TMessage> {
    handle(message: TMessage, context: ZLinkPublishContext): Promise<void>;
}
```

생략한 `pageSize`는 100이다. 명시한 값은 `1..1000` 범위의 정수여야 하며 continuation token은 provider만
해석하는 opaque value다.

## 4. Request와 RouteMesh client

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
    readonly channelName: string;
}

export interface ZLinkRequestHandler<TRequest, TResponse> {
    handle(request: TRequest, context: ZLinkRequestContext): Promise<TResponse>;
}

export interface ZLinkRouteClient {
    sendToNode(meshName: string, targetNodeRid: RoutingId, message: unknown): ZLinkSendCall;
    requestToNode(meshName: string, targetNodeRid: RoutingId, request: unknown): ZLinkRequestCall;
    sendToChannel(channelName: string, message: unknown): ZLinkSendCall;
    requestToChannel(channelName: string, request: unknown): ZLinkRequestCall;
    sendToSpot(spotRid: SpotRid, message: unknown): ZLinkSpotSendCall;
    requestToSpot(spotRid: SpotRid, request: unknown): ZLinkSpotRequestCall;
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
    readonly leaseGeneration: bigint;
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
    channel(channelName: string): ZLinkMeshChannelRuntimeOptions;
}

export interface ZLinkMeshChannelRuntimeOptions {
    weight: number;
}
```

`maxMessageSize`는 startup 전에만 설정하며 실행 중 property를 제공하지 않는다. `0`은 binding 또는
transport가 수신할 수 있는 최대 complete message 크기로 정규화한다. Transport가 unlimited이면 service
wire의 `uint32` 표현 한계에서 envelope overhead를 뺀 값을 사용한다. 양수는 그 표현 한계를 넘을 수 없으며
넘으면 startup 설정 오류로 거부한다. Peer는 정규화한 값을 내부 handshake로 교환하고 sender와 receiver는
두 값 중 작은 effective bound를 complete message allocation 전에 적용한다. 이 negotiation을 위한 public
option은 제공하지 않는다.

## 5. Route handler와 one-way submit

```ts
export interface ZLinkRouteRequestContext extends ZLinkRouteSendContext {
}

export interface ZLinkRouteRequestHandler<TRequest, TReply> {
    handle(request: TRequest, context: ZLinkRouteRequestContext): Promise<TReply>;
}

export interface ZLinkRouteSendContext extends ZLinkHandlerContext {
    readonly meshName: string;
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
    readonly channelName: string;
}

export interface ZLinkSendHandler<TMessage> {
    handle(message: TMessage, context: ZLinkSendContext): Promise<void>;
}
```

모든 server one-way call의 `submit(signal?)`과 session Actor `relay(...)`는 local outbound admission 결과를
`Promise`로 반환한다. 유효한 call은 pending 공간을 확인하기 전에 해당 family가 실제로 사용하는 admission
primitive를 non-blocking 방식으로 정확히 한 번 호출한다. Remote 경로는 transport submit을 사용하고, local
경로는 mailbox 또는 relay queue admission을 사용한다. 이 첫 시도가 즉시 성공하면 pending 공간이 차 있어도
이미 완료된 `Promise`로 `Submitted`를 반환할 수 있다. Remote transport의 capacity가 부족하거나 local
admission capacity가 부족할 때만 해당 operation family의 send timeout까지 기다린다. 첫 시도 뒤 bounded
pending 공간도 가득 차 있으면 `Backpressured`, deadline까지 수락되지 않으면 `TimedOut`으로 완료한다. Local
경로가 즉시 수락할 수 있는데 pending 공간만 가득 찼다는 이유로 `Backpressured`를 반환하면 안 된다.
`Submitted`는 remote handler나 subscriber가 실행을 마쳤다는 뜻이 아니다.

`AbortSignal`이 `submit(...)` 또는 `relay(...)` 전에 이미 abort 상태이면 runtime admission을 시작하지 않고
`AbortError`로 reject한다.
Admission이 시작된 뒤에는 abort, timeout, shutdown과 수락 중 먼저 확정된 terminal 결과만 남기며, abort나
timeout 뒤에 같은 operation을 다시 제출하지 않는다. Cancellation은 `ZLinkSubmitStatus`에 값을 추가하지
않는다. 잘못된 argument·handle·state와 중복 submit은 result status가 아니라 exceptional completion으로
처리한다. STREAM reply의 유효한 첫 terminator는 transport를 시작하기 전에 one-shot reply token을 원자적으로
claim하고 소비한다. 같은 token에서 만든 두 call이 경쟁하면 claim에 실패한 call은 transport를 시도하지 않고
exceptional completion으로 끝난다. Token을 소비한 call이 timeout, `Backpressured` 또는 abort로 끝나도 token을
다시 사용할 수 없다. 이미 사용한 token도 exceptional completion으로 처리한다. STREAM reply는 client request
timeout을 전달받지 않으며 해당 STREAM socket의 send timeout만 사용한다.

RouteMesh node·Channel·Spot·Actor는 선택한 MeshNode ROUTER, ClientServer는 client DEALER, classic fanout은
publisher socket, STREAM send·reply는 해당 STREAM socket의 send timeout을 사용한다. Bound session은
local·remote Actor route가 바뀌어도 framework socket send timeout 하나를 사용한다. 공개 설정이 없으면
1초를 사용한다. One-way admission에 사용하는 millisecond 설정은 `1..2147483647` 범위의 유한 정수만
허용한다. `undefined`는 기본값을 선택하며 `0`, 음수, 정수가 아닌 값과 상한 초과는
`ZLinkConfigurationError`로 거부한다.

Logical Multicast의 `ZLinkPublishCall.submit(...)`은 예외다. Framework는 pending queue 없이 bounded I/O
executor에 direct handoff한다. 즉시 worker slot을 얻지 못하면 blocking publish attempt를 시작하지 않고
`Backpressured`를 반환한다. Slot을 얻은 뒤 publish attempt가 시작되기 전에는 abort와 shutdown이 operation
시작을 막을 수 있다. Publish attempt를 시작한 시점이 operation commit barrier이며, 그 뒤의 abort는 이미 확정한 snapshot
operation을 중단하지 않는다. 이때
`Promise`는 Framework runtime이 확정한 최종 `ZLinkPublishResult`로 완료한다. Target별 send timeout 뒤에
확정된 capacity 실패는 `Backpressured`와 partial detail로 유지하며 `TimedOut`으로 바꾸거나 전체 publish를 다시 실행하지
않는다. Snapshot target이 모두 0이면 `TargetNotFound`다. Remote capacity drop이 없고 모든 remote target의
route가 준비되지 않은 경우에는 `Submitted`와 unreachable detail을 반환할 수 있다. Local Spot drop은
top-level status를 바꾸지 않고 detail에만 반영한다. Remote count는
`snapshotRemoteNodeCount === admittedRemoteNodeCount + droppedRemoteNodeCount + unreachableRemoteNodeCount`를
만족한다.

## 6. Serializer와 STREAM session

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
    readonly actorId: ActorId;
    readonly ref: ActorRef;
    relay(payload: ZLinkMessage, signal?: AbortSignal): Promise<ZLinkSubmitResult>;
    relay(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage,
        signal?: AbortSignal): Promise<ZLinkSubmitResult>;
    notifyDisconnected(signal?: AbortSignal): Promise<void>;
}

export interface ZLinkSessionActors {
    readonly bound: readonly ZLinkSessionActor[];
    bind(actor: ActorRef, signal?: AbortSignal): Promise<ZLinkSessionActor>;
    bindOrGet(actor: ActorRef, signal?: AbortSignal): Promise<ZLinkSessionActor>;
    find(actorId: ActorId): ZLinkSessionActor | undefined;
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
    readonly metadata: ZLinkMessageMetadata;
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
    submit(signal?: AbortSignal): Promise<ZLinkSubmitResult>;
}

export interface ZLinkSessionSendCall {
    metadata(key: string, value: string): this;
    metadata(metadata: ZLinkMessageMetadata): this;
    compress(enabled?: boolean): this;
    submit(signal?: AbortSignal): Promise<ZLinkSubmitResult>;
}
```

Payload만 받는 `relay(...)`는 one-way admission이다. Dispatch context를 받는 overload는 explicit current
STREAM request reply capability를 호출 즉시 runtime에 이전한다. Submitted면 Actor typed reply가 original
STREAM correlation을 terminal-once로 완료하고 admission failure면 Framework가 같은 correlation을 typed
failure로 완료한다. Caller는 별도 reply·retry를 하지 않는다. One-way dispatch context는 reply
capability가 없으므로 admission만 반환한다.
