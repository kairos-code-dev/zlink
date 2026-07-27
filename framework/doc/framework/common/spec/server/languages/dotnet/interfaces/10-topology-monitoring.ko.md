# .NET topology와 host monitoring 공개 인터페이스

[.NET exact interface 목차](README.ko.md)

## 1. Runtime snapshot, event와 host termination

```csharp
public enum ZLinkFrameworkRuntimeState
{
    Preparing = 0,
    Serving = 1,
    Retiring = 2,
    Draining = 3,
    Stopped = 4,
    Error = 5
}

public enum ZLinkFrameworkTerminationIntent
{
    Retire = 0,
    Shutdown = 1
}

public enum ZLinkFrameworkTerminationOutcome
{
    Stopped = 0,
    Blocked = 1,
    ForceStopped = 2
}

public enum ZLinkFrameworkTerminationReason
{
    None = 0,
    TargetUnavailable = 1,
    StoreUnavailable = 2,
    RelocationDisabled = 3,
    StateIncompatible = 4,
    DeadlineExceeded = 5,
    RelocationFailed = 6,
    TeardownFailed = 7,
    RuntimeNotReady = 8,
    ManualTopologyUnsupported = 9
}

public readonly record struct ZLinkFrameworkTerminationResult(
    ZLinkFrameworkTerminationIntent EffectiveIntent,
    ZLinkFrameworkTerminationOutcome Outcome,
    ZLinkFrameworkTerminationReason Reason);

public sealed record ZLinkFrameworkRuntimeSnapshot(
    ZLinkFrameworkRuntimeState State,
    ZLinkFrameworkTerminationIntent? EffectiveIntent,
    DateTimeOffset? Deadline,
    bool WorkSealed,
    ZLinkFrameworkTerminationReason? BlockerReason,
    ulong PendingRequestCount,
    ulong PendingRelocationCount,
    ulong PendingStreamBarrierCount,
    ZLinkFrameworkTerminationResult? TerminalResult,
    ulong Sequence,
    DateTimeOffset ObservedAt);

public sealed record ZLinkFrameworkRuntimeEvent(
    string Identifier,
    ulong Sequence,
    DateTimeOffset Timestamp,
    ZLinkFrameworkRuntimeState State,
    ZLinkFrameworkTerminationIntent? EffectiveIntent,
    ZLinkFrameworkTerminationOutcome? Outcome,
    ZLinkFrameworkTerminationReason? Reason);

public interface IZLinkFrameworkRuntime
{
    ZLinkFrameworkRuntimeState State { get; }
    bool IsReady { get; }
    ZLinkFrameworkRuntimeSnapshot Snapshot();
    IAsyncEnumerable<ZLinkFrameworkRuntimeEvent> ObserveAsync(
        int capacity = 1024,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkFrameworkTerminationResult> RetireAsync(
        TimeSpan? deadline = null,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkFrameworkTerminationResult> ShutdownAsync(
        TimeSpan? deadline = null,
        CancellationToken cancellationToken = default);
}

public enum ZLinkMeshNodeState
{
    Starting = 0,
    Serving = 1,
    Draining = 2,
    Drained = 3,
    ForceStopping = 4,
    Stopped = 5,
    Faulted = 6
}

public sealed record ZLinkMeshPeerSnapshot(
    RoutingId Rid,
    ulong LifecycleGeneration,
    ulong DescriptorRevision,
    string Endpoint,
    string AdmissionState,
    bool Ready,
    string DrainState,
    IReadOnlyList<string> ChannelNames,
    string? LastFailure);

public sealed record ZLinkMeshChannelSnapshot(
    string ChannelName,
    int LocalWeight,
    int ReadyMemberCount,
    bool Selectable);

public sealed record ZLinkMeshClaimSnapshot(
    bool ApplicationActive,
    ulong PendingApplicationWork,
    bool InfrastructureActive,
    ulong PendingInfrastructureWork);

public sealed record ZLinkInstanceSpotTypeSnapshot(
    string InstanceSpotType,
    ulong ActiveCount,
    ulong ActivatingCount,
    ulong ClosingCount,
    ulong PendingMessageCount,
    ulong PendingByteCount,
    string? LastActivationOutcome);

public sealed record ZLinkLocationRuntimeSnapshot(
    string State,
    DateTimeOffset? LastSuccessAt,
    DateTimeOffset? LastFailureAt);

public sealed record ZLinkMeshNodeSnapshot(
    string MeshName,
    RoutingId Rid,
    ulong LifecycleGeneration,
    ulong DescriptorRevision,
    string Endpoint,
    ZLinkMeshNodeState State,
    ulong Sequence,
    DateTimeOffset ObservedAt,
    IReadOnlyList<string> DescriptorSources,
    IReadOnlyList<ZLinkMeshPeerSnapshot> Peers,
    IReadOnlyList<ZLinkMeshChannelSnapshot> Channels,
    ZLinkMeshClaimSnapshot Claims,
    ZLinkLocationRuntimeSnapshot Location)
{
    public long ApplicationVersion { get; init; }
    public ZLinkMeshNodeObjectRole ObjectRole { get; init; }
    public int PlacementWeight { get; init; }
    public ZLinkPlacementCapacity PopulationCapacity { get; init; }
        = new(new(0, 0, 0), new(0, 0, 0), Array.Empty<ZLinkSpotTypeCapacity>());
    public ZLinkActivationConcurrency ActivationConcurrency { get; init; }
        = new(0, 128);
    public ulong PlacementReservationFailureCount { get; init; }
    public string? LastPlacementReservationFailure { get; init; }
    public IReadOnlyList<ZLinkObjectCapability> ObjectCapabilities { get; init; }
        = Array.Empty<ZLinkObjectCapability>();
    public IReadOnlyList<ZLinkInstanceSpotTypeSnapshot> InstanceSpots { get; init; }
        = Array.Empty<ZLinkInstanceSpotTypeSnapshot>();
}

public sealed record ZLinkMeshRuntimeEvent(
    string Identifier,
    ulong Sequence,
    DateTimeOffset Timestamp,
    string MeshName,
    RoutingId SourceRid,
    RoutingId? PeerRid,
    ulong? LifecycleGeneration,
    ulong? DescriptorRevision,
    string? ChannelName,
    string? ClaimDomain,
    string? MessageKind,
    string? PlacementOutcome,
    ZLinkCapacityVector? Capacity,
    ZLinkPlacementCapacity? PopulationCapacity,
    ZLinkActivationConcurrency? ActivationConcurrency,
    string? Reason,
    ZLinkMeshNodeState? State)
    : Zlink.Framework.Contracts.Eventing.IZLinkRuntimeEvent
{
    public string SourceName => MeshName;
}

public interface IZLinkRouteMeshRuntime
{
    ZLinkMeshNodeSnapshot Snapshot(string meshName);
    IAsyncEnumerable<ZLinkMeshRuntimeEvent> ObserveAsync(
        string meshName,
        int capacity = 1024,
        CancellationToken cancellationToken = default);
    bool IsReady(string meshName);
}

public enum ZLinkClientServerRole
{
    Client = 1,
    Server = 2,
    ClientAndServer = 3
}

public enum ZLinkClientServerServerState
{
    Configured = 0,
    Connecting = 1,
    Ready = 2,
    Draining = 3,
    Disconnected = 4,
    Rejected = 5
}

public sealed record ZLinkClientServerServerSnapshot(
    RoutingId ServerRid,
    ulong LifecycleGeneration,
    ulong DescriptorRevision,
    string Endpoint,
    int Weight,
    bool Ready,
    ZLinkClientServerServerState State,
    string DescriptorSource,
    string? LastFailure);

public sealed record ZLinkClientServerChannelSnapshot(
    string ChannelName,
    ZLinkClientServerRole LocalRole,
    bool Selectable,
    int ReadyServerCount,
    int ConnectionIntentCount,
    int PendingRequestCount,
    ulong Sequence,
    DateTimeOffset ObservedAt,
    IReadOnlyList<ZLinkClientServerServerSnapshot> Servers,
    ZLinkLocationRuntimeSnapshot Location);

public sealed record ZLinkClientServerRuntimeEvent(
    string Identifier,
    ulong Sequence,
    DateTimeOffset Timestamp,
    string ChannelName,
    RoutingId? ServerRid,
    ulong? LifecycleGeneration,
    ulong? DescriptorRevision,
    int? Weight,
    bool? Ready,
    ZLinkClientServerServerState? State,
    string? Reason);

public interface IZLinkClientServerRuntime
{
    ZLinkClientServerChannelSnapshot Snapshot(string channelName);
    IAsyncEnumerable<ZLinkClientServerRuntimeEvent> ObserveAsync(
        string channelName,
        int capacity = 1024,
        CancellationToken cancellationToken = default);
    bool IsReady(string channelName);
}

public enum ZLinkFanoutPublisherConnectionState
{
    Connecting = 0,
    Ready = 1,
    Disconnected = 2,
    Reconnecting = 3,
    ExcludedDraining = 4,
    ExcludedStale = 5
}

public sealed record ZLinkFanoutPublisherConnectionSnapshot(
    RoutingId PublisherRid,
    ulong LifecycleGeneration,
    ulong DescriptorRevision,
    string Endpoint,
    bool ConnectionIntent,
    bool Ready,
    ZLinkFanoutPublisherConnectionState State,
    string? LastFailure);

public sealed record ZLinkFanoutChannelSnapshot(
    string ChannelName,
    int ConnectionIntentCount,
    int ReadyConnectionCount,
    ulong Sequence,
    DateTimeOffset ObservedAt,
    IReadOnlyList<ZLinkFanoutPublisherConnectionSnapshot> Publishers,
    ZLinkLocationRuntimeSnapshot Location);

public abstract record ZLinkFanoutRuntimeEvent
{
    private protected ZLinkFanoutRuntimeEvent(
        string identifier,
        ulong sequence,
        DateTimeOffset timestamp,
        string channelName)
    {
        Identifier = identifier;
        Sequence = sequence;
        Timestamp = timestamp;
        ChannelName = channelName;
    }

    public string Identifier { get; }
    public ulong Sequence { get; }
    public DateTimeOffset Timestamp { get; }
    public string ChannelName { get; }

    public sealed record PublisherChanged(
        ulong Sequence,
        DateTimeOffset Timestamp,
        string ChannelName,
        ZLinkFanoutPublisherConnectionSnapshot Entry)
        : ZLinkFanoutRuntimeEvent(
            "zlink.runtime.fanout.publisher_changed",
            Sequence,
            Timestamp,
            ChannelName);

    public sealed record LocationChanged(
        ulong Sequence,
        DateTimeOffset Timestamp,
        string ChannelName,
        ZLinkLocationRuntimeSnapshot Location)
        : ZLinkFanoutRuntimeEvent(
            "zlink.runtime.location.store_changed",
            Sequence,
            Timestamp,
            ChannelName);
}

public interface IZLinkFanoutRuntime
{
    ZLinkFanoutChannelSnapshot Snapshot(string channelName);
    IAsyncEnumerable<ZLinkFanoutRuntimeEvent> ObserveAsync(
        string channelName,
        int capacity = 1024,
        CancellationToken cancellationToken = default);
}
```

