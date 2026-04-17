[스펙 목차](../../../README.ko.md)

# Draft -- ZLink Framework ASP.NET Core Registry Integration

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `ASP.NET Core`에서 Registry 서버를 어떤 API로
> 드러낼지 방향을 정리한다.

## 1. 목표

`ZLink Framework`의 service discovery는 Registry 서버가 중심이다.
Registry는 서비스 등록, heartbeat, topology broadcast를 담당하며,
`Discovery`가 이 Registry에 연결해서 service view를 자동으로 갱신한다.

현재 C API와 `.NET` binding은 Registry를 두 가지 방식으로 사용할 수 있게
설계되어 있다.

1. **embedded** -- 애플리케이션 프로세스 안에서 Registry를 함께 구동한다.
2. **standalone** -- Registry만 단독 프로세스로 구동한다.

`ZLink Framework`도 이 두 방식을 그대로 지원해야 한다.
특히 `ASP.NET Core`에서는 Registry를 `IHostedService` lifecycle 안에서
자연스럽게 다룰 수 있어야 한다.

이 문서는 아래에 초점을 맞춘다.

- `ASP.NET Core` 애플리케이션에 Registry를 embedded로 올리는 방법
- Registry 설정 (heartbeat, broadcast interval, clustering)
- topology snapshot/query를 DI로 사용하는 방법
- `RegistryQueryClient`를 통한 원격 topology 조회

## 2. 기반이 되는 .NET binding

현재 하부 토대는 아래 binding 표면이다.

- `Registry` -- Registry 서버 인스턴스. `Bind(pubEndpoint, routerEndpoint)`로
  시작하고, `SetId`, `AddPeer`, `SetHeartbeat`, `SetBroadcastInterval`로 설정한다.
- `RegistryQueryClient` -- 원격 Registry에 topology를 질의하는 클라이언트.
  `Connect(endpoint)` 후 `Snapshot(filter?)`로 조회한다.

즉 이 문서의 핵심은 Registry 기능을 새로 만드는 일이 아니라,
기존 binding 기능을 `ASP.NET Core` lifecycle과 DI 안에 녹이는 방법이다.

## 3. 두 가지 배포 모델

### 3.1 embedded

애플리케이션이 Registry 서버를 자기 프로세스 안에서 함께 구동한다.
소규모 배포, 개발 환경, 또는 Registry가 특정 서비스와 자연스럽게 묶이는
경우에 적합하다.

이 모드에서 `ASP.NET Core` 애플리케이션은 아래를 동시에 하게 된다.

- Registry 서버 구동 (topology 관리, heartbeat 수신, broadcast)
- 자기 자신의 서비스 handler 처리
- 필요하면 다른 서비스로의 outbound 호출

### 3.2 standalone

Registry만 단독 프로세스로 구동한다.
운영 환경에서 Registry를 서비스 로직과 분리하고 싶을 때 적합하다.

이 모드에서도 `ASP.NET Core` 위에 올릴 수 있다. 즉 "standalone"이
반드시 console app이어야 한다는 뜻은 아니다. `ASP.NET Core` 애플리케이션이
Registry만 올리고 서비스 handler는 등록하지 않는 구성도 가능하다.

두 모드의 차이는 배포 구성의 차이이지, framework API의 차이가 아니다.
같은 `AddZLinkRegistry(...)` 등록을 쓰되, 서비스 handler를 함께 등록하느냐
마느냐가 달라질 뿐이다.

## 4. ASP.NET Core 등록 모델 초안

### 4.1 embedded 구성

서비스 handler와 Registry를 함께 올리는 가장 일반적인 구성이다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.ServiceId = "api";
    options.NodeName = "api-1";
    options.UseDiscovery(registry =>
    {
        registry.Add("tcp://127.0.0.1:5551");
    });
    options.Codecs.AddProtobuf();
});

builder.Services.AddZLinkRegistry(registry =>
{
    registry.PubEndpoint = "tcp://0.0.0.0:5550";
    registry.RouterEndpoint = "tcp://0.0.0.0:5551";
});

