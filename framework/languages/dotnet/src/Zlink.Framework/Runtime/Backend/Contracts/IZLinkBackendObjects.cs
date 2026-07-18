namespace Zlink.Framework.Runtime.Backend.Contracts;

internal enum ZLinkBackendSpotDispatchEvent
{
    Internal = 0,
    RouteReadable = 1,
    ChannelReplyReadable = 2,
    ActorJoinReadable = 3,
    ActorReadable = 4,
    SubscribeReadable = 5,
    ActorLifecycleReadable = 6
}

internal enum ZLinkBackendActorLifecycleEventKind
{
    Joined = 1,
    Left = 2,
    Disconnected = 3
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

internal readonly record struct ZLinkBackendSpotActorLifecycleInfo(
    ZLinkBackendActorRef? PreviousActor,
    ZLinkBackendActorRef? CurrentActor,
    RoutingId? PreviousSpotRid,
    RoutingId? CurrentSpotRid,
    ulong JoinEpoch,
    uint Flags);

internal readonly record struct ZLinkBackendSpotActorLifecycleEvent(
    ZLinkBackendActorLifecycleEventKind Kind,
    ZLinkBackendSpotActorLifecycleInfo Info);

internal sealed record ZLinkBackendActorPart(
    ZLinkBackendActorRef Actor,
    RoutingId SourceNodeRid,
    RoutingId SourceSessionRid,
    ulong RequestId,
    uint Flags,
    Message Message,
    bool More,
    ZLinkBackendActorRef? ReplyActor = null);

internal class ZLinkBackendActorJoinRequest(
    ZLinkBackendActorRef sourceActor,
    ZLinkBackendActorRef targetActor,
    RoutingId sourceNodeRid,
    RoutingId targetSpotRid,
    ulong joinEpoch,
    Message message,
    IReadOnlyList<Message> parts)
{
    public ZLinkBackendActorRef SourceActor { get; } = sourceActor;

    public ZLinkBackendActorRef TargetActor { get; } = targetActor;

    public RoutingId SourceNodeRid { get; } = sourceNodeRid;

    public RoutingId TargetSpotRid { get; } = targetSpotRid;

    public ulong JoinEpoch { get; } = joinEpoch;

    public Message Message { get; } = message;

    public IReadOnlyList<Message> Parts { get; } = parts;
}

internal readonly record struct ZLinkBackendSpotDispatchInfo(
    ZLinkBackendSpotDispatchEvent Event,
    Action? DrainChannelReply = null,
    IReadOnlyList<ZLinkBackendActorPart>? ActorParts = null,
    IReadOnlyList<ZLinkBackendRouteReceived>? RoutedMessages = null);

internal readonly record struct ZLinkBackendSocketMonitorEvent(
    ZLinkSocketNativeEventType NativeEvent,
    RoutingId? RoutingId,
    string LocalAddr,
    string RemoteAddr,
    uint Value);

internal interface IZLinkBackendContext : IAsyncDisposable
{
    void Shutdown();
}
