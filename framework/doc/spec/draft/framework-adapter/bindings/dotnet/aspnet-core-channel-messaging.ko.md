[스펙 목차](../../../README.ko.md)

[.NET 묶음](./README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [channel 샘플](./channel-messaging-samples.ko.md) | [SPOT](./aspnet-core-spot.ko.md) | [STREAM](./aspnet-core-stream.ko.md) | [Registry](./aspnet-core-registry.ko.md)

# Draft -- ZLink Framework ASP.NET Core Channel Messaging

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `ASP.NET Core`에서 direct channel call과 event
> messaging을 어떤 API로 드러낼지 방향을 정리한다.

## 1. 목표

`ASP.NET Core` 애플리케이션에서 아래 경험을 제공하는 것이 목표다.

- `channel name` 기준 direct call
- DI로 주입되는 공용 outbound client
- event publish
- channel별 `Discovery` 기반 요청
- handler 등록과 DI 통합

여기서 outbound client는 ZLink 메시지 handler 안에서도, 기존 ASP.NET Core HTTP
handler/controller 안에서도 똑같이 쓸 수 있어야 한다.

즉 사용자는 `DealerSocket`, `RouterSocket`, `Discovery`를 직접 조합하기보다,
`AddZLinkFramework(...)`, `IZLinkClient`, handler registration 같은 표면으로
작업하게 한다.

등록부터 handler, HTTP endpoint, outbound 호출까지 이어서 보는 샘플은
[channel-messaging-samples.ko.md](./channel-messaging-samples.ko.md)를
참고한다.

## 2. 기반이 되는 .NET binding

현재 초안은 아래 `.NET` binding 기능을 하부 토대로 본다.

- `Discovery`
- `DealerSocket`
- `RouterSocket`
- request-reply helper
- `PubSocket` / `SubSocket`

`ZLink Framework`는 이 표면을 감추기보다, 그 위에 channel별 outbound runtime을 관리하는
더 높은 수준의 프레임워크 통합 API를 얹는 방향으로 본다.

## 3. ASP.NET Core에서 기대하는 등록 방식

### 3.1 channel 등록

현재 초안은 channel마다 어떤 역할을 열지 먼저 선언하고, request client
capability에 대해서는 자동 연결과 수동 연결을 둘 다 지원하되 **같은 channel의
request client에서는 둘 중 하나만 선택**하는 편을 기본으로 본다.

여기서 중요한 점은 channel이 곧바로 하나의 소켓 조합을 뜻하지 않는다는 점이다.
framework 사용자는 아래처럼 역할 이름으로 읽는 편이 자연스럽다.

- `EnableServer()` -- local request/send handler를 받는다
- `EnableClient()` -- 그 channel로 request/send outbound 호출을 보낸다
- `EnablePublisher()` -- 그 channel의 event를 publish한다
- `EnableSubscriber()` -- 그 channel의 event를 subscribe한다

즉 inbound handler를 붙이지 않고 outbound client만 쓰는 앱이라면, local server
capability 없이 `EnableClient()`만 선언한 channel만 두고 시작할 수 있어야 한다.

자동 연결 예시는 아래와 같다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.AddChannel("api", channel =>
    {
        channel.EnableServer();
    });

    options.AddChannel("profile", channel =>
    {
        channel.EnableClient();
    });

    options.AddChannel("account", channel =>
    {
        channel.EnableClient();
    });

    options.UseDiscovery(registry =>
    {
        registry.Add("tcp://registry1:5551");
        registry.Add("tcp://registry2:5551");
    });
});
```

이 등록은 framework 전역 runtime, channel runtime factory, codec registry의 기본
구성을 맡는다. `AddChannel("profile", channel => channel.EnableClient())` 같은
선언은 그 channel에 접근할 outbound runtime과 `DEALER(client)`를 framework가
관리한다는 뜻이다.

위 예시는 `api` channel에서 server 역할을 하고, `profile`, `account` channel에
대해서는 client 역할만 하는 애플리케이션 전제다.

수동 연결 예시는 아래와 같이 둘 수 있다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.AddChannel("api", channel =>
    {
        channel.EnableServer();
    });

    options.AddChannel("profile", channel =>
    {
        channel.EnableClient(client =>
        {
            client.UseManualConnections(peers =>
            {
                peers.Connect("tcp://10.0.10.15:7101");
            });
        });
    });
});
```

