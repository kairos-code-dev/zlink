# .NET topology와 host monitoring 공개 인터페이스

[.NET exact interface 목차](README.ko.md) ·
[Runtime monitoring](../../../../24-runtime-monitoring.ko.md) ·
[Host Relocate, Shutdown & Handoff](../../../../28-graceful-drain-handoff.ko.md)

## 1. 범위

이 문서는 .NET application이 host 종료를 요청하고 RouteMesh·ClientServer·Fanout의 운영 상태를
확인할 때 사용하는 public interface를 고정한다. Status와 관찰 stream에는 application이 상태를 판단하거나
대응 방법을 선택하는 데 필요한 값만 포함한다.

Framework가 topology를 조정할 때 사용하는 descriptor revision, lifecycle generation, endpoint,
admission·claim·reservation 단계와 Location Store record는 public interface에 포함하지 않는다.
이 값은 application이 변경할 수 없으며 Framework가 stale state와 ownership을 판정할 때만 사용한다.

## 2. Host lifecycle

`Relocating`, `Relocated`와 `Draining`은 application에 미치는 영향이 다르므로 별도 상태로 제공한다.
`Relocating`에서는 새 placement와 application admission을 받지 않고 현재 object를 다른 node로 이전한다.
`Relocated`에서는 이전이 완료되었지만 host infrastructure를 유지한다. `Draining`에서는 relocation 없이
남아 있는 application 처리와 resource를 정리한다.

```csharp
public enum ZLinkFrameworkRuntimeState
{
    Preparing = 0,
    Serving = 1,
    Relocating = 2,
    Relocated = 3,
    Draining = 4,
    Stopped = 5,
    Error = 6
}

public enum ZLinkFrameworkRelocationOutcome
{
    Relocated = 0,
    Blocked = 1
}

public enum ZLinkFrameworkRelocationMode
{
    PlannedMaintenance = 0,
    RollingUpdate = 1
}

public enum ZLinkFrameworkRelocationReason
{
    None = 0,
    TargetUnavailable = 1,
    StoreUnavailable = 2,
    RelocationDisabled = 3,
    StateIncompatible = 4,
    DeadlineExceeded = 5,
    RelocationFailed = 6,
    RuntimeNotReady = 7,
    ManualTopologyUnsupported = 8,
    ShutdownRequested = 9,
    OperationInProgress = 10
}

public sealed record ZLinkFrameworkRelocationOptions
{
    public required ZLinkFrameworkRelocationMode Mode { get; init; }
    public long? TargetApplicationVersion { get; init; }
    public TimeSpan? Deadline { get; init; }
}

public readonly record struct ZLinkFrameworkRelocationResult(
    ZLinkFrameworkRelocationMode Mode,
    long TargetApplicationVersion,
    ZLinkFrameworkRelocationOutcome Outcome,
    ZLinkFrameworkRelocationReason Reason);

public enum ZLinkFrameworkTerminationOutcome
{
    Stopped = 0,
    ForceStopped = 1
}

public enum ZLinkFrameworkTerminationReason
{
    None = 0,
    DeadlineExceeded = 1,
    TeardownFailed = 2
}

public readonly record struct ZLinkFrameworkTerminationResult(
    ZLinkFrameworkTerminationOutcome Outcome,
    ZLinkFrameworkTerminationReason Reason);

public sealed record ZLinkFrameworkRuntimeStatus(
    ZLinkFrameworkRuntimeState State,
    bool IsReady,
    bool AcceptingWork,
    DateTimeOffset? Deadline,
    ZLinkFrameworkRelocationResult? RelocationResult,
    ZLinkFrameworkTerminationResult? TerminationResult,
    ulong Sequence,
    DateTimeOffset ObservedAt);

public interface IZLinkFrameworkRuntime
{
    ZLinkFrameworkRuntimeStatus Status { get; }

    IAsyncEnumerable<ZLinkFrameworkRuntimeStatus> ObserveAsync(
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkFrameworkRelocationResult> RelocateAsync(
        ZLinkFrameworkRelocationOptions options,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkFrameworkTerminationResult> ShutdownAsync(
        TimeSpan? deadline = null,
        CancellationToken cancellationToken = default);
}
```