builder.Services.AddZLinkHandlersFromAssemblyContaining<Program>();
```

이 구성에서 중요한 점은 아래와 같다.

- `AddZLinkFramework(...)` 와 `AddZLinkRegistry(...)`는 별도 호출이다.
  Registry는 framework runtime의 일부가 아니라, 독립된 infrastructure
  컴포넌트이기 때문이다.
- `AddZLinkRegistry(...)`가 `IHostedService`를 등록한다. host 시작 시
  Registry가 bind되고, host 종료 시 자동으로 정리된다.
- 같은 프로세스 안의 `Discovery`도 이 Registry에 연결할 수 있다.
  `UseDiscovery`에서 같은 router endpoint를 가리키면 된다.

### 4.2 standalone 구성

Registry만 올리는 구성이다.

```csharp
builder.Services.AddZLinkRegistry(registry =>
{
    registry.PubEndpoint = "tcp://0.0.0.0:5550";
    registry.RouterEndpoint = "tcp://0.0.0.0:5551";
});
```

`AddZLinkFramework(...)`도 `AddZLinkHandlers...()`도 없다.
이 애플리케이션은 메시지 handler를 가지지 않고, Registry 서버만 구동한다.

필요하면 `ASP.NET Core`의 health check endpoint나 management API를
HTTP로 함께 올릴 수 있다. 이때 topology 정보를 HTTP endpoint에서
조회하는 방법은 section 7에서 다룬다.

### 4.3 왜 AddZLinkFramework와 분리하는가

Registry는 service runtime의 부속이 아니다. 오히려 service runtime이
Registry에 의존한다. 이 관계가 등록 API에도 드러나야 한다.

- `AddZLinkFramework(...)` -- 서비스 런타임. Discovery에 연결하는 쪽이다.
- `AddZLinkRegistry(...)` -- Registry 서버. Discovery가 연결하는 대상이다.

둘을 한 호출에 섞으면 embedded 전용 API로 보이기 쉽다.
분리하면 standalone과 embedded를 같은 등록 API로 다룰 수 있다.

## 5. Registry 설정

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
| `RouterEndpoint` | (필수) | 서비스 등록, heartbeat, query를 받는 ROUTER endpoint |
| `RegistryId` | 0 | Registry 클러스터에서 이 인스턴스를 식별하는 ID |
| `HeartbeatInterval` | 5000 ms | 서비스가 보내야 하는 heartbeat 주기 |
| `HeartbeatTimeout` | 15000 ms | heartbeat가 이 시간 안에 오지 않으면 서비스를 lost로 본다 |
| `BroadcastInterval` | 30000 ms | 전체 service list를 PUB으로 내보내는 주기 |

이 값들은 하부 C API의 기본값을 그대로 따른다.

### 5.2 Registry 클러스터링

운영 환경에서 Registry를 하나만 두면 단일 장애점이 된다.
C API는 `zlink_registry_add_peer()`로 peer Registry의 PUB endpoint를
추가하면, Registry끼리 topology를 동기화하는 기능을 제공한다.

framework에서는 아래처럼 설정한다.

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

이때 peer에 추가하는 주소는 상대 Registry의 **PUB endpoint**다.
각 Registry가 서로의 broadcast를 구독해서 topology를 합산하는 구조이기
때문이다.

## 6. Lifecycle 통합

### 6.1 시작 순서

`AddZLinkRegistry(...)`가 등록하는 `IHostedService`는 아래 순서를 따른다.

1. `Context` 생성
2. `Registry` 인스턴스 생성
3. 설정 적용 (`SetId`, `SetHeartbeat`, `SetBroadcastInterval`, `AddPeer`)
4. `Bind(pubEndpoint, routerEndpoint)` 호출

embedded 구성에서 `AddZLinkFramework(...)`의 hosted service가 Discovery를
연결하기 전에 Registry가 먼저 bind되어 있어야 한다. framework는 이 순서를
보장해야 한다.

### 6.2 종료 순서

host shutdown 시 아래 순서를 따른다.

1. service runtime shutdown (handler dispatcher 종료, outbound channel 정리)
2. Registry shutdown (`Registry.Dispose()`)
3. `Context` 정리

서비스가 먼저 내려간 뒤 Registry가 내려가야, 다른 노드의 Discovery가
이 서비스가 사라졌음을 감지할 수 있다.

## 7. Topology 조회 API

### 7.1 in-process 조회

Registry를 embedded로 올린 경우, 같은 프로세스에서 topology를 직접 조회할 수
있어야 한다.

```csharp
builder.Services.AddZLinkRegistry(registry =>
{
    registry.PubEndpoint = "tcp://0.0.0.0:5550";
    registry.RouterEndpoint = "tcp://0.0.0.0:5551";
});
```

이렇게 등록하면 framework는 `IZLinkRegistryQuery`를 DI에 함께 등록한다.

```csharp
public interface IZLinkRegistryQuery
{
    RegistryStatus StatusSnapshot();
    RegistryServiceSummaryEntry[] ServiceSummarySnapshot(
        RegistryServiceSummaryFilter? filter = null);
    RegistryTopologyEntry[] TopologySnapshot();
    RegistryTopologyEntry[] TopologyQuery(
        RegistryTopologyFilter? filter = null);
    MemberPeerEntry[] MemberPeers(
        ServiceType serviceType, string serviceName);
}
```

이 인터페이스는 하부 `Registry` 클래스의 snapshot/query 메서드를 그대로
노출한다. 운영 점검, warm-up 확인, 관리 화면 등에서 사용한다.

HTTP endpoint에서 topology를 조회하는 예시는 아래와 같다.

```csharp
app.MapGet("/admin/topology", (IZLinkRegistryQuery registry) =>
{
    var entries = registry.TopologySnapshot();
    return Results.Ok(entries);
});

