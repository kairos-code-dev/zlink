<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework NestJS SPOT](nestjs-spot.ko.md) | [다음: ZLink Framework NestJS Actor](nestjs-actor.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../README.ko.md)

[Node.js 묶음](../README.ko.md) | [인터페이스](handler-interfaces.ko.md) | [channel](nestjs-channel-messaging.ko.md) | [SPOT](nestjs-spot.ko.md)

> 이 문서는 Node.js `ZLink Framework`(NestJS)의 STREAM **스펙**이다. 표면은 NestJS
> 모양이다. 표기가 어긋나면 `framework/languages/node` 코드가 기준이다. 번역 규칙은
> [dotnet→node 표면 매핑](../internals/dotnet-to-node-surface-mapping.ko.md) 를
> 따른다.

# ZLink Framework NestJS STREAM Integration

## 1. 목표

이 절은 STREAM 표면이 어떤 모양을 따라야 하는지를 정리한다.

`STREAM` 은 일반 request-response 와 성격이 다르다. 다음 요소가 훨씬 더 중요한
축이 된다.

- 연결 수명
- peer 식별
- packet framing[^framing]
- session lifecycle[^session-lifecycle]

이 문서의 목표는 NestJS framework 표면에서 `STREAM` 을 framework Header 기반의
packet session 방식으로 정리하는 것이다.

현재 스펙에서는 application 이 직접 `recv` loop 를 돌리는 방식은 지원 대상으로
잡지 않는다. framework 가 수신 dispatch 를 맡고, 사용자는 session 만 구현하는
쪽을 기본으로 둔다. NestJS server application 은 `@zlink-systems/stream-connector`
의 frame/header codec 을 사용해서 server 를 직접 만들지 않는다. connector 패키지는
외부 client 가 STREAM server 에 접속할 때 쓰는 public API 이며, server host 는
framework stream node runtime 이 담당한다.

## 2. 기본 방향

이 절은 STREAM 표면이 따르는 설계 원칙을 정리한다.

`STREAM` 은 일반 channel messaging handler 와 같은 감각으로 무리하게 맞추지
않는다. 특히 다음 원칙을 둔다.

- framework 가 stream header 를 decode 한 뒤 `ZlinkStreamHeader header` 와
  `ZLinkMessage payload` 를 session callback 에 전달한다.
- `playhouse` 처럼 header 는 framework 내부에서 packet name 과 metadata 로
  해석한다. application 은 `header.name` 을 보고 각 packet 타입으로
  decode 하는 모델을 자연스러운 기본으로 본다.
- payload decode는 transport 본체에 섞지 않는다. 대신 framework runtime이 등록된
  codec registry로 `ZLinkMessage`를 만들고, application은 필요한 packet만
  `decode<T>()`로 읽는다.
- recv loop 는 application 표면에 직접 올리지 않는다.
- `onConnected(...)`, `onDisconnected(...)` 는 session lifecycle 의
  기본 표면으로 올린다.
- `onError(...)` 는 application 예외가 아니라, monitor 에서 관찰 가능한
  transport 오류를 session 단위로 다시 올려주는 축으로만 제한한다.

요컨대 현재 방향은 framework Header 기반 packet session 위에 session lifecycle
을 함께 올리는 쪽이다. raw chunk[^raw-chunk] 직접 처리와 사용자 정의 Header
framing 은 MVP[^mvp] 범위에 넣지 않는다.

NestJS 통합에서 stream session type 은 NestJS provider 로 등록한다. framework 는
새 stream 연결을 session 으로 활성화할 때 provider resolver 를 통해 session 또는
session factory 를 resolve 한다. session 이 repository, actor manager, outbound
client 같은 service 를 필요로 하면 생성자 주입으로 받는다. 연결별 transport,
session id, header 같은 런타임 값은 provider 가 아니며 framework context 나
DI-managed factory 의 `create(...)` 인자로 전달한다.

