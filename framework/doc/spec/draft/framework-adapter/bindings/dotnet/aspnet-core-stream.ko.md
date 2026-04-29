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
연결 수명, peer 식별, packet framing, raw payload 처리 같은 요소가 더 중요할 수
있다. 이 문서의 목표는 `.NET` framework 표면에서 `STREAM`을 아래 두 session
방식으로 정리하는 것이다.

- packet session
- raw session

현재 초안에서는 application이 직접 `recv` loop를 돌리는 방식은 지원 대상으로 보지
않는다. framework가 수신 dispatch를 맡고, 사용자는 handler를 구현하는 쪽을
기본으로 본다.

## 2. 기본 방향

`STREAM`은 일반 channel messaging handler와 같은 감각으로 억지로 맞추지 않는다.
특히 아래 원칙을 둔다.

- raw payload chunk를 직접 다루고 재조립까지 application이 맡고 싶으면 raw session을 쓴다.
- C API가 잘라 준 `header/body` packet 단위를 처리하고 싶으면 packet session을 쓴다.
- `playhouse`처럼 header는 고정 메타데이터이고, body는 `header.MsgId`를 보고 각 packet 타입으로 decode하는 방식을 자연스러운 기본 모델로 본다.
- 이 decode helper는 `playhouse/extensions`처럼 transport 본체에 넣기보다,
  `Message` 위에 얹는 serializer extension 계층으로 두는 쪽을 기본으로 본다.
- recv loop는 application 표면에 직접 올리지 않는다.
- `OnConnectedAsync(...)`, `OnDisconnectedAsync(...)`는 session lifecycle 기본
  표면으로 올린다.
- `OnErrorAsync(...)`는 application 예외가 아니라, monitor에서 관찰 가능한
  transport 오류를 session 단위로 다시 올리는 축으로 제한한다.

즉 현재 방향은 "packet session + raw session" 두 축 위에 session lifecycle을 같이
올리는 쪽이다.

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

    bool Write(
        Message payload,
        SendFlags flags = SendFlags.None);

    bool Write(
        Message header,
        Message body,
        SendFlags flags = SendFlags.None);
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

public interface IZLinkSession
{
    IZLinkSessionContext Context { get; set; }

    ValueTask OnConnectedAsync(CancellationToken cancellationToken);

    ValueTask OnDisconnectedAsync(CancellationToken cancellationToken);

    ValueTask OnErrorAsync(
        ZLinkStreamError error,
        CancellationToken cancellationToken);

    ValueTask OnDispatchAsync(
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken);
}

public interface IZLinkSessionContext
{
    string SessionId { get; }
    RoutingId? RoutingId { get; }
    string? LocalAddr { get; }
    string? RemoteAddr { get; }

    IZLinkRequestCall RequestChannel<TRequest>(string channelName, TRequest request);
    IZLinkSendCall SendChannel<TMessage>(string channelName, TMessage message);

    IZLinkSessionSendCall Send<TMessage>(TMessage message);
    IZLinkSessionReplyCall Reply<TMessage>(TMessage message);

    ValueTask CloseAsync(
        CancellationToken cancellationToken = default);

    ValueTask AttachActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default);

    ValueTask DispatchToActorAsync(
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken = default);

    ValueTask DisconnectActorAsync(CancellationToken cancellationToken = default);
}
```

여기서 기대하는 점은 아래와 같다.

- session callback은 stream 객체를 직접 인자로 받지 않는다.
- session 정보, channel request, stream send, actor stream 연결/dispatch/disconnect는
  `Context`를 통해 호출한다.
- `CloseAsync(...)`는 현재 stream client 연결을 서버 쪽에서 끊는다.
- header session은 C API가 이미 header/body로 나눈 framed packet을 받는다.
- body는 보통 고정 타입 하나로 바로 올리지 않고, header 안의 `msgId` 같은 값을 보고
  각 packet 타입으로 decode한다.
- 이 decode 과정은 가능하면 `Message.AsReadOnlySpan()`이나 그 위에 얹는 helper를
  사용해서 추가 복사를 피하는 쪽을 기본으로 본다.
- 두 방식 모두 `IZLinkStream`의 `SessionId`, `RoutingId`, `LocalAddr`,
  `RemoteAddr`로 peer와 connection metadata를 읽는다.
- 둘 다 framework dispatch 경로 위에 올라가고, application은 직접 recv loop를
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

- packet session, zlink stream header session, raw session을 분리해서 붙인다.
- 한 `stream node`에는 세 session 종류 중 한 종류만 둔다.
- 같은 node에 stream session을 둘 이상 함께 두지 않는다.
- recv callback이나 recv loop를 application이 직접 노출받지 않는다.
- 어떤 session이 packet path인지 raw path인지 등록 시점에 분명히 보인다.

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
ClientInput input = body.Parse<ClientInput>();
ChatRequest request = body.Parse<ChatRequest>();
```

이 구조의 장점은 아래와 같다.

- protobuf/json/messagepack dependency를 transport core에 고정하지 않는다.
- serializer를 추가 패키지로 분리하기 쉽다.
- `Message.AsReadOnlySpan()` 기반 helper를 써서 불필요한 복사를 줄이기 쉽다.
- `playhouse/extensions`와 비슷한 사용 경험을 만들 수 있다.

## 6. recv 방식은 왜 기본에서 빼는가

recv 방식은 low-level binding에서는 유효할 수 있다.
하지만 framework 표면까지 그대로 올리면 아래 문제가 생긴다.

- framework가 dispatch, DI, filter, logging을 일관되게 묶기 어렵다.
- application이 직접 loop와 cancellation, backpressure를 떠안게 된다.
- packet session과 raw session을 함께 설명하기가 어려워진다.

따라서 현재 초안은 recv 기반 사용을 막자는 뜻이 아니라, **framework의 기본
application 표면으로는 올리지 않는다**는 뜻이다.

## 7. 결정된 기준

- stream session 등록은 attribute 기반으로 열지 않는다.
  `AddStreamNode(...).AddHeaderSession<T>()` 같은 명시 등록만 기본 표면으로 둔다.
- body decode helper와 encode helper는 framework 본체가 아니라 serializer 확장
  패키지가 맡는다.
  framework core는 `Message`, `AsReadOnlySpan()`, session contract까지만 책임진다.
- protobuf/json/messagepack serializer는 확장 패키지로 분리한다.
  transport core나 framework 기본 runtime에 codec 구현을 직접 섞지 않는다.
- `OnErrorAsync(...)`는 session-correlatable transport 오류만 받는다.
  handshake 실패와 socket/node 단위 오류는 runtime monitoring에서 다루고, session
  callback에는 올리지 않는다.
- raw chunk 직접 처리 표면은 현재 공개 계약에 넣지 않는다. 현 단계의 session은
  framework가 decode한 `ZlinkStreamHeader`와 `Message body`를 받는 계약으로 둔다.