이 경우 framework는 `Discovery`를 강제하지 않는다. 해당 channel의 request client
capability가 수동으로 주어진 peer 목록만 기준으로 연결을 관리한다.

이 draft에서 channel client manual 연결은 remote `RoutingId`를 받지 않는다.
하부 모델이 이미 connect된 `DEALER`를 attach하는 방식이기 때문에, framework
표면도 endpoint 집합만 다루는 편이 맞다.

앱 전체에서는 두 방식을 함께 둘 수 있다. 다만 그 뜻은 "같은 channel의 request
client capability가 두 방식을 동시에 섞는다"가 아니다. 예를 들어 `profile`
channel은 `Discovery` 기반 자동 연결로 두고, `account` channel은 수동 연결로
둘 수 있다는 뜻이다.

이 구분이 필요한 이유는 zlink core에서 `Discovery`가 붙은 `DEALER`는 수동
`connect`, `disconnect`, `unbind`, `close`를 허용하지 않기 때문이다. 따라서
framework도 같은 channel runtime 안에서 자동 연결과 수동 연결을 함께 섞는
모델로 설명하면 안 된다.

중요한 점은 수동 연결이 `channel` 전체 설정이 아니라 **capability별 설정**이라는
점이다. 예를 들어 같은 `profile` channel이라도 아래 둘은 다른 연결 집합이다.

- `profile.client`
- `profile.subscriber`

따라서 수동 연결 API도 `channel.UseManualConnections(...)`처럼 channel 전체에
두기보다, `EnableClient(client => ...)`, `EnableSubscriber(subscriber => ...)`
같이 역할별 builder 아래에 두는 편이 맞다.

현재 초안에서는 manual capability에 대해 런타임 `Connect`, `Disconnect`,
`ListConnections`를 제공하는 별도 manager surface도 필요하다고 본다.

### 3.1.1 outbound-only 앱 예시

아래처럼 local handler를 붙이지 않고 `IZLinkClient`만 쓰는 앱도 가능해야 한다.
이 경우 framework는 server capability를 열지 않고, client capability를 선언한
remote channel에 대해서만 outbound `DEALER(client)`를 만든다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.DefaultTimeout = TimeSpan.FromSeconds(1);
    options.Codecs.AddProtobuf();
    options.AddChannel("profile", channel =>
    {
        channel.EnableClient();
    });

    options.UseDiscovery(registry =>
    {
        registry.Add("tcp://registry1:5551");
        registry.Add("tcp://registry2:5551");
    });
});
```

### 3.2 outbound client 등록

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.DefaultTimeout = TimeSpan.FromSeconds(1);
    options.Codecs.AddProtobuf();
});
```

핵심은 아래 네 점이다.

- `IZLinkClient`는 DI로 주입된다.
- 호출 대상은 gateway 주소가 아니라 `channel name`
- runtime은 등록한 channel capability마다 필요한 runtime을 만든다.
- request client capability를 둔 channel은 그 channel 전용 `Discovery`와 outbound
  `DEALER(client)` socket을 가진다.

여기서 outbound `DEALER(client)`는 framework 관점에서 주로 request의 reply를
받는 경로다. 일반 request/send handler dispatch는 local `ROUTER(server)`가 받은
메시지를 기준으로 설명하는 편이 맞다.

### 3.3 handler 등록

```csharp
builder.Services.AddZLinkHandlersFromAssemblyContaining<Program>();
```

또는 endpoint-style registration도 가능하지만, 1차 초안은 DI와 attribute 기반을
우선 검토한다.