> **packet session vs raw session.** 초기 node 드래프트는
> `ZLinkPacketStreamSession`(`onPacket`)과 `ZLinkRawStreamSession`(`onRaw`)을
> 두 축으로 그렸고, `ZLinkStream` 에 `write` 와 `writePacket` 을 함께 두었다.
> 그러나 dotnet **코드**의 공개 계약에는 raw session 표면과 `writePacket` 이
> 없다. 코드의 session 진입점은 `OnDispatchAsync(header, payload, ct)` 하나이며
> (`Contracts/Streams/IZLinkSession.cs`), `IZLinkStream` 의 raw write 도
> `Write(payload, flags)` 하나뿐이다(`Contracts/Streams/IZLinkStream.cs`).
> 따라서 이 스펙은 **코드를 기준으로** packet session 단일 축으로 고정하고,
> callback 이름을 `onDispatch` 로, raw write 를 `write` 하나로 정렬한다. raw
> chunk 직접 처리 표면은 §7 의 결정에 따라 공개 계약에 넣지 않는다(드래프트의
> `onRaw`/`writePacket` 은 채택하지 않는다).

## 3. 인터페이스 기준

이 절은 STREAM 표면이 노출하는 핵심 타입을 정리한다.

인터페이스 전체 기준은 [handler-interfaces.ko.md](handler-interfaces.ko.md)
를 참고한다. TypeScript 에서는 C# 의 `I` prefix 관례를 쓰지 않으므로
`IZLinkX` 는 `ZLinkX` 로 옮긴다. `STREAM` 쪽 핵심 기준은 다음과 같다.

```ts
export interface ZLinkStream {
  readonly sessionId: string;

  readonly routingId: string | undefined;

  readonly localAddr: string | undefined;

  readonly remoteAddr: string | undefined;

  /**
   * 명시 raw stream frame 을 보낸다.
   * 송신 성공 여부를 boolean 으로 반환한다.
   */
  write(payload: Message, flags?: SendFlags): boolean;

  close(signal?: AbortSignal): Promise<void>;
}

export enum ZLinkStreamSessionError {
  Internal = 0,
  TransportError = 1,
  // onError 로 전달되지 않는다. handshake 실패는 runtime monitoring 에만 남긴다.
  // onError 로 전달하지 않는다는 정책은 아래 결정된 기준을 따른다.
  HandshakeFailed = 2,
}

export interface ZLinkStreamDiagnostic {
  readonly nativeCode: number;
  readonly message: string | undefined;
}

export interface ZLinkStreamError {
  readonly error: ZLinkStreamSessionError;
  readonly diagnostic: ZLinkStreamDiagnostic | undefined;
}

export class ZLinkMessageMetadata {
  static readonly empty: ZLinkMessageMetadata;

  readonly values: ReadonlyMap<string, string>;

  find(key: string): string | undefined;
}

export interface ZLinkSession {
  readonly context: ZLinkSessionContext;

  onConnected?(context: ZLinkSessionContext): Promise<void>;

  onDisconnected?(context: ZLinkSessionContext): Promise<void>;

  onError?(context: ZLinkSessionContext, error: ZLinkStreamError): Promise<void>;

  /**
   * framework 가 소유한 inbound stream payload 를 처리한다.
   * payload 는 codec registry 와 함께 framework 가 감싼 값이다. session 은
   * 필요한 packet 만 decode 하고, relay(...) 같은 framework API 에 넘길 때는
   * decode 하지 않은 채 그대로 넘길 수 있다.
   */
  onDispatch?(
    header: ZlinkStreamHeader,
    payload: ZLinkMessage,
    signal?: AbortSignal,
  ): Promise<void>;
}

export interface ZLinkSessionClient {
  send<TMessage>(message: TMessage): ZLinkSessionSendCall;

  reply<TMessage>(message: TMessage): ZLinkSessionReplyCall;
}

export interface ZLinkSessionActors {
  readonly bound: ReadonlyArray<ZLinkSessionActor>;

  bind(actor: ZLinkActor, signal?: AbortSignal): Promise<ZLinkSessionActor>;

  bind(actor: ActorRef, signal?: AbortSignal): Promise<ZLinkSessionActor>;

  find(actorId: string): ZLinkSessionActor | undefined;
}

export interface ZLinkActorManager {
  create(
    actorId: string,
    actorType: string,
    signal?: AbortSignal,
  ): Promise<ZLinkActor>;

  find(actorId: string, signal?: AbortSignal): Promise<ZLinkActor | undefined>;

  getOrCreate(
    actorId: string,
    actorType: string,
    signal?: AbortSignal,
  ): Promise<ZLinkActor>;
}

export interface ZLinkSessionActor {
  readonly actorId: string; // = ref.actorId

  readonly ref: ActorRef;

  relay(
    header: ZlinkStreamHeader,
    payload: ZLinkMessage,
    signal?: AbortSignal,
  ): Promise<void>;

  notifyDisconnected(signal?: AbortSignal): Promise<void>;
}

export interface ZLinkSessionSendCall {
  metadata(key: string, value: string): ZLinkSessionSendCall;
  packetName(messageName: string): ZLinkSessionSendCall;
  compress(): ZLinkSessionSendCall;
  submit(signal?: AbortSignal): Promise<void>;
}

export interface ZLinkSessionReplyCall {
  metadata(key: string, value: string): ZLinkSessionReplyCall;
  compress(): ZLinkSessionReplyCall;
  submit(signal?: AbortSignal): Promise<void>;
}

export interface ZLinkSessionContext {
  readonly sessionId: string;

  readonly routingId: string | undefined;

  readonly localAddr: string | undefined;

  readonly remoteAddr: string | undefined;

  readonly client: ZLinkSessionClient;

  readonly actors: ZLinkSessionActors;

  close(signal?: AbortSignal): Promise<void>;
}
```

