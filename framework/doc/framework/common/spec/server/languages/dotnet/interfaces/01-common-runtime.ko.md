# .NET common runtime 공개 인터페이스

[.NET exact interface 목차](README.ko.md)

## 1. 공통 metadata와 call

Handler metadata는 변경할 수 없는 snapshot이다.

```csharp
public sealed class ZLinkMessage
{
    public static ZLinkMessage Empty { get; }
    public string? ContentType { get; }
    public bool IsEmpty { get; }
    public ZlinkStreamCodec? StreamCodec { get; }
    public static ZLinkMessage From<T>(T value);
    public T Decode<T>();
}

public sealed class ZLinkMessageMetadata
{
    public ZLinkMessageMetadata(
        IReadOnlyDictionary<string, string> values);
    public static ZLinkMessageMetadata Empty { get; }
    public IReadOnlyDictionary<string, string> Values { get; }
    public string? Find(string key);
}

public interface IZLinkSendCall : IZLinkMetadataCall<IZLinkSendCall>
{
    ValueTask<ZLinkSubmitResult> SubmitAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkRequestCall : IZLinkMetadataCall<IZLinkRequestCall>
{
    IZLinkRequestCall Timeout(TimeSpan timeout);
    ValueTask<TReply> Async<TReply>(
        CancellationToken cancellationToken = default);
    ValueTask<TReply> Yield<TReply>(
        CancellationToken cancellationToken = default);
}

public interface IZLinkPublishCall : IZLinkMetadataCall<IZLinkPublishCall>
{
    ValueTask<ZLinkPublishResult> SubmitAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkFanoutPublishCall
{
    ValueTask<ZLinkSubmitResult> SubmitAsync(
        CancellationToken cancellationToken = default);
}

public enum ZLinkSubmitStatus
{
    Submitted = 0,
    Backpressured = 1,
    TimedOut = 2,
    TargetNotFound = 3,
    RouteNotConnected = 4,
    Shutdown = 5
}

public readonly record struct ZLinkSubmitResult(ZLinkSubmitStatus Status);

public readonly record struct ZLinkLogicalMulticastDetail(
    ulong SnapshotRemoteNodeCount,
    ulong AdmittedRemoteNodeCount,
    ulong DroppedRemoteNodeCount,
    ulong UnreachableRemoteNodeCount,
    ulong SnapshotLocalSpotCount,
    ulong AdmittedLocalSpotCount,
    ulong DroppedLocalSpotCount);

public readonly record struct ZLinkPublishResult(
    ZLinkSubmitStatus Status,
    ZLinkLogicalMulticastDetail Detail);

public interface IZLinkWorkerCall<TResult>
{
    IZLinkWorkerCall<TResult> Timeout(TimeSpan timeout);
    void Submit(CancellationToken cancellationToken = default);
    ValueTask<TResult> Async(CancellationToken cancellationToken = default);
    ValueTask<TResult> Yield(CancellationToken cancellationToken = default);
}

public interface IZLinkWorkerOptions
{
    int MinThreads { get; set; }
    int MaxThreads { get; set; }
    TimeSpan IdleTimeout { get; set; }
    int MaxQueueLength { get; set; }
}
```

`SubmitAsync()`는 local outbound queue가 operation을 수락한 결과만 반환한다. 유효한 call은 pending 공간을
확인하기 전에 해당 family가 실제로 사용하는 admission primitive를 non-blocking 방식으로 정확히 한 번
호출한다. Remote 경로는 transport submit을 사용하고, local 경로는 mailbox 또는 relay queue admission을
사용한다. 첫 시도가 즉시 성공하면 pending 공간이 차 있어도 `Submitted`다. Public raw socket이 capacity
부족(`EAGAIN`)을
반환하거나 local admission capacity가 부족할 때만 family가 소유한 send timeout까지 기다리며 원격 handler나
subscriber가 실행될 때까지 기다리지 않는다. 첫 시도 뒤 pending admission 공간까지 가득 차 있으면
`Backpressured`, deadline까지 수락하지 못하면 `TimedOut`이다. Local 경로가 즉시 수락할 수 있는데 pending
공간만 가득 찼다는 이유로 `Backpressured`를 반환하면 안 된다. 논리 target이 없으면 `TargetNotFound`,
target은 확인했지만 route가 준비되지 않았으면 `RouteNotConnected`, drain이나 shutdown 뒤의 신규 admission은
`Shutdown`을 반환한다.

