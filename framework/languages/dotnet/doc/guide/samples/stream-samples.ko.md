<!-- framework-adapter-nav:start -->
[문서 목록](../../../../../doc/README.ko.md) | [이전: ZLink Framework .NET SPOT Samples](./spot-samples.ko.md) | [다음: ZLink Stream Connector For .NET](./streaming-client.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../../doc/spec/draft/README.ko.md)

[.NET 묶음](../../README.ko.md) | [STREAM](../../spec/aspnet-core-stream.ko.md) | [STREAM Decisions](../../draft/stream-open-items.ko.md) | [인터페이스](../../spec/handler-interfaces.ko.md)

# ZLink Framework .NET STREAM Samples

> 이 문서는 실행 가능한 sample 중심 문서다. 장기 연결이나 실시간 상태 서버에 ZLink 를
> 넣을지 판단하는 설명은 [12-grpc-alternative](../12-grpc-alternative.ko.md)와 각
> 케이스 스터디가 맡고, 이 문서는 STREAM 등록과 session packet 흐름을 따라 하는 데
> 집중한다.

## 1. 이 문서의 목적

이 절은 이 문서가 어떤 STREAM 흐름만 담고, 무엇은 빼는지를 정리한다.

`STREAM` 은 두 가지 표면이 한자리에 섞이면 읽기가 무척 어려워진다. 즉 recv
loop 를 직접 돌려야 하는 low-level 표면과, framework 가 dispatch 를 대신 맡아
주는 session 표면이 함께 등장하면 그렇다. 그래서 이 문서는 framework 초안을
기준으로 다음 두 가지만 다룬다.

1. header session
2. header session

recv 방식을 사용하는 샘플은 이 문서에 포함하지 않는다.

## 2. 인터페이스 초안

이 절은 STREAM 샘플이 전제로 삼는 최소 타입을 정리한다.

`STREAM` 샘플이 전제로 삼는 최소 인터페이스는 대략 아래와 같다.

> **주의**: 아래 정의는 [handler-interfaces.ko.md](../../spec/handler-interfaces.ko.md)의
> 해당 섹션과 같다. 인터페이스가 바뀌면 두 문서를 반드시 함께 갱신해야 한다.
> 최신 계약의 기준은 언제나 `handler-interfaces.ko.md`다.

```csharp
public interface IZLinkStream
{
    bool Write(
        Message payload,
        SendFlags flags = SendFlags.None);

    ValueTask CloseAsync();
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
        Message payload,
        CancellationToken cancellationToken);
}

public interface IZLinkSessionActors
{
    IReadOnlyCollection<IZLinkSessionActor> Bound { get; }

    ValueTask<IZLinkSessionActor> BindAsync(
        string actorId,
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

    ValueTask RelayAsync(
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default);

    ValueTask NotifyDisconnectedAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkSessionContext :
    IZLinkSessionContext,
    IZLinkSessionClient,
    IZLinkSessionActors,
    IZLinkSessionContext;
```

session context 는 callback 인자가 아니라 session 인스턴스 생성 시점에
주입되는 계약이다. framework 는 생성된 session 의 `Context` 가 주입한
context 와 같은 인스턴스인지 확인한다.

`CloseAsync()` 는 현재 session 의 client stream 연결을 서버 쪽에서 끊을 때
사용한다. 예를 들어 인증에 실패했거나 protocol 위반이 확인되어 응답을 돌려준 뒤,
더 이상 packet[^packet] 을 받지 않으려는 상황을 생각할 수 있다. 그럴 때 session
handler[^handler] 가 이 함수를 호출하면 된다.

이 문서에서는 stream packet 을 다룰 때 발생하는 불필요한 메모리 복사를 줄이는
데 무게를 둔다. 그래서 `Message.ToArray()` 를 기본 경로로 두지 않는다. 대신
`Message` 가 노출하는 `AsReadOnlySpan()` 위에서 decode helper 가 동작하는
방향을 더 자연스럽다고 본다.