app.MapGet("/admin/services", (IZLinkRegistryQuery registry) =>
{
    var summary = registry.ServiceSummarySnapshot();
    return Results.Ok(summary);
});

app.MapGet("/admin/registry/status", (IZLinkRegistryQuery registry) =>
{
    var status = registry.StatusSnapshot();
    return Results.Ok(status);
});
```

### 7.2 원격 조회

Registry가 다른 프로세스에서 구동될 때는 `RegistryQueryClient`로 원격 조회한다.

framework에서는 아래처럼 등록한다.

```csharp
builder.Services.AddZLinkRegistryQueryClient(query =>
{
    query.Endpoint = "tcp://registry-1:5551";
});
```

이렇게 등록하면 `IZLinkRegistryQueryClient`를 DI로 주입받을 수 있다.

```csharp
public interface IZLinkRegistryQueryClient
{
    RegistryTopologyEntry[] Snapshot(
        RegistryTopologyFilter? filter = null);
}
```

HTTP endpoint에서 원격 topology를 조회하는 예시는 아래와 같다.

```csharp
app.MapGet("/admin/topology", (IZLinkRegistryQueryClient query) =>
{
    var entries = query.Snapshot();
    return Results.Ok(entries);
});
```

### 7.3 in-process와 원격 조회의 차이

| 항목 | `IZLinkRegistryQuery` | `IZLinkRegistryQueryClient` |
|------|----------------------|---------------------------|
| 대상 | 같은 프로세스의 embedded Registry | 다른 프로세스의 Registry |
| 등록 | `AddZLinkRegistry(...)` 시 자동 등록 | `AddZLinkRegistryQueryClient(...)` 로 별도 등록 |
| 제공 API | status, service summary, topology, member peers | topology snapshot만 |
| 네트워크 | 없음 (in-process 호출) | ROUTER endpoint로 요청 |

`RegistryQueryClient`가 제공하는 API가 in-process보다 좁은 이유는
하부 C API(`zlink_registry_query_snapshot`)가 topology snapshot만
지원하기 때문이다.

## 8. 전체 구성 예시

### 8.1 embedded: Registry + 서비스를 한 프로세스에서

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
    options.ServiceId = "api";
    options.NodeName = "api-1";
    options.UseDiscovery(discovery =>
    {
        discovery.Add("tcp://127.0.0.1:5551");
    });
    options.Codecs.AddProtobuf();
});
builder.Services.AddZLinkHandlersFromAssemblyContaining<Program>();

var app = builder.Build();

// --- 관리 endpoint ---
app.MapGet("/admin/topology", (IZLinkRegistryQuery registry) =>
{
    return Results.Ok(registry.TopologySnapshot());
});
app.MapGet("/admin/registry/status", (IZLinkRegistryQuery registry) =>
{
    return Results.Ok(registry.StatusSnapshot());
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
app.MapGet("/health", (IZLinkRegistryQuery registry) =>
{
    var status = registry.StatusSnapshot();
    return status.State == RegistryState.Active
        ? Results.Ok(status)
        : Results.StatusCode(503);
});
app.MapGet("/admin/topology", (IZLinkRegistryQuery registry) =>
{
    return Results.Ok(registry.TopologySnapshot());
});
app.MapGet("/admin/services", (IZLinkRegistryQuery registry) =>
{
    return Results.Ok(registry.ServiceSummarySnapshot());
});

app.Run();
```

### 8.3 원격 조회: 다른 서비스에서 Registry topology 조회

```csharp
var builder = WebApplication.CreateBuilder(args);

builder.Services.AddZLinkFramework(options =>
{
    options.ServiceId = "dashboard";
    options.NodeName = "dashboard-1";
    options.UseDiscovery(discovery =>
    {
        discovery.Add("tcp://registry-1:5551");
    });
});
builder.Services.AddZLinkRegistryQueryClient(query =>
{
    query.Endpoint = "tcp://registry-1:5551";
});

var app = builder.Build();

app.MapGet("/admin/topology", (IZLinkRegistryQueryClient query) =>
{
    return Results.Ok(query.Snapshot());
});

app.Run();
```

## 9. 아직 확정하지 않는 것

- `AddZLinkRegistry`의 `IHostedService` startup 순서를 framework가 자동으로
  보장할지, 사용자가 등록 순서로 제어할지
- Registry health check를 `ASP.NET Core`의 `IHealthCheck` 인터페이스로
  자동 등록할지
- embedded 구성에서 `UseDiscovery`가 같은 프로세스의 Registry를 자동으로
  감지할지, 명시적으로 endpoint를 적어야 할지
- `IZLinkRegistryQuery`와 `IZLinkRegistryQueryClient`를 공용 인터페이스로
  묶을지
- topology 변경 이벤트를 callback이나 `IObservable`로 노출할지
- `RegistryQueryClient`의 연결 실패 시 retry 정책을 framework가 제공할지
