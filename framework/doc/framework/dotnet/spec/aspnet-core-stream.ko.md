<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: Stage Wrapper On SPOT](stage-wrapper-on-spot.ko.md) | [다음: ZLink Framework ASP.NET Core Actor](aspnet-core-actor.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../common/README.ko.md)

[.NET 묶음](../README.ko.md) | [인터페이스](handler-interfaces.ko.md) | [STREAM 샘플](../guide/samples/stream-samples.ko.md) | [channel](aspnet-core-channel-messaging.ko.md) | [SPOT](aspnet-core-spot.ko.md)

# ZLink Framework ASP.NET Core STREAM Integration

## 1. 목표

이 절은 STREAM 표면이 어떤 모양을 따라야 하는지를 정리한다.

`STREAM` 은 일반 request-response 와 성격이 다르다. 다음 요소가 훨씬 더 중요한
축이 된다.

- 연결 수명
- peer 식별
- packet framing[^framing]
- session lifecycle[^session-lifecycle]

이 문서의 목표는 `.NET` framework 표면에서 `STREAM` 을 framework Header 기반의
packet session 방식으로 정리하는 것이다.

현재 스펙에서는 application 이 직접 `recv` loop 를 돌리는 방식은 지원 대상으로
잡지 않는다. framework 가 수신 dispatch 를 맡고, 사용자는 handler 만 구현하는
쪽을 기본으로 둔다.

## 2. 기본 방향

이 절은 STREAM 표면이 따르는 설계 원칙을 정리한다.

`STREAM` 은 일반 channel messaging handler 와 같은 감각으로 무리하게 맞추지
않는다. 특히 다음 원칙을 둔다.

- framework 가 stream header 를 decode 한 뒤 `ZlinkStreamHeader header` 와
  `ZLinkMessage payload` 를 session callback 에 전달한다.
- `playhouse` 처럼 header 는 framework 내부에서 packet name 과 metadata 로
  해석한다. application 은 `header.Name` 을 보고 각 packet 타입으로
  decode 하는 모델을 자연스러운 기본으로 본다.
- payload decode는 transport 본체에 섞지 않는다. 대신 framework runtime이 등록된
  codec registry로 `ZLinkMessage`를 만들고, application은 필요한 packet만
  `Decode<T>()`로 읽는다.
- recv loop 는 application 표면에 직접 올리지 않는다.
- `OnConnectedAsync(...)`, `OnDisconnectedAsync(...)` 는 session lifecycle 의
  기본 표면으로 올린다.
- `OnErrorAsync(...)` 는 application 예외가 아니라, monitor 에서 관찰 가능한
  transport 오류를 session 단위로 다시 올려주는 축으로만 제한한다.

요컨대 현재 방향은 framework Header 기반 packet session 위에 session lifecycle
을 함께 올리는 쪽이다. raw chunk[^raw-chunk] 직접 처리와 사용자 정의 Header
framing 은 MVP[^mvp] 범위에 넣지 않는다.

## 3. 인터페이스 초안

이 절은 STREAM 표면이 노출하는 핵심 타입을 정리한다.

인터페이스 전체 기준은 [handler-interfaces.ko.md](handler-interfaces.ko.md)
를 참고한다. `STREAM` 쪽 핵심 초안은 다음과 같다.

