[스펙 목차](../../../README.ko.md)

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

public interface IZLinkStreamPacketHandler<in THeader, in TBody>
{
    ValueTask HandleAsync(
        THeader header,
        TBody body,
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

## 3. packet handler 샘플

아래 샘플은 C API가 이미 잘라 준 `header/body` packet 단위를 처리하는 방향이다.

```csharp
using Gateway.Protocol; // protoc generated

builder.Services.AddZLinkFramework(options =>
{
    options.Codecs.AddProtobuf();

    options.AddStreamNode("gateway.stream", stream =>
    {
        stream.Bind("tcp://0.0.0.0:9100");
        stream.AddPacket<GatewayPacketHandler>();
    });
});

public sealed class GatewayPacketHandler
    : IZLinkStreamPacketHandler<ClientInputHeader, ClientInputBody>
{
    private readonly IZLinkClient _client;

    public GatewayPacketHandler(IZLinkClient client)
    {
        _client = client;
    }

    public async ValueTask HandleAsync(
        ClientInputHeader header,
        ClientInputBody body,
        ZLinkStreamContext context,
        CancellationToken cancellationToken)
    {
        await _client.SendAsync(
            "api",
            new ForwardInputCommand(),
            cancellationToken);
    }
}

// ClientInputHeader, ClientInputBody, ForwardInputCommand는
// .proto에서 생성된 타입이라고 가정한다.
```

이 샘플을 읽을 때 중요한 점은 아래와 같다.

- framework가 raw `Message header`, `Message body`를
  `ClientInputHeader`, `ClientInputBody`로 변환한다.
- application은 recv loop 대신 `HandleAsync(...)`만 구현한다.
- 다른 서버로의 outbound 호출은 handler가 `IZLinkClient`를 DI로 받아 처리한다.

## 4. raw handler 샘플

아래 샘플은 framing이나 decode 이전 raw payload를 직접 보고 싶은 경우다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.AddStreamNode("gateway.raw", stream =>
    {
        stream.Bind("tcp://0.0.0.0:9200");
        stream.AddRaw<GatewayRawHandler>();
    });
});

public sealed class GatewayRawHandler : IZLinkStreamRawHandler
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
  - 필요하면 framework가 사용자 정의 `header/body` 타입으로 변환
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
- packet handler의 header/body 변환을 codec으로 할지 mapper로 할지
- connection open/close hook을 추가로 노출할지