`context` 는 framework 가 session 을 생성할 때 생성자 인자로 제공한다.
session 구현체는 이 값을 읽기 전용 property 로 그대로 노출해야 하며, runtime 은
생성 직후 같은 context 인스턴스인지 검증한다.

framework 에서는 low-level zlink binding 수신 표면을 application 에 직접 노출하지
않는다. framework runtime 이 binding 의 수신자가 되고, session callback 에는
framework `ZLinkMessage` 를 전달한다.

`onDispatch(...)` 로 전달된 `payload` 는 framework `ZLinkMessage` 다. session 은
`payload.decode<T>()` 로 DTO를 읽거나 `ZLinkSessionActor.relay(...)` 같은 framework
API 에 그대로 전달한다.

application 이 raw frame 을 직접 보내야 할 때만 `Message` 를 `ZLinkStream.write(...)`
에 넘긴다. 보통의 session 응답은 `context.client.send(...)`,
`context.client.reply(...)` 같은 typed builder 를 사용한다.

`context.client.send(...)` 와 `ZLinkBoundSession.send(...)` 의 packet name
해석은 channel client 와 같은 규칙을 따른다. class instance 처럼 런타임 생성자
이름이 의미 있는 payload 는 기본 packet name 으로 쓸 수 있다. 반대로 plain
object, primitive, array, `Buffer`, `Uint8Array`, `Date` 처럼 구조적 값이거나
내장 타입인 payload 는 packet 타입을 나타내지 못하므로 `packetName(...)` 을
명시해야 한다. 이 제약은 TypeScript interface 가 런타임에 사라지는 언어 특성
때문에 필요하다.

session 이 일부 packet 만 직접 처리하고 나머지 정책을 스스로 정하고 싶을 때는
`ZLinkSessionPacketHandler<TSessionContext>` 와
`ZLinkSessionPacketDispatcher<TSessionContext>` 를 사용할 수 있다.

```ts
export interface ZLinkSessionPacketHandler<TSessionContext> {
  readonly packetName: string;

  handle(
    context: TSessionContext,
    header: ZlinkStreamHeader,
    payload: ZLinkMessage,
    signal?: AbortSignal,
  ): Promise<void>;
}

export interface ZLinkSessionPacketDispatcher<TSessionContext> {
  /**
   * 등록된 packet handler 가 있는 packet 만 dispatch 한다.
   * handler 가 처리하면 true, 없으면 false 를 반환해 session 이 relay/reject/
   * ignore/log 중 무엇을 할지 정하게 한다.
   */
  tryHandle(
    context: TSessionContext,
    header: ZlinkStreamHeader,
    payload: ZLinkMessage,
    signal?: AbortSignal,
  ): Promise<boolean>;
}
```