`IsReady`는 `State == Serving`일 때만 true다. `AcceptingWork`는 현재 host가 새로운 application
operation을 받아들이는지를 나타낸다. 두 값은 relocation unit 수나 queue 내부 상태를 application에
노출하지 않고도 readiness와 admission을 판단할 수 있게 한다.

`RelocateAsync(...)`는 mode가 정한 application version의 target으로 현재 stateful object를 이전한다.
`PlannedMaintenance`에서는 `TargetApplicationVersion`을 지정하지 않으며 Framework가 source host의
`ApplicationVersion`을 effective target version으로 고정한다. `RollingUpdate`에서는 source보다 큰
`TargetApplicationVersion`을 반드시 지정한다. 다른 값 조합은 operation을 시작하기 전에
`ArgumentException`으로 거부한다.

Target version 조건은 capability, capacity와 weight를 적용하기 전에 후보 집합을 제한한다.

- `PlannedMaintenance`: source와 application version이 같은 target만 사용한다.
- `RollingUpdate`: 호출자가 지정한 application version과 정확히 같은 target만 사용한다. 그보다 높거나
  낮은 다른 version으로 자동 전환하지 않는다.

두 mode 모두 source node를 제외하고 `Serving` 상태인 Object Server, stable type, relocation policy,
Snapshot adapter와 capacity가 호환되는 target만 사용한다. Source에 `MaintenanceWave`가 설정되어 있으면
같은 wave의 target을 제외한다. 남은 후보가 여러 개이면 기존 node-wide placement weight를 적용한다.
요청한 version의 eligible target이 없으면 deadline까지 descriptor와 Core ready 상태의 수렴을 기다린 뒤
`Blocked/TargetUnavailable`을 반환한다.

모든 object의 이전이 끝나면 `Relocated`를 반환하고 host는 `Relocated`가 된다.
이 상태에서는 새 application operation을 받지 않지만 infrastructure와 연결은 유지한다. 이전을 안전하게
시작하거나 완료할 수 없으면 `Blocked`를 반환한다. Framework는 아직 commit하지 않은 변경을 정리하고
host가 계속 처리할 local object가 있으면 `Serving`으로 복귀한다.

`ShutdownAsync(...)`는 relocation을 시작하지 않는다. `Serving`에서 호출하면 남은 application 처리와
resource를 정리하고, `Relocated`에서 호출하면 infrastructure와 연결만 정리한다. 두 경우 모두 종료를
완료하면 `Stopped`가 된다. `deadline == null`이면 각 operation의 기본값은 30초다.

`ShutdownAsync(...)`가 `Relocating` 중 호출되면 현재 atomic relocation unit의 terminal 결과까지만
확정하고 나머지 relocation을 시작하지 않는다. Relocation waiter는 `Blocked/ShutdownRequested`를 받고
shutdown operation은 source에 남은 object와 resource를 정리한다.

호출자가 전달한 `CancellationToken`은 해당 waiter만 종료한다. 이미 시작한 shared lifecycle operation은
계속 실행되며 다른 waiter와 host lifecycle에 영향을 주지 않는다. 같은 operation을 반복 호출한 waiter는
진행 중인 operation과 terminal 결과를 공유한다. `Mode`와 effective target application version이 모두 같은
호출만 합류한다. 다른 options로 호출하면 기존 operation을 변경하지 않고
`Blocked/OperationInProgress`를 반환한다.

## 3. 공통 topology 상태

Host state는 process 전체의 lifecycle을 나타낸다. `ZLinkTopologyState`는 `MeshName` 또는
`ChannelName`으로 등록한 topology 하나의 가용성을 나타낸다. Host가 `Serving`이어도 특정
topology에 ready peer나 target이 없으면 그 topology만 `Degraded`일 수 있다.

Topology status는 사용자가 readiness와 장애 범위를 판단할 수 있는 닫힌 상태만 제공한다.
`ZLinkTopologyReason`은 application이 설정을 확인하거나 잠시 후 다시 관찰할지를 결정하는 데 사용한다.
세부 transport 또는 Store 오류는 .NET logging과 tracing에 기록한다.

