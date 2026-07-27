# Runtime 상태와 운영 진단

[공통 스펙 목차](README.ko.md) · [Location runtime](40-location-runtime.ko.md) ·
[Runtime metrics](51-runtime-metrics.ko.md) · [Message flow tracing](52-message-flow-tracing.ko.md) ·
[Host relocation와 shutdown](54-graceful-drain-handoff.ko.md)

## 1. 범위와 독자

이 문서는 application 운영자가 runtime readiness, degraded 기능과 종료 진행 상태를
확인하는 계약을 정의한다.

| 이 문서가 소유하는 정보 | 용도 |
|---|---|
| Runtime status와 닫힌 상태 값 | 현재 상태를 읽는다. |
| Status stream | 상태 변화와 순서를 관찰한다. |
| Structured log identifier | 상태가 바뀐 이유를 진단한다. |

집계 수치의 이름과 label은 [Runtime metrics](51-runtime-metrics.ko.md), message 한 건의
진행 기록은 [Message flow tracing](52-message-flow-tracing.ko.md), relocation과 shutdown의
상태 전이는 [Host relocation와 shutdown](54-graceful-drain-handoff.ko.md)이 소유한다.

Public interface에는 descriptor revision, owner lease, admission, claim, reservation과
socket 상태를 노출하지 않는다. Exporter, 저장소, raw event DTO와 native handle도 노출하지 않는다.

## 2. Application이 읽는 상태

Application은 등록한 이름으로 각 기능의 완전한 status를 읽는다. 내부 service의 값을
직접 조합하지 않는다.

| 상태 범위 | Application이 확인하는 값 |
|---|---|
| Host | Runtime state, ready 여부, 새 작업을 받는지, deadline, relocation 결과와 shutdown 결과를 제공한다. |
| RouteMesh | `MeshName`, 전체 state, ready peer 수, Channel별 ready target 수, peer별 운영 상태와 이 process의 Actor·Spot 수를 제공한다. |
| ClientServer | `ChannelName`, local role, 전체 state, ready target 수와 target별 운영 상태·weight를 제공한다. |
| Automatic fanout | `ChannelName`, 전체 state, 연결을 시도하는 publisher 수와 ready publisher 수를 제공한다. |

다음 C#은 공통 동작을 보여 주는 비규범적 발췌다. 정확한 type과 signature는
[.NET topology monitoring](server/languages/dotnet/interfaces/10-topology-monitoring.ko.md)이 정한다.

```csharp
public interface IZLinkRouteMeshRuntime
{
    ZLinkRouteMeshStatus GetStatus(string meshName); // 등록한 RouteMesh 상태
    IAsyncEnumerable<ZLinkRouteMeshStatus> ObserveAsync(
        string meshName,
        CancellationToken cancellationToken = default); // 이후 RouteMesh 상태
}
```

