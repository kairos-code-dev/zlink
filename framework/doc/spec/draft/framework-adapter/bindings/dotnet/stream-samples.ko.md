[스펙 목차](../../../README.ko.md)

[.NET 묶음](./README.ko.md) | [STREAM](./aspnet-core-stream.ko.md) | [STREAM Decisions](./stream-open-items.ko.md) | [인터페이스](./handler-interfaces.ko.md)

# Draft -- ZLink Framework .NET STREAM Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET`에서 `STREAM` header session과 header session을
> 실제 코드 흐름으로 보기 위한 샘플 문서다.

## 1. 이 문서의 목적

`STREAM`은 recv loop를 직접 돌리는 low-level 표면과, framework가 dispatch를 맡는
session 표면이 섞이면 읽기 어려워진다. 이 문서는 framework 초안 기준으로 아래 두
가지만 보여 준다.

1. header session
2. header session

recv 방식은 이 샘플 문서에 넣지 않는다.

## 2. 인터페이스 초안

`STREAM` 샘플이 기대하는 최소 인터페이스는 아래 정도다.

> **주의**: 아래 정의는 [handler-interfaces.ko.md](./handler-interfaces.ko.md)의
> 해당 섹션과 동일하다. 인터페이스가 변경되면 두 문서를 함께 갱신해야 한다.
> 최신 계약은 항상 `handler-interfaces.ko.md`를 기준으로 한다.

```csharp
public interface IZLinkStream
{
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

`CloseAsync(...)`는 현재 session의 client stream 연결을 서버 쪽에서 끊을 때
사용한다. 예를 들어 인증 실패나 protocol 위반을 응답한 뒤 더 이상 packet을
받지 않으려면 session handler가 이 함수를 호출한다.

이 초안에서는 stream packet 처리에서 불필요한 메모리 복사를 줄이는 것을 중요하게
본다. 그래서 `Message.ToArray()`를 기본 경로로 두기보다, `Message`가 가진
`AsReadOnlySpan()` 위에서 decode helper가 동작하는 쪽을 더 자연스러운 방향으로
본다.

또한 객체 직렬화 계층은 `playhouse/extensions`처럼 transport 본체와 분리하는
방향을 기본으로 본다. 즉 header session은 `Message`를 받고, protobuf/json 같은
객체 변환은 extension helper가 맡는다.

`OnErrorAsync(...)`가 받는 값도 raw monitor event를 그대로 노출하지 않는다.
샘플 기준으로는 `ZLinkStreamError`가 먼저 높은 수준의 오류 분류를 주고,
필요하면 `Diagnostic`을 통해 native errno와 메시지를 함께 보는 방향을 기본으로
본다.

## 3. header session 샘플

아래 샘플은 `playhouse`의 `RouteHeader + Payload`처럼, header는 고정 메타데이터로
읽고 body는 `header.MsgId`를 보고 각 packet 타입으로 다시 parse하는 방향이다.

```csharp
using Gateway.Protocol; // protoc generated
using PlayHouse.Runtime.Proto; // RouteHeader protobuf generated

builder.Services.AddZLinkFramework(options =>
{
    options.Codecs.AddProtobuf();

    options.AddStreamNode("client.stream", stream =>
    {
        stream.Bind("tcp://0.0.0.0:9100");
        stream.AddHeaderSession<ClientHeaderSession>();
    });
});

