namespace Zlink.Framework.Runtime.Backend.DotNet.Mappings;

internal static class ZLinkDotNetBackendMappings
{
    public static ZLinkSpotKind ToFramework(this SpotKind spotKind)
    {
        return spotKind switch
        {
            SpotKind.Entry => ZLinkSpotKind.Entry,
            SpotKind.User => ZLinkSpotKind.User,
            _ => ZLinkSpotKind.Invalid
        };
    }

    public static ZLinkSpotNodeStatus ToFramework(this SpotNodeStatus status)
    {
        return new ZLinkSpotNodeStatus(
            status.ChannelName,
            status.LocalEndpoint,
            status.NodeRoutingId,
            (ZLinkSpotNodeState)status.State,
            status.ConfiguredPeerCount,
            status.ActivePeerCount,
            status.ConnectedPeerCount,
            status.SubjectCount,
            status.ReadySubjectCount,
            status.LastError,
            status.LastChangedMs);
    }

    public static ZLinkSpotNodePeerEntry ToFramework(this SpotNodePeerEntry entry)
    {
        return new ZLinkSpotNodePeerEntry(
            entry.ChannelName,
            entry.LocalEndpoint,
            entry.PeerEndpoint,
            (ZLinkSpotPeerSource)entry.Source,
            (ZLinkSpotPeerKind)entry.Kind,
            (ZLinkSpotPeerState)entry.State,
            entry.Weight,
            entry.ConnectedSinceMs,
            entry.LastChangedMs);
    }

    public static ZLinkSpotNodeSubjectEntry ToFramework(this SpotNodeSubjectEntry entry)
    {
        return new ZLinkSpotNodeSubjectEntry(
            (ZLinkSpotRole)entry.Role,
            entry.Subject,
            (ZLinkSubjectKind)entry.SubjectKind,
            entry.ReadyPeerCount,
            entry.ActivePeerCount,
            entry.LastChangedMs);
    }

    public static ZLinkBackendSocketMonitorEvent ToFramework(this MonitorEvent monitorEvent)
    {
        return new ZLinkBackendSocketMonitorEvent(
            (ZLinkSocketNativeEventType)monitorEvent.Event,
            monitorEvent.RoutingId,
            monitorEvent.LocalAddr,
            monitorEvent.RemoteAddr,
            monitorEvent.Value);
    }

    public static ZLinkBackendSpotDispatchInfo ToFramework(this SpotDispatchInfo info)
    {
        IReadOnlyList<ZLinkBackendActorPart>? actorParts = null;
        if (info.Event == SpotDispatchEvent.ActorReadable && info.ActorMessages.Count > 0)
        {
            var mapped = new List<ZLinkBackendActorPart>();
            foreach (var message in info.ActorMessages)
                mapped.AddRange(message.ToFrameworkParts());
            actorParts = mapped;
        }

        return new ZLinkBackendSpotDispatchInfo(
            info.Event switch
            {
                SpotDispatchEvent.SubscribeReadable => ZLinkBackendSpotDispatchEvent.SubscribeReadable,
                SpotDispatchEvent.RoutedReadable => ZLinkBackendSpotDispatchEvent.RouteReadable,
                SpotDispatchEvent.ChannelReplyReadable => ZLinkBackendSpotDispatchEvent.ChannelReplyReadable,
                SpotDispatchEvent.ActorJoinReadable => ZLinkBackendSpotDispatchEvent.ActorJoinReadable,
                SpotDispatchEvent.ActorReadable => ZLinkBackendSpotDispatchEvent.ActorReadable,
                SpotDispatchEvent.ActorLifecycleReadable => ZLinkBackendSpotDispatchEvent.ActorLifecycleReadable,
                _ => ZLinkBackendSpotDispatchEvent.Internal
            },
            info.Event == SpotDispatchEvent.ChannelReplyReadable
            && info.SubjectKind == SpotDispatchSubjectKind.ChannelDealer
                ? info.DrainChannelReply
                : null,
            actorParts);
    }

    public static IReadOnlyList<ZLinkBackendActorPart> ToFrameworkParts(
        this ActorReceived message)
    {
        var parts = new ZLinkBackendActorPart[message.Parts.Count];
        for (var i = 0; i < parts.Length; i++)
            parts[i] = new ZLinkBackendActorPart(
                message.Info.Actor.ToBackend(),
                message.Info.SourceNodeRid,
                message.Info.SourceSessionRid,
                message.Info.RequestId,
                message.Info.Flags,
                message.Parts[i],
                i + 1 < parts.Length);

        return parts;
    }

    public static ZLinkBackendSpotActorLifecycleInfo ToFramework(
        this SpotActorLifecycleInfo info)
    {
        return new ZLinkBackendSpotActorLifecycleInfo(
            info.PreviousActor is { } previousActor
                ? previousActor.ToBackend()
                : null,
            info.CurrentActor is { } currentActor
                ? currentActor.ToBackend()
                : null,
            info.PreviousSpotRid,
            info.CurrentSpotRid,
            info.JoinEpoch,
            info.Flags);
    }

    public static ZLinkBackendSpotActorLifecycleEvent ToFramework(
        this SpotActorLifecycleEvent lifecycle)
    {
        return new ZLinkBackendSpotActorLifecycleEvent(
            lifecycle.Kind switch
            {
                SpotActorLifecycleEventKind.Joined => ZLinkBackendActorLifecycleEventKind.Joined,
                SpotActorLifecycleEventKind.Left => ZLinkBackendActorLifecycleEventKind.Left,
                SpotActorLifecycleEventKind.Disconnected => ZLinkBackendActorLifecycleEventKind.Disconnected,
                _ => ZLinkBackendActorLifecycleEventKind.Joined
            },
            lifecycle.Info.ToFramework());
    }

    public static ZLinkBackendActorRef ToBackend(this ActorRef actorRef)
    {
        return new ZLinkBackendActorRef(actorRef.NodeRid, actorRef.ActorId, actorRef.Generation);
    }

    public static ActorRef ToNative(this ZLinkBackendActorRef actorRef)
    {
        return new ActorRef(actorRef.NodeRid, actorRef.ActorId, actorRef.Generation);
    }
}