dispatcher 는 등록된 handler 의 `packetName` 과 일치하는 packet 만 처리하고,
처리한 경우 `true` 를 반환한다. 일치하는 handler 가 없으면 `false` 를 반환하며,
이 뒤에 actor 로 relay 할지, 오류로 거절할지, 로그만 남길지는 session 구현체가
정한다. framework 는 이 단계에서 자동 relay 나 자동 무시 정책을 적용하지 않는다.

handler 가 받는 `payload` 도 `onDispatch(...)` 와 같은 framework `ZLinkMessage` 다.
handler 구현은 DI 를 사용할 수 있으며, framework 등록 과정은 session 이 주입받는 dispatcher 의
context 타입에 맞는 handler 구현을 provider 로 자동 등록한다.

여기서 기대하는 동작은 다음과 같다.

- session callback 은 stream 객체를 직접 인자로 받지 않는다.
- session 정보, stream send, actor dispatch 는 `context` 를 통해 호출한다.
- 다른 channel 로 보내는 send/request 는 session context 표면이 아니라 DI 로
  주입받은 `ZLinkChannelClient` 를 사용한다. 이 호출은 stream socket 이 아니라
  해당 channel 의 client socket 을 사용하기 때문이다.
- session disconnect 를 actor 에 알려야 할 때는 application 이 대상 actor 를 고른
  뒤 `ZLinkSessionActor.notifyDisconnected(...)` 를 호출한다.
- `context.close(...)` 는 현재 stream client 의 연결을 서버 쪽에서 끊는다.
  `ZLinkStream.close(...)` 도 같은 동작을 raw stream 표면에서 노출한다.
- header session 은 C API 가 잘라 준 stream frame 을 framework 가 header 와
  payload 로 나누어 받은 뒤 처리한다.
- application 은 packet name 을 보고 각 packet 타입으로 decode 한다.
- session packet dispatcher 는 등록된 packet handler 호출만 돕고, 미등록 packet
  처리 정책은 application 에 남긴다.
- decode 과정은 `ZLinkMessage` 가 등록된 codec registry를 통해 처리한다. application
  은 binding `Message` view를 직접 만들지 않는다.
- `ZLinkStream` 의 `sessionId`, `routingId`, `localAddr`, `remoteAddr` 로 peer
  와 연결 metadata 를 읽는다.
- session 은 framework 의 dispatch 경로 위에서 동작한다. 따라서 application 은
  직접 recv loop 를 만들지 않는다.
- session callback 은 native 나 socket callback 안에서 직접 호출되지 않는다.
  framework 가 callback 을 managed task(microtask/queue) 로 넘긴 뒤,
  `onConnected(...)`, `onDispatch(...)`, `onError(...)`, `onDisconnected(...)` 를
  호출한다.
- 같은 session 의 callback 은 직렬로 실행된다. 즉 같은 연결에서 두 packet
  dispatch 나 lifecycle callback 이 서로 겹쳐 실행되지 않는다.
- stream socket 은 같은 session 의 frame 도착 순서를 보존한다. framework 는 그
  frame 을 session 별 직렬 실행 경로에 넣어 callback 순서를 유지한다. 따라서
  session 에는 actor 와 별개로 application 이 관리해야 하는 mailbox[^mailbox]
  를 두지 않는다.
- 서로 다른 session 의 callback 은 독립적으로 진행될 수 있다. 즉 framework 가
  보장하는 순서는 session 단위 순서다.
- `onConnected(...)` 와 `onDisconnected(...)` 는 monitor 의 connection 수명
  이벤트에 대응하는 session callback 으로 본다.
- `onError(...)` 는 monitor 에서 관찰 가능한, session 에 귀속되는 transport
  오류만 받는다.
