[스펙 목차](../../../README.ko.md)

[.NET 묶음](./README.ko.md) | [STREAM](./aspnet-core-stream.ko.md) | [STREAM open items](./stream-open-items.ko.md) | [인터페이스](./handler-interfaces.ko.md)

# Draft -- ZLink Framework .NET STREAM Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET`에서 `STREAM` packet handler와 raw handler를
> 실제 코드 흐름으로 보기 위한 샘플 문서다.

## 1. 이 문서의 목적

`STREAM`은 recv loop를 직접 돌리는 low-level 표면과, framework가 dispatch를 맡는
handler 표면이 섞이면 읽기 어려워진다. 이 문서는 framework 초안 기준으로 아래 두
가지만 보여 준다.

1. packet handler
2. raw handler

recv 방식은 이 샘플 문서에 넣지 않는다.

## 2. 인터페이스 초안

`STREAM` 샘플이 기대하는 최소 인터페이스는 아래 정도다.

```csharp
public interface IZLinkStreamPacketHandler
{
    ValueTask HandleAsync(
        Message header,
        Message body,
        ZLinkStreamContext context,
        CancellationToken cancellationToken);
}

public interface IZLinkStreamPacketHandler<in THeader>
{
    ValueTask HandleAsync(
        THeader header,
        Message body,
        ZLinkStreamContext context,
        CancellationToken cancellationToken);
}

public interface IZLinkStreamRawHandler
{
    ValueTask HandleAsync(
        Message payload,
        ZLinkStreamContext context,
        CancellationToken cancellationToken);
}
```

이 초안에서는 stream packet 처리에서 불필요한 메모리 복사를 줄이는 것을 중요하게
본다. 그래서 `Message.ToArray()`를 기본 경로로 두기보다, `Message`가 가진
`AsReadOnlySpan()` 위에서 decode helper가 동작하는 쪽을 더 자연스러운 방향으로
본다.

또한 객체 직렬화 계층은 `playhouse/extensions`처럼 transport 본체와 분리하는
방향을 기본으로 본다. 즉 packet handler는 `Message`를 받고, protobuf/json 같은
객체 변환은 extension helper가 맡는다.

## 3. packet handler 샘플

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
        stream.AddPacket<ClientPacketHandler>();
    });
});

public sealed class ClientPacketHandler
    : IZLinkStreamPacketHandler<RouteHeader>
{
    private readonly IZLinkClient _client;

    public ClientPacketHandler(IZLinkClient client)
    {
        _client = client;
    }

    public async ValueTask HandleAsync(
        RouteHeader header,
        Message body,
        ZLinkStreamContext context,
        CancellationToken cancellationToken)
    {
        switch (header.MsgId)
        {
            case "ClientInput":
            {
                ClientInput input = body.Parse<ClientInput>();

                await _client.SendAsync(
                    "play",
                    new ForwardInputCommand
                    {
                        StageId = header.StageId,
                        AccountId = header.AccountId,
                        Input = input
                    },
                    cancellationToken);

                break;
            }

            case "Ping":
            {
                Ping ping = body.Parse<Ping>();

                await _client.SendAsync(
                    "api",
                    new ReportPingCommand
                    {
                        From = header.From,
                        Sequence = ping.Sequence
                    },
                    cancellationToken);

                break;
            }
        }
    }
}

// RouteHeader, ClientInput, Ping은 .proto에서 생성된 타입이라고 가정한다.
```

이 샘플을 읽을 때 중요한 점은 아래와 같다.

- framework가 raw `Message header`를 `RouteHeader`로 변환한다.
- body는 고정 타입 하나로 바로 올리지 않는다.
- handler가 `header.MsgId`를 보고 `ClientInput`, `Ping` 같은 각 packet 타입으로
  parse한다.
- application은 recv loop 대신 `HandleAsync(...)`만 구현한다.
- 다른 서버로의 outbound 호출은 handler가 `IZLinkClient`를 DI로 받아 처리한다.
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

## 4. raw handler 샘플

아래 샘플은 framing이나 decode 이전 raw payload를 직접 보고 싶은 경우다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.AddStreamNode("client.raw", stream =>
    {
        stream.Bind("tcp://0.0.0.0:9200");
        stream.AddRaw<ClientRawHandler>();
    });
});

public sealed class ClientRawHandler : IZLinkStreamRawHandler
{
    public ValueTask HandleAsync(
        Message payload,
        ZLinkStreamContext context,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}
```

이 샘플은 아래 상황에 더 가깝다.

- socket에서 들어오는 raw payload chunk를 직접 보고 싶다.
- packet 재조립이나 framing 판단을 application이 맡아야 한다.
- 그래도 recv loop는 직접 만들고 싶지 않다.

## 5. packet 과 raw 를 어떻게 구분하면 되는가

- packet handler
  - C API가 이미 잘라 준 `header/body` packet 처리
  - 필요하면 framework가 사용자 정의 `header` 타입으로 변환
  - body는 `msgId`를 보고 각 packet 타입으로 parse
- raw handler
  - socket raw payload chunk 처리
  - 필요하면 application이 직접 packet 재조립

둘 다 framework dispatch 위에 올라간다는 점은 같다.
차이는 application이 어떤 레벨에서 payload를 받고 싶은가다.

## 6. recv 방식은 왜 샘플에 없는가

현재 초안에서는 recv 방식을 framework 기본 표면으로 보지 않는다.
이 문서에서 recv 샘플을 넣지 않는 이유도 같다.

- framework가 DI, filter, logging, dispatch를 공통으로 다루기 어렵다.
- application이 loop, cancel, backpressure를 직접 떠안게 된다.
- packet handler와 raw handler보다 사용 경험이 더 low-level이다.

즉 recv가 하부 binding에서 불가능하다는 뜻이 아니라, **framework 샘플의 기본
방향으로는 채택하지 않는다**는 뜻이다.

## 7. 피드백 포인트

- `STREAM`은 packet handler와 raw handler 두 축이면 충분한가
- raw handler 등록 이름을 `AddRaw`로 둘지 더 분명한 이름으로 둘지
- packet handler의 header 변환을 codec으로 할지 mapper로 할지
- body parse helper를 framework가 어디까지 제공할지
- `Message`에 protobuf/json decode helper를 둘지
- protobuf/json/messagepack을 별도 extension 패키지로 둘지
- connection open/close hook을 추가로 노출할지
