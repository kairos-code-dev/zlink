<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework ASP.NET Core Monitoring](aspnet-core-monitoring.ko.md) | [다음: ZLink Framework .NET Channel Messaging Samples](../guide/samples/channel-messaging-samples.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../common/README.ko.md)

[.NET 묶음](../README.ko.md) | [인터페이스](handler-interfaces.ko.md) | [channel](aspnet-core-channel-messaging.ko.md) | [SPOT](aspnet-core-spot.ko.md) | [STREAM](aspnet-core-stream.ko.md)

# ZLink Framework ASP.NET Core Registry Integration

## 1. 목표

이 절은 Registry 가 framework 에서 어떤 역할을 맡는지, 그리고 이 문서가 그중
어디까지 다루는지를 정리한다.

`ZLink Framework` 의 channel discovery 는 Registry 서버를 중심에 두고 동작한다.
Registry 는 channel 등록, heartbeat[^heartbeat], topology[^topology] broadcast 를
담당한다. `Discovery`[^discovery] 는 이 Registry 에 붙어서 자신의 channel view
를 자동으로 갱신해 나간다.

현재 C API 와 `.NET` binding 은 Registry 를 두 가지 방식으로 활용할 수 있도록
설계되어 있다.

1. **embedded**[^embedded] -- 애플리케이션 프로세스 안에 Registry 를 함께 띄우는
   방식이다.
2. **standalone**[^standalone] -- Registry 만 따로 떼어 단독 프로세스로 띄우는
   방식이다.

`ZLink Framework` 역시 이 두 가지 방식을 모두 지원해야 한다. 특히 `ASP.NET Core`
위에서는 Registry 를 `IHostedService`[^hosted-service] lifecycle 에 자연스럽게
녹여 다룰 수 있어야 한다.

이 문서가 다루는 범위는 다음과 같다.

- `ASP.NET Core` 애플리케이션에 Registry를 embedded 방식으로 올리는 방법
- Registry의 동작을 결정하는 설정값(heartbeat, broadcast 주기, clustering)
- topology snapshot/query 표면을 DI[^di]로 사용하는 방법
- `RegistryQueryClient`를 통해 원격 Registry의 topology를 조회하는 방법

## 2. 기반이 되는 .NET binding

이 절은 framework 가 위에 얹는 하부 binding 표면을 정리한다.

이 문서의 바탕이 되는 하부 binding 표면은 다음과 같다.

- `Registry` -- Registry 서버 인스턴스를 나타낸다. `Bind(pubEndpoint,
  routerEndpoint)` 로 서버를 띄운다. 그리고 `SetId`, `AddPeer`, `SetHeartbeat`,
  `SetBroadcastInterval` 로 동작을 조정한다.
- `RegistryQueryClient` -- 원격 Registry 에 topology 를 묻는 클라이언트다.
  `Connect(endpoint)` 로 접속한 뒤 `TopologyAsync(filter?)` 를 통해 조회한다.

즉 이 문서가 새로 만드는 Registry 기능은 없다. 이미 존재하는 binding 표면을
`ASP.NET Core` 의 lifecycle 과 DI 안에 자연스럽게 녹여 내는 일에 초점이 있다.

## 3. 두 가지 배포 모델

이 절은 Registry 를 어떻게 배포할 수 있는지를 정리한다.

### 3.1 embedded

애플리케이션이 자기 프로세스 안에서 Registry 서버를 함께 구동하는 모델이다.
다음과 같은 경우에 잘 맞는다.

- 소규모 배포
- 개발 환경
- Registry 가 특정 서비스와 자연스럽게 한 묶음으로 다뤄지는 경우

이 모드에서 `ASP.NET Core` 애플리케이션은 다음 세 가지 일을 동시에 수행한다.

- Registry 서버 구동(topology 관리, heartbeat 수신, broadcast)
- 자기 자신의 서비스 handler 처리
- 필요한 경우 다른 서비스로의 outbound 호출

### 3.2 standalone

Registry 만 단독 프로세스로 띄우는 모델이다. 운영 환경에서 Registry 를 서비스
로직과 명확히 분리하고 싶을 때 적합하다.

이 모드라고 해서 반드시 console app 이어야 하는 것은 아니다. `ASP.NET Core`
애플리케이션이 Registry 만 올리고 서비스 handler 는 전혀 등록하지 않는 형태로
구성할 수도 있다.