- `onError(...)` 가 받는 `ZLinkStreamError` 는 framework error category enum 을
  먼저 준다. 필요할 때만 optional diagnostic detail 로 native errno 와 메시지를
  함께 들고 있는 편이 자연스럽다.
- server-to-client 압축은 `ZLinkSessionSendCall.compress()` 또는
  `ZLinkSessionReplyCall.compress()` builder 호출로 활성화한다.

## 4. 등록 모델 기준

이 절은 STREAM node 를 framework 에 어떻게 등록하는지를 정리한다.

dotnet 의 `AddStreamNode(name)` fluent builder 는 NestJS 의 `zlinkFramework()`
builder 로 옮긴다. dotnet builder 메서드 한 개 = node builder 메서드 한 개로
1:1 대응시킨다. dotnet `IZLinkStreamNodeBuilder` 코드
(`Contracts/Configuration/Builders.cs`, `Runtime/.../ZLinkStreamNodeBuilder.cs`)
는 `Bind(endpoint)`, `AttachActorGateway(spotNodeName)`,
`RegisterSession<TSession>()` 세 메서드를 노출하므로, node builder 도 이 셋을
메서드로 매핑한다.

```ts
// node (NestJS)
@Module({
  imports: [
    ZLinkModule.forRoot(
      zlinkFramework()
        .addSpotNode('game.spot')
          .enableRouter('tcp://0.0.0.0:9110')
        .addStreamNode('client.stream')
          .bind('tcp://0.0.0.0:9100')
          .attachActorGateway('game.spot')
          .registerSession(ClientHeaderSession)
        .build()
    ),
  ],
  providers: [ClientHeaderSession],
})
export class AppModule {}
```

`session` 으로 등록하는 클래스는 `ZLinkSession` 을 구현하거나
`ZLinkSessionFactory` 로 session 을 만들어야 한다. session 이 연결별 context 만
필요하면 session class 자체를 등록한다. session 이 repository 나 domain service 같은
NestJS provider 를 주입받아야 하면 factory provider 를 등록하고, factory 의
`create(context)` 가 연결별 session 을 만든다. 두 경우 모두 application 은 stream
socket 이나 frame codec 을 직접 열지 않는다.

NestJS `providers` 에는 application 이 직접 소유하는 handler, domain service,
factory 만 둔다. framework runtime 이 소유하는 socket accept, frame decode,
reply frame 작성, session token alias 는 application provider 목록에 넣지 않는다.
actor 를 쓰는 stream server 는 fluent builder 의 `actorFactory(...)` 와
`addSpotNode(...)` 로 actor factory 와 SpotNode 를 선언할 수 있다. 이 선언은
같은 공개 계약을 만들지만, server entrypoint 에서 raw options 객체를 직접 조립하지
않아도 되게 한다.

이 등록 모델에서 짚어 둘 점은 다음과 같다.

- framework Header 기반 packet session 만 붙인다.
- header binary 형식은 framework 와 connector 가 공유하는 내부 프로토콜로 고정된다.
  application 은 이 형식을 바꾸는 설정을 갖지 않는다.
- 한 `stream node` 에는 stream session 을 하나만 둔다(`session` 키는 단수).
- 같은 node 에 stream session 을 둘 이상 함께 두지 않는다. dotnet
  `RegisterSession<T>()` 가 두 번째 등록에서 startup validation 예외를 던지듯,
  node 도 같은 node 에 session 을 중복 지정하면 startup validation 에서 거부한다.
- recv callback 이나 recv loop 를 application 이 직접 노출받지 않는다.
- server application 은 `net.createServer(...)`, `ZlinkStreamFrameCodec`,
  `ZlinkStreamHeaderCodec` 으로 STREAM server 를 만들지 않는다. 이런 코드는
  framework runtime 또는 connector 구현에만 둔다.
- 등록 시점에 이 node 가 framework Header 기반 packet 경로라는 사실이 분명하게
  드러난다.