```csharp
public interface IZLinkStream
{
    string SessionId { get; }

    RoutingId? RoutingId { get; }

    string? LocalAddr { get; }

    string? RemoteAddr { get; }

    bool Write(
        Message payload,
        SendFlags flags = SendFlags.None);

    ValueTask CloseAsync();
}

public enum ZLinkStreamSessionError
{
    Internal = 0,
    TransportError,
    // OnErrorAsync로 전달되지 않는다. handshake 실패는 runtime monitoring에만 남긴다.
    // stream-open-items.ko.md section 4.2 참고.
    HandshakeFailed
}

public readonly record struct ZLinkStreamError(
    ZLinkStreamSessionError Error,
    ZLinkStreamDiagnostic? Diagnostic);

public readonly record struct ZLinkStreamDiagnostic(
    int NativeCode,
    string? Message);

public sealed class ZLinkMessageMetadata
{
    public static ZLinkMessageMetadata Empty { get; }

    public IReadOnlyDictionary<string, string> Values { get; }

    public string? Find(string key);
}

public interface IZLinkSession
{
    IZLinkSessionContext Context { get; }

    ValueTask OnConnectedAsync(CancellationToken cancellationToken);

    ValueTask OnDisconnectedAsync(CancellationToken cancellationToken);

    ValueTask OnErrorAsync(
        ZLinkStreamError error,
        CancellationToken cancellationToken);

    ValueTask OnDispatchAsync(
        ZlinkStreamHeader header,
        ZLinkMessage payload,
        CancellationToken cancellationToken);
}

public interface IZLinkSessionClient
{
    IZLinkSessionSendCall Send<TMessage>(TMessage message);

    IZLinkSessionReplyCall Reply<TMessage>(TMessage message);
}

public interface IZLinkSessionActors
{
    IReadOnlyCollection<IZLinkSessionActor> Bound { get; }

    ValueTask<IZLinkSessionActor> BindAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkSessionActor> BindAsync(
        ActorRef actor,
        CancellationToken cancellationToken = default);

    IZLinkSessionActor? Find(string actorId);
}

public interface IZLinkActorManager
{
    ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkActor?> FindAsync(
        string actorId,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkActor> GetOrCreateAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);
}

public interface IZLinkSessionActor
{
    string ActorId => Ref.ActorId;

    ActorRef Ref { get; }

    ValueTask RelayRawAsync(
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default);

    ValueTask RelayAsync(
        ZlinkStreamHeader header,
        ZLinkMessage payload,
        CancellationToken cancellationToken = default);

    ValueTask NotifyDisconnectedAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkSessionSendCall
{
    IZLinkSessionSendCall Metadata(string key, string value);
    IZLinkSessionSendCall PacketName(string messageName);
    IZLinkSessionSendCall Compress();
    ValueTask Async();
}

public interface IZLinkSessionReplyCall
{
    IZLinkSessionReplyCall Metadata(string key, string value);
    IZLinkSessionReplyCall Compress();
    ValueTask Async();
}

public interface IZLinkSessionContext
{
    string SessionId { get; }

    RoutingId? RoutingId { get; }

    string? LocalAddr { get; }

    string? RemoteAddr { get; }

    IZLinkSessionClient Client { get; }

    IZLinkSessionActors Actors { get; }

    ValueTask CloseAsync();
}
```

`Context` 는 framework 가 session 을 생성할 때 생성자 인자로 제공한다.
session 구현체는 이 값을 get-only property 로 그대로 노출해야 하며, runtime 은
생성 직후 같은 context 인스턴스인지 검증한다.

low-level zlink binding 을 직접 사용할 때는 recv 또는 callback 으로 받은
`Message` 를 application 이 해제한다. framework 에서는 이 binding 표면을
application 에 직접 노출하지 않는다. framework runtime 이 binding 의 수신자가
되며, 수신한 `Message` 의 해제 책임도 framework runtime 이 가진다.

`OnDispatchAsync(...)` 로 전달된 `payload` 는 framework `ZLinkMessage` 다. session 은
`payload.Decode<T>()` 로 DTO를 읽거나 `IZLinkSessionActor.RelayAsync(...)` 같은 framework
API 에 그대로 전달한다.

반대로 application 이 직접 만든 `Message` 를 `IZLinkStream.Write(...)` 같은
raw write API 에 넘길 때는 framework 가 그 `Message` 의 소유권을 가져가지
않는다. write 호출자는 자신이 만든 `Message` 의 수명을 계속 책임진다. 보통의
session 응답은 `Context.Client.Send(...)`, `Context.Client.Reply(...)` 같은 typed builder 를
사용하므로 raw `Message` 수명을 직접 다룰 일이 적다.

session 이 일부 packet 만 직접 처리하고 나머지 정책을 스스로 정하고 싶을 때는
`IZLinkSessionPacketHandler<TSessionContext>` 와
`IZLinkSessionPacketDispatcher<TSessionContext>` 를 사용할 수 있다. dispatcher 는
등록된 handler 의 `PacketName` 과 일치하는 packet 만 처리하고, 처리한 경우
`true` 를 반환한다. 일치하는 handler 가 없으면 `false` 를 반환하며, 이 뒤에
actor 로 relay 할지, 오류로 거절할지, 로그만 남길지는 session 구현체가 정한다.
framework 는 이 단계에서 자동 relay 나 자동 무시 정책을 적용하지 않는다.

handler 가 받는 `payload` 도 `OnDispatchAsync(...)` 와 같은 framework `ZLinkMessage` 다.
handler 구현은 DI 를 사용할 수 있으며, framework 등록 과정은 session 이 주입받는 dispatcher 의
context 타입에 맞는 handler 구현을 service 로 자동 등록한다.

여기서 기대하는 동작은 다음과 같다.

- session callback 은 stream 객체를 직접 인자로 받지 않는다.
- session 정보, stream send, actor dispatch 는 `Context` 를 통해 호출한다.
- 다른 channel 로 보내는 send/request 는 session context 표면이 아니라 DI 로
  주입받은 `IZLinkChannelClient` 를 사용한다. 이 호출은 stream socket 이 아니라
  해당 channel 의 client socket 을 사용하기 때문이다.
- session disconnect 를 actor 에 알려야 할 때는 application 이 대상 actor 를 고른
  뒤 `IZLinkSessionActor.NotifyDisconnectedAsync(...)` 를 호출한다.
