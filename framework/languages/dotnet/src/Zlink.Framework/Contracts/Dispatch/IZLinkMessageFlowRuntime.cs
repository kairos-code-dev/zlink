using Systems.Zlink;

namespace Zlink.Framework.Contracts.Dispatch;

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

public interface IZLinkMessageFlowRuntime
{
    ZLinkRuntimeMessageFlowMode Mode { get; set; }

    IAsyncEnumerable<ZLinkRuntimeMessageFlowEvent> ObserveAsync(
        int capacity = 1024,
        CancellationToken cancellationToken = default);
}
