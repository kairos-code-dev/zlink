<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: Monitoring — runtime 이벤트](11-monitoring.ko.md) | [다음: 인터페이스 카탈로그](13-interface-catalog.ko.md)
<!-- framework-adapter-nav:end -->

# 12. 운영 — 런타임 메트릭 · graceful drain · readiness

> 정식 계약은 공통 스펙 [런타임 메트릭](../../spec/server/51-runtime-metrics.ko.md)과
> [Graceful Drain & Handoff](../../spec/server/54-graceful-drain-handoff.ko.md)가 다룬다.
> `.NET` 표면의 정식 정의는 [spec/aspnet-core-monitoring §10·§12](../../spec/server/languages/dotnet/01-system-structure.ko.md)다.
> 이 챕터는 운영 환경에서 실제로 무엇을 붙이고 무엇을 선언하는지 사용법 중심으로 다룬다.

## 0. 무엇을 해주는가

서비스를 운영에 올리면 [11-monitoring](11-monitoring.ko.md)의 이벤트 관측 외에 세 가지가
더 필요하다.

1. **메트릭** — CCU, 큐 깊이, 요청 지연 같은 수치를 대시보드로 본다.
2. **graceful drain** — 배포·축소로 노드를 내릴 때 접속 유저를 튕기지 않고 정리한다.
3. **readiness** — "이 노드가 새 요청을 받아도 되는가"를 배포 인프라에 알린다.

framework는 메트릭 계기와 host 종료 시의 drain 절차를 제공한다. 앱은 meter 이름을 수집
파이프라인에 넣고, 배포 환경이 호출할 readiness endpoint를 공개 런타임 조회 API로 구성한다.

처음 나오는 용어는 다음과 같다.

| 용어 | 한 줄 풀이 |
|---|---|
| Meter / 계기(instrument) | .NET 표준 메트릭 방출 단위. counter·gauge·histogram이 계기다 |
| OpenTelemetry(OTel) | 메트릭·트레이스 수집 표준. Prometheus 등 exporter로 내보낸다 |
| Retire | continuity를 target으로 relocation한 뒤 host를 종료하는 operation |
| Shutdown | 새 relocation 없이 local resource를 bounded cleanup하는 operation |
| readiness probe | "새 요청을 받아도 되는가"를 묻는 배포 인프라의 상태 확인 |

## 1. 런타임 메트릭

framework는 `"zlink.framework"`라는 이름의 `System.Diagnostics.Metrics.Meter` 하나로
모든 계기를 방출한다. 앱은 이 정식 meter 이름을 수집 파이프라인에 등록한다.

```csharp
// 이 한 줄로 zlink 계기 전체가 앱의 OTel 파이프라인에 들어간다.
builder.Services.AddOpenTelemetry().WithMetrics(m => m
    .AddMeter("zlink.framework") // Framework가 계기를 방출하는 정식 meter 이름이다.
    .AddPrometheusExporter());
```

- zlink 전용 메트릭 API는 없다. `.NET` 표준 `Meter`/`MeterListener`가 그대로 표면이다.
  OTel 없이 수집하려면 `MeterListener`에서 meter 이름 `"zlink.framework"`를 직접 구독한다.
- 어떤 listener도 붙지 않으면 계기 갱신은 최소 비용의 비활성 경로로 끝난다. 계기를
  등록만 해 두고 켜지 않아도 messaging 성능에 영향이 없다.
- 대시보드와 exporter 선택은 앱 몫이다. framework는 내장 scrape 서버를 두지 않는다.