두 모델의 차이는 배포 구성의 차이일 뿐이다. framework API 자체가 달라지지는
않는다. 즉 같은 `AddZLinkRegistry(...)` 호출을 그대로 쓰되, 서비스 handler 를
함께 등록하느냐 마느냐에 따라 모델이 갈릴 뿐이다.

## 4. ASP.NET Core 등록 모델

이 절은 두 배포 모델을 각각 `AddZLinkRegistry(...)` 로 어떻게 표현하는지를
정리한다.

### 4.1 embedded 구성

서비스 handler 와 Registry 를 한 프로세스에 함께 올리는, 가장 일반적인 형태다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    {
        var channel =     options.AddClientServerChannel("api");
        channel.EnableServer("tcp://0.0.0.0:7101");

    }
        options.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:5551");
    options.Codecs.Use(ZLinkProtobufCodec.Default);
    options.AddHandlersFromAssemblyOf<Program>();
});

builder.Services.AddZLinkRegistry(registry =>
{
    registry.PubEndpoint = "tcp://0.0.0.0:5550";
    registry.RouterEndpoint = "tcp://0.0.0.0:5551";
});

```

이 구성에서 눈여겨봐야 할 점은 다음과 같다.

- `AddZLinkFramework(...)` 와 `AddZLinkRegistry(...)` 는 별개의 호출이다. Registry
  는 framework runtime 의 일부가 아니다. 그와 분리된 독립적인 infrastructure
  컴포넌트이기 때문이다.
- `AddZLinkRegistry(...)` 는 내부에서 `IHostedService` 를 등록한다. host 가
  시작되면 Registry 가 bind 되고, host 가 종료될 때 자동으로 정리된다.
- 같은 프로세스 안에 있는 `Discovery` 도 이 Registry 에 그대로 연결할 수 있다.
  `UseDiscovery` 에 동일한 router endpoint 를 가리키도록 적어 주면 된다.

### 4.2 standalone 구성

Registry 만 띄우는 구성이다.

```csharp
builder.Services.AddZLinkRegistry(registry =>
{
    registry.PubEndpoint = "tcp://0.0.0.0:5550";
    registry.RouterEndpoint = "tcp://0.0.0.0:5551";
});
```

이 구성에는 `AddZLinkFramework(...)` 도 `AddZLinkHandlers...()` 도 보이지 않는다.
즉 이 애플리케이션은 메시지 handler 를 전혀 가지지 않는다. 오직 Registry 서버만
구동한다.

필요하다면 여기에 `ASP.NET Core` 의 health check endpoint 나 management API 를
HTTP 로 함께 얹어도 된다. 이때 topology 정보를 HTTP endpoint 로 노출하는 방법은
7장에서 다룬다.

### 4.3 왜 AddZLinkFramework 와 분리하는가

이 절은 Registry 등록과 framework 등록을 한 호출로 묶지 않은 이유를 정리한다.

Registry 는 channel runtime 의 부속물이 아니다. 오히려 반대로 channel runtime 이
Registry 에 의존하는 구조다. 이 의존 방향은 등록 API 표면에도 그대로 드러나야
한다.

- `AddZLinkFramework(...)` -- channel runtime 이다. Discovery 를 통해 Registry
  에 연결해 가는 쪽이다.
- `AddZLinkRegistry(...)` -- Registry 서버다. Discovery 가 연결을 맺으러 오는
  쪽이다.

이 둘을 하나의 호출로 묶어 버리면 자칫 embedded 전용 API 처럼 보이기 쉽다.
분리해 두면 standalone 과 embedded 를 같은 등록 API 로 일관되게 다룰 수 있다.

## 5. Registry 설정

이 절은 Registry 동작을 결정하는 주요 설정값을 정리한다.

### 5.1 기본 설정

```csharp
builder.Services.AddZLinkRegistry(registry =>
{
    registry.PubEndpoint = "tcp://0.0.0.0:5550";
    registry.RouterEndpoint = "tcp://0.0.0.0:5551";
    registry.RegistryId = 1;
    registry.HeartbeatInterval = TimeSpan.FromSeconds(5);
    registry.HeartbeatTimeout = TimeSpan.FromSeconds(15);
    registry.BroadcastInterval = TimeSpan.FromSeconds(30);
});
```

| 설정 | 기본값 | 설명 |
|------|--------|------|
| `PubEndpoint` | (필수) | topology broadcast를 내보내는 PUB endpoint |
| `RouterEndpoint` | (필수) | 서비스 등록, heartbeat, query 요청을 받는 ROUTER endpoint |
| `RegistryId` | 0 | Registry 클러스터[^cluster] 안에서 이 인스턴스를 식별하는 ID |
| `HeartbeatInterval` | 5000 ms | 서비스가 보내야 하는 heartbeat 주기 |
| `HeartbeatTimeout` | 15000 ms | heartbeat가 이 시간 안에 들어오지 않으면 서비스를 lost 상태로 본다 |
| `BroadcastInterval` | 30000 ms | 전체 service list를 PUB으로 내보내는 주기 |

위 값들은 하부 C API 가 정해 둔 기본값을 그대로 따라간다.

### 5.2 Registry 클러스터링

운영 환경에서 Registry 를 하나만 두면 그 자체가 단일 장애점이 되어 버린다. C
API 는 `zlink_registry_add_peer()` 로 peer Registry 의 PUB endpoint 를 등록해
두는 방식을 제공한다. 이렇게 두면 Registry 끼리 서로의 topology 를 동기화할 수
있다.

framework 에서는 다음과 같이 설정한다.

```csharp
builder.Services.AddZLinkRegistry(registry =>
{
    registry.PubEndpoint = "tcp://0.0.0.0:5550";
    registry.RouterEndpoint = "tcp://0.0.0.0:5551";
    registry.RegistryId = 1;
    registry.AddPeer("tcp://registry-2:5550");
    registry.AddPeer("tcp://registry-3:5550");
});
```

여기서 peer 로 추가하는 주소는 상대 Registry 의 **PUB endpoint** 다. 각 Registry
가 서로의 broadcast 를 구독하면서 topology 를 합산해 가는 구조이기 때문이다.

## 6. Lifecycle 통합

이 절은 Registry 가 `ASP.NET Core` host 의 시작 / 종료와 어떻게 맞물리는지를
정리한다.

### 6.1 시작 순서

`AddZLinkRegistry(...)` 가 등록하는 `IHostedService` 는 다음 순서로 시작한다.

1. `Context` 생성
2. `Registry` 인스턴스 생성
3. 설정 적용(`SetId`, `SetHeartbeat`, `SetBroadcastInterval`, `AddPeer`)
4. `Bind(pubEndpoint, routerEndpoint)` 호출

embedded 구성에서는 `AddZLinkFramework(...)` 의 hosted service 가 Discovery 연결
을 맺기 전에, Registry 가 먼저 bind 되어 있어야 한다. framework 는 이 순서를
자동으로 보장해 준다.

### 6.2 종료 순서

host shutdown 시에는 다음 순서를 따른다.

1. channel runtime shutdown(handler dispatcher 종료, outbound channel 정리)
2. Registry shutdown(`Registry.Dispose()`)
3. `Context` 정리

서비스가 먼저 내려간 뒤에 Registry 가 내려가야 한다. 그래야 다른 노드의
Discovery 가 이 서비스의 소멸을 정상적으로 감지할 수 있다.

## 7. Topology 조회 API

이 절은 Registry topology 를 조회하는 두 가지 표면, 즉 in-process 조회와 원격
조회를 정리한다.

### 7.1 in-process 조회

Registry 를 embedded 로 띄운 경우에는, 같은 프로세스 안에서 topology 를 직접
조회할 수 있어야 한다.

```csharp
builder.Services.AddZLinkRegistry(registry =>
{
    registry.PubEndpoint = "tcp://0.0.0.0:5550";
    registry.RouterEndpoint = "tcp://0.0.0.0:5551";
});
```

이렇게 등록하면 framework 가 `IZLinkRegistryQuery` 를 DI 컨테이너에 함께 등록해
둔다.

```csharp
public interface IZLinkRegistryQuery
{
    ValueTask<ZLinkRegistryStatus> StatusAsync(
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkRegistryServiceSummaryEntry[]> ServiceSummaryAsync(
        ZLinkRegistryServiceSummaryFilter? filter = null,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkRegistryTopologyEntry[]> TopologyAsync(
        ZLinkRegistryTopologyFilter? filter = null,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkMemberPeerEntry[]> MemberPeersAsync(
        string channelName,
        CancellationToken cancellationToken = default);
}
```

이 인터페이스는 하부 `Registry` 클래스가 가진 snapshot / query 메서드를 그대로
노출하는 표면이다. framework 가 host lifecycle 과 startup ownership 을 직접
관리하기 때문에, query 표면도 그 경계를 숨기지 않도록 비동기로 맞춰 두었다.
운영 점검, warm-up 확인, 관리 화면 등에서 활용한다.

HTTP endpoint 에서 topology 를 조회하는 예시는 다음과 같다.

```csharp
app.MapGet("/admin/topology", async (IZLinkRegistryQuery registry) =>
{
    var entries = await registry.TopologyAsync();
    return Results.Ok(entries);
});

app.MapGet("/admin/services", async (IZLinkRegistryQuery registry) =>
{
    var summary = await registry.ServiceSummaryAsync();
    return Results.Ok(summary);
});

app.MapGet("/admin/registry/status", async (IZLinkRegistryQuery registry) =>
{
    var status = await registry.StatusAsync();
    return Results.Ok(status);
});
```

### 7.2 원격 조회

Registry 가 다른 프로세스에서 동작하는 경우에는 `RegistryQueryClient` 로 원격
조회를 수행한다.

framework 에서는 다음과 같이 등록한다.

```csharp
builder.Services.AddZLinkRegistryQueryClient(query =>
{
    query.Endpoint = "tcp://registry-1:5551";
});
```

이렇게 등록해 두면 `IZLinkRegistryQueryClient` 를 DI 를 통해 주입받아 쓸 수
있다.

```csharp
public interface IZLinkRegistryQueryClient
{
    ValueTask<ZLinkRegistryTopologyEntry[]> TopologyAsync(
        ZLinkRegistryTopologyFilter? filter = null,
        CancellationToken cancellationToken = default);
}
```

HTTP endpoint 에서 원격 topology 를 조회하는 예시는 다음과 같다.

```csharp
app.MapGet("/admin/topology", async (IZLinkRegistryQueryClient query) =>
{
    var entries = await query.TopologyAsync();
    return Results.Ok(entries);
});
```

### 7.3 in-process 와 원격 조회의 차이

이 절은 두 조회 표면이 어떤 점에서 다른지를 한 표로 정리한다.

| 항목 | `IZLinkRegistryQuery` | `IZLinkRegistryQueryClient` |
|------|----------------------|---------------------------|
| 대상 | 같은 프로세스 안의 embedded Registry | 다른 프로세스에 떠 있는 Registry |
| 등록 | `AddZLinkRegistry(...)` 호출 시 자동 등록 | `AddZLinkRegistryQueryClient(...)`로 별도 등록 |
| 제공 API | status, service summary, topology, member peers | topology snapshot만 |
| 네트워크 | 없음(in-process 호출) | ROUTER endpoint로 요청 전송 |

`RegistryQueryClient` 가 제공하는 API 폭이 in-process 보다 좁은 이유가 있다. 하부
C API 인 `zlink_registry_query_client_topology` 이 topology snapshot 만 지원하기
때문이다.

## 8. 전체 구성 예시

이 절은 앞에서 본 등록 모델을 그대로 묶은 전체 예시를 보여 준다.

### 8.1 embedded: Registry 와 서비스를 한 프로세스에 함께

```csharp
var builder = WebApplication.CreateBuilder(args);

// --- Registry 서버 ---
builder.Services.AddZLinkRegistry(registry =>
{
    registry.PubEndpoint = "tcp://0.0.0.0:5550";
    registry.RouterEndpoint = "tcp://0.0.0.0:5551";
    registry.RegistryId = 1;
    registry.HeartbeatInterval = TimeSpan.FromSeconds(5);
    registry.HeartbeatTimeout = TimeSpan.FromSeconds(15);
    registry.BroadcastInterval = TimeSpan.FromSeconds(30);
});

// --- 서비스 런타임 ---
builder.Services.AddZLinkFramework(options =>
{
    {
        var channel =     options.AddClientServerChannel("api");
        channel.EnableServer("tcp://0.0.0.0:7101");

    }
        options.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:5551");
    options.Codecs.Use(ZLinkProtobufCodec.Default);
    options.AddHandlersFromAssemblyOf<Program>();
});

var app = builder.Build();

// --- 관리 endpoint ---
app.MapGet("/admin/topology", async (IZLinkRegistryQuery registry) =>
{
    return Results.Ok(await registry.TopologyAsync());
});
app.MapGet("/admin/registry/status", async (IZLinkRegistryQuery registry) =>
{
    return Results.Ok(await registry.StatusAsync());
});

app.Run();
```

### 8.2 standalone: Registry 전용 프로세스

```csharp
var builder = WebApplication.CreateBuilder(args);

// --- Registry 서버만 ---
builder.Services.AddZLinkRegistry(registry =>
{
    registry.PubEndpoint = "tcp://0.0.0.0:5550";
    registry.RouterEndpoint = "tcp://0.0.0.0:5551";
    registry.RegistryId = 1;
    registry.AddPeer("tcp://registry-2:5550");
    registry.AddPeer("tcp://registry-3:5550");
});

var app = builder.Build();

// --- 관리 endpoint ---
app.MapGet("/health", async (IZLinkRegistryQuery registry) =>
{
    var status = await registry.StatusAsync();
    return status.State == ZLinkRegistryState.Active
        ? Results.Ok(status)
        : Results.StatusCode(503);
});
app.MapGet("/admin/topology", async (IZLinkRegistryQuery registry) =>
{
    return Results.Ok(await registry.TopologyAsync());
});
app.MapGet("/admin/services", async (IZLinkRegistryQuery registry) =>
{
    return Results.Ok(await registry.ServiceSummaryAsync());
});

app.Run();
```

### 8.3 원격 조회: 다른 서비스에서 Registry topology 조회

```csharp
var builder = WebApplication.CreateBuilder(args);

builder.Services.AddZLinkRegistryQueryClient(query =>
{
    query.Endpoint = "tcp://registry-1:5551";
});

var app = builder.Build();

app.MapGet("/admin/topology", async (IZLinkRegistryQueryClient query) =>
{
    return Results.Ok(await query.TopologyAsync());
});

app.Run();
```

### 8.4 Registry 기반 route 기본 구현 사용

Registry 를 사용하는 actor/Spot route 샘플은 별도 파일 metadata store 를 만들지 않고
framework 의 Registry 기반 기본 구현을 사용한다. session 위치는 Registry 로 검색하지
않고 session bind 시 actor runtime state 에 저장한다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
        options.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:5551");

    {
        var channel =     options.AddRouteMesh("play");
        channel.Bind("tcp://0.0.0.0:7201");

    }

    options.UseRegistrySpotRemoteAddresses("game");
});
```

`UseRegistrySpotRemoteAddresses(...)` 는 Spot owner 조회와 Spot RID directory 를 함께
등록한다. actor-session route 는 session bind 시 actor runtime state 에 저장된다.

이 API 들은 Registry 를 일반 key-value 저장소처럼 노출하지 않는다. framework 는
Discovery 가 제공하는 owner-bound route/topology 를 사용하고, application 은 route key 나
payload 형식을 알 필요가 없다. Redis 나 database 같은 별도 저장소가 필요한 경우에만
`AddSpotRemoteAddressResolver<TResolver>()` 로 custom resolver 를 등록해 SPOT owner route
조회를 그 저장소로 위임한다.


## 9. 결정된 기준

이 절은 Registry 표면이 따르는 고정된 결정 사항을 모아둔 것이다.

- `AddZLinkRegistry(...)` 와 `AddZLinkFramework(...)` 를 함께 사용한 경우,
  framework 가 Registry hosted service 를 먼저 bind 하도록 startup 순서를
  자동으로 맞춰 준다.
- Registry 용 health check 는 `IHealthCheck` 로 자동 등록하지 않는다. health
  endpoint 가 필요하다면, 응용 측에서 `IZLinkRegistryQuery` 를 사용해 명시적으로
  노출하는 것을 기본으로 본다.
- embedded 구성이라 해도 `UseDiscovery().AddRegistryEndpoint(...)` 가 같은 프로세스의 Registry 를
  자동으로 찾아 주지는 않는다. Discovery endpoint 는 문서와 설정에 분명히
  드러나도록 명시적으로 적는다.
- Registry 기반 route 기본 구현은 `UseDiscovery().AddRegistryEndpoint(...)` 와 별개로 명시적으로 켠다.
  `UseDiscovery().AddRegistryEndpoint(...)` 만으로 actor remote address resolver, Spot remote address resolver,
  actor-session route 저장소는 public API 로 제공하지 않는다.
- `IZLinkRegistryQuery` 와 `IZLinkRegistryQueryClient` 는 하나로 묶지 않는다.
- topology 변경 알림은 `IObservable` 보다 framework 의 일반 handler / callback
  표면 위로 올리는 쪽을 기본 방향으로 본다.
- `RegistryQueryClient` 는 연결 실패 시 framework 가 몰래 retry 를 끼워 넣지
  않는다. retry 가 필요하다면 호출자나 monitoring 계층에서 명시적으로 정책을
  정한다.

## 10. 회귀 테스트

이 절은 Registry 표면이 어떤 테스트로 회귀를 막는지를 정리한다.

Registry 문서의 항목은 다음이 모두 유지되어야 한다.

- embedded / standalone startup
- in-process query
- remote query
- framework topology 노출

Registry 가 framework 보다 먼저 시작되어야 한다는 순서 또한 회귀 기준에
포함된다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `RegistryAndMonitoringTests.AddZLinkRegistry_Throws_WhenPubEndpointIsMissing` | Registry pub endpoint 누락은 startup validation 예외로 드러난다. |
| `RegistryAndMonitoringTests.AddZLinkRegistry_Throws_WhenRouterEndpointIsMissing` | Registry router endpoint 누락은 startup validation 예외로 드러난다. |
| `HostTests.Host_Starts_EmbeddedRegistry_Before_FrameworkRuntime` | embedded Registry가 framework runtime보다 먼저 시작된다. |
| `EmbeddedRegistryTests.EmbeddedRegistry_Query_Service_Resolves_And_Reads_Status` | `IZLinkRegistryQuery`가 DI에서 resolve되고 status snapshot을 읽어 온다. |
| `EmbeddedRegistryTests.RemoteRegistryQueryClient_Can_Read_Topology_Snapshot` | 별도 host의 query client가 remote topology snapshot을 정상 조회한다. |
| `TopologyTests.RemoteRegistryQueryClient_Reads_FrameworkTopology_From_TestHostProcesses` | 여러 프로세스 구성에서도 framework topology 조회가 성공한다. |

[^public-contract]: public contract는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 뜻한다.
[^heartbeat]: heartbeat는 서비스가 자신이 살아 있음을 일정 주기로 Registry에 알리는 신호다. 일정 시간 안에 도착하지 않으면 그 서비스는 lost 상태로 간주된다.
[^topology]: topology는 어떤 노드(channel, spot, registry 등)가 어디에 있는지, 그리고 서로 어떻게 연결되어 있는지를 나타내는 구성 정보다.
[^discovery]: Discovery는 Registry에 붙어 채널/노드 정보를 받아 와 자신의 view를 갱신해 두는 클라이언트 측 구성 요소다.
[^embedded]: embedded 모델은 Registry를 별도 프로세스로 두지 않고 애플리케이션 프로세스 안에 함께 띄우는 배포 방식이다.
[^standalone]: standalone 모델은 Registry를 서비스 로직과 분리해 단독 프로세스로 띄우는 배포 방식이다.
[^hosted-service]: hosted service는 `ASP.NET Core` 호스트가 시작·종료될 때 함께 시작·종료되는 백그라운드 컴포넌트를 가리킨다(`IHostedService`).
[^di]: DI(Dependency Injection)는 객체가 필요한 의존성을 직접 만들지 않고 외부에서 주입받도록 하는 패턴이며, `ASP.NET Core`의 표준 서비스 컨테이너가 이를 담당한다.
[^cluster]: cluster는 여러 Registry 인스턴스가 서로 topology를 동기화하면서 함께 동작하는 묶음을 가리킨다. 단일 장애점을 피하기 위해 운영 환경에서 자주 쓴다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework ASP.NET Core Monitoring](aspnet-core-monitoring.ko.md) | [다음: ZLink Framework .NET Channel Messaging Samples](../guide/samples/channel-messaging-samples.ko.md)
<!-- framework-adapter-nav:bottom:end -->