현재 1차 attribute 후보는 아래처럼 둔다. 기본 packet key는 payload 타입 이름을
쓰고, 정말 필요할 때만 `PacketName`을 override하는 방향을 기준으로 본다.

```csharp
[ZLinkRequest]
[ZLinkSend]
[ZLinkEvent(PacketName = "profile.cache-invalidated")]
```

이 등록 모델에서 중요한 점은 handler 인스턴스 생성도 `.NET DI`가 맡는다는 것이다.
즉 framework는 매핑만 잡고, 실제 handler 객체는 `IServiceProvider`를 통해 resolve
한다. 따라서 constructor injection은 일반 `ASP.NET Core` 서비스와 같은 방식으로
동작해야 한다.

여기서 local request/send handler가 붙는 channel은 route prefix가 아니라 앱이
그 channel에서 server 역할을 한다는 뜻이다. 반대로 outbound-only 앱이라면
server capability가 있는 channel 자체를 두지 않을 수 있어야 한다.

## 4. 서버 쪽 프로그래밍 모델 초안

실제 handler 인터페이스 초안은 [handler-interfaces.ko.md](./handler-interfaces.ko.md)를
기준으로 본다. 이 문서는 그중 `ASP.NET Core` 매핑 경험을 설명하는 쪽에 집중한다.

여기서 channel messaging handler는 `SPOT` room 핫패스와 완전히 같은 성능 문맥을
기본 전제로 두지는 않는다. 그렇다고 성능이 낮아도 된다는 뜻은 아니다. 이 계층도
불필요한 reflection과 할당은 줄여야 한다. 다만 `SPOT` packet 처리처럼 "FPS room
핫패스"를 전제로 가장 강한 최적화를 우선하는 대신, 일반 channel messaging 쪽은
조금 더 많은 편의 기능을 허용할 여지가 있다는 뜻에 가깝다.

### 4.1 request handler

```csharp
public sealed class UserHandlers
{
    private readonly IZLinkClient _client;

    public UserHandlers(IZLinkClient client)
    {
        _client = client;
    }

    [ZLinkRequest]
    public async ValueTask<UserReply> GetUserAsync(
        UserRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        var account = await _client
            .Request(
                "account",
                new GetAccountRequest { AccountId = request.AccountId })
            .ExecAsync(cancellationToken);

        return new UserReply
        {
            AccountId = request.AccountId,
            Nickname = account.Nickname
        };
    }
}
```

이 모델에서 기대하는 점은 아래와 같다.

- body는 typed object로 역직렬화된다.
- `ZLinkContext`에서 header, correlation, deadline, caller metadata를 읽는다.
- `CancellationToken`으로 timeout/cancel을 연결한다.
- handler class는 `UserHandlers`, `ItemHandlers`처럼 주제별로 묶어도 된다.
- 패킷 하나당 class 하나로 쪼개도 된다.
- 기본 dispatch key는 request payload 타입 이름이다.
- 예: `UserRequest`는 기본적으로 `UserRequest` packet으로 매핑된다.
- 같은 이름 충돌이나 외부 계약 때문에 다른 키가 필요할 때만 `PacketName`을
  explicit override한다.

### 4.2 event handler

```csharp
public sealed class CacheEventHandlers
{
    [ZLinkEvent(PacketName = "cache.invalidate")]
    public ValueTask HandleAsync(
        CacheInvalidateEvent message,
        ZLinkEventContext context,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}
```

request-response와 event는 분리된 표면으로 보이는 편이 좋다.

### 4.3 inbound dispatch 시퀀스

아래 시퀀스는 `GetProfileRequest` packet이 local `ROUTER(server)`로 들어왔을 때
runtime이 handler를 찾고, DI로 handler를 resolve해서 응답을 돌려주는 흐름을 보여
준다. 여기서 outbound channel runtime은 startup 시점에 `Discovery` 기반 자동
연결 또는 수동 연결 중 하나를 선택해 둔다.