계기 카탈로그는 다음과 같다. MeshNode, object·STREAM, location·fanout 계기의 라벨·단위·종류는
[Runtime Metrics §§3~5](../../spec/server/51-runtime-metrics.ko.md)가 정하고, drain 계기는
[Graceful Drain §9](../../spec/server/54-graceful-drain-handoff.ko.md#9-observability-identifiers)가 정한다.

| 계기 | 무엇을 재나 |
|---|---|
| `zlink.stream.connections.active` | 활성 STREAM 연결 수(CCU) |
| `zlink.stream.connections.opened` | 누적 STREAM 연결 시작 수 |
| `zlink.stream.connections.closed` | 누적 STREAM 연결 종료 수 |
| `zlink.spot.count` | 활성 spot 수 |
| `zlink.spot.queue.depth` | Spot application queue의 pending work 수 |
| `zlink.spot.queue.wait.duration` | Spot work admission부터 turn 시작까지의 시간 |
| `zlink.actor.count` | 활성 Actor 수 |
| `zlink.actor.queue.depth` | Actor application queue의 pending payload 수 |
| `zlink.actor.queue.wait.duration` | Actor payload admission부터 turn 시작까지의 시간 |
| `zlink.relocation.started` | Actor·User·Instance Spot relocation 시작 누계 |
| `zlink.relocation.completed` | relocation terminal 결과 누계 |
| `zlink.relocation.duration` | prepare부터 terminal phase까지의 시간 |
| `zlink.relocation.recovered` | recovery coordinator가 이어서 처리한 relocation 수 |
| `zlink.relocation.journal.messages` | relocation root에 포함한 accepted message 수 |
| `zlink.relocation.bytes` | immutable relocation envelope 크기 |
| `zlink.instance_spot.activations` | Instance Spot activation 결과 누계 |
| `zlink.instance_spot.activation.duration` | 첫 주소 확인부터 Ready 또는 terminal 실패까지의 시간 |
| `zlink.instance_spot.pending.messages` | activation barrier 앞에서 기다리는 message 수 |
| `zlink.instance_spot.pending.bytes` | activation barrier 앞에서 예약한 payload byte 수 |
| `zlink.instance_spot.claim.conflicts` | Instance location claim 충돌 누계 |
| `zlink.mesh_node.peers.configured` | descriptor에 존재하는 peer 수 |
| `zlink.mesh_node.peers.connected` | transport가 연결된 peer 수 |
| `zlink.mesh_node.peers.ready` | admission과 handler readiness를 통과한 peer 수 |
| `zlink.mesh_node.channels.ready_members` | ChannelName select-one에 사용할 수 있는 member 수 |
| `zlink.mesh_node.channel.selections` | ChannelName select-one 결과 누계 |
| `zlink.mesh_node.requests.inflight` | reply를 기다리는 request 수 |
| `zlink.mesh_node.request.duration` | request submit부터 terminal completion까지의 시간 |
| `zlink.mesh_node.request.timeouts` | request timeout 누계 |
| `zlink.mesh_node.messages.dropped` | Framework가 원인을 확인한 one-way drop 누계 |
| `zlink.mesh_node.claim.queue.depth` | owner application·infrastructure pending work 수 |
| `zlink.mesh_node.claim.active` | 현재 실행 중인 claim 수 |
| `zlink.mesh_node.claim.wait.duration` | ready부터 claim 획득까지의 시간 |
| `zlink.mesh_node.turn.duration` | application turn 실행 시간 |
| `zlink.fanout.published` | classic fanout publish 누계 |
| `zlink.fanout.received` | classic fanout receive 누계 |
| `zlink.fanout.dropped` | Framework가 원인을 확인한 classic fanout drop 누계 |
| `zlink.location.records` | 유효한 descriptor·Spot·Actor record 수 |
| `zlink.location.store.errors` | Redis read·write·lease failure 누계 |
| `zlink.location.owner_lease.renew.failures` | owner lease 갱신 실패 누계 |
| `zlink.location.owner_lease.renew.lateness` | 예정 시각 대비 owner lease 갱신 지연 |
| `zlink.observability.events.overflow` | monitoring·trace observer queue overflow 누계 |
| `zlink.termination.state` | 현재 host Framework runtime state |
| `zlink.termination.duration` | Retire·Shutdown 시작부터 terminal result까지의 시간 |
| `zlink.termination.blocked` | admission을 바꾸지 않고 끝난 Retire 수 |
| `zlink.termination.forced` | bounded teardown으로 끝난 operation 수 |

## 2. Retire — continuity를 유지하는 host 종료

라이브 User Spot, Actor와 활성 STREAM session이 있는 host는 `Retire`로 다른 Serving node에 continuity를
이전한 뒤 종료한다. `Retire`는 host 전체 operation이며 MeshName별 종료 정책을 받지 않는다.

1. Preflight에서 모든 stateful object, target capability·capacity와 Relocation Store를 확인한다. Eligible
   target이 없으면 source admission을 바꾸지 않고 `Blocked`로 끝난다.
2. Host를 `Retiring`으로 게시하고 standalone Actor, Instance Spot과 User Spot aggregate execution queue에
   infrastructure notification을 예약한다.
3. Notification이 turn boundary에 도달했을 때 현재 실행 중인 turn만 source에서 완료한다. Outbound·inbound,
   `Capture`·`Restore`와 encoded payload permit을 모두 얻은 ready unit만 queue를 seal한다. Permit을 얻지
   못한 unit은 source에서 application message와 timer를 계속 처리한다.
4. Seal 시점에 실행하지 않은 message, accepted journal, logical timer registration·pending tick과 optional
   Snapshot bytes를 immutable relocation root에 저장한다. Target factory·`Restore`와 journal staging은
   owner·membership commit 전에 끝낸다.
5. User Spot과 member Actor는 하나의 aggregate commit으로 owner·membership을 함께 바꾼다. Standalone Entry
   member Actor는 commit 뒤 target Entry Spot `OnActorRelocatedAsync`, source `OnLeaveActorAsync` 완료 또는
   durable source cleanup, journal replay 순서로 진행한다. 일반 join의 `OnJoinedActorAsync`는 사용하지 않는다.
6. Frozen queue·timer를 target에 복원하고 seal 뒤 source hold를 target으로 relay한다. Source cleanup,
   `Completed`, bound STREAM route ACK와 steady normalization을 끝낸 뒤 target admission을 연다.
7. 모든 unit이 source dispatch에서 분리되면 `Draining`으로 전환하고 topology resource를 bounded cleanup한다.

첫 relocation commit 전 failure는 source queue와 admission을 복원할 수 있다. 첫 commit 뒤에는 source로
rollback하지 않고 target recovery를 계속하며 deadline을 넘기면 `ForceStopped`로 끝낸다.

## 3. Shutdown — relocation 없는 bounded cleanup

Hosting stop은 `ShutdownAsync(...)`를 호출한다. `Shutdown`은 새 relocation을 시작하지 않고 진행 중인 work를
deadline 안에서 terminal 상태로 만든 뒤 Entry·User·Instance Spot에 `OnClosingAsync`를
`HostShutdown` reason으로 알린다. Callback 완료 뒤 scope, authority, session과 topology resource를 정리한다.
Continuity가 필요한 배포 자동화는 hosting stop 전에 `RetireAsync(...)`를 명시적으로 호출해야 한다.

일반 request가 끝났다는 이유만으로 User·Instance Spot을 닫지 않는다. `Retire`에서는 User Spot aggregate와
Instance Spot을 target으로 relocation하며 logical ID와 `ObjectGeneration`을 유지한다. Missing Instance Spot의
cold activation은 별도 address나 manager create가 아니라 global SpotRid direct fluent call의 explicit
Instance marker만 시작한다.

## 4. 명시 제어와 readiness

`IZLinkFrameworkRuntime`은 host maintenance를 소유하는 DI singleton이다.

```csharp
var runtime = app.Services.GetRequiredService<IZLinkFrameworkRuntime>();
var result = await runtime.RetireAsync(
    TimeSpan.FromSeconds(25),           // host 전체 continuity 이전 deadline
    cancellationToken: ct);

if (result.Outcome == ZLinkFrameworkTerminationOutcome.ForceStopped)
    Console.Error.WriteLine($"host retire force-stopped: {result.Reason}");
```

`RetireAsync(...)`와 `ShutdownAsync(...)`의 `deadline == null`은 30초다. 먼저 확정된 operation의 deadline과
effective intent를 공유하며, cancellation은 해당 waiter만 끝낸다. `Retire` preflight의 `Blocked`는 host
terminal state로 저장하지 않는다.

Readiness는 host `IZLinkFrameworkRuntime.IsReady`와 업무에 필요한 component runtime의 readiness를 함께
확인해 기존 HTTP endpoint에 연결한다.

```csharp
app.MapGet("/healthz/ready", (IZLinkFrameworkRuntime runtime) =>
    runtime.IsReady
        ? Results.Ok()
        : Results.StatusCode(StatusCodes.Status503ServiceUnavailable));
```

Kubernetes 배포에 연결하면 다음 개념이 된다.

```yaml
# readiness probe → /healthz/ready — Draining 진입 즉시 신규 트래픽 대상에서 제외
# preStop hook + terminationGracePeriodSeconds ≥ drain deadline — 자동 drain이 끝날 시간을 확보
```

## 5. MeshNode 런타임 제어와 관측

`AddRouteMesh`로 등록한 MeshNode는 두 DI singleton으로 운영한다.

**런타임 옵션 — `IZLinkRouteMeshRuntimeOptions`.** serving 중에 바꿀 수 있는 값은
두 가지뿐이다. 나머지 소켓 옵션(HWM·timeout)은 시작 전 `ConfigureRouterSocket()`
전용이다.

```csharp
var meshOptions = app.Services.GetRequiredService<IZLinkRouteMeshRuntimeOptions>();
meshOptions.MeshNode("game.room").MaxMessageSize = 8 * 1024 * 1024;  // 0 = 무제한
meshOptions.Channel("game.room").Weight = 0;                          // 신규 select-one 대상에서 제외
```

`Weight`는 0~100이고 즉시 반영된다. 0은 그 membership을 새 select-one과 Logical
Multicast 원격 대상에서 빼는 값이라, 재배포 전 트래픽을 빼는 용도로 쓴다. 등록되지
않은 mesh나 membership을 조회하면 `ZLinkConfigurationException`이다.

**상태 조회 — `IZLinkRouteMeshRuntime`.** Mesh 하나에 대해 일관된 snapshot 한 장과
순서 있는 component 이벤트 스트림을 제공한다. Host termination은 `IZLinkFrameworkRuntime`이 소유한다.

```csharp
var meshRuntime = app.Services.GetRequiredService<IZLinkRouteMeshRuntime>();

var snapshot = meshRuntime.Snapshot("game.room");   // 노드 상태·peer·channel 일관 스냅샷
var ready = meshRuntime.IsReady("game.room");

await foreach (var meshEvent in meshRuntime.ObserveAsync("game.room", cancellationToken: ct))
{
    // state/peer 전이가 Sequence 순서로 온다 — 11-monitoring의
    // AddMeshNodeEvents(mesh)는 이 스트림을 event 버스에 올린 것이다.
}
```

## 6. Host termination 상태 관측

Host `Retire`·`Shutdown` 상태 전이는 `IZLinkFrameworkRuntime`의 bounded event stream에서 관측한다. MeshName별
runtime은 component snapshot을 제공하지만 별도 termination authority나 partial drain operation을 만들지 않는다.

```csharp
var runtime = app.Services.GetRequiredService<IZLinkFrameworkRuntime>();

await foreach (var hostEvent in runtime.ObserveAsync(cancellationToken: ct))
{
    // Host 전체 state, effective intent와 terminal outcome을 sequence 순서로 기록한다.
    logger.LogInformation("host termination: {State} {Intent} {Outcome} {Reason}",
        hostEvent.State,
        hostEvent.EffectiveIntent,
        hostEvent.Outcome,
        hostEvent.Reason);
}
```

`ZLinkFrameworkRuntimeState`의 `Preparing`·`Serving`·`Retiring`·`Draining`·`Stopped`·`Error`를 그대로
관측한다. Terminal event의 intent·outcome·reason은 `RetireAsync` 또는 `ShutdownAsync` 결과와 같아야 한다.
수치로 보려면 §1의 `zlink.termination.*` 계기를 사용한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: Monitoring — runtime 이벤트](11-monitoring.ko.md) | [다음: 인터페이스 카탈로그](13-interface-catalog.ko.md)
<!-- framework-adapter-nav:bottom:end -->