또한 객체 직렬화 계층은 `playhouse/extensions` 처럼 transport[^transport] 본체와
분리하는 쪽을 기본으로 본다. 즉 header session 은 `Message` 만 다루고,
protobuf / json 같은 객체 변환은 extension helper 가 맡는 구조다.

`OnErrorAsync(...)` 로 들어오는 값 역시 raw monitor event 를 그대로 노출하지는
않는다. 샘플 기준으로는 다음과 같이 둔다. 먼저 `ZLinkStreamError` 가 거친 수준의
오류 분류를 전달한다. 필요할 때는 `Diagnostic` 을 통해 native errno 와 메시지까지
함께 확인할 수 있다.

## 3. header session 샘플

이 절은 header 가 알려 준 packet name 으로 payload 타입을 골라 decode 하는
흐름을 보여 준다.

아래 샘플은 `playhouse` 의 `RouteHeader + Payload` 처럼, framework 가 header 를
읽어 만들어 둔 packet name 을 기준으로 각 payload 타입을 decode 하는 흐름이다.

```csharp
using Gateway.Protocol; // protoc generated
using PlayHouse.Runtime.Proto; // RouteHeader protobuf generated

builder.Services.AddZLinkFramework(options =>
{
    options.Codecs.AddProtobuf();

    options.AddStreamNode("client.stream", stream =>
    {
        stream.Bind("tcp://0.0.0.0:9100");
        stream.RegisterSession<ClientHeaderSession>();
    });
});

public sealed class ClientHeaderSession(
    IZLinkSessionContext context,
    IZLinkChannelClient channels)
    : IZLinkSession
{
    public IZLinkSessionContext Context { get; } = context;

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
        Message payload,
        CancellationToken cancellationToken)
    {
        switch (header.Name)
        {
            case "ClientInput":
            {
                ClientInput input = payload.Decode<ClientInput>();

                await channels
                    .Send(
                        "play",
                        new ForwardInputCommand
                        {
                            Input = input
                        })
                    .SubmitAsync(cancellationToken);

                break;
            }

            case "Ping":
            {
                Ping ping = payload.Decode<Ping>();

                await channels
                    .Send(
                        "api",
                        new ReportPingCommand
                        {
                            Sequence = ping.Sequence
                        })
                    .SubmitAsync(cancellationToken);

                await context
                    .Reply(new Pong
                    {
                        Sequence = ping.Sequence
                    })
                    .SubmitAsync();
                break;
            }
        }
    }
}

// ClientInput, Ping은 .proto에서 생성된 타입이라고 가정한다.
```

이 샘플을 읽을 때 짚어야 할 점은 다음과 같다.

- application 은 `ZlinkStreamHeader.Name` 을 dispatch 기준으로 사용한다.
- packet 은 고정 타입 하나로 곧장 올라오는 구조가 아니다.
- header session 이 내부 header 를 해석해 `ClientInput`, `Ping` 같은 packet
  name 을 뽑아 준다. 그러면 application 은 그 이름에 맞는 타입으로 decode 한다.
- application 측에는 recv loop 가 없다. session callback 만 구현하면 된다.
- framework runtime 이 수신 payload 의 해제를 책임진다. 따라서 session callback
  안에서는 payload 를 해제하거나 `Move()` 로 소비하지 않고, 바로 읽거나 다른
  framework API 에 넘긴다.
- 다른 서버로의 outbound 호출은 session 이 생성자에서 함께 받은
  `IZLinkChannelClient` 로 `SendToChannel(...)` 또는 `RequestToChannel(...)` 를 호출해 처리한다.
  이 호출은 stream 연결이 아니라 channel client socket 을 사용한다.
- packet decode 는 payload 의 serializer helper 를 거쳐 수행한다.
- 타입이 protobuf generated 타입(`IMessage<T>` 계열) 이면 protobuf 로 읽는다.
- 그 외의 일반 class 는 json 으로 읽는 것을 샘플 기본 규칙으로 둔다.
- 이 helper 는 내부에서 `Message.AsReadOnlySpan()` 을 활용한다. 즉 추가 복사를
  가급적 피하는 방향을 기본으로 본다.
