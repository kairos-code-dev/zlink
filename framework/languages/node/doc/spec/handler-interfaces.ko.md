<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [표면 매핑 정책](../internals/dotnet-to-node-surface-mapping.ko.md) | [다음: ZLink Framework NestJS Channel Messaging](./nestjs-channel-messaging.ko.md)
<!-- framework-adapter-nav:end -->

[Node.js 묶음](../README.ko.md) | [channel](./nestjs-channel-messaging.ko.md) | [SPOT](./nestjs-spot.ko.md) | [STREAM](./nestjs-stream.ko.md) | [Actor](./nestjs-actor.ko.md) | [Monitoring](./nestjs-monitoring.ko.md) | [Registry](./nestjs-registry.ko.md) | [샘플 계획](../sample-implementation-plan.ko.md)

# ZLink Framework Node.js Interface Catalog

## 1. 목적

이 문서는 한 가지 역할만 맡는다. `Node.js` `ZLink Framework`(NestJS 통합)가
노출하는 **모든 공용 interface / decorator / context / enum / options / client /
builder 정의** 를 한곳에 모아 두는 카탈로그다.

이 문서는 [.NET Interface Catalog](../../../dotnet/doc/spec/handler-interfaces.ko.md)
를 TypeScript / NestJS 표면으로 옮긴 결과다. 번역 규칙은
[.NET → Node.js 표면 매핑 정책](../internals/dotnet-to-node-surface-mapping.ko.md)
이 소유한다. 두 문서의 표기가 어긋나면 dotnet **코드**
(`framework/languages/dotnet/src/Zlink.Framework/Contracts/`)가 기능의 최종 기준이다.

이 문서대로 구현하면 .NET 버전과 **동일한 계약 표면**(contract surface)을 가진 Node.js
framework 가 나온다. 개념·의미론·동작은 dotnet 과 동일하고, 표면만 NestJS / TypeScript
로 옮긴다.

번역 규칙(요약):