- `CloseAsync()` 는 현재 stream client 의 연결을 서버 쪽에서 끊는다.
- header session 은 C API 가 잘라 준 stream frame 을 framework 가 header 와
  payload 로 나누어 받은 뒤 처리한다.
- application 은 packet name 을 보고 각 packet 타입으로 decode 한다.
- session packet dispatcher 는 등록된 packet handler 호출만 돕고, 미등록 packet
  처리 정책은 application 에 남긴다.
- 이 decode 과정은 `ZLinkMessage.Decode<T>()` 를 사용한다. codec 선택은
  handler 가 아니라 startup/options 에 등록된 codec registry 에서 결정된다.
- `IZLinkStream` 의 `SessionId`, `RoutingId`, `LocalAddr`, `RemoteAddr` 로 peer
  와 연결 metadata 를 읽는다.
- session 은 framework 의 dispatch 경로 위에서 동작한다. 따라서 application 은
  직접 recv loop 를 만들지 않는다.
- session callback 은 native 나 socket callback 안에서 직접 호출되지 않는다.
  framework 가 callback 을 managed task 로 넘긴 뒤, `OnConnectedAsync(...)`,
  `OnDispatchAsync(...)`, `OnErrorAsync(...)`, `OnDisconnectedAsync(...)` 를
  호출한다.
- 같은 session 의 callback 은 직렬로 실행된다. 즉 같은 연결에서 두 packet
  dispatch 나 lifecycle callback 이 서로 겹쳐 실행되지 않는다.
- stream socket 은 같은 session 의 frame 도착 순서를 보존한다. framework 는 그
  frame 을 session 별 직렬 실행 경로에 넣어 callback 순서를 유지한다. 따라서
  session 에는 actor 와 별개로 application 이 관리해야 하는 mailbox[^mailbox]
  를 두지 않는다.
- 서로 다른 session 의 callback 은 독립적으로 진행될 수 있다. 즉 framework 가
  보장하는 순서는 session 단위 순서다.
- `OnConnectedAsync(...)` 와 `OnDisconnectedAsync(...)` 는 monitor 의 connection
  수명 이벤트에 대응하는 session callback 으로 본다.
- `OnErrorAsync(...)` 는 monitor 에서 관찰 가능한, session 에 귀속되는 transport
  오류만 받는다.
- `OnErrorAsync(...)` 가 받는 `ZLinkStreamError` 는 framework error category
  enum 을 먼저 준다. 필요할 때만 optional diagnostic detail 로 native errno 와
  메시지를 함께 들고 있는 편이 자연스럽다.
- server-to-client 압축은 `IZLinkSessionSendCall.Compress()` 또는
  `IZLinkSessionReplyCall.Compress()` builder 호출로 활성화한다.

## 4. 등록 모델 초안

이 절은 STREAM node 를 framework 에 어떻게 등록하는지를 정리한다.