- `attachActorGateway` 는 session→actor bind/relay 가 향할 SpotNode 이름을
  연결한다. 참조 대상 SpotNode 는 router 역할을 켜야 한다. actor 로 relay
  하지 않는 순수 stream node 는 이 키를 생략한다.

## 5. serializer 계층

이 절은 codec 을 framework 본체에 섞지 않고 분리해서 두는 이유와 모양을
정리한다.

`playhouse/extensions` 를 보면 protobuf / json / messagepack 지원을 transport
본체에 직접 섞지 않는다. 대신 별도의 codec extension / helper 계층으로 얹는다.
`STREAM` 도 같은 감각이 자연스럽다.

framework 의 기본 표면은 다음 정도까지만 유지한다.

- `ZLinkSession`
- `ZLinkSessionContext`
- `ZLinkStream`
- `ZLinkMessage`

객체 변환은 binding core 의 `Message` 자체가 아니라, framework `ZLinkMessage` 와
codec extension / serializer provider 가 맡는다. 패키지 분리는
[표면 매핑 §2](../internals/dotnet-to-node-surface-mapping.ko.md) 의
`@zlink-systems/stream-connector-{json,msgpack,protobuf}` 구성을 따른다.

예를 들면 다음과 같이 쓴다.

```ts
const input = payload.decode<ClientInput>();
const request = payload.decode<ChatRequest>();
```

이 구조의 장점은 다음과 같다.

- protobuf / json / messagepack 의존성을 transport core 에 고정하지 않아도
  된다.
- serializer 를 별도 패키지로 분리하기 쉽다.
- application 코드는 `payload.decode<T>()` 를 사용하므로 binding buffer 구조를
  직접 알 필요가 없다.
- `playhouse/extensions` 와 비슷한 사용 경험을 만들 수 있다.

## 6. recv 방식은 왜 기본에서 빼는가

이 절은 recv loop 를 application 표면으로 끌어올리지 않은 이유를 정리한다.

recv 방식은 low-level binding 에서는 충분히 의미가 있다. 하지만 framework 표면
까지 그대로 끌어올리면 다음과 같은 문제가 생긴다.

- framework 가 dispatch, DI[^di], filter, logging 을 일관되게 묶기 어려워진다.
- application 이 직접 loop 와 cancellation, backpressure[^backpressure] 를
  떠안게 된다.
- framework Header 기반 packet dispatch 를 일관된 모델로 설명하기 어려워진다.

따라서 현재 스펙은 recv 기반 사용을 금지하자는 것이 아니다. **framework 의 기본
application 표면으로는 올리지 않는다** 는 뜻으로 본다.

## 7. 결정된 기준

이 절은 STREAM 표면이 따르는 고정된 결정 사항을 모아둔 것이다.

- stream session 등록은 decorator 기반으로 열지 않는다.
  `streams[name].session = T`(dotnet `AddStreamNode(...).RegisterSession<T>()`)
  같은 명시 등록만 기본 표면으로 둔다.
- packet decode 와 encode 는 framework `ZLinkMessage` 가 등록된 serializer provider를
  통해 수행한다. framework core 는 session contract 와 codec registry 연결을 책임진다.
- protobuf / json / messagepack serializer 는 확장 패키지로 분리한다. transport
  core 나 framework 기본 runtime 에 codec[^codec] 구현을 직접 섞지 않는다.
- `onError(...)` 는 session 에 귀속되는 transport 오류만 받는다.
  handshake 실패와 socket / node 단위 오류는 runtime monitoring 에서 다룬다.
  즉 session callback 에 올리지 않는다(`HandshakeFailed` 는 enum 에 존재하지만
  `onError` 로 전달되지 않는다).
- raw chunk 직접 처리 표면은 현재 공개 계약에 넣지 않는다. 지금 단계의 session
  은 framework 가 decode 한 `ZlinkStreamHeader` 와 `ZLinkMessage` payload 를 받는
  계약으로 둔다. 초기 드래프트의 `onRaw`/`writePacket` 은 채택하지 않으며,
  raw 표면은 `ZLinkStream.write(...)` 한 개로만 노출한다.