`PopulationCapacity`는 Actor 전체, Spot 전체와 등록한 User·Instance Spot type별
active·reserved·limit을 구분한다. Limit `0`은 제한 없음이다. Entry Spot 자체는 Spot count에서 제외하고
Entry Spot의 Actor는 Actor 전체 count에 포함한다. `ActivationConcurrency`의 active·limit은 population
reservation과 별도로 제공한다. Placement event의 `Capacity`는 해당 operation의 typed vector이고
`PopulationCapacity`는 관찰 시점의 node aggregate다.

`InstanceSpots`는 이 MeshNode에 startup에서 등록한 Instance type별 immutable 집계다. `ActiveCount`는
`Ready` 상태에서 업무 message를 처리할 수 있는 수이고, 나머지 count는 `Activating`, `Closing`, activation
barrier 앞의 pending message와 byte를 각각 나타낸다. `LastActivationOutcome`은 아직 terminal activation을
관찰하지 않았으면 `null`이고, 값이 있으면 `ready`, `rejected`, `conflict`, `timed_out`, `shutdown`,
`store_failure`, `fenced` 가운데 하나다. 이 snapshot에는 Spot ID, owner ID, `ObjectGeneration`,
`AuthorityOwnerGeneration`, `StoreVersion`과 owner lease fence의 개별 목록을 포함하지 않는다.