- `IZLinkX` → `ZLinkX` (C# 의 `I` prefix 를 떼되 `ZLink` prefix 는 유지)
- `ValueTask` / `ValueTask<T>` → `Promise<void>` / `Promise<T>`
- `CancellationToken cancellationToken` → 생략 또는 `signal?: AbortSignal`
- `HandleAsync` → `handle` (메서드는 camelCase)
- `record` / `readonly record struct` → TS `interface` 또는 `type`
- `enum` → TS string enum
- `RoutingId(string)` → `type RoutingId = string`
- `Message` / `ReadOnlyMemory<byte>` → `Message`(payload 구조 타입) / `Buffer`
- `TimeSpan period` → `periodMs: number`
- generic 인자는 그대로 유지 (`<TSpot, TActor, TRequest, TReply>`)
- attribute → decorator: `[ZLinkRequest]` → `@ZLinkRequest()`,
  `[ZLinkPacket("x")]` → `@ZLinkPacket('x')`

사용 예시나 프로그래밍 모델 설명은 여기 넣지 않는다. 실제 사용법은 아래 문서를 참고한다.

- 서버 간 messaging 프로그래밍 모델 → [nestjs-channel-messaging.ko.md](./nestjs-channel-messaging.ko.md)
- 서버 간 messaging 샘플 → [sample-implementation-plan.ko.md](../sample-implementation-plan.ko.md)
- SPOT 통합 → [nestjs-spot.ko.md](./nestjs-spot.ko.md)
- SPOT 샘플 → [sample-implementation-plan.ko.md](../sample-implementation-plan.ko.md)
- STREAM 통합 → [nestjs-stream.ko.md](./nestjs-stream.ko.md)
- STREAM 샘플 → [sample-implementation-plan.ko.md](../sample-implementation-plan.ko.md)
- Actor 통합 → [nestjs-actor.ko.md](./nestjs-actor.ko.md)
- Registry 통합 → [nestjs-registry.ko.md](./nestjs-registry.ko.md)

### 1.1 공통 표면 규칙

- TypeScript 에서는 C# 의 `I` prefix 인터페이스 관례를 쓰지 않는다.
- 서버 framework public 타입은 `ZLink` prefix(대문자 `L`)를 쓴다.
- 메서드·필드·함수는 `camelCase`, 클래스·interface·decorator·enum 타입은 `PascalCase` 다.
- `RoutingId` 는 wire 식별자이므로 분기된 `string` alias 로 둔다.

```ts
/** transport routing id. C# RoutingId(string) 의 TS 대응. */
export type RoutingId = string;

/** payload message. C# Message 의 TS 대응이며 framework 가 구조를 소유한다. */
export interface Message {
  data(): Buffer;
  toBytes(): Uint8Array;
  copy(): Message;
  size(): number;
  isEmpty(): boolean;
  getString(encoding?: BufferEncoding): string;
  close(): void;
}

/** stream wire header. codec 가 해석하기 전까지는 framework 가 payload 로만 취급한다. */
export type ZlinkStreamHeader = unknown;

/** actor runtime handle ref. C# ActorRef 의 TS 대응. */
export interface ActorRef {
  readonly nodeRid: RoutingId;
  readonly actorId: string;
  readonly generation: bigint;
}
```

## 2. 인터페이스 전체 목록

| 분류 | 인터페이스 | 역할 | section |
|------|-----------|------|---------|
| context | `ZLinkHandlerContext` | 모든 handler context의 공통 기반 | 3.1 |
| context | `ZLinkRequestContext` | request handler context | 3.2 |
| context | `ZLinkSendContext` | send handler context | 3.2 |
| context | `ZLinkPublishContext` | publish handler context (topic, source) | 3.2 |
| context | `ZLinkRouteSendContext` | routed channel send handler context | 4.2.1 |
| context | `ZLinkRouteRequestContext` | routed channel request handler context | 4.2.1 |
| context | `ZLinkSpotActorSendContext` | Spot actor send handler context (metadata) | 4.4.2 |
| context | `ZLinkSpotActorRequestContext` | Spot actor request handler context (metadata + reply 옵션) | 4.4.2 |
| handler | `ZLinkRequestHandler<TRequest, TResponse>` | request-response handler | 4.1 |
| handler | `ZLinkSendHandler<TMessage>` | one-way send handler | 4.2 |
| handler | `ZLinkRouteSendHandler<TMessage>` | routed channel one-way send handler | 4.2.1 |
| handler | `ZLinkRouteRequestHandler<TRequest, TReply>` | routed channel request-response handler | 4.2.1 |
| handler | `ZLinkPublishHandler<TMessage>` | pub/sub publish handler | 4.3 |
| handler | `ZLinkSpotPacketHandler<TSpot, TMessage>` | SPOT one-way packet handler | 4.3.1 |
| handler | `ZLinkSpotRequestHandler<TSpot, TRequest, TReply>` | SPOT request-response handler | 4.3.1 |
| handler | `ZLinkSpotSubscriptionHandler<TSpot, TEvent>` | SPOT subscription handler | 4.3.1 |
| handler | `ZLinkSpotTimerHandler<TSpot>` | SPOT lifecycle timer handler | 4.3.1 |
| lifecycle | `ZLinkSpot` | user Spot lifecycle registration base | 4.3.1 |
| lifecycle | `ZLinkEntrySpot` | Entry Spot lifecycle registration base | 4.3.1 |
| registry | `ZLinkActorHandlerRegistry` | actor handler 등록 표면 | 4.3.1 |
| registry | `ZLinkSpotHandlerRegistry` | spot handler 등록 표면(actor registry 확장) | 4.3.1 |
| context | `ZLinkSpotContext` | user Spot 실행 context (handlers, outbound, timer) | 4.3.1 |
| context | `ZLinkEntrySpotContext` | Entry Spot 실행 context | 4.3.1 |
| handler | `ZLinkSpotActorSendHandler<TSpot, TActor, TMessage>` | user Spot actor send handler | 4.4.2 |
| handler | `ZLinkSpotActorRequestHandler<TSpot, TActor, TRequest, TReply>` | user Spot actor request handler | 4.4.2 |
| handler | `ZLinkSpotPostActorJoinedHandler<TSpot, TActor>` | actor join commit 이후 lifecycle handler | 4.3.1 |
| handler | `ZLinkSpotActorLeftHandler<TSpot, TActor>` | actor leave commit 이후 lifecycle handler | 4.3.1 |
| handler | `ZLinkSpotActorDisconnectedHandler<TSpot, TActor>` | actor disconnect 알림 handler | 4.3.1 |
| handler | `ZLinkEntrySpotActorSendHandler<TEntrySpot, TActor, TMessage>` | Entry Spot actor send handler | 4.4.2 |
| handler | `ZLinkEntrySpotActorRequestHandler<TEntrySpot, TActor, TRequest, TReply>` | Entry Spot actor request handler | 4.4.2 |
| handler | `ZLinkEntrySpotActorDisconnectedHandler<TEntrySpot, TActor>` | Entry Spot actor disconnect handler | 4.3.1 |
| handler | `ZLinkSpotActorJoinHandler<TSpot, TActor, TRequest, TReply>` | spot에 actor가 join할 때 호출되는 handler | 4.4.1 |
| value | `ZLinkSpotActorChangeKind` | actor membership 변화 enum | 4.3.1 |
| value | `ZLinkSpotActorChangeResult` | actor membership 변화 결과 | 4.3.1 |
| options | `ZLinkSpotActorReplyOptions` | spot actor request reply 옵션 빌더 | 4.4.2 |
| stream | `ZLinkStream` | stream I/O와 peer 식별 | 4.4 |
| value | `ZLinkStreamSessionError` | stream session error category enum | 4.4 |
| value | `ZLinkStreamError` | stream error detail | 4.4 |
| value | `ZLinkStreamDiagnostic` | stream error native diagnostic | 4.4 |
| handler | `ZLinkSession` | stream session lifecycle + dispatch callback | 4.4 |
| context | `ZLinkSessionContext` | stream session identity, client, actor binding, close 제어 | 4.4 |
| context | `ZLinkSessionClient` | session에서 client stream으로 send/reply | 4.4 |
| context | `ZLinkSessionActors` | session에서 actor handle bind와 lookup | 4.4 |
| call | `ZLinkSessionSendCall` | session client send 빌더 | 4.4 |
| call | `ZLinkSessionReplyCall` | session client reply 빌더 | 4.4 |
| value | `ZLinkSessionActor` | session이 dispatch target으로 들고 있는 actor handle | 4.4.1 |
| handler | `ZLinkSessionPacketHandler<TSessionContext>` | session packet handler | 4.4 |
| dispatcher | `ZLinkSessionPacketDispatcher<TSessionContext>` | 등록된 session packet handler dispatch | 4.4 |
| handler | `ZLinkActor` | actor runtime 안에서 생성되는 application actor | 4.4.1 |
| context | `ZLinkActorContext` | actor 상태 조회와 spot join 호출 | 4.4.1 |
| call | `ZLinkActorJoinSpotCall` | actor → user Spot join 빌더 | 4.4.1 |
| call | `ZLinkActorJoinEntrySpotCall` | actor → Entry Spot join 빌더 | 4.4.1 |
| value | `ZLinkActorJoinResult<TReply>` | actor join 결과 (코드 + ref + reply) | 4.4.1 |
| factory | `ZLinkActorFactory` | actor type별 actor 생성 | 4.4.1 |
| manager | `ZLinkActorManager` | actor id와 actor type으로 actor 생성, 조회, 재사용 | 4.4.1 |
| value | `ZLinkMessageMetadata` | actor/bound session call 의 metadata snapshot | 4.4.2 |
| policy | `ZLinkMessageMetadataPolicy` | metadata forwarding 허용 여부 | 4.4.2 |
| value | `ZLinkDispatchMode` | dispatch activation/performance mode enum | 4.4.3 |
| options | `ZLinkDispatchOptions` | dispatch mode + unhandled + diagnostics 설정 | 4.4.3 |
| options | `ZLinkUnhandledDispatchOptions` | 미등록 packet 처리 정책 | 4.4.3 |
| options | `ZLinkDiagnosticsOptions` | message flow 진단 정책 | 4.4.3 |
| value | `ZLinkUnhandledDispatchAction` | 미등록 packet 처리 동작 enum | 4.4.3 |
| value | `ZLinkMessageFlowLogMode` | message flow log 모드 enum | 4.4.3 |
| codec | `ZLinkCodecRegistryBuilder` | codec registry builder | 4.5 |
| client | `ZLinkSendCall` | channel send 빌더 | 5.1 |
| client | `ZLinkRequestCall` | channel request 빌더 | 5.1 |
| client | `ZLinkPublishCall` | publish 빌더 | 5.4 |
| client | `ZLinkChannelClient` | 일반 channel request/send outbound client | 5.1 |
| client | `ZLinkSpotOutbound` | SPOT outbound client | 5.2 |
| client | `ZLinkRouteClient` | route mesh channel 로 target node 호출 | 5.2.1 |
| client | `ZLinkSpotPublisherClient` | spot channel publish client | 5.3 |
| client | `ZLinkFanoutClient` | pub/sub fanout publish client | 5.4 |
| client | `ZLinkBoundSession` | 현재 actor → 현재 client session 호출 | 5.6 |
| call | `ZLinkBoundSessionSendCall` | bound session send 빌더 | 5.6 |
| resolver | `ZLinkSpotRemoteAddressResolver` | spot rid에서 user Spot route 조회 | 5.7 |
| value | `ZLinkSpotKind` | spot 종류 enum (Entry/User) | 5.7 |
| value | `ZLinkSpotRemoteAddress` | resolver 가 돌려주는 주소 | 5.7 |
| manager | `ZLinkSpotManager` | spot 인스턴스 생성/조회/삭제 | 6.3 |
| value | `ZLinkSpotCreateResult` | spot 생성 결과 | 6.3 |
| value | `ZLinkSpotInfo` | spot 조회 결과 | 6.3 |
| builder | `ZLinkFrameworkOptions` | framework 등록 루트 builder (= module options) | 6.1 |
| builder | `ZLinkClientServerChannelBuilder` | client-server channel 등록 builder | 6.1 |
| builder | `ZLinkFanoutChannelBuilder` | fanout (pub/sub) channel 등록 builder | 6.1 |
| builder | `ZLinkDealerMeshChannelBuilder` | dealer mesh channel 등록 builder | 6.1 |
| builder | `ZLinkRouteChannelBuilder` | route channel 등록 builder | 6.1 |
| builder | `ZLinkRouteMeshChannelBuilder` | route mesh channel 등록 builder | 6.1 |
| builder | `ChannelServerCapabilityBuilder` | channel server capability builder | 6.1 |
| builder | `ChannelClientCapabilityBuilder` | channel client capability builder | 6.1 |
| builder | `DealerMeshChannelClientCapabilityBuilder` | dealer mesh client capability builder | 6.1 |
| builder | `ChannelPublisherCapabilityBuilder` | channel publisher capability builder | 6.1 |
| builder | `ChannelSubscriberCapabilityBuilder` | channel subscriber capability builder | 6.1 |
| builder | `ZLinkStreamNodeBuilder` | STREAM node 등록 builder | 6.1 |
| builder | `ZLinkSpotNodeBuilder` | SPOT node 등록 builder | 6.3 |
| builder | `ZLinkSpotMeshBuilder` | SPOT mesh 등록 builder | 6.3 |
| builder | `ZLinkSpotMeshNodeBuilder` | SPOT mesh node 등록 builder | 6.3 |
| builder | `SpotRouterCapabilityBuilder` | spot router capability builder | 6.3 |
| builder | `SpotPubSubCapabilityBuilder` | spot pub/sub capability builder | 6.3 |
| builder | `SpotPublisherClientCapabilityBuilder` | spot publisher client capability builder | 6.3 |
| builder | `SpotChannelClientCapabilityBuilder` | spot channel client capability builder | 6.3 |
| builder | `ZLinkSpotRouteChannelAcceptanceBuilder` | spot route 수락 builder | 6.3 |
| builder | `ZLinkDiscoveryBuilder` | discovery endpoint builder | 6.1 |
| builder | `ZLinkMetadataPolicyBuilder` | metadata forward 정책 builder | 6.1 |
| options | `ZLinkSocketConfig` | 공통 socket 옵션 | 6.4 |
| options | `ZLinkRouteConfig` | routed peer 정책 옵션 | 6.4 |
| options | `ZLinkOutboundRouteConfig` | outbound route 정책 옵션 | 6.4 |
| options | `ZLinkSpotPublisherConfig` | spot publisher 옵션 | 6.4 |
| options | `ZLinkSpotSubscriberConfig` | spot subscriber 옵션 | 6.4 |
| options | `ZLinkEntrySpotOptions` | Entry Spot routing id 옵션 | 6.4 |
| options | `ZLinkRegistrySpotRemoteAddressesOptions` | registry 기반 spot 주소 옵션 | 6.1 |
| config | `ZLinkEndpointConnections` | manual 연결 편집 표면 | 6.2 |
| timer | `ZLinkTimer` | timer handle | 7 |
| options | `ZLinkTimerOptions` | timer 옵션 | 7 |
| value | `ZLinkTimerOverrunPolicy` | timer overrun 정책 enum | 7 |
| value | `ZLinkTimerTick` | timer tick payload | 7 |
| filter | `ZLinkHandlerFilter` | handler 전후 공통 처리 | 8 |
| filter | `ZLinkHandlerInvocation` | filter pipeline 호출 context | 8 |
| filter | `ZLinkHandlerDelegate` | filter pipeline next delegate | 8 |
| serializer | `ZLinkMessageSerializer` | `Message` payload 직렬화/역직렬화 | 4.5 |
| registry | `ZLinkRegistryQuery` | in-process Registry 조회 | 10.1 |
| registry | `ZLinkRegistryQueryClient` | 원격 Registry 조회 | 10.2 |
| options | `ZLinkMonitoringOptions` | runtime monitoring source 등록 옵션 | 10.3 |
| handler | `ZLinkRuntimeEventHandler<TEvent>` | runtime monitoring event handler | 10.3 |
| value | `ZLinkRuntimeEvent` | runtime event 공통 기반 | 10.3 |
| publisher | `ZLinkRuntimeEventPublisher` | runtime event publish 표면 | 10.3 |
| value | `ZLinkSocketEventKind`, `ZLinkSocketEvent` | socket runtime event | 10.3 |
| value | `ZLinkRegistryEventKind`, `ZLinkRegistryEvent` | registry runtime event | 10.3 |
| value | `ZLinkSpotEventKind`, `ZLinkSpotEvent` | spot runtime event | 10.3 |

decorator 와 enum, registry/monitoring model 의 전체 목록은 §11(decorator),
§10.3(monitoring), §10.4(registry/monitoring models)에 둔다.

## 3. Context 인터페이스

### 3.1 공통 context

모든 handler context 가 공유하는 최소 집합이다. C# `IZLinkHandlerContext` 대응이다.

```ts
export interface ZLinkHandlerContext {
  readonly channelName?: string;
  readonly packetName?: string;
  readonly contentType?: string;
  /** C# ConnectionAborted (CancellationToken) 의 TS 대응. */
  readonly connectionAborted?: AbortSignal;
}
```

DI 컨테이너(`IServiceProvider`)는 handler context 에 넣지 않는다. handler 안에서
서비스가 필요하면 context 의 service locator 가 아니라 provider 생성자 주입으로 받는다
(dotnet 과 동일 원칙).

### 3.2 파생 context

handler 종류마다 받아야 하는 부가 정보가 다르다. 그 차이를 공통 context 에 다 넣지 않고
종류별 context 타입을 둔다.

| context 타입 | 사용처 | 추가 정보 |
|-------------|--------|----------|
| `ZLinkRequestContext` | request handler | 공통 필드만 |
| `ZLinkSendContext` | send handler | 공통 필드만 |
| `ZLinkPublishContext` | publish handler | `topic`, `source` |
| `ZLinkRouteSendContext` | routed channel send handler | `routerChannelId`, `sourceNodeRid` |
| `ZLinkRouteRequestContext` | routed channel request handler | `routerChannelId`, `sourceNodeRid` |
| `ZLinkSpotActorSendContext` | Spot actor send handler | `metadata` |
| `ZLinkSpotActorRequestContext` | Spot actor request handler | `metadata`, `reply` |

```ts
/** request-response handler context. C# ZLinkRequestContext 대응. 공통 필드만. */
export interface ZLinkRequestContext extends ZLinkHandlerContext {}

/** one-way send handler context. C# ZLinkSendContext 대응. 공통 필드만. */
export interface ZLinkSendContext extends ZLinkHandlerContext {}

/** publish handler context. C# ZLinkPublishContext 대응. */
export interface ZLinkPublishContext extends ZLinkHandlerContext {
  readonly topic: string;
  readonly source?: string;
}
```

> 참고: 기존 draft 는 request/send/publish context 를 `ZLinkEventContext` 등으로
> 두었으나, dotnet **코드**는 publish handler context 를 `ZLinkPublishContext` 로,
> publish handler interface 를 `ZLinkPublishHandler` 로 고정한다. 코드 기준을 따른다.

SPOT 자신의 identity 는 별도 `Self` wrapper 를 두지 않는다. user Spot 은
`ZLinkSpotContext.spotRid` / `.nodeRid` 를, Entry Spot 은 `ZLinkEntrySpotContext` 의
같은 필드를 직접 읽는다(§4.3.1).

handler 호출마다 따라붙는 `ZLinkRequestContext` 등은 "이번 호출 한 건" 정보이고,
SPOT 객체가 들고 있는 `ZLinkSpotContext` 는 "이 spot 인스턴스 전체" 정보다.

## 4. Handler 인터페이스

### 4.1 request-response handler

요청 하나에 응답 하나가 대응하는 handler 다.

```ts
export interface ZLinkRequestHandler<TRequest, TResponse> {
  handle(
    request: TRequest,
    context: ZLinkRequestContext,
  ): Promise<TResponse>;
}
```

- `TRequest` 는 이미 decode 된 payload 다.
- `TResponse` 도 framework 가 encode 할 typed 결과다.
- raw multipart header 는 인자로 넘기지 않는다.
- 이 interface 를 구현한 class 는 discovery 가 자동으로 endpoint **후보** 로 찾는다.
  실제 노출은 §4.2 의 명시적 등록(`requestHandlers: [...]`,
  `@ZLinkHandlerGroup`)이 정한다.

decorator 방식(한 클래스에 여러 handler):

```ts
@Injectable()
@ZLinkHandlerGroup('api')
export class ProfileHandlers {
  @ZLinkRequest()
  async getProfile(request: GetProfileRequest, context: ZLinkRequestContext): Promise<GetProfileReply> {
    // ...
  }
}
```

### 4.2 send handler

응답을 돌려주지 않는 one-way 전송을 처리하는 handler 다.

```ts
export interface ZLinkSendHandler<TMessage> {
  handle(
    message: TMessage,
    context: ZLinkSendContext,
  ): Promise<void>;
}
```

interface 구현 방식과 `@ZLinkSend()` decorator 방식을 모두 지원한다. discovery 는
handler 를 **찾고**, 실제 노출은 명시적 등록이 정한다. 자동으로 모든 handler 를 모든
channel 에 열지 않는다(dotnet 정책 동일).

### 4.2.1 routed channel handler

route channel(`addRouteMeshChannel(...)`)이 수신하는 메시지를 처리하는 handler 다.
일반 channel handler 와 달리 source `RoutingId` 를 포함한 라우팅 정보를 context 로 함께 노출한다.

```ts
export interface ZLinkRouteSendHandler<TMessage> {
  handle(
    message: TMessage,
    context: ZLinkRouteSendContext,
  ): Promise<void>;
}

export interface ZLinkRouteRequestHandler<TRequest, TReply> {
  handle(
    request: TRequest,
    context: ZLinkRouteRequestContext,
  ): Promise<TReply>;
}

export interface ZLinkRouteSendContext extends ZLinkHandlerContext {
  readonly routerChannelId: string;
  readonly sourceNodeRid: RoutingId;
}

export interface ZLinkRouteRequestContext extends ZLinkHandlerContext {
  readonly routerChannelId: string;
  readonly sourceNodeRid: RoutingId;
}
```

route channel handler 등록은 route channel builder 가 책임진다. `addSendHandler(...)`,
`addRequestHandler(...)` 처럼 handler 타입만 지정하거나, message/reply 타입을 함께
지정하는 명시적 오버로드를 통해 등록한다. §6.1 의 `ZLinkRouteChannelBuilder` 참고.

### 4.3 publish handler

pub/sub 로 publish 된 메시지를 처리하는 handler 다.

```ts
export interface ZLinkPublishHandler<TMessage> {
  handle(
    message: TMessage,
    context: ZLinkPublishContext,
  ): Promise<void>;
}
```

이름은 producer 동사(`ZLinkFanoutClient.publish(...)`)에 맞춘다. 그래서
request / send / publish 세 표면이 같은 패턴으로 읽힌다. topic 이나 source 가 필요하면
별도 handler 이름을 늘리지 않고 `ZLinkPublishContext` 에서 읽는다.

interface 방식과 `@ZLinkPublish()` decorator 방식을 모두 지원한다.

### 4.3.1 SPOT lifecycle handler

이 절은 SPOT 객체의 lifecycle 표면과, 그 안에서 동작하는 handler 종류를 정의한다.

SPOT 은 Actor 와 같은 원칙을 따른다. callback 표면과 실행 context 표면을 분리한다.
user Spot 은 `ZLinkSpot` 을, Entry Spot 은 `ZLinkEntrySpot` 을 구현한다.

```ts
export interface ZLinkSpot {
  readonly context: ZLinkSpotContext;

  configure?(): void;

  onCreate?(createReqs: readonly Message[]): Promise<void>;

  onInitialize?(): Promise<void>;

  onClosing?(): Promise<void>;
}

export interface ZLinkEntrySpot {
  readonly context: ZLinkEntrySpotContext;

  configure?(): void;

  onInitialize?(): Promise<void>;

  onClosing?(): Promise<void>;
}
```

> 코드 기준(중요): dotnet `IZLinkSpotContext` 는 handler 등록 표면과 outbound 표면을
> **interface 상속**으로 합치지 않는다. 대신 `Handlers` / `Outbound` 두 sub-property 로
> 노출한다(`Contracts/Spots/Contracts.cs`). 즉 `context.handlers.addPacket(...)`,
> `context.outbound.sendToSpot(...)` 형태다. 이 문서는 코드 표면을 따른다.

```ts
/** C# IZLinkActorHandlerRegistry 대응. actor handler 등록 표면. */
export interface ZLinkActorHandlerRegistry {
  addHandler<THandler>(handlerType: Type<THandler>): void;
  addHandler<THandler>(handlerType: Type<THandler>, packetName: string): void;

  addActorPacket<THandler, TActor extends ZLinkActor>(
    handlerType: Type<THandler>,
    actorType: Type<TActor>,
  ): void;
  addActorPacket<THandler, TActor extends ZLinkActor>(
    handlerType: Type<THandler>,
    actorType: Type<TActor>,
    packetName: string,
  ): void;

  addPostActorJoined<THandler, TActor extends ZLinkActor>(
    handlerType: Type<THandler>,
    actorType: Type<TActor>,
  ): void;

  addActorLeft<THandler, TActor extends ZLinkActor>(
    handlerType: Type<THandler>,
    actorType: Type<TActor>,
  ): void;

  addActorDisconnected<THandler, TActor extends ZLinkActor>(
    handlerType: Type<THandler>,
    actorType: Type<TActor>,
  ): void;
}

/** C# IZLinkSpotHandlerRegistry 대응. actor registry 를 확장한 spot handler 등록 표면. */
export interface ZLinkSpotHandlerRegistry extends ZLinkActorHandlerRegistry {
  addPacket<THandler>(handlerType: Type<THandler>): void;

  addSubscribe<THandler>(handlerType: Type<THandler>, topic: string): void;

  addActorJoin<THandler, TActor extends ZLinkActor, TRequest, TReply>(
    handlerType: Type<THandler>,
    actorType: Type<TActor>,
  ): void;
  addActorJoin<THandler>(handlerType: Type<THandler>): void;
}

/** user Spot 실행 context. C# IZLinkSpotContext 대응. */
export interface ZLinkSpotContext {
  readonly spotRid: RoutingId;
  readonly nodeRid: RoutingId;

  readonly handlers: ZLinkSpotHandlerRegistry;
  readonly outbound: ZLinkSpotOutbound;

  leaveActor(actor: ZLinkActor): Promise<void>;

  addTimer<THandler>(
    name: string,
    periodMs: number,
    handlerType: Type<THandler>,
    options?: ZLinkTimerOptions,
  ): Promise<ZLinkTimer>;
}

/** Entry Spot 실행 context. C# IZLinkEntrySpotContext 대응. leaveActor 는 없다. */
export interface ZLinkEntrySpotContext {
  readonly spotRid: RoutingId;
  readonly nodeRid: RoutingId;

  readonly handlers: ZLinkSpotHandlerRegistry;
  readonly outbound: ZLinkSpotOutbound;

  addTimer<THandler>(
    name: string,
    periodMs: number,
    handlerType: Type<THandler>,
    options?: ZLinkTimerOptions,
  ): Promise<ZLinkTimer>;
}
```

> `Type<T>` 는 NestJS 의 `Type<T> = new (...args: any[]) => T` 이다. dotnet 의
> `where THandler : class` 처럼 "이 handler 타입을 spot scope 에서 resolve 해 달라"
> 는 등록 선언이다. service locator 가 아니다.

#### SPOT handler interface

```ts
export interface ZLinkSpotPacketHandler<TSpot, TMessage> {
  handle(spot: TSpot, message: TMessage): Promise<void>;
}

export interface ZLinkSpotRequestHandler<TSpot, TRequest, TReply> {
  handle(spot: TSpot, request: TRequest): Promise<TReply>;
}

export interface ZLinkSpotSubscriptionHandler<TSpot, TEvent> {
  handle(spot: TSpot, message: TEvent): Promise<void>;
}

export interface ZLinkSpotTimerHandler<TSpot> {
  handle(spot: TSpot, tick: ZLinkTimerTick): Promise<void>;
}
```

#### actor packet / lifecycle handler interface

```ts
export interface ZLinkSpotActorSendHandler<TSpot, TActor extends ZLinkActor, TMessage> {
  handle(
    spot: TSpot,
    actor: TActor,
    context: ZLinkSpotActorSendContext,
    message: TMessage,
  ): Promise<void>;
}

export interface ZLinkSpotActorRequestHandler<TSpot, TActor extends ZLinkActor, TRequest, TReply> {
  handle(
    spot: TSpot,
    actor: TActor,
    context: ZLinkSpotActorRequestContext,
    request: TRequest,
  ): Promise<TReply>;
}

export interface ZLinkSpotPostActorJoinedHandler<TSpot, TActor extends ZLinkActor> {
  handle(spot: TSpot, actor: TActor, result: ZLinkSpotActorChangeResult): Promise<void>;
}

export interface ZLinkSpotActorLeftHandler<TSpot, TActor extends ZLinkActor> {
  handle(spot: TSpot, actor: TActor, result: ZLinkSpotActorChangeResult): Promise<void>;
}

export interface ZLinkSpotActorDisconnectedHandler<TSpot, TActor extends ZLinkActor> {
  handle(spot: TSpot, actor: TActor): Promise<void>;
}

export interface ZLinkEntrySpotActorSendHandler<
  TEntrySpot extends ZLinkEntrySpot,
  TActor extends ZLinkActor,
  TMessage,
> {
  handle(
    entrySpot: TEntrySpot,
    actor: TActor,
    context: ZLinkSpotActorSendContext,
    message: TMessage,
  ): Promise<void>;
}

export interface ZLinkEntrySpotActorRequestHandler<
  TEntrySpot extends ZLinkEntrySpot,
  TActor extends ZLinkActor,
  TRequest,
  TReply,
> {
  handle(
    entrySpot: TEntrySpot,
    actor: TActor,
    context: ZLinkSpotActorRequestContext,
    request: TRequest,
  ): Promise<TReply>;
}

export interface ZLinkEntrySpotActorDisconnectedHandler<
  TEntrySpot extends ZLinkEntrySpot,
  TActor extends ZLinkActor,
> {
  handle(entrySpot: TEntrySpot, actor: TActor): Promise<void>;
}
```

> 코드 기준: Entry Spot lifecycle(join/left) handler 는 dotnet 에서 user Spot 과 **같은**
> `ZLinkSpotPostActorJoinedHandler<TEntrySpot, TActor>` / `ZLinkSpotActorLeftHandler<TEntrySpot, TActor>`
> 를 재사용한다(`TEntrySpot extends ZLinkEntrySpot`). Entry Spot 전용 별도 interface 를
> 두지 않는다. disconnected 만 `ZLinkEntrySpotActorDisconnectedHandler` 로 분리되어 있다.

#### actor membership 변화 값

```ts
export enum ZLinkSpotActorChangeKind {
  JoinSpot = 'joinSpot',
  JoinEntrySpot = 'joinEntrySpot',
  LeaveSpot = 'leaveSpot',
}

export interface ZLinkSpotActorChangeResult {
  readonly kind: ZLinkSpotActorChangeKind;
}
```

`ZLinkSpotActorChangeResult` 는 정의되지 않은 kind 를 허용하지 않는다. 0 값이나 enum 에
없는 값은 application callback 으로 올라오지 않는다.

#### lifecycle callback 의미

- `onCreate(createReqs)` 는 생성 요청이 넘긴 multipart payload 를 spot 상태로 해석하는
  단계다. framework 가 새 spot 인스턴스를 만든 경우에만 호출된다.
- `onInitialize()` 는 payload 와 무관한 lifecycle 준비 단계다. timer 등록 같은 작업을 둔다.
- 새 spot 생성 시 호출 순서: `configure()`, descriptor binding, `onCreate(createReqs)`,
  `onInitialize()`. 이미 ready 상태인 spot 을 반환하는 `getOrCreate(...)` 는
  `onCreate` / `onInitialize` 를 다시 호출하지 않는다.
- `onClosing()` 은 `ZLinkSpotManager.remove(...)` 로 정상 제거할 때 실행 문맥 안에서
  호출된다. destructor 가 아니므로 host shutdown / process 종료 시 반드시 호출되는 것은 아니다.

다음 handler 등록 호출은 `configure()` 단계 안에서만 허용된다. 초기화 후 추가하면
framework 가 예외를 던진다.

- `context.handlers.addPacket(...)`
- `context.handlers.addHandler(...)`
- `context.handlers.addActorPacket(...)`
- `context.handlers.addPostActorJoined(...)`
- `context.handlers.addActorLeft(...)`
- `context.handlers.addActorDisconnected(...)`
- `context.handlers.addSubscribe(...)`
- `context.handlers.addActorJoin(...)`

`addHandler(handlerType)` 는 `handlerType` 이 구현한 actor handler interface 를 보고
actor 타입 / send·request·lifecycle 종류 / packet 이름 기본값을 추론한다. handler 가
여러 actor handler interface 를 구현해서 모호하면 명시적
`addActorPacket(handlerType, actorType)`, `addPostActorJoined(...)`, `addActorLeft(...)`,
`addActorDisconnected(...)` 를 쓴다.

handler 선언은 두 방식이다.

- interface 방식: 위 handler interface 중 하나를 구현한다. 컴파일 타임 시그니처 확인이 강하다.
- decorator 방식: `@ZLinkSpotActorRequest()` 같은 method decorator 를 단다. 한 클래스에
  여러 역할을 모을 수 있지만 검증은 startup validation 단계에서 한다.

하나의 handler 타입이 두 개 이상의 actor packet interface 를 구현하거나, 두 개 이상의
actor handler decorator method 를 선언하면 startup validation 오류다.

##### Entry Spot 등록 예시

```ts
@Injectable()
export class PlayerEntrySpot implements ZLinkEntrySpot {
  constructor(readonly context: ZLinkEntrySpotContext) {}

  configure(): void {
    this.context.handlers.addHandler(AuthenticateHandler);
    this.context.handlers.addHandler(JoinMatchHandler);
    this.context.handlers.addHandler(PlayerEntryJoinedHandler);
    this.context.handlers.addHandler(PlayerEntryLeftHandler);
  }
}
```

decorator 방식 actor request handler:

```ts
@Injectable()
export class JoinMatchHandler {
  @ZLinkSpotActorRequest()
  async handle(
    entrySpot: PlayerEntrySpot,
    actor: PlayerActor,
    context: ZLinkSpotActorRequestContext,
    request: JoinMatchReq,
  ): Promise<JoinMatchRes> {
    // ...
  }
}
```

Entry Spot 에서 등록한 actor packet handler 는 해당 actor 가 user Spot 에 join 하기 전에
도착한 message 만 처리한다. join 이후 message 는 user Spot registry 가 담당한다.

##### user Spot 등록 예시

```ts
@Injectable()
export class MatchSpot implements ZLinkSpot {
  constructor(readonly context: ZLinkSpotContext) {}

  configure(): void {
    this.context.handlers.addHandler(PlaceMarkHandler);
    this.context.handlers.addHandler(PlayerMatchJoinedHandler);
    this.context.handlers.addHandler(PlayerMatchLeftHandler);
  }
}
```

user Spot handler 는 spot 객체와 actor 객체를 함께 받는다. room/game/stage 같은 실행
문맥 상태는 spot 에서, player 상태는 actor 에서 읽는다.

##### actor join/leave lifecycle 등록

actor 가 Entry Spot 또는 user Spot 에 들어오거나 빠져나간 직후 후속 처리는
`addPostActorJoined(handlerType, actorType)` 와 `addActorLeft(handlerType, actorType)` 로
등록한다. `onJoinActor` / `onLeaveActor` 같은 method override 는 공개 계약에 두지 않는다.

```ts
@Injectable()
export class PlayerMatchJoinedHandler {
  @ZLinkSpotPostActorJoined()
  async handle(
    spot: MatchSpot,
    actor: PlayerActor,
    info: ZLinkSpotActorChangeResult,
  ): Promise<void> {
    // ...
  }
}
```

`addPostActorJoined(...)` / `addActorLeft(...)` 로 등록한 handler 는 join/leave commit 이
끝난 뒤 동일 실행 문맥에서 호출된다. admission 결정 hook 이 아니라 commit 이후 후속
처리 단계다. disconnected handler 는 join/leave 와 별개이며 actor membership 을 바꾸지
않는다. `notifyDisconnected(...)` 로 대상 actor 를 명시하면 호출된다.

중복 등록은 같은 registry 단위로 검사한다. Entry Spot registry 와 각 user Spot registry 는
서로 별개의 namespace 다. 같은 registry 안에서 동일한 `actor type + packet kind + packet name`
조합이 둘 이상 등록되면 startup validation 오류다.

#### outbound / publish 표면

`ZLinkSpotContext.outbound` 가 노출하는 호출 표면(§5.2 `ZLinkSpotOutbound`):

- `context.outbound.sendToSpot(...)` / `requestToSpot(...)` 은 현재 SPOT 문맥에서 다른
  SPOT 으로 routed send/request 를 보낸다. target 은 `RoutingId` 로 지정하고, target node 와
  route channel 은 `ZLinkSpotRemoteAddressResolver` 가 해소한다.
- `context.outbound.publish(topic, ...)` 는 현재 SPOT 이 속한 active SPOT channel 로
  publish 한다.
- `context.outbound.sendToChannel(...)` / `requestToChannel(...)` 은 attach 된 channel client 를 호출한다.

#### timer 실행 문맥

framework runtime 이 만든 managed timer 가 tick 을 만들고, user Spot timer 는 그 tick 을
**같은 spot execution context** 안으로 enqueue 해서 `ZLinkSpotTimerHandler<TSpot>.handle(...)`
를 호출한다. Entry Spot timer 는 Entry Spot 전체 queue 에 묶이지 않고 별도 흐름에서 호출한다.
`addTimer(...)` 가 돌려주는 `ZLinkTimer.cancel()` 은 이 managed timer loop 를 중단하는
고수준 handle 이다(§7).

`requestToChannel(...)` 의 completion 도 **항상 같은 spot execution context** 안에서
실행된다. 임의의 thread/microtask 에서 promise 를 직접 완료하지 않는다.

#### 4.3.2 SPOT 실행 문맥 정책

핵심은 내부 구현이 아니라 사용자에게 보이는 실행 계약이다.

- 사용자는 `recv(...)` / `drain(...)` loop 를 직접 작성하지 않는다.
- 사용자는 고수준 표면만 쓴다: `context.handlers.addPacket(...)`,
  `context.handlers.addSubscribe(...)`, `context.addTimer(...)`, stream attach 등.
- 같은 user Spot 에 속한 handler, timer handler, channel reply continuation 은 framework 가
  정의한 동일한 실행 문맥 규칙을 따른다.
- 이 계약이 유지되는 한 사용자는 `match.actorCount` 같은 spot state 를 handler 내부에서
  자유롭게 다룰 수 있다.

mailbox 사용 여부, queue 개수, single consumer task 운용은 framework 내부 구현 영역이다.

### 4.4 stream session

stream session 의 lifecycle 과 packet 흐름을 정의한다. STREAM application 표면은 별도
`ZLinkStreamContext` 가 아니라 `ZLinkStream` 객체를 중심으로 본다.

```ts
export interface ZLinkStream {
  readonly sessionId: string;
  readonly routingId?: RoutingId;
  readonly localAddr?: string;
  readonly remoteAddr?: string;

  /** raw frame write. payload 의 소유권을 가져가지 않는다. C# Write(Message, SendFlags). */
  write(payload: Message, flags?: SendFlags): boolean;

  close(): Promise<void>;
}

export enum ZLinkStreamSessionError {
  Internal = 'internal',
  TransportError = 'transportError',
  /**
   * onError 로 전달되지 않는다. handshake 실패는 runtime monitoring 에만 남긴다.
   */
  HandshakeFailed = 'handshakeFailed',
}

export interface ZLinkStreamDiagnostic {
  readonly nativeCode: number;
  readonly message?: string;
}

export interface ZLinkStreamError {
  readonly error: ZLinkStreamSessionError;
  readonly diagnostic?: ZLinkStreamDiagnostic;
}
```

> 코드 기준: dotnet `IZLinkStream.Write` 는 `bool` 을 반환한다(기존 draft 의 `Promise<void>`
> 가 아님). 코드 표면을 따른다.

#### session lifecycle

```ts
export interface ZLinkSession {
  readonly context: ZLinkSessionContext;

  onConnected(): Promise<void>;

  onDisconnected(): Promise<void>;

  onError(error: ZLinkStreamError): Promise<void>;

  /**
   * framework 가 decode 한 ZlinkStreamHeader 와 Message payload 를 받는다.
   * payload 는 callback 동안 borrowed 이다. 읽거나 relayAsync 로 넘길 수 있지만,
   * callback 뒤에도 보관할 때만 별도 copy/move 한다.
   */
  onDispatch?(header: ZlinkStreamHeader, payload: Message): Promise<void>;
}

export interface ZLinkSessionContext {
  readonly sessionId: string;
  readonly routingId?: RoutingId;
  readonly localAddr?: string;
  readonly remoteAddr?: string;

  readonly client: ZLinkSessionClient;
  readonly actors: ZLinkSessionActors;

  close(): Promise<void>;
}

export interface ZLinkSessionClient {
  send<TMessage>(message: TMessage): ZLinkSessionSendCall;
  reply<TMessage>(message: TMessage): ZLinkSessionReplyCall;
}

export interface ZLinkSessionActors {
  readonly bound: ReadonlyArray<ZLinkSessionActor>;

  bind(actor: ZLinkActor): Promise<ZLinkSessionActor>;
  bind(actor: ActorRef): Promise<ZLinkSessionActor>;

  find(actorId: string): ZLinkSessionActor | undefined;
}

export interface ZLinkSessionSendCall {
  metadata(key: string, value: string): ZLinkSessionSendCall;
  packetName(messageName: string): ZLinkSessionSendCall;
  compress(): ZLinkSessionSendCall;
  submit(): Promise<void>;
}

export interface ZLinkSessionReplyCall {
  metadata(key: string, value: string): ZLinkSessionReplyCall;
  compress(): ZLinkSessionReplyCall;
  submit(): Promise<void>;
}
```

session 구현체는 framework 가 생성자에 넘긴 `ZLinkSessionContext` 를 `context` 로 그대로
노출해야 한다. 이는 문서 가이드가 아니라 runtime 이 검증하는 계약이다.

- `close()` 는 현재 session 의 stream peer 연결을 서버 쪽에서 끊는다. 인증 실패, protocol
  위반, idle timeout 처럼 더 이상 packet 을 받을 이유가 없을 때 호출한다. 연결 종료 후
  session binding 정리는 framework 가 `sessionId + bindingToken` 기준으로 담당한다.
- `stream.write(...)` 는 framework Header 기반 packet session 에서 stream 으로 보내는
  low-level submit 이다. 일반 application 코드는 `context.client.reply(...)`,
  `ZLinkBoundSession` 같은 helper 를 쓴다.
- session handler 에서 다른 channel 로 send/request 하려면 `ZLinkSessionContext` 가 아니라
  DI 로 주입받은 `ZLinkChannelClient` 를 쓴다. channel 호출은 현재 stream peer 가 아니라
  channel 이름에 맞는 framework client socket 으로 나가기 때문이다.
- `onError(...)` 는 application handler 내부 예외 callback 이 아니다. `SocketMonitor` 로
  관찰 가능한 transport 오류만 `ZLinkStreamError` 로 다시 올린다.

session callback 실행 계약:

- session callback 은 native/socket callback 안에서 직접 호출하지 않는다. framework 가
  managed task 로 넘긴 뒤 호출한다.
- 같은 session 안에서는 `onConnected`, `onDispatch`, `onError`, `onDisconnected` 가 서로
  병렬 실행되지 않는다. framework 가 같은 session 의 callback 순서를 보존한다.
- 서로 다른 session 의 callback 은 상호 독립이다. 전역 단일 실행 순서는 보장하지 않는다.

`ZLinkStreamSessionError` 는 1차 오류 분류 enum 이고, 부족하면 `diagnostic` 의 native
detail 을 확인한다. `diagnostic` 은 항상 채워지지 않는 optional detail 이다.

stream 핫패스에서는 메모리 할당을 최소화한다. `Message.toArray()` 같은 불필요한 추가 복사를
기본 사용법으로 두지 않는다. protobuf/json decode helper 도 가능한 한 추가 할당 없이 동작하게 한다.

#### session packet handler / dispatcher

```ts
export interface ZLinkSessionPacketHandler<TSessionContext> {
  readonly packetName: string;

  /** payload 는 onDispatch 와 같은 borrowed lifetime 이다. dispose/move 하지 않는다. */
  handle(
    context: TSessionContext,
    header: ZlinkStreamHeader,
    payload: Message,
  ): Promise<void>;
}

export interface ZLinkSessionPacketDispatcher<TSessionContext> {
  /**
   * 등록된 packet handler 가 있는 packet 만 dispatch 한다.
   * handler 가 처리하면 true, 아니면 false 를 반환해 session 이 relay/reject/ignore/log 를 결정한다.
   */
  tryHandle(
    context: TSessionContext,
    header: ZlinkStreamHeader,
    payload: Message,
  ): Promise<boolean>;
}
```

#### 4.4.1 actor/session 상위 모델

actor join, actor factory, stream-attached actor 모델이 포함된다. 기준 계약은
`ZLinkActor`, `ZLinkActorContext.joinSpot(...)`, Entry/user Spot 의 handler 등록 표면,
stream session 의 actor dispatch 표면(`ZLinkSessionContext`)이다.

actor 실행 객체와 session dispatch handle 은 분리한다. session 은 `ZLinkSessionActor` 를
저장하고 dispatch 에 사용한다.

```ts
export interface ZLinkSessionActor {
  readonly actorId: string;
  readonly ref: ActorRef;

  /**
   * caller payload 를 소비하지 않고 bound actor 로 stream packet 을 relay 한다.
   * framework 가 큐/원격 ActorGateway 를 위한 내부 copy 를 만든다.
   */
  relay(header: ZlinkStreamHeader, payload: Message): Promise<void>;

  notifyDisconnected(): Promise<void>;
}

export interface ZLinkActor {
  readonly actorId: string;
  readonly context: ZLinkActorContext;

  configure?(): void;
}

export interface ZLinkActorContext {
  readonly spotRid?: RoutingId;
  readonly isJoined: boolean;

  readonly boundSession: ZLinkBoundSession;

  getSpot(): ZLinkSpot;
  getSpotAs<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): TSpot;

  joinSpot<TRequest>(spotRid: RoutingId, request: TRequest): ZLinkActorJoinSpotCall;

  joinEntrySpot(spotNodeRid: RoutingId): ZLinkActorJoinEntrySpotCall;
}

/** C# ZLinkActorJoinResult<TReply> 대응. join 결과 코드 + ref + reply. */
export interface ZLinkActorJoinResult<TReply> {
  readonly resultCode: number;
  readonly actor: ActorRef;
  readonly reply: TReply;
}

export interface ZLinkActorJoinSpotCall {
  timeout(timeoutMs: number): ZLinkActorJoinSpotCall;
  submit<TReply>(): Promise<ZLinkActorJoinResult<TReply>>;
}

export interface ZLinkActorJoinEntrySpotCall {
  timeout(timeoutMs: number): ZLinkActorJoinEntrySpotCall;
  submit(): Promise<ActorRef>;
}

export interface ZLinkActorFactory {
  create(actorId: string, context: ZLinkActorContext): Promise<ZLinkActor>;
}

export interface ZLinkActorManager {
  create(actorId: string, actorType: string): Promise<ZLinkActor>;
  find(actorId: string): Promise<ZLinkActor | undefined>;
  getOrCreate(actorId: string, actorType: string): Promise<ZLinkActor>;
}
```

> 코드 기준(중요): dotnet `IZLinkActorJoinSpotCall.SubmitAsync<TReply>` 는 bare `TReply`
> 가 아니라 `ZLinkActorJoinResult<TReply>`(resultCode + ActorRef + reply)를 반환한다.
> `IZLinkActorContext` 는 generic `GetSpot<TSpot>()` 오버로드와 `JoinEntrySpot(...)` 을
> 가진다. C# overload 가 TS 에서 generic method overload 로 표현되지 않는 부분은
> `getSpot()` / `getSpotAs<TSpot>(spotType)` 로 분리했다. 의미는 dotnet 과 동일하다.

##### actor join handler

```ts
export interface ZLinkSpotActorJoinHandler<TSpot, TActor extends ZLinkActor, TRequest, TReply> {
  handle(spot: TSpot, actor: TActor, request: TRequest): Promise<TReply>;
}
```

decorator 방식:

```ts
@Injectable()
export class JoinMatchSpotHandler {
  @ZLinkSpotActorJoin()
  async handle(spot: MatchSpot, actor: PlayerActor, request: JoinMatchReq): Promise<JoinMatchSpotResult> {
    // ...
  }
}
```

actor join handler 가 정상 응답을 반환하면 framework 가 join commit 을 수행한다.
application handler 는 별도 `joinActor(...)` 를 호출하지 않는다. `leaveActor(...)` 는 현재
user Spot 에서 actor 를 Entry Spot 으로 되돌리는 편의 API 다. 성공하면 source Spot 의
`actorLeft` 와 Entry Spot 의 post-joined lifecycle handler 가 호출된다. 실패하면 actor 위치와
framework state 는 기존을 유지하고 lifecycle handler 는 호출되지 않는다.

- actor context 는 현재 client session 의 식별만 `boundSession` 으로 노출한다. session rid /
  binding token 은 runtime 내부 metadata 이므로 actor context 에 드러내지 않는다.
- outbound 는 actor context 의 기능이 아니다. 다른 Spot/channel 로 보내려면 handler 가 받은
  `spot.context.outbound.*` 를 쓴다. client stream 으로 push 하려면 actor 의
  `context.boundSession` 을 쓴다.
- `getSpot(...)` 는 actor 가 Spot 에 join 한 뒤에만 유효하다. join 전 호출은 명확한 실패다.
- `joinSpot(spotRid, request)` 는 user Spot routing id(`RoutingId`)를 받는다. domain key 는
  application registry 가 먼저 `RoutingId` 로 변환한다. `joinEntrySpot(spotNodeRid)` 는 target
  SpotNode routing id 를 받는다(Entry Spot 은 SpotNode 마다 하나).
- `boundSession.send(...)` 는 현재 actor 에 연결된 stream client 로 packet 을 보낸다.
  request 응답은 actor request handler 의 반환값으로 보낸다.
- stream 이 연결되지 않은 actor 에서 `boundSession.send(...)` 를 호출하면 명확한 실패다.

#### 4.4.2 session actor dispatch handler

session actor dispatch 는 actor 객체 callback 을 직접 호출하지 않는다. 현재 actor 위치에
맞는 registry(Entry Spot 또는 user Spot)에 등록된 typed handler 를 호출한다. handler 는 raw
routed envelope, stream sequence, session rid 같은 저수준 정보를 보지 않는다.

- Entry Spot handler: `ZLinkEntrySpotActorSendHandler<...>` 또는
  `ZLinkEntrySpotActorRequestHandler<...>` 중 하나를 구현한다.
- user Spot handler: `ZLinkSpotActorSendHandler<...>` 또는
  `ZLinkSpotActorRequestHandler<...>` 중 하나를 구현한다.

실행 순서는 actor 위치에 따른다. Entry Spot 에 있으면 actor 별 mailbox 에서 순서대로,
user Spot 에 있으면 user Spot 실행 queue 에서 처리된다.

actor request handler 의 응답 body 는 반환값으로 정한다. 응답 stream header 에 metadata 를
추가하거나 payload 압축을 켜려면 `ZLinkSpotActorRequestContext.reply` 를 쓴다. 이 표면은
응답 전송 자체를 수행하지 않고, framework 가 response frame 을 만들 때 사용할 옵션만 기록한다.

```ts
export interface ZLinkSpotActorSendContext extends ZLinkHandlerContext {
  readonly metadata: ZLinkMessageMetadata;
}

export interface ZLinkSpotActorRequestContext extends ZLinkHandlerContext {
  readonly metadata: ZLinkMessageMetadata;
  readonly reply: ZLinkSpotActorReplyOptions;
}

export interface ZLinkSpotActorReplyOptions {
  metadata(key: string, value: string): ZLinkSpotActorReplyOptions;
  compress(enabled?: boolean): ZLinkSpotActorReplyOptions;
}
```

공통 metadata 타입은 모든 호출 경로(actor dispatch, bound session, channel 호출)에서 같은
snapshot 규칙을 따른다.

```ts
export interface ZLinkMessageMetadata {
  readonly values: ReadonlyMap<string, string>;
  find(key: string): string | undefined;
}

/** 빈 metadata snapshot. C# ZLinkMessageMetadata.Empty 대응. */
export const ZLinkMessageMetadataEmpty: ZLinkMessageMetadata;

export interface ZLinkMessageMetadataPolicy {
  canForward(key: string): boolean;
}
```

#### 4.4.3 dispatch mode

SPOT 과 actor packet 처리에는 편의 모드와 고성능 모드를 둔다.

```ts
export enum ZLinkDispatchMode {
  Compiled = 'compiled',
  Dynamic = 'dynamic',
}

export interface ZLinkDispatchOptions {
  spotDispatchMode: ZLinkDispatchMode;
  streamDispatchMode: ZLinkDispatchMode;
  readonly unhandled: ZLinkUnhandledDispatchOptions;
  readonly diagnostics: ZLinkDiagnosticsOptions;
}

export interface ZLinkUnhandledDispatchOptions {
  request: ZLinkUnhandledDispatchAction;
  send: ZLinkUnhandledDispatchAction;
  publish: ZLinkUnhandledDispatchAction;
  /** NestJS LoggerService level 에 대응. C# SendLogLevel. */
  sendLogLevel: LogLevel;
  publishLogLevel: LogLevel;
}

export interface ZLinkDiagnosticsOptions {
  messageFlow: ZLinkMessageFlowLogMode;
  sampleRate: number;
  includeMessageSizes: boolean;
  includeNativeDiagnostics: boolean;
}

export enum ZLinkUnhandledDispatchAction {
  ReplyError = 'replyError',
  LogAndDrop = 'logAndDrop',
  Drop = 'drop',
  Throw = 'throw',
}

export enum ZLinkMessageFlowLogMode {
  Off = 'off',
  ErrorsOnly = 'errorsOnly',
  KeyTransitions = 'keyTransitions',
  Verbose = 'verbose',
  Diagnostic = 'diagnostic',
}
```

> 코드 기준: dotnet `IZLinkDispatchOptions` 는 단순 두 mode 가 아니라 `Unhandled`,
> `Diagnostics` 두 sub-option 과 관련 enum 을 함께 가진다. spec 문서는 두 mode 만 적었지만
> 코드가 더 넓으므로 전체를 옮긴다.

- `Compiled`: reflection 은 registration / warm-up 까지만. packet hot path 에서는 cached
  delegate, prebuilt dispatch table, 미리 선택한 factory 만 쓴다.
- `Dynamic`: 유연한 등록과 늦은 바인딩 우선. 성능이 덜 중요한 관리용 handler / 초기 실험 단계용.

### 4.5 message serializer / codec

serializer 계층은 transport interface 와 분리한다. STREAM handler 는 `Message` 를 받기만
하고, protobuf/json 변환은 별도 serializer/helper 가 담당한다.

```ts
export interface ZLinkMessageSerializer {
  readonly name: string;

  canSerialize(type: unknown): boolean;
  canDeserialize(type: unknown): boolean;

  serialize<T>(value: T): Message;
  deserialize<T>(message: Message): T;
  tryDeserialize<T>(message: Message): { ok: true; value: T } | { ok: false };
}
```

> 코드 기준: C# `TryDeserialize<T>(Message, out T?)` 의 out 파라미터는 TS 에서 판별 union
> 반환으로 옮긴다(매핑 정책 §4 의 "out 파라미터 → 반환 객체").

`Message` 위에 type 기준 parse helper 를 얹는 구조를 기본으로 본다.

```ts
/** codec extension package 가 제공하는 helper. binding core 의 필수 메서드가 아니다. */
export function parseMessage<T>(message: Message, type: Type<T>): T;
```

codec registry 등록 표면(`ZLinkFrameworkOptions.codecs`, §6.1):

```ts
export interface ZLinkCodecRegistryBuilder {
  addProtobuf(): void;
  addJson(): void;
  addMessagePack(): void;
}
```

> 코드 기준: dotnet `IZLinkCodecRegistryBuilder` 는 `AddProtobuf` / `AddJson` /
> `AddMessagePack` 세 메서드를 가진다.

## 5. Client 인터페이스

서버에서 다른 서버로 메시지를 보내는 client interface 다. 모두 DI(provider token)로 주입된다.

### 5.1 ZLinkChannelClient

서버 간 outbound 호출용 공용 client 다. `channelName` 기준 호출을 기본 축으로 둔다.
Discovery 가 대상을 선택한다. 일반 channel messaging 에서는 특정 ROUTER 를 `rid` 로 직접
지정하지 않는다(rid 호출은 SPOT spot-to-spot / route 경로에만 둔다).

packet key 는 매번 별도 문자열로 받지 않는 것이 기본이다. payload 타입 이름(생성자 이름)을
기본 packet key 로 본다. 변형은 builder 체이닝(`packetName`, `timeout`)으로 이어 붙인다.

```ts
export interface ZLinkSendCall {
  packetName(packetName: string): ZLinkSendCall;
  submit(): Promise<void>;
}

export interface ZLinkRequestCall {
  packetName(packetName: string): ZLinkRequestCall;
  timeout(timeoutMs: number): ZLinkRequestCall;
  submit<TReply>(): Promise<TReply>;
}

export interface ZLinkChannelClient {
  sendToChannel<TMessage>(channelName: string, message: TMessage): ZLinkSendCall;
  requestToChannel<TMessage>(channelName: string, request: TMessage): ZLinkRequestCall;
}
```

> 코드 기준: dotnet `IZLinkSendCall.Submit` / `IZLinkRequestCall.SubmitAsync<TReply>` 는
> 빌더가 돌려주는 별도 call 객체의 메서드다. 기존 draft 의 `send(channel, msg, opts):
> Promise<boolean>` 평면 API 가 아니라 builder 표면을 따른다. submit 반환은 `boolean` 이
> 아니라 `void` / `TReply` 다(backpressure 는 내부 queue 로 처리, no-wait 표면 없음).

client-server channel 과 dealer mesh channel 은 같은 request/send 표면을 쓴다. 그래서
client-server 전용 별칭을 두지 않고 `ZLinkChannelClient` 하나만 public DI 표면으로 둔다.

packet key 해석 순서:

1. builder 의 `packetName(...)` 이 지정되어 있으면 그 값
2. payload 타입의 `@ZLinkPacket` metadata
3. payload 생성자 이름(`Type.name`)

timeout 규칙:

- `requestToChannel(...)` 는 reply 를 기다리므로 `timeout(...)` 을 둘 수 있다.
- `sendToChannel(...)` / `publish(...)` 는 응답을 기다리지 않으므로 timeout 을 두지 않는다.
- `submit()` 은 handler 완료 대기가 아니라, framework 가 transport 에 위임할 수 있을 때까지
  기다리는 비동기 submit 이다.
- send backpressure 한계는 builder 가 아니라 channel/socket 의 `sendTimeout` 옵션을 따른다.
  framework 기본값은 200ms 이며, `sendTimeout = null` 명시 시 무한 대기로 본다.
- public no-wait 옵션은 제공하지 않는다. temporary backpressure 는 내부 queue + ready
  notification 으로 처리한다.

```ts
const reply = await client
  .requestToChannel('profile', { accountId } satisfies GetProfileRequest)
  .timeout(200)
  .submit<GetProfileReply>();

await client
  .sendToChannel('profile', { accountId } satisfies RefreshProfileCacheCommand)
  .packetName('profile.refresh-cache')
  .submit();
```

### 5.2 ZLinkSpotOutbound

현재 spot runtime 안에서의 outbound 호출 client 다. `ZLinkChannelClient` 와 독립된
interface 이며 하부에서 서로 다른 C API 를 감싼다. 세 가지 축: 현재 SPOT channel publish,
attach 된 channel 의 send/request, spot rid 기반 routed spot send/request.

```ts
export interface ZLinkSpotOutbound {
  sendToSpot<TMessage>(spotRid: RoutingId, message: TMessage): ZLinkSendCall;
  requestToSpot<TMessage>(spotRid: RoutingId, request: TMessage): ZLinkRequestCall;
  publish<TEvent>(topic: string, message: TEvent): ZLinkPublishCall;
  sendToChannel<TMessage>(channelName: string, message: TMessage): ZLinkSendCall;
  requestToChannel<TMessage>(channelName: string, request: TMessage): ZLinkRequestCall;
}
```

`ZLinkSpotContext.outbound` / `ZLinkEntrySpotContext.outbound` 가 이 표면을 노출한다. SPOT
lifecycle callback / handler 안에서는 별도 client 주입 없이 `context.outbound.*` 를 쓴다.

`ZLinkChannelClient` 와의 차이:

- `publish(topic, ...)` 가 포함된다(SPOT 은 현재 channel 안 topic publish 를 함께 쓰는 경우가 많다).
- `sendToSpot` / `requestToSpot` 은 spot remote address resolver 를 쓴다.
- `sendToChannel` / `requestToChannel` 은 attach 된 channel client 를 통해 해소한다.
- local `SpotNode` 가 없는 앱의 기본 outbound 는 `ZLinkChannelClient` 다. 외부 SPOT channel
  publish 만 필요하면 `ZLinkSpotPublisherClient` 를 별도로 쓴다.

`targetRid + spotRid` 를 직접 넘기는 raw route 함수는 application public 표면에 두지 않는다.

#### 5.2.1 route client 와 Spot route 경계

`ZLinkRouteClient` 는 route mesh channel 로 target node 에 send/request 할 때 쓴다. 반환
타입은 channel client 와 같은 `ZLinkSendCall` / `ZLinkRequestCall` 이다.

```ts
export interface ZLinkRouteClient {
  send<TMessage>(routerChannelId: string, targetNodeRid: RoutingId, message: TMessage): ZLinkSendCall;
  request<TRequest>(routerChannelId: string, targetNodeRid: RoutingId, request: TRequest): ZLinkRequestCall;
}
```

Spot 으로 가는 routed transport 는 application 이 직접 egress client 를 고르는 public 표면으로
노출하지 않는다. current Spot callback 안에서는 `spot.context.outbound.sendToSpot(...)` /
`requestToSpot(...)` 을 쓴다. current Spot 이 없는 session/HTTP handler/background 에서는
actor 생성 또는 entry spot join 으로 `ActorRef` 를 얻은 뒤 session actor handle 로 bind 한다.

### 5.3 ZLinkSpotPublisherClient

local spot 인스턴스가 없는 외부 노드가 특정 SPOT channel 로 publish 할 때 쓰는 client 다.
(현재 실행 중인 local spot 문맥에서는 `spot.context.outbound.publish(...)` 를 쓴다.)

```ts
export interface ZLinkSpotPublisherClient {
  publishSpot<TEvent>(channelName: string, topic: string, message: TEvent): ZLinkPublishCall;
}
```

> 코드 기준: dotnet 에서 이 interface 는 `Contracts.Spots` 에 있고 메서드 이름은
> `PublishSpot` 이다(기존 draft 의 `publish` 가 아님). `channelName` 은 target SPOT channel
> 이름이다.

### 5.4 ZLinkFanoutClient

일반 PUB/SUB event publish interface 다. SPOT publish 와 별개의 channel messaging 경로다.

```ts
export interface ZLinkPublishCall {
  packetName(packetName: string): ZLinkPublishCall;
  submit(): Promise<void>;
}

export interface ZLinkFanoutClient {
  publish<TEvent>(channelName: string, topic: string, message: TEvent): ZLinkPublishCall;
}
```

- `channelName` 은 publish 할 논리 channel 의 PUB/SUB mesh 를 지정한다.
- `topic` 은 그 channel 안에서 어떤 subscriber 집합이 수신할지를 지정한다.

`publish('profile', 'profile.cache-refreshed', evt)` 는 `profile` channel 안의
`profile.cache-refreshed` topic 으로 fan-out 한다. publish 도 timeout 을 두지 않고, packet
이름 override 만 둘 수 있다. `submit()` 의 비동기 의미는 remote 처리 완료 대기가 아니라
local publish transport 에 submit 되는 시점까지의 대기다.

실패 처리: temporary backpressure 는 내부 queue + ready notification, 그 외 submit 실패
(route-not-ready 등)는 예외다. subscriber 마다 task/직렬화를 새로 만들지 않고 topic/payload
frame 을 한 번만 만든다. `NoDrop` 정책이 켜져 있으면 drop 대신 `sendTimeout` 까지 기다린다.

### 5.5 actor route resolver / route transport helper

session 에서 actor 로 relay 할 때는 `ZLinkSessionActor.relay(...)` 를 쓴다. actor runtime 을
직접 호출하는 별도 public client 는 두지 않는다. remote actor 위치는 session 이 직접
계산하지 않고, actor id/type 으로 local handle 을 만들거나 생성/join 결과의 `ActorRef` 로
remote handle 을 만들면 core ActorGateway 가 relay 한다.

route transport helper 는 application public surface 가 아니라 internal transport helper 다.
일반 application 코드는 `RoutingId` 를 직접 넘기지 않고 actor id / spot key 기반 client 를 쓴다.
(이 helper 는 public 카탈로그에 노출하지 않는다.)

### 5.6 ZLinkBoundSession

actor handler 가 현재 client session 으로 push 할 때 쓰는 client 다. client 에게 새 request
를 보내는 API 는 제공하지 않는다. client request 응답은 actor request handler 의 반환값으로 처리한다.

`ZLinkBoundSession` 은 연결된 client stream 을 향한 proxy 이며 dotnet 에서는
`Contracts.Streams` 에 둔다. Spot actor handler 는 actor 의 `context.boundSession` 으로
접근하고, stream packet metadata 는 `ZLinkSpotActorSendContext` / `ZLinkSpotActorRequestContext`
로 받는다. application handler 는 actor id 만 넘기고, session server `RoutingId`, stream
`sessionId`, binding token 은 framework/core binding 안에만 머문다.

```ts
export interface ZLinkBoundSession {
  send<TMessage>(message: TMessage): ZLinkBoundSessionSendCall;
  disconnect(): Promise<void>;
}

export interface ZLinkBoundSessionSendCall {
  packetName(packetName: string): ZLinkBoundSessionSendCall;
  metadata(key: string, value: string): ZLinkBoundSessionSendCall;
  submit(): Promise<void>;
}
```

`disconnect()` 도 현재 actor 의 binding 상태를 쓴다. actor 가 client 연결을 끊기로 결정한
경우 호출하며, session callback 으로 `onDisconnected(...)` 를 다시 올리지 않는다.

### 5.7 spot remote address resolver

public resolver 는 spot 축으로 둔다. spot rid 로부터 user Spot route 를 조회한다.

```ts
export interface ZLinkSpotRemoteAddressResolver {
  resolveSpotRemoteAddress(spotRid: RoutingId): Promise<ZLinkSpotRemoteAddress>;
}

export enum ZLinkSpotKind {
  Invalid = 'invalid',
  Entry = 'entry',
  User = 'user',
}

export interface ZLinkSpotRemoteAddress {
  readonly routerChannelId: string;
  readonly targetNodeRid: RoutingId;
  readonly spotRid: RoutingId;
  readonly spotKind: ZLinkSpotKind;
}
```

`routerChannelId` 는 실제 router-capable channel 이름이다. 이 값이 가리키는 channel 은
`addClientServerChannel(...)` 의 server ROUTER 이거나 `addRouteMeshChannel(...)` 의 route mesh
ROUTER 여야 한다. target `SpotNode` 는 같은 이름을 `acceptSpotRoutesFromChannel(...)` 로
수락해야 하며, resolver 는 연결을 만들지 않는다.

resolver 입력에는 metadata, packet name, raw message, decoded payload 를 넘기지 않는다.
resolver 의 책임은 위치 저장소 접근뿐이다. actor-session route 는 public contract 가 아니라
framework runtime 내부 상태다.

actor-session binding 재사용 규칙: 같은 actor id 가 새 stream session 으로 다시 bind 되면
framework 는 새 논리 actor 를 만들지 않고 기존 actor runtime state 를 재사용하며 binding
token 만 교체한다. factory 가 새 actor 를 만드는 경우에도 반환 `actorId` 는 요청한 id 와
정확히 일치해야 한다. 불일치는 configuration 오류로 실패한다.

## 6. 등록과 관리 인터페이스

### 6.1 framework 등록 루트 (= NestJS module options)

dotnet 의 `AddZLinkFramework(options => ...)` 는 node 에서 `ZLinkModule.forRoot(...)` /
`forRootAsync(...)` 가 반환하는 `DynamicModule` 로 매핑된다. dotnet builder 메서드 한 개 =
node options 키 한 개로 1:1 대응시키는 것을 기본으로 한다(표면 매핑 §5).

두 가지 표면을 함께 둔다. (A) NestJS 선언적 module-options, (B) fluent builder. 둘은 같은
등록을 표현한다. builder 는 dotnet `IZLinkFrameworkOptions` 와 1:1 대응한다.

#### (B) fluent builder — `ZLinkFrameworkOptions`

```ts
export interface ZLinkFrameworkOptions {
  defaultTimeoutMs: number; // C# DefaultTimeout, 기본 30_000

  readonly codecs: ZLinkCodecRegistryBuilder;

  addHandlersFromModule(moduleType: Type<unknown>): void; // C# AddHandlersFromAssemblyOf<TMarker>
  addHandlersFromModules(moduleTypes: readonly Type<unknown>[]): void;

  configureMetadata(configure: (builder: ZLinkMetadataPolicyBuilder) => void): void;

  addActorFactory<TFactory extends ZLinkActorFactory>(
    actorType: string,
    factoryType: Type<TFactory>,
  ): void;

  addSpotRemoteAddressResolver<TResolver extends ZLinkSpotRemoteAddressResolver>(
    resolverType: Type<TResolver>,
  ): void;

  useRegistrySpotRemoteAddresses(namespaceName: string): void;
  useRegistrySpotRemoteAddresses(
    namespaceName: string,
    configure: (options: ZLinkRegistrySpotRemoteAddressesOptions) => void,
  ): void;

  addClientServerChannel(channelName: string, configure: (ch: ZLinkClientServerChannelBuilder) => void): void;
  addFanoutChannel(channelName: string, configure: (ch: ZLinkFanoutChannelBuilder) => void): void;
  addDealerMeshChannel(channelName: string, configure: (ch: ZLinkDealerMeshChannelBuilder) => void): void;
  addRouteMeshChannel(channelName: string, configure: (ch: ZLinkRouteMeshChannelBuilder) => void): void;

  useDiscovery(configure: (builder: ZLinkDiscoveryBuilder) => void): void;

  useFilter<TFilter extends ZLinkHandlerFilter>(filterType: Type<TFilter>): void;

  configureDispatch(configure: (options: ZLinkDispatchOptions) => void): void;

  addStreamNode(streamNodeName: string, configure: (st: ZLinkStreamNodeBuilder) => void): void;

  addSpotMesh(channelName: string, configure: (mesh: ZLinkSpotMeshBuilder) => void): void;
}

export interface ZLinkRegistrySpotRemoteAddressesOptions {
  routerChannelId?: string;
}

export interface ZLinkDiscoveryBuilder {
  add(endpoint: string): void;
}

export interface ZLinkMetadataPolicyBuilder {
  forward(key: string): void;
}
```

> 코드 기준: dotnet `IZLinkFrameworkOptions` 는 위 메서드 전체를 가진다(스펙 문서가 일부만
> 적었던 `AddClientServerChannel` 등 채널 등록 메서드 포함). `AddHandlersFromAssemblyOf` 는
> node 에서 NestJS DiscoveryService 기반 `addHandlersFromModule(s)` 로 매핑한다.
> `UseRegistrySpotRemoteAddresses` 는 코드에만 있던 표면이라 옮겼다.

`defaultTimeoutMs` 기본값은 30초(30000)다. 각 메서드 의미:

- `defaultTimeoutMs`: request 가 별도 timeout 을 지정하지 않을 때 쓰는 기본값.
- `codecs`: protobuf/json/messagepack codec provider 를 registry 에 등록(§4.5).
- `configureMetadata(...)`: session actor dispatch / bound session 경로에서 전달할 metadata key 등록.
- `addActorFactory(actorType, factoryType)`: actor type 문자열에 대응하는 factory 등록.
- `addSpotRemoteAddressResolver(...)`: spot rid → user Spot route 조회 resolver 등록.
- `useRegistrySpotRemoteAddresses(...)`: registry 기반 spot 주소 해소 namespace 등록.
- `addClientServerChannel(...)`: request/send 용 client-server 채널 등록.
- `addFanoutChannel(...)`: pub/sub fanout 채널 등록.
- `addDealerMeshChannel(...)`: DEALER mesh 채널 등록.
- `addRouteMeshChannel(...)`: route mesh 채널 등록.
- `useDiscovery(...)`: 일반 channel capability 가 공유할 registry endpoint 집합 등록(루트에 한 번).
- `useFilter(filterType)`: handler filter 타입 등록.
- `configureDispatch(...)`: dispatch mode / unhandled / diagnostics 설정.
- `addStreamNode(...)`: STREAM node 등록(한 node 에 session 하나만).
- `addSpotMesh(channelName, ...)`: 여러 SpotNode 가 같은 SPOT mesh discovery view 를 공유하도록 묶어 등록.

#### (A) NestJS module-options 대응

builder 람다는 NestJS 선언적 options 객체로 옮긴다. 정확한 키와 형태는 각 채널별 spec
(`nestjs-channel-messaging`, `nestjs-spot`, `nestjs-stream`, `nestjs-registry`)이 확정한다.

```ts
@Module({
  imports: [
    ZLinkModule.forRoot({
      defaultTimeoutMs: 30_000,
      codecs: ['protobuf', 'json'],
      channels: {
        api: {
          server: { bind: 'tcp://0.0.0.0:7101' },
          handlerGroups: ['api'],
        },
        profile: {
          client: {},
          requestHandlers: [GetProfileHandler],
        },
        'api.events': {
          subscriber: {},
          handlerGroups: ['api.events'],
        },
      },
      discovery: { registries: ['tcp://registry:7000'] },
      filters: [LoggingZLinkFilter, ValidationZLinkFilter],
      dispatch: { spotDispatchMode: 'compiled', streamDispatchMode: 'compiled' },
    }),
  ],
  providers: [GetProfileHandler],
})
export class AppModule {}
```

| dotnet builder 메서드 | node module options 키 | spec |
|------|------|------|
| `addClientServerChannel(name, ...)` | `channels[name] = { server, client, requestHandlers, sendHandlers, handlerGroups }` | nestjs-channel-messaging |
| `addFanoutChannel(name, ...)` | `channels[name] = { publisher, subscriber, publishHandlers, handlerGroups }` | nestjs-channel-messaging |
| `addDealerMeshChannel(name, ...)` | `channels[name] = { dealerMesh: {...} }` | nestjs-channel-messaging |
| `addRouteMeshChannel(name, ...)` | `channels[name] = { routeMesh: {...} }` | nestjs-channel-messaging |
| `addSpotMesh(name, ...)` | `spotMeshes[name] = {...}` | nestjs-spot |
| `addStreamNode(name, ...)` | `streamNodes[name] = {...}` | nestjs-stream |
| `useDiscovery(...)` | `discovery: { registries: [...] }` | nestjs-registry |
| `useFilter(...)` | `filters: [FilterClass]` | handler-interfaces §8 |
| `configureDispatch(...)` | `dispatch: { spotDispatchMode, streamDispatchMode, unhandled, diagnostics }` | §4.4.3 |
| `addHandlersFromModule(s)(...)` | `discover: { modules / include }` | 매핑 정책 §4.2 |
| `addActorFactory(...)` | `actorFactories: [{ actorType, factory }]` | nestjs-actor |
| `codecs` | `codecs: ['protobuf'|'json'|'messagepack']` | §4.5 |
| `configureMetadata(...)` | `metadata: { forward: [...] }` | nestjs-actor |
| `useRegistrySpotRemoteAddresses(...)` | `spotRemoteAddresses: { namespace, routerChannelId? }` | nestjs-spot |

#### channel capability builder

```ts
export interface ChannelServerCapabilityBuilder {
  bind(endpoint: string): void;
  configureSocket(configure: (socket: ZLinkSocketConfig) => void): void;
  configureRouting(configure: (route: ZLinkRouteConfig) => void): void;
}

export interface ChannelClientCapabilityBuilder {
  configureSocket(configure: (socket: ZLinkSocketConfig) => void): void;
  configureRouting(configure: (route: ZLinkOutboundRouteConfig) => void): void;
  useManualConnections(configure: (connections: ZLinkEndpointConnections) => void): void;
}

export interface DealerMeshChannelClientCapabilityBuilder extends ChannelClientCapabilityBuilder {
  bind(endpoint: string): void;
}

export interface ChannelPublisherCapabilityBuilder {
  bind(endpoint: string): void;
  configureSocket(configure: (socket: ZLinkSocketConfig) => void): void;
}

export interface ChannelSubscriberCapabilityBuilder {
  configureSocket(configure: (socket: ZLinkSocketConfig) => void): void;
  useManualConnections(configure: (connections: ZLinkEndpointConnections) => void): void;
}
```

#### channel builder

```ts
export interface ZLinkClientServerChannelBuilder {
  enableServer(configure?: (server: ChannelServerCapabilityBuilder) => void): void;
  enableClient(configure?: (client: ChannelClientCapabilityBuilder) => void): void;

  addHandlerGroup(groupName: string): void;

  addSendHandler<THandler extends ZLinkSendHandler<TMessage>, TMessage>(
    handlerType: Type<THandler>, packetName?: string): void;
  addSendHandler<THandler>(handlerType: Type<THandler>, packetName?: string): void;

  addRequestHandler<THandler extends ZLinkRequestHandler<TRequest, TReply>, TRequest, TReply>(
    handlerType: Type<THandler>, packetName?: string): void;
  addRequestHandler<THandler>(handlerType: Type<THandler>, packetName?: string): void;

  enableSpotRouteEgress(targetSpotNodeChannelName: string): void;
}

export interface ZLinkFanoutChannelBuilder {
  enablePublisher(configure?: (publisher: ChannelPublisherCapabilityBuilder) => void): void;
  enableSubscriber(configure?: (subscriber: ChannelSubscriberCapabilityBuilder) => void): void;

  addHandlerGroup(groupName: string): void;

  addPublishHandler<THandler extends ZLinkPublishHandler<TMessage>, TMessage>(
    handlerType: Type<THandler>, packetName?: string): void;
  addPublishHandler<THandler>(handlerType: Type<THandler>, packetName?: string): void;
}

export interface ZLinkDealerMeshChannelBuilder {
  enableClient(configure?: (client: DealerMeshChannelClientCapabilityBuilder) => void): void;

  addHandlerGroup(groupName: string): void;

  addSendHandler<THandler extends ZLinkSendHandler<TMessage>, TMessage>(
    handlerType: Type<THandler>, packetName?: string): void;
  addSendHandler<THandler>(handlerType: Type<THandler>, packetName?: string): void;

  addRequestHandler<THandler extends ZLinkRequestHandler<TRequest, TReply>, TRequest, TReply>(
    handlerType: Type<THandler>, packetName?: string): void;
  addRequestHandler<THandler>(handlerType: Type<THandler>, packetName?: string): void;
}

export interface ZLinkRouteChannelBuilder {
  bind(endpoint: string): void;
  configureSocket(configure: (socket: ZLinkSocketConfig) => void): void;
  configureRouting(configure: (route: ZLinkRouteConfig) => void): void;
  useManualConnections(configure: (connections: ZLinkEndpointConnections) => void): void;

  addHandlerGroup(groupName: string): void;

  addSendHandler<THandler extends ZLinkRouteSendHandler<TMessage>, TMessage>(
    handlerType: Type<THandler>, packetName?: string): void;
  addSendHandler<THandler>(handlerType: Type<THandler>, packetName?: string): void;

  addRequestHandler<THandler extends ZLinkRouteRequestHandler<TRequest, TReply>, TRequest, TReply>(
    handlerType: Type<THandler>, packetName?: string): void;
  addRequestHandler<THandler>(handlerType: Type<THandler>, packetName?: string): void;

  enableSpotRouteEgress(targetSpotNodeChannelName: string): void;
}

/** route mesh 는 route channel builder 를 그대로 확장한다. */
export interface ZLinkRouteMeshChannelBuilder extends ZLinkRouteChannelBuilder {}

export interface ZLinkStreamNodeBuilder {
  bind(endpoint: string): void;
  attachActorGateway(spotNodeName: string): void;
  registerSession<TSession extends ZLinkSession>(sessionType: Type<TSession>): void;
}
```

> 코드 기준: `IZLinkDealerMeshChannelBuilder` 는 client capability 만이 아니라
> `AddHandlerGroup` / `AddSendHandler` / `AddRequestHandler` 도 가진다(스펙 문서는 client 만
> 적음). `IZLinkRouteMeshChannelBuilder` 는 `IZLinkRouteChannelBuilder` 를 그대로 상속한다.
> `IZLinkStreamNodeBuilder` 는 `AttachActorGateway(spotNodeName)` 를 가진다.

채널 등록 규칙:

- handler 는 자동으로 모든 channel 에 열리지 않는다. `addHandlerGroup(...)` 매핑 또는 명시
  등록이 노출을 정한다.
- `enableServer(...)` / `enablePublisher(...)` capability 는 다른 프로세스가 접근할 local bind
  endpoint 가 필요하므로 builder 안에서 `bind(...)` 를 같이 지정한다.
- 수동 연결은 `channel + capability` 단위다. 같은 capability 안에서 discovery 와 manual 을
  섞지 않는다. `client` 와 `subscriber` 는 서로 다른 연결 집합으로 본다.

### 6.2 channel 수동 연결

수동 연결은 capability builder 의 `useManualConnections(...)` 에서 등록한다. public 계약은
host 시작 뒤 endpoint 를 바꾸는 별도 runtime 연결 관리 표면을 제공하지 않는다.

```ts
export interface ZLinkEndpointConnections {
  connect(endpoint: string): void;
  disconnect(endpoint: string): void;
  listConnections(): readonly string[];
}
```

이 표면은 설정 객체를 편집하는 용도이며 실행 중 socket 에 직접 연결 명령을 보내는 runtime
handle 이 아니다. discovery 모드 capability 는 peer 소유권이 discovery 에 있으므로, 수동
연결이 필요하면 해당 capability 를 manual 모드로 등록한다.

### 6.3 Spot 관리와 등록

`ZLinkSpotManager` 는 `SpotNode` 안에서 spot 인스턴스를 생성/삭제하는 데 쓴다. spot 을
만드는 주체는 handler 가 아니라 manager 다.

```ts
export interface ZLinkSpotCreateResult {
  readonly spotRid: RoutingId;
  readonly created: boolean;
}

export interface ZLinkSpotInfo {
  readonly spotRid: RoutingId;
}

export interface ZLinkSpotManager {
  create<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): Promise<ZLinkSpotCreateResult>;
  create<TSpot extends ZLinkSpot>(
    spotType: Type<TSpot>, createParts: readonly Message[]): Promise<ZLinkSpotCreateResult>;

  getOrCreate<TSpot extends ZLinkSpot>(
    spotType: Type<TSpot>, spotRid: RoutingId, createParts: readonly Message[]): Promise<ZLinkSpotCreateResult>;
  getOrCreate<TSpot extends ZLinkSpot>(
    spotType: Type<TSpot>, spotRid: RoutingId): Promise<ZLinkSpotCreateResult>;

  find(spotRid: RoutingId): Promise<ZLinkSpotInfo | undefined>;
  list(): Promise<readonly ZLinkSpotInfo[]>;
  remove(spotRid: RoutingId): Promise<boolean>;
}
```

> 코드 기준(중요): dotnet `IZLinkSpotManager` 는 generic `TSpot` 으로 factory 를 고른다
> (기존 draft 의 `spotName: string` 이 아님). 조회 메서드는 `FindAsync`(=`find`)이며
> 결과는 public 식별자 `SpotRid` 만 돌려준다(spot 타입/factory 정보는 노출하지 않음).
> `ZLinkSpotInfo` 는 `spotRid` 만 가진다. `ZLinkSpotCreateResult` 는 `spotRid` + `created`.
> C# generic + overload 를 TS 에서 `spotType: Type<TSpot>` 첫 인자로 표현한다.

- `create(spotType)`: generic 타입으로 factory 선택, runtime 이 새 spotRid 발급.
- `create(spotType, createParts)`: runtime 이 새 spotRid 발급, multipart create payload 를 `onCreate(...)` 에 전달.
- `getOrCreate(spotType, spotRid, createParts)`: 호출자가 logical spot rid 지정. 이미 같은
  spotRid 가 있으면 기존 spot 반환, 새 createParts 는 전달하지 않음.
- `find(...)` / `list(...)`: 조회 표면. `SpotRid` 만 돌려준다.
- `remove(...)`: 정상 제거(이때 `onClosing()` 호출).

`onCreate(createReqs)` 는 caller 가 넘긴 part 수와 순서를 유지해서 전달한다. 여러 part 를
하나의 envelope 로 합치지 않는다. `createParts` 인자는 `null`/`undefined` 이면 안 된다(빈
배열 허용). 같은 spotRid 에 대해 다른 `TSpot` 으로 `getOrCreate(...)` 하면 `SpotTypeMismatch`
로 실패한다.

#### SpotNode / mesh builder

```ts
export interface ZLinkSpotNodeBuilder {
  enableRouter(configure?: (router: SpotRouterCapabilityBuilder) => void): void;
  enablePubSub(configure?: (pubsub: SpotPubSubCapabilityBuilder) => void): void;

  attachChannelClient(
    channelName: string, configure?: (client: SpotChannelClientCapabilityBuilder) => void): void;
  attachSpotPublisherClient(
    channelName: string, configure?: (client: SpotPublisherClientCapabilityBuilder) => void): void;

  acceptSpotRoutesFromChannel(
    channelName: string, configure?: (acceptance: ZLinkSpotRouteChannelAcceptanceBuilder) => void): void;

  configureEntrySpot(configure: (options: ZLinkEntrySpotOptions) => void): void;

  addSpotFactory<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): void;
  addEntrySpot<TEntrySpot extends ZLinkEntrySpot>(entrySpotType: Type<TEntrySpot>): void;
}

/** mesh node 는 spot node builder 를 그대로 확장한다. */
export interface ZLinkSpotMeshNodeBuilder extends ZLinkSpotNodeBuilder {}

export interface ZLinkSpotMeshBuilder {
  useDiscovery(configure: (builder: ZLinkDiscoveryBuilder) => void): void;
  addNode(spotNodeName: string, configure: (node: ZLinkSpotMeshNodeBuilder) => void): void;
}

export interface SpotRouterCapabilityBuilder {
  setRouterBind(endpoint: string): void;
  setRoutingId(routingId: RoutingId): void;
  configureSocket(configure: (socket: ZLinkSocketConfig) => void): void;
  configureRouting(configure: (route: ZLinkRouteConfig) => void): void;
  useManualConnections(configure: (connections: ZLinkEndpointConnections) => void): void;
}

export interface SpotPubSubCapabilityBuilder {
  setPubBind(endpoint: string): void;
  setRoutingId(routingId: RoutingId): void;
  configurePublisherConfig(configure: (config: ZLinkSpotPublisherConfig) => void): void;
  configureSubscriberConfig(configure: (config: ZLinkSpotSubscriberConfig) => void): void;
  useManualConnections(configure: (connections: ZLinkEndpointConnections) => void): void;
}

export interface SpotPublisherClientCapabilityBuilder {
  configureSocket(configure: (socket: ZLinkSocketConfig) => void): void;
  useManualConnections(configure: (connections: ZLinkEndpointConnections) => void): void;
}

export interface SpotChannelClientCapabilityBuilder {
  configureSocket(configure: (socket: ZLinkSocketConfig) => void): void;
  configureRouting(configure: (route: ZLinkOutboundRouteConfig) => void): void;
  useManualConnections(configure: (connections: ZLinkEndpointConnections) => void): void;
}

export interface ZLinkSpotRouteChannelAcceptanceBuilder {
  useManualConnections(configure: (connections: ZLinkEndpointConnections) => void): void;
}
```

> 코드 기준(중요): dotnet `IZLinkSpotNodeBuilder` 에는 node 자체 `Bind(...)` 가 **없다**.
> bind 는 capability 쪽에서 한다: router 는 `SetRouterBind(...)`, pub/sub 는 `SetPubBind(...)`.
> `ISpotPubSubCapabilityBuilder` 는 `ConfigurePublisherConfig` / `ConfigureSubscriberConfig`
> 이름을 쓴다(스펙 문서의 `ConfigurePublisherOptions` 가 아님). router/pubsub capability 는
> `SetRoutingId(...)` 도 가진다. `IZLinkSpotMeshNodeBuilder` 는 `IZLinkSpotNodeBuilder` 를
> 그대로 상속한다. `AddSpotFactory<TSpot>` 의 제약은 `where TSpot : IZLinkSpot` 이다.

builder 함수 의미:

- `enableRouter(...)`: spot-to-spot routed packet 을 처리할 local router capability 활성화.
- `enablePubSub(...)`: 현재 SPOT channel 의 publish/subscribe capability 활성화.
- `attachChannelClient(...)`: 다른 channel 로 send/request 할 outbound DEALER(client) 부착.
- `attachSpotPublisherClient(...)`: 외부 노드가 특정 SPOT channel 로 publish 할 outbound publisher client 부착.
- `acceptSpotRoutesFromChannel(...)`: 지정 channel 에서 들어오는 spot route 수락.
- `configureEntrySpot(...)`: Entry Spot routing id 등 옵션 설정.
- `addSpotFactory(spotType)`: 이 node 가 생성/소유할 spot factory 를 타입 기준 등록. 같은
  `TSpot` 재등록은 예외. `create`/`getOrCreate` 는 이 타입과 정확히 일치하는 factory 를 고름.
- `addEntrySpot(entrySpotType)`: 이 node 의 자동 Entry Spot registry 등록. node 당 하나. 두 번
  등록하면 startup validation 오류. 미등록 시 빈 Entry Spot registry 를 쓰고, 매칭 handler
  없는 actor packet 은 명확한 dispatch error.

ActorGateway 는 별도 node builder 를 두지 않는다. `addNode(...)` 로 등록한 SpotNode 에
`enableRouter(...)` 와 router bind 를 설정한 뒤, stream 이 `attachActorGateway(spotNodeName)`
으로 그 local ingress node 를 참조한다(§6.1 `ZLinkStreamNodeBuilder`).

`addSpotMesh(channelName, ...)` 는 SPOT channel 이름과 node 묶음을 함께 소유한다. 그래서
`addNode(...)` 안에서 같은 channel 이름을 다시 받지 않는다.

### 6.4 socket / route / spot config

```ts
export interface ZLinkSocketConfig {
  maxMessageSize: number;
  sendHighWaterMark: number;
  receiveHighWaterMark: number;
  sendBufferSize: number;
  receiveBufferSize: number;
  linger?: number;          // ms (C# TimeSpan?)
  receiveTimeout?: number;  // ms
  sendTimeout?: number;     // ms
  connectTimeout?: number;  // ms
  handshakeInterval?: number; // ms
  ipv6: boolean;
  tcpNoDelay: boolean;
  immediate: boolean;
}

export interface ZLinkRouteConfig {
  routingId: RoutingId;
  requireKnownPeer: boolean;
  allowPeerHandover: boolean;
  enablePeerProbe: boolean;
  connectRoutingId: RoutingId;
}

export interface ZLinkOutboundRouteConfig {
  routingId: RoutingId;
  probeRouterOnConnect: boolean;
}

export interface ZLinkSpotPublisherConfig {
  sendHighWaterMark: number; // write-only (C# set;)
  sendTimeout?: number;      // ms
  linger?: number;           // ms
  noDrop: boolean;
}

export interface ZLinkSpotSubscriberConfig {
  receiveHighWaterMark: number; // write-only
  receiveTimeout?: number;      // ms
  linger?: number;              // ms
}

export interface ZLinkEntrySpotOptions {
  routingId: RoutingId;
}
```

> 코드 기준: dotnet 의 이름은 `IZLinkSocketConfig` / `IZLinkRouteConfig` /
> `IZLinkOutboundRouteConfig` / `IZLinkSpotPublisherConfig` / `IZLinkSpotSubscriberConfig` 다
> (스펙 문서의 `IZLinkCommonSocketOptions` 등과 이름이 다름). `ZLinkRouteConfig` 는 `RoutingId`,
> `ConnectRoutingId` 까지 포함한다. `TimeSpan?` 필드는 모두 `number`(ms)로 옮긴다.

- `timeout(...)`(call 단위): request 한 번에만 적용. capability runtime 기본값을 바꾸지 않음.
- `configureRouting(...)`: capability 가 routed peer 와 맺는 연결 규칙. public 표면에서 remote
  `RoutingId` 자체를 강제로 설정하지는 않으며, discovery 경로는 resolver/registry 가, manual
  연결은 endpoint 집합이 위치값을 소유한다.

## 7. Timer 인터페이스

spot lifecycle 안에서 `context.addTimer<THandler>(...)` 가 돌려주는 timer handle 이다.

```ts
export interface ZLinkTimer {
  readonly isDisposed: boolean;
  cancel(): Promise<void>;
}

export interface ZLinkTimerOptions {
  overrunPolicy?: ZLinkTimerOverrunPolicy; // 기본 SkipLateTicks
  maxCatchUpTicks?: number;                // 기본 1
  stopOnUnhandledException?: boolean;      // 기본 false
}

export enum ZLinkTimerOverrunPolicy {
  SkipLateTicks = 'skipLateTicks',
  CatchUpBounded = 'catchUpBounded',
  DelayNextTick = 'delayNextTick',
}

export interface ZLinkTimerTick {
  readonly name: string;
  readonly deliveryIndex: bigint;   // C# ulong
  readonly scheduledIndex: bigint;  // C# ulong
  readonly periodMs: number;        // C# TimeSpan
  readonly scheduledAt: Date;       // C# DateTimeOffset
  readonly startedAt: Date;
  readonly scheduledElapsedMs: number;
  readonly startedElapsedMs: number;
  readonly delayMs: number;
  readonly skippedTicks: bigint;    // C# ulong
}
```

framework 의 timer abstraction 은 native timer 를 그대로 노출하지 않는다. framework runtime 이
managed timer 를 만든 뒤 각 tick 을 handler 실행 문맥으로 넘긴다. user Spot timer 는 spot
직렬 실행 경로로 들어가고, Entry Spot timer 는 Entry Spot 전체 직렬 줄에 묶이지 않는다.

`ZLinkTimerTick` 은 timer 이름, 실제 전달된 callback 번호(`deliveryIndex`), fixed-rate 시간표의
tick 번호(`scheduledIndex`), 예정/시작 시각, 지연, 건너뛴 tick 수를 담는다. 지연·skip 은
monotonic clock 기준이며, `Date` 값은 로그/운영 관찰용 wall-clock 이다. `ulong` 은 안전하게
`bigint` 로 옮긴다.

overrun 정책:

- `SkipLateTicks`: 늦은 tick 을 합쳐 건너뛰고 최신 예정 시각 기준으로 이어 간다.
- `CatchUpBounded`: 밀린 tick 을 `maxCatchUpTicks` 개 callback 까지만 연속 실행.
- `DelayNextTick`: fixed-delay(handler 완료 뒤 다시 period 대기).

`maxCatchUpTicks` 는 `CatchUpBounded` 에서만 의미 있고 `0` 보다 커야 한다. 알 수 없는
overrun 정책 값은 설정 오류다. handler 예외는 기본적으로 monitoring 의 `TimerHandlerFailed`
event 로 기록하고 timer 는 계속 실행한다. `stopOnUnhandledException` 이 `true` 이면 첫 예외
뒤 중단하고 `TimerStoppedAfterUnhandledException` event 를 기록한다.

`cancel()` 은 native timer stop wrapper 가 아니라 framework managed timer loop 를 중단하는 표면이다.

## 8. Handler Filter

ZLink handler 호출 전후의 공통 처리 표면이다. HTTP middleware 와 별개 메커니즘이다.

```ts
/**
 * filter pipeline 의 next 단계. 호출하면 다음 filter 또는 실제 handler 가 실행되고
 * 결과가 반환된다. C# ZLinkHandlerDelegate 대응.
 */
export type ZLinkHandlerDelegate = () => Promise<unknown>;

/** filter 에 전달되는 호출 context. 역직렬화된 message 와 handler context 를 함께 들고 다닌다. */
export interface ZLinkHandlerInvocation {
  readonly message: unknown;
  readonly context: ZLinkHandlerContext;
  readonly channelName?: string;
  readonly packetName?: string;
}

export interface ZLinkHandlerFilter {
  invoke(invocation: ZLinkHandlerInvocation, next: ZLinkHandlerDelegate): Promise<unknown>;
}
```

등록:

```ts
ZLinkModule.forRoot({
  filters: [LoggingZLinkFilter, ValidationZLinkFilter],
});
// 또는 fluent: options.useFilter(LoggingZLinkFilter)
```

filter 는 framework 가 직접 `new` 하지 않고 NestJS DI 에서 resolve 한다. 주된 용도: logging,
validation, authorization, metrics, exception → framework 표준 오류 응답 매핑. NestJS 의 HTTP
middleware/interceptor 는 HTTP pipeline 전용이라 ZLink handler 에는 자동 적용되지 않는다.
공통 처리가 필요하면 `ZLinkHandlerFilter` 를 쓴다.

## 9. Request reply 타입 지정

request 메시지 타입에는 framework 전용 marker interface 를 붙이지 않는다.

- 메시지는 codec 이 직렬화할 payload 계약만 표현한다.
- reply 타입은 호출부에서 `submit<TReply>()` 로 명시한다.

```ts
const reply = await client
  .requestToChannel('profile', { accountId } satisfies GetProfileRequest)
  .submit<GetProfileReply>();
```

- handler 는 메서드 시그니처만으로 request/reply 타입을 결정한다.
- client 호출부는 packet 이름과 payload 만 넘기고, 기다릴 reply 타입은 `submit<TReply>()` 에서 지정한다.

기본 packet key 는 payload 생성자 이름(`Type.name`)을 쓴다. 부적절하면 `@ZLinkPacket('name')`
class decorator 로 명시 metadata 를 부여한다. 이 metadata 는 outbound 기본 해석과 inbound
handler 기본 매핑 양쪽에서 공통으로 쓴다.

> TS 한계: 런타임 타입 소거 때문에 payload 식별은 클래스 생성자 이름 또는 명시적
> `@ZLinkPacket` / `packetName` 에 의존한다. 순수 구조적 타입(plain interface)만 쓰면 packet
> key 를 명시해야 한다(코드로 검증되는 제약). 의미가 아니라 표면 제약이다.

## 10. Registry / Monitoring 인터페이스

### 10.1 ZLinkRegistryQuery

같은 프로세스의 embedded Registry 를 조회한다. `AddZLinkRegistry(...)` 시점에 DI 에 등록되며
status, service summary, topology, member peers 를 제공한다. 비동기인 이유는 registry 가 아직
시작되지 않았을 수 있고 snapshot 수집이 host lifecycle 과 맞물리기 때문이다.

```ts
export interface ZLinkRegistryQuery {
  status(): Promise<ZLinkRegistryStatus>;
  serviceSummary(filter?: ZLinkRegistryServiceSummaryFilter): Promise<ZLinkRegistryServiceSummaryEntry[]>;
  topology(filter?: ZLinkRegistryTopologyFilter): Promise<ZLinkRegistryTopologyEntry[]>;
  memberPeers(channelName: string): Promise<ZLinkMemberPeerEntry[]>;
}
```

> 코드 기준: dotnet `IZLinkRegistryQuery.TopologyAsync` 는 filter 오버로드 하나만 있다(스펙
> 문서가 무인자 오버로드를 따로 적었지만 코드는 optional filter 하나). `MemberPeersAsync` 는
> `channelName` 하나만 받는다.

### 10.2 ZLinkRegistryQueryClient

다른 프로세스의 Registry 를 원격 조회한다. `AddZLinkRegistryQueryClient(...)` 로 별도 등록하며
topology snapshot 만 제공한다. 원격 요청 특성상 비동기다.

```ts
export interface ZLinkRegistryQueryClient {
  topology(filter?: ZLinkRegistryTopologyFilter): Promise<ZLinkRegistryTopologyEntry[]>;
}

export interface ZLinkRegistryQueryClientOptions {
  endpoint: string;
}
```

embedded Registry 를 띄우는 옵션(`AddZLinkRegistry`)도 코드에 있다.

```ts
export interface ZLinkRegistryOptions {
  pubEndpoint: string;
  routerEndpoint: string;
  registryId: number;
  heartbeatIntervalMs: number;
  heartbeatTimeoutMs: number;
  broadcastIntervalMs: number;
  addPeer(peerPubEndpoint: string): void;
}
```

### 10.3 runtime monitoring

runtime monitoring 은 운영 표면이다. socket 하부 monitor 와 registry/spot snapshot diff 를 감싼다.

```ts
export interface ZLinkMonitoringOptions {
  addSocketEvents(sourceName: string, ...events: ZLinkSocketEventKind[]): void;
  addRegistryEvents(sourceName: string, intervalMs: number): void;
  addSpotEvents(sourceName: string, intervalMs: number): void;
}

export interface ZLinkRuntimeEvent {
  readonly sourceName: string;
  readonly timestamp: Date;
}

export interface ZLinkRuntimeEventHandler<TEvent extends ZLinkRuntimeEvent> {
  handle(event: TEvent): Promise<void>;
}

export interface ZLinkRuntimeEventPublisher {
  publish<TEvent extends ZLinkRuntimeEvent>(event: TEvent): Promise<void>;
}
```

> 코드 기준: dotnet `IZLinkMonitoringOptions` 는 `AddSocketEvents` / `AddRegistryEvents` /
> `AddSpotEvents` 세 메서드만 가진다(기존 draft 의 `addDiscoveryEvents` 는 코드에 없다.
> discovery 상태는 runtime event 로 올리지 않고 registry topology/service/member snapshot 으로
> 조회한다). `AddSocketEvents(sourceName)` 에서 event 목록을 비우면 해당 source 의 모든 logical
> event kind 를 구독한다는 의미다.

event 표면은 두 단계다. event kind 는 enum, callback payload 는 record(TS interface)다.

```ts
export enum ZLinkSocketEventKind {
  Connected = 'connected',
  ConnectionReady = 'connectionReady',
  Disconnected = 'disconnected',
  HandshakeFailed = 'handshakeFailed',
  PeerAdmissionChanged = 'peerAdmissionChanged',
  Closed = 'closed',
  Internal = 'internal',
}

export enum ZLinkSocketNativeEventType {
  Connected = 0x0001,
  ConnectDelayed = 0x0002,
  ConnectRetried = 0x0004,
  Listening = 0x0008,
  BindFailed = 0x0010,
  Accepted = 0x0020,
  AcceptFailed = 0x0040,
  Closed = 0x0080,
  CloseFailed = 0x0100,
  Disconnected = 0x0200,
  MonitorStopped = 0x0400,
  HandshakeFailedNoDetail = 0x0800,
  ConnectionReady = 0x1000,
  HandshakeFailedProtocol = 0x2000,
  HandshakeFailedAuth = 0x4000,
  PeerAdmissionChanged = 0x8000,
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

export enum ZLinkRegistryEventKind {
  StatusChanged = 'statusChanged',
  TopologyChanged = 'topologyChanged',
  ServiceSummaryChanged = 'serviceSummaryChanged',
}

export interface ZLinkRegistryEvent extends ZLinkRuntimeEvent {
  readonly event: ZLinkRegistryEventKind;
  readonly status?: ZLinkRegistryStatus;
  readonly topology?: readonly ZLinkRegistryTopologyEntry[];
  readonly serviceSummary?: readonly ZLinkRegistryServiceSummaryEntry[];
}

export enum ZLinkSpotEventKind {
  StatusChanged = 'statusChanged',
  PeersChanged = 'peersChanged',
  SubjectsChanged = 'subjectsChanged',
  TimerHandlerFailed = 'timerHandlerFailed',
  TimerStoppedAfterUnhandledException = 'timerStoppedAfterUnhandledException',
}

export interface ZLinkSpotTimerDiagnostic {
  readonly spotRid: RoutingId;
  readonly isEntrySpot: boolean;
  readonly timerName: string;
  readonly handlerType: string;
  readonly deliveryIndex: bigint;
  readonly scheduledIndex: bigint;
  readonly exceptionType: string;
  readonly exceptionMessage: string;
}

export interface ZLinkSpotEvent extends ZLinkRuntimeEvent {
  readonly event: ZLinkSpotEventKind;
  readonly status?: ZLinkSpotNodeStatus;
  readonly peers?: readonly ZLinkSpotNodePeerEntry[];
  readonly subjects?: readonly ZLinkSpotNodeSubjectEntry[];
  readonly timerDiagnostic?: ZLinkSpotTimerDiagnostic;
}
```

> 코드 기준: `ZLinkSpotEventKind` 는 `TimerHandlerFailed`, `TimerStoppedAfterUnhandledException`
> 을 포함한다(스펙 문서 §2 master table 에는 3개만 적었으나 코드는 5개). `ZLinkSpotTimerDiagnostic`
> 의 `SpotRid` 는 **하나**다(스펙 문서의 중복 `SpotRid` 두 필드는 문서 오타). `IZLinkRuntimeEventPublisher`
> 는 framework 가 즉시 발생 event 를 monitoring dispatcher 로 넘길 때 쓰는 public contract 다.
> application 이 직접 구현하는 쪽은 보통 `ZLinkRuntimeEventHandler<TEvent>` 다.

`TimerHandlerFailed` / `TimerStoppedAfterUnhandledException` 은 polling interval 을 기다리는
snapshot diff event 가 아니라, timer handler 의 처리되지 않은 예외 시점에 즉시 발행된다.
exception 객체 자체는 public payload 에 넣지 않고 `ZLinkSpotTimerDiagnostic` 에 직렬화 가능한
요약만 담는다.

source 의미: socket event 는 `SocketMonitor` 를 감싸며 source 이름은 `channel.capability` 또는
`spotNode.capability` 형태(`profile.server`, `stage-node.router`)다. registry/spot event 는 raw
monitor 가 아니라 status/topology/summary 또는 status/peers/subjects 의 polling + diff 합성이다.
discovery 상태는 runtime event 로 올리지 않고 registry snapshot 으로 조회한다.

### 10.4 Registry / Spot monitoring model

monitoring/registry event payload 에 쓰이는 model 과 enum 이다. 상세 의미는
[nestjs-registry.ko.md](./nestjs-registry.ko.md) / [nestjs-monitoring.ko.md](./nestjs-monitoring.ko.md)
가 소유한다. 여기서는 표면만 고정한다.

```ts
export enum ZLinkAutoConnectType {
  Invalid = 'invalid', RouteMesh = 'routeMesh', ClientServer = 'clientServer',
  DealerMesh = 'dealerMesh', Fanout = 'fanout', SpotMesh = 'spotMesh',
}
export enum ZLinkServiceKind { Discovery = 'discovery', SpotSub = 'spotSub', SpotPub = 'spotPub', Socket = 'socket' }
export enum ZLinkServiceRole { Invalid = 'invalid', Spot = 'spot', Router = 'router', Dealer = 'dealer', Pub = 'pub', Sub = 'sub' }
export enum ZLinkRegistryState { Idle = 'idle', Active = 'active', Degraded = 'degraded', Error = 'error' }
export enum ZLinkTopologySource { Manual = 'manual', Discovery = 'discovery', Registry = 'registry' }
export enum ZLinkTopologyState {
  Discovered = 'discovered', Connecting = 'connecting', Ready = 'ready', Lost = 'lost', Error = 'error', Stopped = 'stopped',
}
export enum ZLinkAdmissionState { Serving = 'serving', Draining = 'draining' }

export interface ZLinkRegistryServiceSummaryFilter {
  autoConnectType?: ZLinkAutoConnectType;
  serviceRole?: ZLinkServiceRole;
  channelName?: string;
}
export interface ZLinkRegistryTopologyFilter {
  autoConnectType?: ZLinkAutoConnectType;
  serviceKind?: ZLinkServiceKind;
  serviceRole?: ZLinkServiceRole;
  channelName?: string;
  routingId?: RoutingId;
  state?: ZLinkTopologyState;
  source?: ZLinkTopologySource;
}

export interface ZLinkRegistryStatus {
  registryId: number; bindEndpoint: string; state: ZLinkRegistryState;
  topologyEntryCount: number; peerRegistryCount: number; connectedPeerRegistryCount: number;
  listSeq: bigint; lastError: number; lastChangedMs: bigint;
}
export interface ZLinkRegistryServiceSummaryEntry {
  autoConnectType: ZLinkAutoConnectType; serviceRole: ZLinkServiceRole; channelName: string;
  totalCount: number; connectingCount: number; readyCount: number; errorCount: number;
  stoppedCount: number; lastReportedMs: bigint;
}
export interface ZLinkRegistryTopologyEntry {
  autoConnectType: ZLinkAutoConnectType; routingId?: RoutingId; serviceKind: ZLinkServiceKind;
  serviceRole: ZLinkServiceRole; channelName: string; endpoint: string; source: ZLinkTopologySource;
  state: ZLinkTopologyState; desiredCount: number; readyCount: number; errorCode: number;
  lastReportedMs: bigint; spotKind: ZLinkSpotKind;
}
export interface ZLinkMemberPeerEntry {
  autoConnectType: ZLinkAutoConnectType; serviceRole: ZLinkServiceRole; channelName: string;
  endpoint: string; routingId?: RoutingId; value: bigint; weight: number;
}

export enum ZLinkSpotNodeState { Idle = 'idle', Connecting = 'connecting', PartialReady = 'partialReady', Ready = 'ready', Error = 'error' }
export enum ZLinkSpotPeerSource { Manual = 'manual', Discovery = 'discovery', Mixed = 'mixed' }
export enum ZLinkSpotPeerKind { SpotMesh = 'spotMesh', RouterChannel = 'routerChannel' }
export enum ZLinkSpotPeerState { Configured = 'configured', Connecting = 'connecting', Connected = 'connected' }
export enum ZLinkSubjectKind { None = 'none', Topic = 'topic', Pattern = 'pattern' }
export enum ZLinkSpotRole { Pub = 'pub', Sub = 'sub' }

export interface ZLinkSpotNodeStatus {
  channelName: string; localEndpoint: string; nodeRoutingId?: RoutingId; state: ZLinkSpotNodeState;
  configuredPeerCount: number; activePeerCount: number; connectedPeerCount: number;
  subjectCount: number; readySubjectCount: number; lastError: number; lastChangedMs: bigint;
}
export interface ZLinkSpotNodePeerEntry {
  channelName: string; localEndpoint: string; peerEndpoint: string; source: ZLinkSpotPeerSource;
  kind: ZLinkSpotPeerKind; state: ZLinkSpotPeerState; weight: number;
  connectedSinceMs: bigint; lastChangedMs: bigint;
}
export interface ZLinkSpotNodeSubjectEntry {
  role: ZLinkSpotRole; subject: string; subjectKind: ZLinkSubjectKind;
  readyPeerCount: number; activePeerCount: number; lastChangedMs: bigint;
}
```

`ZLinkSpotNodeStatus` 와 `ZLinkSpotNodePeerEntry` 의 첫 필드는 `channelName` 이다(예전 이름
`serviceName` 에서 channel 단위로 통일되며 rename).

## 11. Decorator 정의

C# attribute 는 node 에서 method/class decorator 로 옮긴다. decorator factory 시그니처를 함께 고정한다.

### 11.1 서버 간 messaging

```ts
/** class decorator. handler 클래스가 어느 논리 그룹에 속하는지 표시한다. (C# [ZLinkHandlerGroup], AllowMultiple) */
export function ZLinkHandlerGroup(groupName: string): ClassDecorator;

/** method decorator. request handler. (C# [ZLinkRequest]) */
export function ZLinkRequest(packetName?: string): MethodDecorator;

/** method decorator. one-way send handler. (C# [ZLinkSend]) */
export function ZLinkSend(packetName?: string): MethodDecorator;

/** class decorator. payload packet key 명시. (C# [ZLinkPacket("name")]) */
export function ZLinkPacket(packetName: string): ClassDecorator;
```

> 코드 기준: dotnet attribute 는 `ZLinkRequestAttribute`(PacketName init),
> `ZLinkSendAttribute`(PacketName init), `ZLinkHandlerGroupAttribute`(groupName ctor),
> `ZLinkPacketAttribute`(packetName ctor)다. publish 는 `ZLinkPublishAttribute` 다(아래 §11.3).
> 기존 draft 의 `ZLinkEvent` 가 아니라 `ZLinkPublish` 로 고정한다.

`ZLinkHandlerGroup` 의 그룹 이름은 사용자가 정하는 임의 문자열이며 실제 channel 이름과
분리된다. 같은 그룹을 여러 channel 에, 같은 channel 에 여러 그룹을 매핑할 수 있다.
`ZLinkRequest` / `ZLinkSend` 는 channel 이름을 받지 않는다. 노출 channel 은 channel
registration 의 `addHandlerGroup(...)` 매핑이 정한다.

```ts
@Injectable()
@ZLinkHandlerGroup('api')
export class ProfileHandlers {
  @ZLinkRequest()
  async get(request: GetProfileRequest, context: ZLinkRequestContext): Promise<GetProfileReply> {
    // ...
  }
}
```

같은 그룹을 여러 channel 에 매핑하는 것은 허용한다. 다만 같은 channel 안에서 동일
`kind + packet name` 이 둘 이상으로 해석되면 startup validation 오류다(그룹 내/그룹 간 충돌 모두).

### 11.2 Spot actor handler

Entry Spot 과 user Spot 에서 같은 decorator 이름을 쓴다. 어느 registry 에 등록되는지는
`configure()` 의 등록 위치가 정한다.

```ts
export function ZLinkSpotActorSend(packetName?: string): MethodDecorator;
export function ZLinkSpotActorRequest(packetName?: string): MethodDecorator;
export function ZLinkSpotActorJoin(): MethodDecorator;
export function ZLinkSpotPostActorJoined(): MethodDecorator;
export function ZLinkSpotActorLeft(): MethodDecorator;
export function ZLinkSpotActorDisconnected(): MethodDecorator;
```

method 시그니처 순서:

- send: `(spotOrEntrySpot, actor, context, message)` 반환 없음
- request: `(spotOrEntrySpot, actor, context, request)` reply 반환
- actor join: `(spot, actor, request)` reply 반환
- joined/left: `(spotOrEntrySpot, actor, result: ZLinkSpotActorChangeResult)` 반환 없음
- disconnected: `(spotOrEntrySpot, actor)` 반환 없음

`packetName` 미지정 시 message/request 타입의 packet 이름을 쓴다. 한 handler 클래스에 여러 spot
actor handler decorator method 가 있으면 `addHandler(handlerType)` 에서 모호하므로 startup
validation 오류다.

### 11.3 publish

```ts
export function ZLinkPublish(packetName?: string): MethodDecorator;
```

이름을 `Event` 가 아니라 `Publish` 로 둔 까닭은 producer 동사(`ZLinkFanoutClient.publish(...)`)에
맞추기 위해서다. 그래야 `@ZLinkRequest` / `@ZLinkSend` / `@ZLinkPublish` 세 표면이 같은 패턴으로
읽힌다. publish handler 도 모든 subscriber channel 에 전역 자동 노출되지 않는다. 노출할 그룹은
`addHandlerGroup(...)` 으로 명시한다.

### 11.4 SPOT

```ts
export function ZLinkSpotRequest(packetName?: string): MethodDecorator;
export function ZLinkSpotSubscription(spotNodeName: string, topic: string): MethodDecorator;
```

> 코드 기준: `ZLinkSpotSubscriptionAttribute` 는 `(spotNodeName, topic)` 두 인자를 가진다.

### 11.5 stream

```ts
export function ZLinkStreamPacket(): MethodDecorator;
export function ZLinkStreamRaw(): MethodDecorator;
```

stream 은 framework Header 기반 packet session 을 하나의 축으로 본다. session lifecycle 은
`onConnected`, `onDisconnected`, `onError` 세 callback 으로 노출한다.

## 12. 시그니처 규칙

decorator 기반 handler 의 메서드 시그니처 규칙:

- 첫 인자: decoded payload 타입
- 두 번째 인자: context 타입(생략 가능)
- request handler 반환: `Promise<T>`
- send/publish handler 반환: `Promise<void>`

framework scanner 와 runtime invoker 는 반환 타입을 등록 단계에서 먼저 판정한다. 허용되지
않는 반환형은 startup validation 오류다(C# 의 `Task`/`Task<T>`/`ValueTask`/`ValueTask<T>` 구분에
대응하는 node 규칙은 "`handle` 은 `Promise` 를 반환해야 한다"로 둔다).

node 경계를 넘는 payload 는 request/reply payload 와 session actor dispatch 두 곳에서 나온다.
이 payload 는 codec 이 직렬화/역직렬화할 수 있는 DTO 여야 한다. root 타입이나 컬렉션 원소가
abstract class / interface 면 명시적 codec/converter 계약 없이는 등록 단계나 첫 submit 이전에
명확한 configuration 오류로 실패시킨다. domain 내부 이벤트 계층을 그대로 reply DTO 로 쓰지 않고,
wire 에 올릴 구체 DTO 로 한 번 변환한다.

핵심 규칙은 하나다: "resolved packet key 하나는 동일한 실행 문맥 안에서 단 하나의 handler 에만
매핑된다." 실행 문맥 구분:

- 일반 channel messaging 의 실행 문맥은 inbound channel capability 다.
- actor 와 spot 은 각각 고유한 실행 문맥을 가진다.

class 구성 방식은 자유롭다(주제별 묶음 `UserHandlers`, packet 별 단일 class `UserGetHandler` 모두 허용).

## 13. DI 동작 기준

- handler class 는 NestJS DI 에서 resolve 한다. handler 의 생성자 주입이 동작해야 한다.
- outbound client(`ZLinkChannelClient`, `ZLinkFanoutClient`, `ZLinkSpotManager` 등)도 같은
  컨테이너에서 provider token 으로 주입된다.
- `ZLinkHandlerFilter` 구현체도 같은 컨테이너에서 resolve 한다.
- framework 는 별도 객체 생성기를 두지 않고 NestJS `ModuleRef`/DI 기반으로 handler invocation 을 구성한다.
- public registration 함수에 DI 컨테이너를 매번 노출할 필요는 없다.
- `Spot`, packet handler, timer handler 는 framework 가 만든 per-spot scope 에서 resolve 한다.
  `context.handlers.addPacket(handlerType)`, `context.addTimer(...)` 는 service locator 가 아니라
  "이 타입을 spot scope 에서 쓰겠다"는 등록 선언이다.
- `onCreate(...)` / `onInitialize(...)` 도 DI 컨테이너를 직접 받지 않고 spot 자체의 생성자 주입과
  cached dependency 를 쓴다.

### 13.1 public service DI 등록 조건

모든 public service 를 항상 DI 에 등록하지는 않는다. 생성자 주입은 그 기능을 쓸 수 있다는
신호이므로 capability 가 없는 service 는 등록하지 않는다.

| Interface | DI 등록 조건 |
|-----------|--------------|
| `ZLinkChannelClient` | 항상 등록. channel 누락은 호출 시 `ZLinkConfigurationException` |
| `ZLinkRouteClient` | 항상 등록. route channel 누락은 호출 시 `ZLinkConfigurationException` |
| `ZLinkFanoutClient` | 항상 등록. publisher capability 누락은 호출 시 `ZLinkConfigurationException` |
| `ZLinkSpotManager` | `SpotNode` 가 하나 이상일 때 등록 |
| `ZLinkSpotPublisherClient` | Spot publisher client capability 가 하나 이상일 때 등록 |
| `ZLinkActorManager` | `SpotNode` 와 actor factory 가 모두 있을 때 등록 |
| `ZLinkBoundSession` | actor bound session runtime 등록 시 |
| `ZLinkSpotRemoteAddressResolver` | 해당 resolver registration 이 있을 때 등록 |

channel 이름의 위치는 handler class/method decorator 가 아니라 channel registration 에 둔다.
outbound-only 앱이라면 server capability 를 가진 channel 이 아예 없을 수도 있다.

## 14. 결정된 기준

- `ZLinkRequestContext` 와 `ZLinkSendContext` 는 합치지 않는다. request-response 와 one-way send
  는 timeout/reply/호출 의미가 다르다.
- `onError(...)` 는 session 에 매핑할 수 있는 transport 오류만 수신한다. application handler 내부
  예외, bind/accept/close 같은 node 단위 오류, handshake 이전 monitor 이벤트는 runtime monitoring
  표면에만 남긴다.
- framework runtime 은 `ZLinkChannelClient` 위에 channel 별 typed wrapper 를 공식 기본 표면으로
  제공하지 않는다. 필요하면 응용/확장 패키지가 얹는다.
- `spotRid` 타입은 `RoutingId` 를 쓴다. transport `RoutingId` 와 logical spot rid 를 같은 타입으로 노출한다.
- `ZLinkRegistryQuery` 와 `ZLinkRegistryQueryClient` 는 묶지 않는다. in-process 조회와 원격 조회는
  lifecycle/실패 모델/제공 범위가 다르다.

## 15. 회귀 테스트

이 문서의 interface 항목은 두 가지를 확인한다.

- public surface 가 backend(Node 바인딩) 구현 세부사항을 새어 내지 않는지.
- 등록 · handler · client 표면이 런타임 테스트와 같은 이름을 유지하는지.

interface 설명을 바꾸면 node 측 대응 회귀 테스트(scaffold smoke / registry+monitoring /
filter order / handler-result / spot actor registry / local session relay)도 함께 조정한다.
정식 대응표는 dotnet `ScaffoldSmokeTests`, `RegistryAndMonitoringTests`, `FiltersAndHttpTests`,
`HandlerResultAwaiterTests`, `ProtocolTests`, `LocalSessionRelayTests` 가 검증하는 계약과 동일한
의미를 node 런타임에서 보존하는 것이다.

[^public-contract]: 라이브러리가 외부에 약속한 공식 API. 한 번 공개되면 호환성을 깨지 않고는 변경하기 어렵다.
[^transport]: 메시지가 실제로 네트워크나 IPC 위에서 오가는 하부 계층. ZLink 에서는 socket, stream, route 등이 이에 해당한다.