- `ZLinkStream.write(...)` 는 즉시 송신 시도 후 boolean 을 반환한다(dotnet
  `bool Write(...)` 와 동일). backpressure 는 public non-blocking 옵션이 아니라
  framework 내부의 pending queue 와 ready notification 으로 처리한다.

## 8. 회귀 테스트

이 절은 STREAM 표면이 어떤 테스트로 회귀를 막는지를 정리한다.

STREAM 문서의 항목이 확인해야 하는 것은 다음이다.

- session lifecycle 과 packet dispatch 가 transport callback 을 직접 실행하지
  않고, managed queue 를 거치는지
- metadata 와 error 의미가 stream session 단위로 고정되어 있는지

회귀 테스트 매핑은
[regression-test-matrix.ko.md](../internals/regression-test-matrix.ko.md) 와
정렬한다. dotnet 의 회귀 케이스를 node 표면으로 옮기면 다음과 같다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `nodesAndServices.throwsWhenStreamNodeRegistersMultipleSessions` | 같은 node에 session을 중복 등록하면 startup validation 예외가 발생한다. |
| `ZLinkModule.forRoot maps stream node options into runtime registration` | Node builder 표면에서도 같은 stream node 에 session 을 두 번 등록하면 startup validation 예외가 발생한다. |
| `protocol.streamSessionRuntimeOnlyExposesEnqueueCallbackEntrypoints` | transport 진입점은 public enqueue API만 노출한다. |
| `stream session node runtime does not invoke user callbacks inside transport callback` | transport callback 은 user `onDispatch(...)` 를 같은 호출 스택에서 직접 실행하지 않고 managed queue 로 넘긴다. |
| `headerStreamSession.receivesRepliesAndTracksLifecycle` | connected, dispatch, reply, metadata, disconnected/error callback이 기대한 순서대로 실행된다. |
| `headerStreamSession.canCloseCurrentClientStream` | session context가 현재 client stream을 서버 쪽에서 닫을 수 있다. |
| `stream session and bound session require packetName for structural payloads` | 구조적 payload 는 stream session send 와 bound session send 양쪽에서 명시 packet name 없이 전송되지 않는다. |

[^public-contract]: public contract는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 가리킨다.
[^framing]: framing은 연속된 바이트 스트림에서 메시지의 시작과 끝을 구분하는 방식을 가리킨다. STREAM에서는 header와 body를 묶어 하나의 packet 단위로 자른다.
[^session-lifecycle]: session lifecycle은 연결이 맺어지고, 메시지를 주고받다가, 끊기기까지의 전체 단계를 가리킨다. STREAM에서는 connect, dispatch, error, disconnect callback 축으로 표현된다.
[^raw-chunk]: raw chunk는 framework가 잘라 주지 않은 바이트 조각이다. application이 직접 framing 규칙을 풀어야 한다.
[^mvp]: MVP(Minimum Viable Product)는 핵심 기능만 갖춘 최초 출시 범위를 가리킨다. 부가 기능은 이후 단계로 미룬다.
[^mailbox]: mailbox는 액터 모델에서 메시지를 순서대로 쌓아 두는 큐를 가리킨다. actor는 자신의 mailbox에서 메시지를 하나씩 꺼내 처리한다.
[^di]: DI(Dependency Injection)는 객체가 필요한 의존 컴포넌트를 직접 생성하지 않고 컨테이너로부터 주입받는 방식이다. `NestJS`에서는 module + provider 기반으로 처리한다.
[^backpressure]: backpressure는 송신 측이 수신 측의 처리 속도를 넘어 메시지를 밀어 넣지 못하도록 흐름을 조절하는 메커니즘이다.
[^codec]: codec은 객체와 바이트 표현 사이의 직렬화/역직렬화를 담당하는 컴포넌트다. 예: Protobuf, MessagePack, JSON.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework NestJS SPOT](nestjs-spot.ko.md) | [다음: ZLink Framework NestJS Actor](nestjs-actor.ko.md)
<!-- framework-adapter-nav:bottom:end -->