Host 상태는 특정 [MeshName](01-glossary.ko.md#meshname)에 속하지 않는다. 종료 결과는 host
status에서 한 번만 제공한다.

Status는 호출이 끝난 뒤에도 안전하게 보관할 수 있는 immutable value다. Native handle,
caller buffer, payload와 application metadata를 참조하지 않는다.

### 2.1 Host 상태

Host runtime state는 다음 값으로 닫혀 있다.

| 값 | 의미 |
|---|---|
| `preparing` | Startup 구성을 검증하고 runtime을 준비하고 있다. |
| `serving` | 새 application operation을 받을 수 있다. |
| `relocating` | 새 admission을 중단하고 stateful object를 다른 node로 이전하고 있다. |
| `drained` | Relocation을 끝냈으며 infrastructure와 연결은 유지한다. |
| `draining` | Relocation 없이 남은 처리와 resource를 정리하고 있다. |
| `stopped` | Runtime과 infrastructure 정리가 끝났다. |
| `error` | Runtime을 계속 운영할 수 없는 오류가 발생했다. |

`IsReady`는 state가 `serving`일 때만 true다. `AcceptingWork`는 새 application operation의
admission 여부를 나타낸다. Relocation option과 result의 의미는
[Host relocation와 shutdown](54-graceful-drain-handoff.ko.md)이 정한다.

### 2.2 Topology 상태

Topology 상태는 application이 장애 범위와 대응 방법을 정할 수 있는 값만 제공한다.

| 상태 종류 | 닫힌 값 |
|---|---|
| Operational state | `starting`, `ready`, `degraded`, `stopping`, `stopped`, `failed` |
| Operational reason | `runtime_not_ready`, `no_ready_peer`, `no_ready_target`, `location_unavailable`, `capacity_exceeded`, `draining`, `internal_failure` |
| Peer state | `connecting`, `ready`, `draining`, `unavailable` |
| ClientServer local role | `client`, `server`, `client_and_server` |

RouteMesh peer는 Node RID와 운영 상태를 제공한다. Node RID는 deployment 정보와 대응하는
transport identity다. Endpoint, descriptor revision과 connection generation은 제공하지 않는다.

RouteMesh placement 상태는 새 object 수락 여부와 현재 active Actor·Spot 수를 제공한다.
Stable type별 reservation, activation barrier와 내부 capacity counter는 제공하지 않는다.
Placement weight는 signed integer `0..10000`이다.

같은 process의 ClientServer Server도 remote Server와 같은 후보다. Status는 target 수와 각
target의 상태·weight를 제공한다. `client_and_server`는 같은 `ChannelName`에 두 역할이
등록되었다는 뜻이며 별도 registration role이 아니다.

Automatic fanout은 socket 연결 뒤 application record 또는 liveness beacon을 받으면 ready다.
Disconnect를 확인하거나 15초 동안 record가 없으면 해당 publisher를 제외한다. 연결 계획이나
`connect` 수락만으로 ready가 되지 않는다.

## 3. 상태 조회와 변화 관찰

각 언어는 현재 status 조회와 비동기 변화 관찰을 제공한다. 이름과 type은 exact interface가 정한다.

```csharp
var current = routeMeshRuntime.GetStatus("game-mesh");
// 현재 readiness와 target 상태를 한 값에서 읽는다.

await foreach (var status in routeMeshRuntime.ObserveAsync("game-mesh", cancellationToken))
{
    await RecordStatusAsync(status, cancellationToken);
    // 관찰 코드는 routing과 lifecycle 결정을 바꾸지 않는다.
}
```

Status는 runtime instance 안에서 단조 증가하는 `Sequence`와 관찰 시각을 포함한다. 같은
source에서는 큰 sequence가 더 나중 상태다. 서로 다른 source끼리는 비교하지 않는다.
Process가 다시 시작되면 0부터 시작할 수 있다.

변화 stream의 각 항목은 완전한 status다. Nullable field를 조합하는 범용 event DTO는
제공하지 않는다. Sequence gap이 있으면 최신 status를 다시 읽는다.

느린 소비자가 message dispatch, location claim과 host lifecycle을 막아서는 안 된다. Framework는
중간 상태를 합칠 수 있지만 다음 조건을 지킨다.

- 가장 최근 status sequence를 잃지 않는다.
- Relocation과 shutdown의 terminal status를 생략하지 않는다.
- 한 소비자의 지연이나 실패가 다른 소비자와 runtime 결과를 바꾸지 않는다.

관찰 취소는 해당 stream만 종료한다. 다른 관찰자와 runtime 동작은 바뀌지 않는다.

## 4. Object 위치 조회

운영 도구는 Actor ID 또는 Spot ID의 현재 위치를 exact 조회하거나 authority를 bounded page로
열거할 수 있다. 이 결과는 messaging target이나 placement selector가 아니다. Field, page와
cache 계약은 [Location runtime](40-location-runtime.ko.md#64-운영-도구에서-현재-위치를-조회한다)이 정한다.

## 5. Structured log

Framework는 상태가 바뀐 이유를 표준 structured logger에 기록한다. Application이 provider와
backend를 구성한다. Framework는 public sink, file path, exporter lifecycle과 event DTO를 제공하지 않는다.

다음 identifier는 모든 언어에서 같은 문자열을 사용한다.

| Identifier | 기록하는 변화 |
|---|---|
| `zlink.runtime.mesh_node.state_changed` | MeshNode의 lifecycle 또는 ready 상태가 바뀌었다. |
| `zlink.runtime.mesh_node.peer_changed` | Peer의 admission, ready 또는 service 상태가 바뀌었다. |
| `zlink.runtime.mesh_node.channel_changed` | Channel weight, ready target 수 또는 선택 가능 상태가 바뀌었다. |
| `zlink.runtime.mesh_node.mailbox_changed` | Application 또는 infrastructure mailbox 상태가 바뀌었다. |
| `zlink.runtime.object.placement_changed` | Reservation, Ready, abort, capacity exhaustion 또는 relocation으로 placement 집계가 바뀌었다. |
| `zlink.runtime.mesh_node.routing_id_conflict` | Automatic Node RID owner claim이 active conflict로 실패했다. |
| `zlink.runtime.host.relocation_changed` | Relocation mode, effective target version, host state 또는 terminal result가 바뀌었다. |
| `zlink.runtime.host.termination_changed` | Shutdown state 또는 terminal result가 바뀌었다. |
| `zlink.runtime.relocation.changed` | Actor 또는 Spot relocation phase나 recovery 상태가 바뀌었다. |
| `zlink.runtime.client_server.state_changed` | ClientServer local role, lifecycle 또는 ready 상태가 바뀌었다. |
| `zlink.runtime.client_server.server_changed` | ClientServer target의 weight, ready 또는 service 상태가 바뀌었다. |
| `zlink.runtime.fanout.publisher_changed` | Automatic publisher의 연결 대상 또는 ready 상태가 바뀌었다. |
| `zlink.runtime.location.store_changed` | Location Store가 ready와 degraded 사이에서 바뀌었다. |

Log는 timestamp, source 종류와 등록 이름을 기록한다. 필요하면 Node RID, weight, reason과 state를
추가한다. Payload, metadata, Actor ID, Spot ID, owner token, generation, raw frame와 native handle은
기록하지 않는다.

Publisher 상태는 `excluded_draining`, `excluded_stale`, `reconnecting`, `disconnected`로 기록한다.
Log는 당시 판단이며 현재 authority가 아니다. 현재 상태는 fanout status에서 읽는다.

## 6. Startup과 실패

- 등록하지 않은 MeshName이나 ChannelName의 status를 요청하면 구성 오류다.
- Manual subscriber로만 등록한 fanout ChannelName에 automatic status를 요청하면 구성 오류다.
- Location Store가 없는 runtime은 store 상태를 `not_configured`로 표시한다.
- Object role이 `Client` 또는 `Server`인데 Location Store가 없으면 host startup이 실패한다.
- Metric이나 trace를 끄더라도 runtime status는 계속 사용할 수 있다.
- Logger provider의 실패는 message dispatch, reply, topology 조정과 host lifecycle 결과를 바꾸지 않는다.

## 7. 구현 및 contract test 검증 요구

- Host status 하나로 readiness, admission, relocation과 shutdown 결과를 판단할 수 있다.
- RouteMesh, ClientServer와 automatic fanout은 각각 하나의 status로 readiness와 target 상태를 제공한다.
- Public status에 endpoint, descriptor revision, owner lease, claim, reservation, native handle과 raw event DTO가 나타나지 않는다.
- Publish target 수와 target별 수락·실패 결과를 status나 runtime structured log에 포함하지 않는다.
- Automatic fanout의 beacon timeout은 해당 publisher만 unavailable로 바꾼다.
- 느린 소비, 취소와 logger failure가 dispatch, reply와 lifecycle terminal result를 바꾸지 않는다.
- Sequence gap 뒤 최신 status를 다시 읽어 상태를 복원할 수 있다.
- Placement weight `0`, capacity exhaustion과 recovery가 public status와 일치한다.
- Object 위치 조회가 Location runtime의 bounded page와 cache 계약을 지킨다.
- Actor ID, Spot ID, RID, endpoint, correlation ID와 flow ID를 metric label에 사용하지 않는다.
