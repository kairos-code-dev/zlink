<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: Stage Wrapper On SPOT](stage-wrapper-on-spot.ko.md) | [다음: ZLink Framework ASP.NET Core Actor](aspnet-core-actor.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)

[.NET 묶음](./README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [STREAM 샘플](./stream-samples.ko.md) | [STREAM Decisions](./stream-open-items.ko.md) | [channel](./aspnet-core-channel-messaging.ko.md) | [SPOT](./aspnet-core-spot.ko.md)

# Draft -- ZLink Framework ASP.NET Core STREAM Integration

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `ASP.NET Core`에서 `STREAM`을 어떤 handler 모델로
> 올릴지 방향을 정리한다.
>
> serializer, write, lifecycle 결정 기준은
> [stream-open-items.ko.md](./stream-open-items.ko.md)에 함께 정리한다.

## 1. 목표

`STREAM`은 일반 request-response와 성격이 다르다.
연결 수명, peer 식별, packet framing, session lifecycle 같은 요소가 더 중요하다.
이 문서의 목표는 `.NET` framework 표면에서 `STREAM`을 framework Header 기반 packet
session 방식으로 정리하는 것이다.

현재 초안에서는 application이 직접 `recv` loop를 돌리는 방식은 지원 대상으로 보지
않는다. framework가 수신 dispatch를 맡고, 사용자는 handler를 구현하는 쪽을
기본으로 본다.

## 2. 기본 방향

`STREAM`은 일반 channel messaging handler와 같은 감각으로 억지로 맞추지 않는다.
특히 아래 원칙을 둔다.

- framework가 decode한 stream frame을 `IZLinkSessionPacket`으로 감싼 뒤 처리한다.
- `playhouse`처럼 header는 framework 내부에서 packet name과 metadata로 해석하고,
  application은 `packet.PacketName`을 보고 각 packet 타입으로 decode하는 방식을
  자연스러운 기본 모델로 본다.
- 이 decode helper는 `playhouse/extensions`처럼 transport 본체에 넣기보다,
  `Message` 위에 얹는 serializer extension 계층으로 두는 쪽을 기본으로 본다.
- recv loop는 application 표면에 직접 올리지 않는다.
- `OnConnectedAsync(...)`, `OnDisconnectedAsync(...)`는 session lifecycle 기본
  표면으로 올린다.
- `OnErrorAsync(...)`는 application 예외가 아니라, monitor에서 관찰 가능한
  transport 오류를 session 단위로 다시 올리는 축으로 제한한다.

즉 현재 방향은 framework Header 기반 packet session 위에 session lifecycle을 같이
올리는 쪽이다. raw chunk 직접 처리와 사용자 정의 Header framing은 MVP 범위에 넣지
않는다.

## 3. 인터페이스 초안

전체 인터페이스 기준은 [handler-interfaces.ko.md](./handler-interfaces.ko.md)를
참고한다. `STREAM` 쪽 핵심 초안은 아래와 같다.

```csharp
public interface IZLinkStream
{
    string SessionId { get; }

    RoutingId? RoutingId { get; }

    string? LocalAddr { get; }

    string? RemoteAddr { get; }

    ValueTask WriteAsync(
        Message payload,
        CancellationToken cancellationToken = default);

    ValueTask WriteAsync(
        Message header,
        Message body,
        CancellationToken cancellationToken = default);
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
}

public interface IZLinkSessionPacket
{
    string PacketName { get; }

    ZLinkMessageMetadata Metadata { get; }

    TMessage Decode<TMessage>();
}

public interface IZLinkSession
{
    IZLinkSessionContext Context { get; set; }

    ValueTask OnConnectedAsync(CancellationToken cancellationToken);

    ValueTask OnDisconnectedAsync(CancellationToken cancellationToken);

    ValueTask OnErrorAsync(
        ZLinkStreamError error,
        CancellationToken cancellationToken);

    ValueTask OnDispatchAsync(
        IZLinkSessionPacket packet,
        CancellationToken cancellationToken);
}

public interface IZLinkSessionActorDispatchContext
{
    ValueTask<IZLinkActorRef> CreateAndBindActorAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkActorRef> BindActorHandleAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);

    IZLinkSessionRequestCall Request<TRequest>(TRequest request);

    ValueTask DispatchToActorAsync(
        IZLinkSessionPacket packet,
        CancellationToken cancellationToken = default);

    ValueTask DispatchToActorAsync(
        IZLinkActorRef actor,
        IZLinkSessionPacket packet,
        CancellationToken cancellationToken = default);
}

public interface IZLinkSessionActorAttachmentContext
{
    ValueTask AttachActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default);

    ValueTask DisconnectActorAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkSessionSendCall
{
    IZLinkSessionSendCall WithMetadata(string key, string value);
    IZLinkSessionSendCall WithPacketName(string messageName);
    IZLinkSessionSendCall Compress();
    ValueTask Submit(CancellationToken cancellationToken = default);
}

public interface IZLinkSessionReplyCall
{
    IZLinkSessionReplyCall WithMetadata(string key, string value);
    IZLinkSessionReplyCall Compress();
    ValueTask Submit(CancellationToken cancellationToken = default);
}

public interface IZLinkSessionContext :
    IZLinkSessionIdentityContext,
    IZLinkSessionChannelClient,
    IZLinkSessionClientStream,
    IZLinkSessionActorDispatchContext,
    IZLinkSessionLifecycle;
```

여기서 기대하는 점은 아래와 같다.

- session callback은 stream 객체를 직접 인자로 받지 않는다.
- session 정보, channel request, stream send, actor dispatch는 `Context`를 통해
  호출한다.
- actor stream 연결과 해제는 별도 `IZLinkSessionActorAttachmentContext` 표면으로
  분리한다.
- `CloseAsync(...)`는 현재 stream client 연결을 서버 쪽에서 끊는다.
- header session은 C API가 나눈 stream frame을 framework가 `IZLinkSessionPacket`으로
  감싼 뒤 받는다.
- application은 packet name을 보고 각 packet 타입으로 decode한다.
- 이 decode 과정은 가능하면 packet 내부의 `Message.AsReadOnlySpan()` 기반 helper를
  사용해서 추가 복사를 피하는 쪽을 기본으로 본다.
- `IZLinkStream`의 `SessionId`, `RoutingId`, `LocalAddr`, `RemoteAddr`로 peer와
  connection metadata를 읽는다.
- session은 framework dispatch 경로 위에 올라가고, application은 직접 recv loop를
  만들지 않는다.
- session callback은 native/socket callback 안에서 직접 호출하지 않는다.
  framework는 callback을 managed task로 넘긴 뒤 `OnConnectedAsync(...)`,
  `OnDispatchAsync(...)`, `OnErrorAsync(...)`, `OnDisconnectedAsync(...)`를 호출한다.
- 같은 session의 callback은 직렬로 실행된다. 같은 연결에서 두 packet dispatch나
  lifecycle callback이 서로 겹쳐 실행되지 않는다.
- 서로 다른 session의 callback은 독립적으로 진행될 수 있다. framework가 보장하는
  순서는 session 단위 순서다.
- `OnConnectedAsync(...)`와 `OnDisconnectedAsync(...)`는 monitor의 connection 수명
  이벤트에 대응하는 session callback으로 본다.
- `OnErrorAsync(...)`는 monitor에서 관찰 가능한 session-correlatable transport
  오류만 받는다.
- `OnErrorAsync(...)`가 받는 `ZLinkStreamError`는 framework error category enum을
  먼저 주고, 필요할 때만 optional diagnostic detail로 native errno와 메시지를 같이
  들고 있는 편이 자연스럽다.
- server-to-client 압축은 `IZLinkSessionSendCall.Compress()` / `IZLinkSessionReplyCall.Compress()` builder 호출로 활성화한다.

## 4. 등록 모델 초안

현재 `.NET` 쪽에서는 `STREAM`도 명시적 등록이 더 자연스럽다.
예를 들면 아래와 같은 등록 표면을 생각할 수 있다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.AddStreamNode("client.stream", stream =>
    {
        stream.Bind("tcp://0.0.0.0:9100");
        stream.AddHeaderSession<ClientHeaderSession>();
    });
});
```

이 등록 모델에서 중요한 점은 아래와 같다.

- framework Header 기반 packet session만 붙인다.
- 한 `stream node`에는 stream session을 하나만 둔다.
- 같은 node에 stream session을 둘 이상 함께 두지 않는다.
- recv callback이나 recv loop를 application이 직접 노출받지 않는다.
- 등록 시점에 이 node가 framework Header 기반 packet path임이 분명히 보인다.

## 5. serializer 계층

`playhouse/extensions`를 보면 protobuf/json/messagepack 지원을 transport 본체에
직접 섞지 않고, 별도 codec extension/helper 계층으로 얹는다. `STREAM`도 같은
감각이 자연스럽다.

즉 framework 기본 표면은 아래 정도로 유지한다.

- `IZLinkSession`
- `IZLinkSessionContext`
- `IZLinkStream`
- `Message`

그리고 객체 변환은 binding core `Message` 자체가 아니라, 그 위에 얹는 별도
확장 패키지나 serializer provider가 맡는다.

예를 들면:

```csharp
ClientInput input = packet.Decode<ClientInput>();
ChatRequest request = packet.Decode<ChatRequest>();
```

이 구조의 장점은 아래와 같다.

- protobuf/json/messagepack dependency를 transport core에 고정하지 않는다.
- serializer를 추가 패키지로 분리하기 쉽다.
- packet 내부의 `Message.AsReadOnlySpan()` 기반 helper를 써서 불필요한 복사를 줄이기 쉽다.
- `playhouse/extensions`와 비슷한 사용 경험을 만들 수 있다.

## 6. recv 방식은 왜 기본에서 빼는가

recv 방식은 low-level binding에서는 유효할 수 있다.
하지만 framework 표면까지 그대로 올리면 아래 문제가 생긴다.

- framework가 dispatch, DI, filter, logging을 일관되게 묶기 어렵다.
- application이 직접 loop와 cancellation, backpressure를 떠안게 된다.
- framework Header 기반 packet dispatch를 일관되게 설명하기가 어려워진다.

따라서 현재 초안은 recv 기반 사용을 막자는 뜻이 아니라, **framework의 기본
application 표면으로는 올리지 않는다**는 뜻이다.

## 7. 결정된 기준

- stream session 등록은 attribute 기반으로 열지 않는다.
  `AddStreamNode(...).AddHeaderSession<T>()` 같은 명시 등록만 기본 표면으로 둔다.
- packet decode helper와 encode helper는 framework 본체가 아니라 serializer 확장
  패키지가 맡는다.
  framework core는 `Message`, `AsReadOnlySpan()`, session contract까지만 책임진다.
- protobuf/json/messagepack serializer는 확장 패키지로 분리한다.
  transport core나 framework 기본 runtime에 codec 구현을 직접 섞지 않는다.
- `OnErrorAsync(...)`는 session-correlatable transport 오류만 받는다.
  handshake 실패와 socket/node 단위 오류는 runtime monitoring에서 다루고, session
  callback에는 올리지 않는다.
- raw chunk 직접 처리 표면은 현재 공개 계약에 넣지 않는다. 현 단계의 session은
  framework가 decode한 `IZLinkSessionPacket`을 받는 계약으로 둔다.

## 8. 회귀 테스트

STREAM 문서의 항목은 session lifecycle과 packet dispatch가 transport callback을 직접
실행하지 않고 managed queue를 거치는지 확인해야 한다. metadata와 error 의미도 stream
session 단위로 고정한다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `RegistrationValidationTests.AddZLinkFramework_Throws_WhenStreamNodeRegistersMultipleHeaderSessions` | 같은 node에 header session을 중복 등록하면 startup validation 예외가 난다. |
| `StreamIntegrationTests.StreamSessionRuntime_Only_Exposes_Enqueue_Callback_Entrypoints` | transport 진입점은 public enqueue API만 노출한다. |
| `StreamIntegrationTests.HeaderStreamSession_Receives_Replies_And_Tracks_Lifecycle` | connected, dispatch, reply, metadata, disconnected/error callback이 기대 순서로 실행된다. |
| `StreamIntegrationTests.HeaderStreamSession_Can_Close_Current_Client_Stream` | session context가 현재 client stream을 서버 쪽에서 닫을 수 있다. |

