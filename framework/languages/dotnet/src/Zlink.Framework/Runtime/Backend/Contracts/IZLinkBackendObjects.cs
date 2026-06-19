using Zlink.Framework;

namespace Zlink.Framework.Runtime.Backend.Contracts;

internal enum ZLinkBackendSpotDispatchEvent
{
    Internal = 0,
    RouteReadable = 1,
    ChannelReplyReadable = 2,
    ActorJoinReadable = 3,
    ActorReadable = 4,
    SubscribeReadable = 5,
}

internal readonly record struct ZLinkBackendActorRef(
    RoutingId NodeRid,
    string ActorId,
    ulong Generation);

internal readonly record struct ZLinkBackendActorJoinResult(
    RequestResult Result,
    int JoinResultCode,
    ZLinkBackendActorRef Actor,
    RoutingId JoinedSpotRid,
    ulong JoinEpoch,
    uint Flags);

internal readonly record struct ZLinkBackendActorJoinEntrySpotResult(
    RequestResult Result,
    int JoinResultCode,
    ZLinkBackendActorRef Actor,
    RoutingId TargetNodeRid,
    RoutingId JoinedSpotRid,
    ulong JoinEpoch,
    uint Flags);

internal delegate void ActorJoinCallback(
    ZLinkBackendActorJoinResult result,
    IReadOnlyList<Message> parts);

internal delegate void ActorJoinEntrySpotCallback(
    ZLinkBackendActorJoinEntrySpotResult result,
    IReadOnlyList<Message> parts);

internal sealed record ZLinkBackendDiscoveryRoute(
    RoutingId OwnerRoutingId,
    Message Value) : IDisposable, IAsyncDisposable
{
    public void Dispose()
    {
        Value.Dispose();
    }

    public ValueTask DisposeAsync()
    {
        Value.Dispose();
        return ValueTask.CompletedTask;
    }
}

internal readonly record struct ZLinkBackendSpotActorLifecycleInfo(
    ZLinkBackendActorRef? PreviousActor,
    ZLinkBackendActorRef? CurrentActor,
    RoutingId? PreviousSpotRid,
    RoutingId? CurrentSpotRid,
    ulong JoinEpoch,
    uint Flags);

internal sealed record ZLinkBackendActorPart(
    ZLinkBackendActorRef Actor,
    RoutingId SourceNodeRid,
    RoutingId SourceSessionRid,
    Message Message,
    bool More);

internal sealed record ZLinkBackendActorJoinRequest(
    ZLinkBackendActorRef SourceActor,
    ZLinkBackendActorRef TargetActor,
    RoutingId SourceNodeRid,
    RoutingId TargetSpotRid,
    ulong JoinEpoch,
    Message Message,
    IReadOnlyList<Message> Parts)
{
    internal object? NativeRequest { get; init; }
}

internal readonly record struct ZLinkBackendSpotDispatchInfo(
    ZLinkBackendSpotDispatchEvent Event,
    Action? DrainChannelReply = null,
    IReadOnlyList<ZLinkBackendActorPart>? ActorParts = null,
    IReadOnlyList<Received>? RoutedMessages = null);

internal readonly record struct ZLinkBackendSocketMonitorEvent(
    ZLinkSocketNativeEventType NativeEvent,
    RoutingId? RoutingId,
    string LocalAddr,
    string RemoteAddr,
    uint Value);

internal interface IZLinkBackendObject
{
    object NativeInstance { get; }
}

internal interface IZLinkBackendContext : IZLinkBackendObject, IAsyncDisposable
{
    void Shutdown();
}