```mermaid
sequenceDiagram
    autonumber
    participant RP as Remote Peer
    participant RT as ZLink Runtime
    participant CH as Channel Runtime
    participant DISC as Discovery
    participant MC as Manual Connections
    participant DSP as Dispatcher
    participant REG as Handler Registry
    participant CODEC as Codec
    participant PIPE as Handler Filter Pipeline
    participant SCOPE as IServiceScope
    participant SP as IServiceProvider
    participant H as ProfileHandlers
    participant SVC as IProfileService

    Note over RT,MC: startup stage
    RT->>CH: GetOrCreateChannel("profile")
    alt discovery-based connection
        CH->>DISC: Attach channel view("profile")
        DISC-->>CH: provider rid set / endpoint updates
    else manual connection
        CH->>MC: Load configured peers/endpoints
        MC-->>CH: target rid + endpoint set
    end
    Note over CH: one outbound channel chooses one connection mode

    RP->>RT: request frame(packet=GetProfileRequest, body, headers)
    RT->>CH: Select inbound session / validate route
    CH-->>RT: session ready

    RT->>DSP: OnRequest(frame)
    DSP->>REG: ResolveEndpoint("GetProfileRequest")
    REG-->>DSP: EndpointInfo
    Note over REG,DSP: handlerType=ProfileHandlers<br/>method=HandleAsync<br/>requestType=ProfileRequest<br/>replyType=ProfileReply

    DSP->>CODEC: Deserialize(ProfileRequest, body)
    CODEC-->>DSP: ProfileRequest

    DSP->>RT: CreateRequestContext(frame metadata)
    RT-->>DSP: ZLinkRequestContext

    DSP->>SCOPE: CreateScope()
    SCOPE-->>DSP: IServiceScope
    DSP->>SP: GetRequiredService(ProfileHandlers)
    SP-->>DSP: ProfileHandlers
    Note over SP,H: constructor injection 수행

    DSP->>PIPE: Invoke(filters, handler)
    PIPE->>PIPE: logging / validation / auth
    PIPE->>H: HandleAsync(request, context, cancellationToken)
    H->>SVC: GetAsync(request, cancellationToken)
    SVC-->>H: ProfileReply
    H-->>PIPE: ProfileReply
    PIPE->>PIPE: metrics / after filters
    PIPE-->>DSP: ProfileReply

    DSP->>CODEC: Serialize(ProfileReply)
    CODEC-->>DSP: reply body

    DSP->>RT: WriteReply(correlationId, reply body, headers)
    RT-->>RP: reply frame

    DSP->>SCOPE: DisposeAsync()

    alt handler or filter throws exception
        H-->>PIPE: exception
        PIPE-->>DSP: exception
        DSP->>RT: MapExceptionToErrorReply()
        RT-->>RP: error reply frame
        DSP->>SCOPE: DisposeAsync()
    end
```

이 흐름에서 중요한 점은 아래와 같다.

- outbound channel runtime은 `Discovery` 기반 자동 연결과 수동 연결 중 하나를
  선택한다.
- 하나의 앱은 channel마다 다른 연결 방식을 택할 수 있다. 예를 들어 `profile`은
  자동 연결, `account`는 수동 연결로 둘 수 있다.
- 일반 request/send handler dispatch는 local `ROUTER(server)` ingress를 기준으로
  설명한다.
- outbound `DEALER(client)` 수신은 우선 reply correlation 경로로 보고,
  `ROUTER -> DEALER` 임의 push는 현재 channel messaging 공용 계약에 넣지 않는다.
- framework는 handler 객체를 직접 `new` 하지 않고 `.NET DI`로 resolve한다.
- filter pipeline이 있으면 handler 호출 전후를 감싼다.
- 예외는 framework가 표준 오류 응답으로 매핑해서 reply로 돌려준다.

위 dispatch 흐름에서 사용하는 handler, client, filter 인터페이스 정의는
[handler-interfaces.ko.md](./handler-interfaces.ko.md)에 모아 두었다.
주요 인터페이스는 아래와 같다.