- 정리하면, stream 핫패스에서는 불필요한 배열 복사와 추가 메모리 할당을 가능한
  한 걷어 내야 한다.

이 방식은 `playhouse` 의 다음 흐름과 같은 감각이다.

- `RouteHeader` 를 먼저 읽는다.
- `RouteHeader.MsgId` 를 dispatch 기준으로 사용한다.
- `Message payload` 를 각 protobuf 타입으로 parse 한다.

예컨대 session payload decode 는 다음처럼 target type 기준으로 serializer 를
골라 두는 방식이 가능하다. 실제 payload bytes 에 직접 접근하는 부분은 serializer
helper 내부 구현에 숨겨 둔다.

```csharp
public static class SessionPayloadCodecs
{
    public static T Decode<T>(Message payload)
    {
        if (IsGeneratedProtoMessage(typeof(T)))
            return DecodeGeneratedProto<T>(payload);

        return DecodeJson<T>(payload);
    }

    private static bool IsGeneratedProtoMessage(Type type)
    {
        return type.GetInterfaces().Any(iface =>
            iface.IsGenericType
            && iface.GetGenericTypeDefinition() == typeof(IMessage<>)
            && iface.GenericTypeArguments[0] == type);
    }

    private static T DecodeGeneratedProto<T>(Message payload) => throw new NotImplementedException();

    private static T DecodeJson<T>(Message payload) => throw new NotImplementedException();
}
```

정리하면 이 샘플은 다음 규칙을 전제로 깔고 있다.

- protobuf generated 타입이라면 payload decode helper 가 protobuf parser 를
  선택한다.
- 일반 POCO class 라면 payload decode helper 가 json parser 를 선택한다.
- application 은 serializer 이름보다 "이 payload 를 어떤 타입으로 읽을 것인가"
  에 집중하면 된다.

이후 같은 타입을 여러 serializer 로 처리해야 할 필요가 생길 수도 있다. 그 시점
에는 `ParseProto<T>()`, `ParseJson<T>()` 같은 명시형 helper 나 context 기반
parse 함수를 별도로 두는 편이 더 안전한 방향이다.

## 4. header session 샘플

이 절은 framework 가 decode 해 준 packet 을 곧바로 응답으로 돌려보내는 흐름을
보여 준다.

아래는 framework 가 decode 해 준 packet 을 곧바로 응답으로 돌려보내는 경우의
샘플이다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.AddStreamNode("client.stream", stream =>
    {
        stream.Bind("tcp://0.0.0.0:9200");
        stream.RegisterSession<ClientHeaderSession>();
    });
});