```csharp
public enum ZLinkTopologyState
{
    Starting = 0,
    Ready = 1,
    Degraded = 2,
    Stopping = 3,
    Stopped = 4,
    Failed = 5
}

public enum ZLinkTopologyReason
{
    RuntimeNotReady = 0,
    NoReadyPeer = 1,
    NoReadyTarget = 2,
    LocationUnavailable = 3,
    CapacityExceeded = 4,
    Draining = 5,
    InternalFailure = 6
}

public enum ZLinkPeerState
{
    Connecting = 0,
    Ready = 1,
    Draining = 2,
    Unavailable = 3
}

public sealed record ZLinkChannelStatus(
    string ChannelName,
    bool IsReady,
    int ReadyTargetCount);

public sealed record ZLinkPeerStatus(
    RoutingId NodeRid,
    ZLinkPeerState State,
    ZLinkTopologyReason? UnavailableReason);
```

`NodeRid`는 MeshNode의 transport identity이며 peer를 log와 deployment 정보에 대응시키는 데 사용한다.
별도의 운영용 node identity를 추가하지 않는다. Endpoint와 connection generation은 public status에서
제공하지 않는다.

## 4. RouteMesh

RouteMesh status는 같은 MeshName의 peer 연결, channel readiness와 object placement 가능 여부를
한 번에 보여 준다. Placement count는 이 process에 존재하는 active object만 집계한다.

```csharp
public sealed record ZLinkPlacementStatus(
    bool IsAvailable,
    int ActiveActorCount,
    int ActiveSpotCount,
    ZLinkTopologyReason? UnavailableReason);

public sealed record ZLinkRouteMeshStatus(
    string MeshName,
    ZLinkTopologyState State,
    bool IsReady,
    int ReadyPeerCount,
    IReadOnlyList<ZLinkChannelStatus> Channels,
    IReadOnlyList<ZLinkPeerStatus> Peers,
    ZLinkPlacementStatus Placement,
    ulong Sequence,
    DateTimeOffset ObservedAt);

public interface IZLinkRouteMeshRuntime
{
    ZLinkRouteMeshStatus GetStatus(string meshName);

    IAsyncEnumerable<ZLinkRouteMeshStatus> ObserveAsync(
        string meshName,
        CancellationToken cancellationToken = default);
}
```

`IsReady`는 host가 `Serving`이고 해당 RouteMesh가 application traffic을 처리할 수 있을 때 true다.
`ReadyPeerCount`는 ready 상태인 remote MeshNode 수다. Local channel도 정상적으로 사용할 수 있으므로
peer가 0개라는 이유만으로 모든 RouteMesh를 unavailable로 판정하지 않는다.

`Placement.IsAvailable`은 이 node가 Object Server role이고 새로운 Actor·Spot을 받을 수 있을 때 true다.
Population reservation, activation barrier와 stable type별 내부 count는 public status에 포함하지 않는다.

## 5. ClientServer

같은 process에 등록한 Server도 remote Server와 같은 weight 규칙을 적용받는 정상적인 target이다.
Status는 선택 가능한 전체 target 수와 target별 운영 상태를 제공하며 endpoint와 discovery revision은
제공하지 않는다.

```csharp
public enum ZLinkClientServerRole
{
    Client = 1,
    Server = 2,
    ClientAndServer = 3
}

public sealed record ZLinkClientServerTargetStatus(
    RoutingId NodeRid,
    int Weight,
    ZLinkPeerState State,
    ZLinkTopologyReason? UnavailableReason);

public sealed record ZLinkClientServerStatus(
    string ChannelName,
    ZLinkClientServerRole LocalRole,
    ZLinkTopologyState State,
    bool IsReady,
    int ReadyTargetCount,
    IReadOnlyList<ZLinkClientServerTargetStatus> Targets,
    ulong Sequence,
    DateTimeOffset ObservedAt);

public interface IZLinkClientServerRuntime
{
    ZLinkClientServerStatus GetStatus(string channelName);

    IAsyncEnumerable<ZLinkClientServerStatus> ObserveAsync(
        string channelName,
        CancellationToken cancellationToken = default);
}
```

`ReadyTargetCount`에는 local·remote 구분 없이 positive weight를 가지고 있으며 draining 상태가 아닌
Ready Server를 포함한다. `Targets`는 진단을 위한 읽기 전용 값이다. 이 목록으로 특정 Server를 선택하거나
target weight를 변경하지 않는다.