- `IZLinkRequestHandler<TRequest, TResponse>` -- request-response handler
- `IZLinkSendHandler<TMessage>` -- one-way send handler
- `IZLinkClient` -- outbound client (`channelName` 기준 호출이 기본)
- `IZLinkHandlerFilter` -- handler 전후 공통 처리

`.NET` 앞면은 "인터페이스 + attribute 둘 다 가능하지만, 일반 사용자는
attribute 매핑과 `IZLinkClient`를 함께 쓴다"를 기본으로 본다.

## 5. 클라이언트 쪽 프로그래밍 모델 초안

### 5.1 outbound client 개요

`IZLinkClient` 인터페이스 전체 정의는
[handler-interfaces.ko.md](./handler-interfaces.ko.md)의 section 5.1을
참고한다.

구현체는 `ZLink Framework`가 DI로 제공한다. 일반 channel messaging에서는 아래
한 축을 기본으로 둔다.

- `channelName` 기준 호출 -- Discovery가 대상을 선택

그리고 기본 packet key는 request/message 타입 이름으로 해석한다.
`PacketName`, `Timeout` 같은 변형은 fluent builder에 이어 붙이는 형태를 기본으로
본다. 호출 시점에는 등록된 outbound channel 이름만 넘기고, 해당 channel의
dealer/runtime 생성과 관리는 framework가 맡는다.

즉 특정 channel의 `ROUTER(server)`를 `rid`로 직접 지정해 호출하는 표면은 두지
않는다. `rid`를 넣는 routed 호출은 SPOT spot-to-spot 경로에서만 다룬다.

중요한 점은 `IZLinkClient`를 쓴다고 해서 local `ROUTER(server)`가 항상 필요한
것은 아니라는 점이다. local inbound handler를 등록하지 않은 앱은 dealer-only
outbound runtime으로 충분하다. 다만 그 경우에도 어떤 remote channel에 접근할지는
startup에서 등록해 두는 편을 현재 방향으로 본다.

### 5.2 HTTP handler에서의 사용

이 client는 ZLink handler 안에서만 쓰는 것이 아니다. 기존 ASP.NET Core HTTP
handler에서도 그대로 주입받아 쓸 수 있어야 한다.

```csharp
app.MapPost("/profiles/get", async (
    GetProfileHttpRequest request,
    IZLinkClient client,
    CancellationToken cancellationToken) =>
{
    var reply = await client
        .Request(
            "profile",
            new GetProfileRequest { AccountId = request.AccountId })
        .ExecAsync(cancellationToken);

    return Results.Ok(reply);
});
```

이 표면은 아래 상황에 유용하다.

- 기존 웹 요청 처리 중 다른 내부 서버 호출
- ZLink handler와 HTTP handler가 같은 outbound 호출 방식을 공유
- framework 내부 공통 helper
- 특정 요청만 별도 timeout이나 packet name override로 보내기

예를 들면 아래처럼 읽히는 표면을 기준으로 본다.

```csharp
var reply = await client
    .Request("profile", new GetProfileRequest { AccountId = accountId })
    .WithTimeout(TimeSpan.FromMilliseconds(200))
    .ExecAsync(cancellationToken);

client
    .Send("profile", new RefreshProfileCacheCommand { AccountId = accountId })
    .WithPacketName("profile.refresh-cache")
    .Exec();
```

## 6. ASP.NET Core middleware, 서비스 AOP, handler pipeline

### 6.1 HTTP middleware와의 관계

기존 `ASP.NET Core`의 `app.Use(...)` middleware는 HTTP pipeline 전용이다.
따라서 ZLink 메시지 handler에 자동으로 그대로 적용되지는 않는다.

```csharp
app.UseAuthentication();
app.UseAuthorization();
app.Use(async (context, next) =>
{
    await next();
});
```

즉 위 같은 middleware는 HTTP endpoint에는 적용되지만, `ZLinkRequest`
handler에는 바로 연결되지 않는다.