`IZLinkFrameworkRuntime`은 host maintenance를 소유하는 singleton이다. `RetireAsync(...)`와
`ShutdownAsync(...)`는 MeshName이나 ChannelName을 받지 않는다. `deadline == null`은 30초이며 cancellation은
해당 waiter만 끝낸다. Host가 `Draining`으로 전환된 뒤에는 먼저 시작한 operation의 deadline과
`EffectiveIntent`가 고정되고 cross-intent waiter도 그 terminal result에 합류한다. `Blocked`는 concurrent
preflight waiter에게만 공유하며 host terminal result로 저장하지 않는다.

`Preparing` 또는 `Error`의 `RetireAsync`는 admission을 바꾸지 않고
`Blocked/RuntimeNotReady`를 반환한다. `ShutdownAsync`는 두 state에서도 bounded cleanup을 시작한다. [User Spot](../../../../01-glossary.ko.md#entry-spot-user-spot과-instance-spot)과
seal 시점의 member Actor는 bounded aggregate로 함께 이전한다. Participant의 `Disabled` policy나 호환 target 부재는
aggregate 전체를 commit 전에 차단한다. Enum의 숫자와 허용 outcome·reason 조합은
[Host Retire, Shutdown & Handoff](../../../../54-graceful-drain-handoff.ko.md)를
그대로 투영한다.

Local manual RouteMesh peer, ClientServer client endpoint, fanout subscriber endpoint 또는 manual fanout publisher가
하나라도 등록되어 있으면 `RetireAsync`는 state와 admission을 바꾸기 전에
`Blocked/ManualTopologyUnsupported`를 반환한다. `ShutdownAsync`에는 이 제한을 적용하지 않는다. Automatic
RouteMesh는 source의 Core peer table에서 descriptor와 같은 RID·lifecycle generation이 `Ready`가 된 뒤에만
`Retiring`으로 전환한다.
이 검사는 현재 process의 registration만 판정한다. 다른 process의 manual endpoint나 Framework 밖의 client
connection mode는 관찰할 수 없으며, 참여 process 전체가 automatic discovery를 사용한다는 조건은 deployment가
보장한다.

`Blocked/DeadlineExceeded`는 모든 target의 `Prepared` 완료와 `Draining` publication 전에 [deadline](../../../../01-glossary.ko.md#deadline)이 끝난
결과다. Framework는 reversible 작업을 정리하고 host state와 admission을 복원한다. `Draining` publication 뒤
bounded teardown이 deadline을 넘으면 `ForceStopped/DeadlineExceeded`를 반환한다. 두 결과는 같은 reason을
사용하지만 phase와 side effect가 다르며 enum을 추가하지 않는다.

`IZLinkClientServerRuntime`은 [ChannelName](../../../../01-glossary.ko.md#channelname)으로 ClientServer [snapshot](../../../../01-glossary.ko.md#snapshot)과 event를 제공하며 [MeshName](../../../../01-glossary.ko.md#meshname)을 받지
않는다. Remote Server RID와 endpoint는 관측 값이고 target 선택 API가 아니다.
같은 ChannelName에 Client와 Server를 함께 등록하면 `LocalRole`은 `ClientAndServer`다. 이 값은 두
별도 registration이 하나의 ClientServer topology를 공유한다는 snapshot aggregate projection이다.
Builder에서 선택하거나 `(ChannelName, Role)` registration key로 사용할 수 없다.

`IZLinkRouteMeshRuntime.IsReady(...)`와 `IZLinkClientServerRuntime.IsReady(...)`는 host
`FrameworkRuntimeState == Serving` projection과 해당 조회 대상의 ready 조건을 함께
만족할 때만 true이며 별도 host lifecycle authority를 만들지 않는다.

`ZLinkMeshNodeState`는 읽기 전용 component 상태다. MeshName을 받는 partial drain operation과 component
termination result는 제공하지 않는다. 모든 topology의 admission과 resource 종료는 host `RetireAsync` 또는
`ShutdownAsync`가 한 번에 조정한다.

`IZLinkFanoutRuntime`은 endpoint 없이 등록한 automatic subscriber ChannelName만 받는다. Snapshot의
`Publishers`와 `ZLinkFanoutRuntimeEvent.PublisherChanged.Entry`는 Publisher RID, lifecycle generation,
descriptor revision과 endpoint를 하나의 immutable identity로 보존한다.
`ZLinkFanoutRuntimeEvent.LocationChanged.Location`은 publisher가 0개여도 store degraded·recovered 상태를
전달한다. 두 sealed variant는 서로의 payload를 nullable field로 섞지 않는다. `State`의 닫힌 값과 event identifier는
[Runtime monitoring](../../../../50-runtime-monitoring.ko.md)의 lowercase identifier를 그대로 사용한다. 이
service는 읽기 전용이며 manual
subscriber의 `IZLinkEndpointConnections`를 대신하거나 그 endpoint 집합을 변경하지 않는다. Manual
subscriber ChannelName을 조회하면 `ZLinkConfigurationException`이 발생한다.

`ObserveAsync(...)`의 `CancellationToken`은 해당 asynchronous enumeration 하나만 종료한다. 취소를
인식하면 아직 소비하지 않은 event를 폐기하고 enumeration을 그 token에 연결된
`OperationCanceledException`으로 종료한다. 이미 실행을 시작한 소비 코드는 반환할 수 있지만 취소를
인식한 뒤에 새 event를 전달하지 않는다. 다른 enumeration, automatic connection과 manual endpoint
집합은 영향을 받지 않는다.

`ConnectionIntent=true`는 automatic planner가 endpoint 연결을 요청했다는 뜻이고 transport readiness가
아니다. `Ready=true`, `ReadyConnectionCount`와 `PublisherChanged`의 `ready` state는 publisher 전용 SUB
socket의 native-ready와 같은 socket의 첫 valid application fanout record 또는 liveness beacon 수신을 모두
반영한다. `disconnected`는 native disconnect 또는 15초 inbound timeout을 반영한다. `Connect` 반환,
native-ready 하나와 내부 active target 수로 이 값을 먼저 바꾸지 않는다.

## 2. Message flow와 metric

```csharp
public enum ZLinkRuntimeMessageFlowMode
{
    Off = 0,
    ErrorsOnly = 1,
    KeyTransitions = 2,
    Verbose = 3
}

public sealed record ZLinkRuntimeMessageFlowEvent(
    string EventId,
    DateTimeOffset Timestamp,
    string? Phase,
    string Surface,
    string MessageKind,
    string Outcome,
    string? Reason,
    string? Action,
    string? MeshName,
    string? ChannelName,
    string? ChannelRouteKind,
    RoutingId? SourceRid,
    RoutingId? TargetRid,
    RoutingId? ServerRid,
    string? PacketName,
    string? Topic,
    string? SpotId,
    string? InstanceSpotType,
    string? ActivationState,
    string? ActorId,
    string? CorrelationId,
    string? FlowId,
    string? FlowOrigin,
    long? MessageSizeBytes,
    double? DurationSeconds);

public sealed record ZLinkRuntimeErrorEvent(
    string EventId,
    DateTimeOffset Timestamp,
    string Kind,
    string Source,
    string Reason);

public interface IZLinkRuntimeMessageFlowObserver
{
    ValueTask OnMessageFlowAsync(
        ZLinkRuntimeMessageFlowEvent flow,
        CancellationToken cancellationToken);
}

public interface IZLinkRuntimeErrorSink
{
    ValueTask OnRuntimeErrorAsync(
        ZLinkRuntimeErrorEvent error,
        CancellationToken cancellationToken);
}

public interface IZLinkDispatchOptions
{
    IZLinkUnhandledDispatchOptions Unhandled { get; }
    IZLinkDiagnosticsOptions Diagnostics { get; }
    IZLinkDispatchOptions TraceSampleRate(double rate);
    IZLinkDispatchOptions IncludeMessageSizes(bool include);
    IZLinkDispatchOptions TraceLogFile(string path);
    IZLinkDispatchOptions TraceLabel(string label);
    IZLinkDispatchOptions SetRuntimeMessageFlowObserver<TObserver>()
        where TObserver : class, IZLinkRuntimeMessageFlowObserver;
    IZLinkDispatchOptions SetRuntimeMessageFlowObserver(
        IZLinkRuntimeMessageFlowObserver observer);
    IZLinkDispatchOptions SetRuntimeErrorSink<TSink>()
        where TSink : class, IZLinkRuntimeErrorSink;
    IZLinkDispatchOptions SetRuntimeErrorSink(IZLinkRuntimeErrorSink sink);
    IZLinkDispatchOptions MessageFlow(ZLinkRuntimeMessageFlowMode mode);
}

public interface IZLinkMessageFlowRuntime
{
    ZLinkRuntimeMessageFlowMode Mode { get; set; }
    IAsyncEnumerable<ZLinkRuntimeMessageFlowEvent> ObserveAsync(
        int capacity = 1024,
        CancellationToken cancellationToken = default);
}
```

Message flow의 기본 mode는 `ErrorsOnly`이며 실행 중 변경할 수 있다. Observer는 immutable event를 받고
dispatch 결정에 참여하지 않는다. `EventId == "zlink.dispatch_error"`일 때 `Outcome`은
`"failed"`이고 `Reason`과 `Action`이 모두 존재한다. runtime error sink는 observer 실패를
`EventId == "zlink.runtime_error"`, `Kind == "observer_failed"`, `Source == "message_flow_observer"`로
받는다. 두 event에 exception object를 포함하지 않으며 sink 실패는 다시 sink를 호출하지
않는다. 닫힌 문자열 값과 조건부 field 규칙은 [Message flow](../../../../52-message-flow-tracing.ko.md)이
소유한다. Instance [Spot](../../../../01-glossary.ko.md#spot) event의 `InstanceSpotType`은 startup에 등록한 type이고 `ActivationState`는
`activating`, `ready`, `closing` 가운데 하나다. Metric은 `System.Diagnostics.Metrics`의 `Meter` 이름
`zlink.framework`로 제공하고, 계기 이름·단위·label은
[Runtime metrics](../../../../51-runtime-metrics.ko.md)의 공통 계약을 그대로 사용한다.

Instance activation 계기는 다음 .NET instrument로 투영한다.

| 계기 | .NET instrument | 단위 | Label |
|---|---|---|---|
| `zlink.instance_spot.activations` | `Counter<long>` | `{activation}` | `mesh_name`, `instance_spot_type`, `outcome` |
| `zlink.instance_spot.activation.duration` | `Histogram<double>` | `s` | `mesh_name`, `instance_spot_type`, `outcome` |
| `zlink.instance_spot.pending.messages` | `ObservableGauge<long>` | `{message}` | `mesh_name`, `instance_spot_type` |
| `zlink.instance_spot.pending.bytes` | `ObservableGauge<long>` | `By` | `mesh_name`, `instance_spot_type` |
| `zlink.instance_spot.claim.conflicts` | `Counter<long>` | `{claim}` | `mesh_name`, `instance_spot_type`, `reason` |
| `zlink.instance_spot.takeovers` | `Counter<long>` | `{takeover}` | `mesh_name`, `instance_spot_type`, `outcome` |

이 계기는 공통 spec의 `mesh_name`, 등록된 `instance_spot_type`, 닫힌 `outcome`·`reason` label만 사용한다.
[Spot ID](../../../../01-glossary.ko.md#spot-id), [owner](../../../../01-glossary.ko.md#owner) ID, internal [authority](../../../../01-glossary.ko.md#authority) fields, endpoint와 correlation ID는 label로 기록하지 않는다. Instance one-way
activation 실패는 `zlink.mesh_node.messages.dropped`에 `surface=instance_spot`으로 기록하며 완료된 submit
결과를 바꾸거나 reply를 만들지 않는다.
## 3. Dispatch policy와 diagnostics configuration

```csharp
public interface IZLinkUnhandledDispatchOptions
{
    ZLinkUnhandledDispatchAction Request { get; set; }
    ZLinkUnhandledDispatchAction Send { get; set; }
    ZLinkUnhandledDispatchAction Publish { get; set; }
}

public interface IZLinkDiagnosticsOptions
{
    ZLinkRuntimeMessageFlowMode MessageFlow { get; }
    double SampleRate { get; }
    bool IncludeMessageSizes { get; }
    string? LogFile { get; }
    string? Label { get; }
    ZLinkRuntimeMessageFlowMode EffectiveMessageFlow { get; }
}

public enum ZLinkUnhandledDispatchAction
{
    ReplyError = 0,
    LogAndDrop = 1,
    Drop = 2,
    Throw = 3
}

```

`IZLinkDispatchOptions.Diagnostics`는 fluent configuration의 read-only view다. Runtime message flow observer와
runtime error sink는 §2의 한 event model을 사용한다. 실행 중 mode 변경과 bounded event stream 관찰은
`IZLinkMessageFlowRuntime`이 함께 소유한다. 별도 control interface, observer interface, mode enum과 event DTO를
병렬로 제공하지 않는다.