## 6. Fanout

Fanout runtime status는 automatic subscriber가 현재 사용할 수 있는 publisher 연결을 보여 준다.
개별 publisher의 endpoint, discovery source와 generation은 Framework가 관리한다.

```csharp
public sealed record ZLinkFanoutStatus(
    string ChannelName,
    ZLinkTopologyState State,
    bool IsReady,
    int ReadyPublisherCount,
    IReadOnlyList<ZLinkPeerStatus> Publishers,
    ulong Sequence,
    DateTimeOffset ObservedAt);

public interface IZLinkFanoutRuntime
{
    ZLinkFanoutStatus GetStatus(string channelName);

    IAsyncEnumerable<ZLinkFanoutStatus> ObserveAsync(
        string channelName,
        CancellationToken cancellationToken = default);
}
```

Manual subscriber의 연결 목록은 manual connection API가 소유한다. Manual ChannelName을
`IZLinkFanoutRuntime`으로 조회하면 `ZLinkConfigurationException`이 발생한다.

## 7. 관찰 stream

각 `ObserveAsync(...)`는 상태가 의미 있게 바뀌었을 때 완성된 immutable status를 전달한다. 소비자가
변경 속도를 따라가지 못하면 중간 status를 합치고 최신 status를 전달한다. Status stream은 모든 전이를
감사하는 event log가 아니다.

Identifier에
따라 nullable field의 의미가 달라지는 범용 event DTO는 사용하지 않는다. 소비자는 event 종류별 field
조합을 해석하지 않고 받은 status 전체를 현재 상태로 사용할 수 있다.

`Sequence`는 같은 runtime instance의 status 순서를 비교하는 값이다. Process가 다시 시작되면 0부터
시작할 수 있으며 persistence나 전역 순서를 보장하지 않는다.

`CancellationToken`은 해당 asynchronous enumeration만 종료한다. 취소를 인식한 뒤에는 새 status를
전달하지 않으며 다른 observer, topology 연결과 host lifecycle에는 영향을 주지 않는다.

## 8. Dispatch policy와 diagnostics

Unhandled message 정책과 diagnostics 설정은 별도의 child interface가 담당한다. Dispatch configuration은
두 interface를 찾는 root 역할만 하며 tracing mode, observer, error sink와 file output을 직접 제공하지
않는다.

```csharp
public enum ZLinkUnhandledDispatchAction
{
    ReplyError = 0,
    LogAndDrop = 1,
    Drop = 2,
    Throw = 3
}

public interface IZLinkUnhandledDispatchOptions
{
    ZLinkUnhandledDispatchAction Request { get; set; }
    ZLinkUnhandledDispatchAction Send { get; set; }
    ZLinkUnhandledDispatchAction Publish { get; set; }
}

public enum ZLinkDiagnosticsLevel
{
    Off = 0,
    Errors = 1,
    Normal = 2,
    Detailed = 3
}

public interface IZLinkDiagnosticsOptions
{
    IZLinkDiagnosticsOptions SetLevel(ZLinkDiagnosticsLevel level);
    IZLinkDiagnosticsOptions SetSampleRate(double rate);
    IZLinkDiagnosticsOptions IncludeMessageSizes(bool include);
}

public interface IZLinkDispatchOptions
{
    IZLinkUnhandledDispatchOptions Unhandled { get; }
    IZLinkDiagnosticsOptions Diagnostics { get; }
}
```

`SetSampleRate(...)`는 `0.0` 이상 `1.0` 이하만 허용한다. 범위를 벗어나면
`ArgumentOutOfRangeException`이 발생한다. Message size를 기록하면 payload 크기 분포가 telemetry에
추가되며 payload 내용은 기록하지 않는다.

.NET runtime은 trace를 `ActivitySource`, metric을 `System.Diagnostics.Metrics.Meter`, log를
`Microsoft.Extensions.Logging.ILogger`로 제공한다. Export 대상과 log 저장 위치는 application의
telemetry와 logging configuration이 결정한다. Framework는 file path를 받거나 자체 exporter lifecycle을
public API로 제공하지 않는다.