public sealed class ClientHeaderSession(IZLinkSessionContext context) : IZLinkSession
{
    public IZLinkSessionContext Context { get; } = context;

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
        return context
            .Reply(new Pong())
            .SubmitAsync();
    }
}
```

이 샘플은 다음 같은 상황에 어울린다.

- framework 가 decode 해 둔 header 와 payload 를 받아 곧장 응답하고 싶다.
- 그렇다고 해서 recv loop 를 손수 작성하고 싶지는 않다.
- 필요할 때 현재 session 에서 생성자 주입된 `IZLinkSessionContext` 로
  `Reply(...)` 를 호출해 응답을 돌려보낼 수 있어야 한다.

## 5. session 처리 수준

이 절은 현재 표면이 어느 수준의 session 처리를 보여 주는지를 정리한다.

- header session
  - C API 가 이미 잘라 둔 stream frame 을 framework 가 header 와 payload 로 나눠
    처리한다
  - session lifecycle[^lifecycle] 과 packet callback 을 함께 구현한다
  - packet name 을 보고 각각의 packet 타입으로 decode 한다

현재 구현의 stream session 표면은 framework session packet 처리에 맞춰 두었다.
raw chunk 를 직접 다루는 표면은 MVP 범위에 포함하지 않는다.

## 6. recv 방식은 왜 샘플에 없는가

이 절은 이 문서가 recv 샘플을 빼는 이유를 정리한다.

현재 스펙은 recv 방식을 framework 의 기본 표면으로 보지 않는다. 이 문서에서 recv
샘플을 넣지 않는 이유 역시 같은 맥락이다.

- framework 가 DI, filter, logging, dispatch 를 일관되게 묶기 어려워진다.
- application 이 loop, cancel, backpressure 를 직접 떠안아야 한다.
- header session 에 비해 사용 경험이 한층 더 low-level 이 된다.

즉 recv 가 하부 binding 에서 불가능하다는 뜻은 아니다. **framework 샘플의 기본
방향으로는 채택하지 않는다** 는 뜻이다.

## 7. 정리

이 절은 위 흐름이 따르는 결정 사항을 짧게 모아둔 것이다.

- `STREAM` 의 기본 표면은 `IZLinkSession` 과 `IZLinkSessionContext` 로 고정한다.
- `OnConnectedAsync(...)` 는 `ConnectionReady` 시점을 기준으로 본다.
- `OnErrorAsync(...)` 는 session 단위로 짝지을 수 있는 transport 오류만 받는다.
  handshake 실패나 socket / node 단위의 오류는 monitoring 쪽으로 분리한다.
- packet 의 decode 와 encode helper 는 framework 본체가 아니라 serializer 확장
  패키지가 맡는다.
- `Message.AsReadOnlySpan()` 기반 helper 를 기본으로 두어, 불필요한 복사를
  줄인다.
- protobuf / json / messagepack serializer 는 확장 패키지로 분리한다.

## 8. 회귀 테스트

이 절은 STREAM 샘플이 어떤 테스트와 묶여 회귀를 막는지를 정리한다.

STREAM 샘플은 다음을 하나의 흐름으로 보여 준다.

- header session 등록
- packet dispatch
- reply
- lifecycle callback

그래서 같은 범위의 integration test 에 묶어 둔다. 샘플에서 raw recv 방식이 다시
기본처럼 보이지 않도록 유의한다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `HeaderStreamSessionTests.HeaderStreamSession_Receives_Replies_And_Tracks_Lifecycle` | header session 샘플의 dispatch와 reply 흐름이 정상 동작한다. |
| `HeaderStreamSessionTests.HeaderStreamSession_Can_Close_Current_Client_Stream` | session context의 close 샘플 의미가 그대로 유지된다. |
| `TopologyTests.StreamRawSession_OnConnected_Emits_Metadata_Once_From_TestHostProcess` | 실제 프로세스 경계에서도 connected metadata가 한 번만 전달된다. |
| `TopologyTests.StreamRawSession_OnError_Reports_TransportError_For_RemoteDisconnect` | remote disconnect 상황이 transport error로 보고된다. |

[^public-contract]: public contract는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 뜻한다.
[^stream]: `STREAM`은 외부 클라이언트와 서버 사이를 잇는, 연결 지향적인 양방향 메시지 통로를 가리키는 ZLink 추상이다. 한 connection 위에서 여러 packet이 순서대로 오가는 구조다.
[^packet]: packet은 stream 위에서 한 단위로 묶여 전달되는 메시지다. header에 종류와 metadata가 들어가고, payload에 실제 payload가 담긴다.
[^handler]: handler는 들어온 요청·메시지·packet을 받아 실제 처리를 수행하는 사용자 코드를 가리킨다.
[^transport]: transport는 실제 네트워크 위에서 바이트를 실어 나르는 계층을 뜻한다(TCP, TLS, WS, WSS 등). 그 위에 framework의 packet/session 추상이 올라간다.
[^lifecycle]: lifecycle은 어떤 컴포넌트가 생성·시작·종료에 이르는 동안 거치는 단계와 순서를 묶어 부르는 말이다.