RouteMesh node·channel, Spot과 Actor는 선택한 MeshNode ROUTER send timeout을 사용한다. ClientServer는
client DEALER, classic fanout은 publisher socket, STREAM send·reply는 해당 STREAM socket의 send timeout을
사용한다. Bound session은 local·remote Actor route가 바뀌어도 framework socket send timeout 하나를
사용한다. 공개 설정이 없을 때의 기본값은 1초이며 timeout을 늘려 backpressure나 reconnect 문제를
숨기지 않는다.

Logical Multicast의 `IZLinkPublishCall`만 일반 writable waiter와 pending queue를 사용하지 않는다. Framework는
bounded I/O executor에 direct handoff하고, 즉시 worker slot을 얻지 못하면 raw socket call을 시작하지 않고
`Backpressured`를 반환한다. Slot을 얻으면 bindings의 public raw socket publish를 정확히 한 번 호출한다. 이
call이 시작된 시점이 operation commit barrier다. Raw socket call이 만든 target
[snapshot](../../../../01-glossary.ko.md#snapshot)과 이미 수락한 target을 rollback하거나 전체 publish를 다시 실행하지 않는다. Target별 send timeout
뒤의 capacity 실패는 `TimedOut`으로 바꾸지 않고 `Backpressured`와 `ZLinkLogicalMulticastDetail`에 보존한다.
Snapshot target이 모두 0이면 `TargetNotFound`다. Remote capacity drop이 없고 모든 remote target의 route가
준비되지 않은 경우에는 `Submitted`와 unreachable detail을 반환할 수 있다. Local [Spot](../../../../01-glossary.ko.md#spot) drop은 top-level
status를 바꾸지 않고 detail에만 반영한다.
Remote count는
`SnapshotRemoteNodeCount == AdmittedRemoteNodeCount + DroppedRemoteNodeCount + UnreachableRemoteNodeCount`를
만족한다.

`CancellationToken`이 admission보다 먼저 확정되면 cancelled `ValueTask`로 한 번만 완료하며 별도 submit
status를 만들지 않는다. Pre-cancellation은 runtime admission을 시작하지 않는다. Admission·timeout·[shutdown](../../../../01-glossary.ko.md#shutdown)과
cancellation이 경쟁하면 원자 terminal winner 하나만 완료하고 timeout이나 cancellation 뒤에 late admission을
만들지 않는다. [Logical Multicast](../../../../01-glossary.ko.md#logical-multicast)는 raw socket call이 시작되기 전 cancellation만 operation 시작을 막는다.
Public raw socket
call이 시작된 뒤에는 snapshot operation을 끝까지 실행하고 최종 `ZLinkPublishResult`를 유지한다.

잘못된 인자·handle·상태, 중복 submit과 이미 사용한 reply token은 submit status가 아니라 .NET exceptional
completion으로 처리한다. Timeout이나 cancellation 뒤에는 operation을 자동으로 다시 제출하지 않는다.
`IZLinkMetadataCall<TSelf>`의 정확한 시그니처와 1024-byte 상한은
[Topology configuration §6](03-configuration-topology.ko.md#6-메시징-metadata)이 소유한다. 같은 key를 여러 번 설정하면
마지막 값이 전송된다. Reply는 request metadata를 자동 복사하지 않는다.

Worker call의 `Submit`, `Async`와 `Yield`는
[비동기 실행 정책 §1.2](../../../../04-async-execution-policy.ko.md#12-worker-offload)의 완료 의미를 따른다.
`Yield`는 현재 callback이 `SpotWide` User Spot 공통 gate 또는 Instance Spot gate에서 실행 중일 때만
유효하다. 다른 문맥에서는 worker를 제출하거나 turn을 반납하지 않고 `InvalidConfiguration`으로 완료한다.
Worker option은 host가 시작되기 전에만 설정할 수 있다.

Assembly scan에서 사용하는 최소 attribute 표면은 다음과 같다.

```csharp
[AttributeUsage(AttributeTargets.Class, AllowMultiple = true)]
public sealed class ZLinkHandlerGroupAttribute(string groupName) : Attribute
{
    public string GroupName { get; } = groupName;
}

[AttributeUsage(AttributeTargets.Method)]
public sealed class ZLinkRequestAttribute : Attribute
{
    public string? PacketName { get; init; }
}

[AttributeUsage(AttributeTargets.Method)]
public sealed class ZLinkSendAttribute : Attribute
{
    public string? PacketName { get; init; }
}

[AttributeUsage(AttributeTargets.Method)]
public sealed class ZLinkPublishAttribute : Attribute
{
    public string? PacketName { get; init; }
}

[AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct)]
public sealed class ZLinkPacketAttribute(string packetName) : Attribute
{
    public string PacketName { get; } = packetName;
}
```

Root에 등록한 assembly에서 method attribute를 찾으며, `ZLinkHandlerGroupAttribute`는 해당 handler가
참여하는 handler group을 지정한다. Method의 `PacketName`을 생략하면 message type의
`ZLinkPacketAttribute`를 확인하고, 그것도 없으면 type 이름을 사용한다. Packet name은 등록할 때 한 번
결정되며 codec 선택으로 바뀌지 않는다.
## 4. Handler attribute

```csharp
[AttributeUsage(AttributeTargets.Method)]
public sealed class ZLinkSpotRequestAttribute : Attribute
{
    public string? PacketName { get; init; }
}

[AttributeUsage(AttributeTargets.Class)]
public sealed class ZLinkSpotPacketHandlerAttribute(string packetName) : Attribute
{
    public string PacketName { get; } = packetName;
}

[AttributeUsage(AttributeTargets.Class)]
public sealed class ZLinkSpotRequestHandlerAttribute(string packetName) : Attribute
{
    public string PacketName { get; } = packetName;
}

[AttributeUsage(AttributeTargets.Method)]
public sealed class ZLinkSpotSubscriptionAttribute : Attribute
{
    public ZLinkSpotSubscriptionAttribute(
        string spotNodeName,
        string topic);
    public ZLinkSpotSubscriptionAttribute(
        string spotNodeName,
        string channelName,
        string topic);
    public string SpotNodeName { get; }
    public string ChannelName { get; }
    public string Topic { get; }
}

[AttributeUsage(AttributeTargets.Class)]
public sealed class ZLinkSpotSubscriptionHandlerAttribute : Attribute
{
    public ZLinkSpotSubscriptionHandlerAttribute(string topic);
    public ZLinkSpotSubscriptionHandlerAttribute(
        string channelName,
        string topic);
    public string ChannelName { get; }
    public string Topic { get; }
}

[AttributeUsage(AttributeTargets.Method)]
public sealed class ZLinkSpotActorSendAttribute : Attribute
{
    public string? PacketName { get; init; }
}

[AttributeUsage(AttributeTargets.Class)]
public sealed class ZLinkSpotActorSendHandlerAttribute(string packetName) : Attribute
{
    public string PacketName { get; } = packetName;
}

[AttributeUsage(AttributeTargets.Method)]
public sealed class ZLinkSpotActorRequestAttribute : Attribute
{
    public string? PacketName { get; init; }
}

[AttributeUsage(AttributeTargets.Class)]
public sealed class ZLinkSpotActorRequestHandlerAttribute(string packetName) : Attribute
{
    public string PacketName { get; } = packetName;
}

[AttributeUsage(AttributeTargets.Class)]
public sealed class ZLinkSpotTimerHandlerAttribute(
    string name,
    double periodMilliseconds) : Attribute
{
    public string Name { get; } = name;
    public double PeriodMilliseconds { get; } = periodMilliseconds;
}

[AttributeUsage(AttributeTargets.Method)]
public sealed class ZLinkStreamPacketAttribute : Attribute;
```