### 6.2 서비스 레이어 AOP

서비스 레이어 AOP는 기존 라이브러리 방식에 맞춰 그대로 사용할 수 있다.
핵심은 handler 메서드 자체보다, handler가 주입받는 서비스 계층에서 AOP가
동작한다는 점이다.

```csharp
public sealed class UserHandlers
{
    private readonly IUserService _service;

    public UserHandlers(IUserService service)
    {
        _service = service;
    }

    [ZLinkRequest]
    public Task<UserReply> GetUserAsync(
        UserRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        return _service.GetAsync(request, cancellationToken);
    }
}
```

여기서 `IUserService`가 decorator, proxy, interceptor 같은 방식으로 감싸져 있다면
그 AOP는 그대로 적용된다. 어떤 방식으로 적용할지는 사용 중인 라이브러리 규칙을
따르면 된다.

### 6.3 ZLink handler filter

logging, validation, authorization, metrics, exception mapping 같은 공통 처리가
필요하면, HTTP middleware와는 별도의 ZLink handler filter가 필요하다.
`IZLinkHandlerFilter` 인터페이스 정의와 등록 방법은
[handler-interfaces.ko.md](./handler-interfaces.ko.md)의 section 8을 참고한다.

## 7. Discovery와 channel runtime

### 7.1 기본 방향

- 호출자는 `channel name`만 지정한다.
- `IZLinkClient`는 등록된 `channel name`마다 별도 channel runtime을 가진다.
- 각 channel은 그 channel view에 묶인 `Discovery`와 outbound `DEALER(client)`
  socket을 가진다.
- Discovery가 현재 channel view의 provider 목록을 유지한다.
- framework는 그 channel의 `rid` 집합과 연결 상태를 보고 요청을 보낸다.
- 필요하면 운영 점검용 별도 서비스가 `Registry` snapshot/query 결과를 읽어 현재
  topology를 노출할 수 있다.

### 7.2 왜 중요한가

이 모델의 핵심은 내부 서비스 호출에서 별도 gateway나 전용 load balancer를
강제하지 않으면서도, core의 fixed channel view 철학을 그대로 따른다는 점이다.

즉 아래 방향을 기본으로 본다.

- `IZLinkClient`는 gateway 주소가 아니라 `channel name`으로 요청한다.
- `ZLink Framework`는 그 channel 전용 outbound 경로로 직접 요청을 보낸다.
- 같은 channel 안의 여러 provider는 그 channel 안에서만 관리한다.

## 8. codec과 message model

현재 초안은 아래 구성을 가정한다.

- message = `header + body`
- body codec = `protobuf` 또는 `json`

`.NET` 표면에서는 codec 등록과 serializer 선택을 아래처럼 보이게 할 수 있다.
이때 `options.Codecs.*`는 binding core에 codec 구현을 직접 섞는다는 뜻이 아니라,
별도 codec extension/provider를 framework registry에 등록하는 흐름으로 본다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.Codecs.AddProtobuf();
    options.Codecs.AddJson();
    options.Codecs.AddMessagePack();
});
```

## 9. lifecycle 초안

`ASP.NET Core`에서는 아래 lifecycle이 중요하다.

- 앱 시작 시 runtime 부팅
- discovery 연결 수립
- handler dispatcher 시작
- 앱 종료 시 graceful shutdown

따라서 내부 구현은 `IHostedService` 또는 그와 비슷한 hosted lifecycle 모델과
잘 맞아야 한다.

## 10. 아직 확정하지 않는 것

- attribute model과 endpoint model 중 어느 쪽을 우선할지
- channel별 typed wrapper를 공식 제공할지
- 일반 서버간 쪽에서 `IZLinkClient`와 event publisher를 어떤 이름으로 확정할지
- channel runtime을 언제 만들고 언제 정리할지
- topology query surface를 운영 API로만 둘지, 일반 DI 서비스로도 열지