현재 `.NET` 쪽에서는 `STREAM` 도 명시적으로 등록하는 편이 더 자연스럽다. 예를
들면 다음과 같은 등록 표면을 생각할 수 있다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    {
        var stream =     options.AddStreamNode("client.stream");
        stream.Bind("tcp://0.0.0.0:9100");
        stream.RegisterSession<ClientHeaderSession>();

    }
});
```

이 등록 모델에서 짚어 둘 점은 다음과 같다.

- framework Header 기반 packet session 만 붙인다.
- header binary 형식은 framework 와 connector 가 공유하는 내부 프로토콜로 고정된다.
  application 은 이 형식을 바꾸는 설정을 갖지 않는다.
- 한 `stream node` 에는 stream session 을 하나만 둔다.
- 같은 node 에 stream session 을 둘 이상 함께 두지 않는다.
- recv callback 이나 recv loop 를 application 이 직접 노출받지 않는다.
- 등록 시점에 이 node 가 framework Header 기반 packet 경로라는 사실이 분명하게
  드러난다.

## 5. serializer 계층

이 절은 codec 을 framework 본체에 섞지 않고 분리해서 두는 이유와 모양을
정리한다.

`playhouse/extensions` 를 보면 protobuf / json / messagepack 지원을 transport
본체에 직접 섞지 않는다. 대신 별도의 codec extension / helper 계층으로 얹는다.
`STREAM` 도 같은 감각이 자연스럽다.

framework 의 기본 표면은 다음 정도까지만 유지한다.

- `IZLinkSession`
- `IZLinkSessionContext`
- `IZLinkStream`
- `Message`

객체 변환은 binding core 의 `Message` 자체가 아니라, 그 위에 얹는 별도
확장 패키지나 serializer provider 가 맡는다.

예를 들면 다음과 같이 쓴다.

```csharp
ClientInput input = payload.Decode<ClientInput>();
ChatRequest request = payload.Decode<ChatRequest>();
```

이 구조의 장점은 다음과 같다.

- protobuf / json / messagepack 의존성을 transport core 에 고정하지 않아도
  된다.
- serializer 를 별도 패키지로 분리하기 쉽다.
- handler 가 codec별 helper를 직접 고르지 않아도 `ZLinkMessage.Decode<T>()`
  로 같은 업무 코드를 유지할 수 있다.
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

- stream session 등록은 attribute 기반으로 열지 않는다.
  `AddStreamNode(...).RegisterSession<T>()` 같은 명시 등록만 기본 표면으로
  둔다.
- packet payload 의 업무 표면은 `ZLinkMessage.Decode<T>()` 와 DTO reply 다.
  serializer 구현은 codec[^codec] 확장 패키지나 custom codec 이 맡고,
  framework runtime 은 등록된 codec registry 를 통해 encode/decode 한다.
- JSON, protobuf, messagepack, custom codec 을 바꿔도 session handler 는 codec별
  helper를 직접 호출하지 않는다. transport core 나 framework 기본 runtime 에 특정
  codec 구현을 직접 섞지 않는다.
- `OnErrorAsync(...)` 는 session 에 귀속되는 transport 오류만 받는다.
  handshake 실패와 socket / node 단위 오류는 runtime monitoring 에서 다룬다.
  즉 session callback 에 올리지 않는다.
- raw chunk 직접 처리 표면은 현재 공개 계약에 넣지 않는다. 지금 단계의 session
  은 framework 가 decode 한 `ZlinkStreamHeader` 와 `ZLinkMessage` payload 를 받는
  계약으로 둔다.

## 8. 회귀 테스트

이 절은 STREAM 표면이 어떤 테스트로 회귀를 막는지를 정리한다.

STREAM 문서의 항목이 확인해야 하는 것은 다음이다.

- session lifecycle 과 packet dispatch 가 transport callback 을 직접 실행하지
  않고, managed queue 를 거치는지
- metadata 와 error 의미가 stream session 단위로 고정되어 있는지

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `NodesAndServicesTests.AddZLinkFramework_Throws_WhenStreamNodeRegistersMultipleSessions` | 같은 node에 session을 중복 등록하면 startup validation 예외가 발생한다. |
| `ProtocolTests.StreamSessionRuntime_Only_Exposes_Enqueue_Callback_Entrypoints` | transport 진입점은 public enqueue API만 노출한다. |
| `HeaderStreamSessionTests.HeaderStreamSession_Receives_Replies_And_Tracks_Lifecycle` | connected, dispatch, reply, metadata, disconnected/error callback이 기대한 순서대로 실행된다. |
| `HeaderStreamSessionTests.HeaderStreamSession_Can_Close_Current_Client_Stream` | session context가 현재 client stream을 서버 쪽에서 닫을 수 있다. |

[^public-contract]: public contract는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 가리킨다.
[^framing]: framing은 연속된 바이트 스트림에서 메시지의 시작과 끝을 구분하는 방식을 가리킨다. STREAM에서는 header와 body를 묶어 하나의 packet 단위로 자른다.
[^session-lifecycle]: session lifecycle은 연결이 맺어지고, 메시지를 주고받다가, 끊기기까지의 전체 단계를 가리킨다. STREAM에서는 connect, dispatch, error, disconnect callback 축으로 표현된다.
[^raw-chunk]: raw chunk는 framework가 잘라 주지 않은 바이트 조각이다. application이 직접 framing 규칙을 풀어야 한다.
[^mvp]: MVP(Minimum Viable Product)는 핵심 기능만 갖춘 최초 출시 범위를 가리킨다. 부가 기능은 이후 단계로 미룬다.
[^mailbox]: mailbox는 액터 모델에서 메시지를 순서대로 쌓아 두는 큐를 가리킨다. actor는 자신의 mailbox에서 메시지를 하나씩 꺼내 처리한다.
[^di]: DI(Dependency Injection)는 객체가 필요한 의존 컴포넌트를 직접 생성하지 않고 컨테이너로부터 주입받는 방식이다. `ASP.NET Core`에서는 `IServiceCollection` 기반으로 처리한다.
[^backpressure]: backpressure는 송신 측이 수신 측의 처리 속도를 넘어 메시지를 밀어 넣지 못하도록 흐름을 조절하는 메커니즘이다.
[^codec]: codec은 객체와 바이트 표현 사이의 직렬화/역직렬화를 담당하는 컴포넌트다. 예: Protobuf, MessagePack, JSON.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: Stage Wrapper On SPOT](stage-wrapper-on-spot.ko.md) | [다음: ZLink Framework ASP.NET Core Actor](aspnet-core-actor.ko.md)
<!-- framework-adapter-nav:bottom:end -->