public sealed class ClientHeaderSession
    : IZLinkSession
{
    public IZLinkSessionContext Context { get; set; } = default!;

    public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    public ValueTask OnErrorAsync(
        ZLinkStreamError error,
        CancellationToken cancellationToken)
    {
        int? errno = error.Diagnostic?.NativeCode;
        string? message = error.Diagnostic?.Message;
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDispatchAsync(
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        switch (header.Name)
        {
            case "ClientInput":
            {
                ClientInput input = body.Parse<ClientInput>();

                Context
                    .SendChannel(
                        "play",
                        new ForwardInputCommand
                        {
                            Input = input
                        })
                    .Exec();

                break;
            }

            case "Ping":
            {
                Ping ping = body.Parse<Ping>();

                Context
                    .SendChannel(
                        "api",
                        new ReportPingCommand
                        {
                            Sequence = ping.Sequence
                        })
                    .Exec();

                await Context
                    .Reply(new Pong
                    {
                        Sequence = ping.Sequence
                    })
                    .SendAsync(cancellationToken);
                break;
            }
        }
    }
}

// ClientInput, Ping은 .proto에서 생성된 타입이라고 가정한다.
```

이 샘플을 읽을 때 중요한 점은 아래와 같다.

- application은 `ZlinkStreamHeader.Name`을 dispatch 기준으로 쓴다.
- body는 고정 타입 하나로 바로 올리지 않는다.
- header session이 `header.MsgId`를 보고 `ClientInput`, `Ping` 같은 각 packet
  타입으로
  parse한다.
- application은 recv loop 대신 session callback만 구현한다.
- 다른 서버로의 outbound 호출은 session이 `Context.SendChannel(...)` 또는
  `Context.RequestChannel(...)`로 처리한다.
- body parse는 `body.Parse<T>()` 같은 extension helper를 통해 처리한다.
- protobuf generated 타입은 `IMessage<T>` 계열인지 보고 protobuf로 해석한다.
- 그 밖의 일반 class는 json으로 해석하는 규칙을 샘플 기본값으로 둔다.
- 이 helper는 내부에서 `Message.AsReadOnlySpan()`를 사용해서 추가 복사를 가능한 한
  피하는 쪽을 기본으로 본다.
- 즉 stream 핫패스에서는 불필요한 배열 복사와 추가 메모리 할당을 가능한 한
  피해야 한다.

이 방식은 `playhouse`의 아래 흐름과 같은 감각이다.

- `RouteHeader`를 먼저 읽는다.
- `RouteHeader.MsgId`를 dispatch 기준으로 쓴다.
- `packet.Payload`를 각 protobuf 타입으로 parse한다.

예를 들면 serializer extension은 아래처럼 하나로 둘 수 있다.

```csharp
public static class MessageExtensions
{
    public static T Parse<T>(this Message message)
    {
        if (IsGeneratedProtoMessage(typeof(T)))
            return ParseGeneratedProto<T>(message);

        return JsonSerializer.Deserialize<T>(message.AsReadOnlySpan())
            ?? throw new InvalidOperationException(
                $"Failed to deserialize {typeof(T).Name}");
    }

    private static bool IsGeneratedProtoMessage(Type type)
    {
        return type.GetInterfaces().Any(iface =>
            iface.IsGenericType
            && iface.GetGenericTypeDefinition() == typeof(IMessage<>)
            && iface.GenericTypeArguments[0] == type);
    }
}
```

즉 이 샘플은 아래 규칙을 전제로 한다.

- protobuf generated 타입이면 `body.Parse<T>()`가 protobuf parser를 고른다.
- 일반 POCO class면 `body.Parse<T>()`가 json parser를 고른다.
- application은 serializer 이름보다 "이 payload를 어떤 타입으로 읽는가"에 더
  집중한다.

나중에 같은 타입을 여러 serializer로 처리해야 한다면, 그때는 `ParseProto<T>()`,
`ParseJson<T>()` 같은 명시형 helper나 context 기반 parse 함수를 따로 두는 편이
더 안전하다.

## 4. header session 샘플

아래 샘플은 framing이나 decode 이전 raw payload를 직접 보고 싶은 경우다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.AddStreamNode("client.raw", stream =>
    {
        stream.Bind("tcp://0.0.0.0:9200");
        stream.AddHeaderSession<ClientHeaderSession>();
    });
});

public sealed class ClientHeaderSession : IZLinkSession
{
    public IZLinkSessionContext Context { get; set; } = default!;

    public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    public ValueTask OnErrorAsync(
        ZLinkStreamError error,
        CancellationToken cancellationToken)
    {
        int? errno = error.Diagnostic?.NativeCode;
        string? message = error.Diagnostic?.Message;
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDispatchAsync(
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        return Context
            .Reply(new Pong())
            .SendAsync(cancellationToken);
    }
}
```

이 샘플은 아래 상황에 더 가깝다.

- framework가 decode한 header/body packet을 그대로 응답하고 싶다.
- 그래도 recv loop는 직접 만들고 싶지 않다.
- 필요하면 현재 session에서 `Context.Reply(...)`로 응답을 보낼 수 있어야 한다.

## 5. session 처리 수준

- header session
  - C API가 이미 잘라 준 `header/body` packet 처리
  - session lifecycle과 packet callback을 함께 구현
  - body는 `msgId`를 보고 각 packet 타입으로 parse

현재 구현의 stream session 표면은 header/body packet 처리에 맞춘다.
raw chunk를 직접 다루는 표면은 별도 초안으로 분리해서 검토한다.

## 6. recv 방식은 왜 샘플에 없는가

현재 초안에서는 recv 방식을 framework 기본 표면으로 보지 않는다.
이 문서에서 recv 샘플을 넣지 않는 이유도 같다.

- framework가 DI, filter, logging, dispatch를 공통으로 다루기 어렵다.
- application이 loop, cancel, backpressure를 직접 떠안게 된다.
- header session보다 사용 경험이 더 low-level이다.

즉 recv가 하부 binding에서 불가능하다는 뜻이 아니라, **framework 샘플의 기본
방향으로는 채택하지 않는다**는 뜻이다.

## 7. 정리

- `STREAM` 기본 표면은 `IZLinkSession`과 `IZLinkSessionContext`로 고정한다.
- `OnConnectedAsync(...)`는 `ConnectionReady` 기준으로 읽는다.
- `OnErrorAsync(...)`는 session-correlatable transport 오류만 받고, handshake 실패와
  socket/node 단위 오류는 monitoring으로 분리한다.
- body parse와 encode helper는 framework 본체가 아니라 serializer 확장 패키지가
  맡는다.
- `Message.AsReadOnlySpan()` 기반 helper를 기본으로 해서 불필요한 복사를 줄인다.
- protobuf/json/messagepack serializer는 확장 패키지로 분리한다.
