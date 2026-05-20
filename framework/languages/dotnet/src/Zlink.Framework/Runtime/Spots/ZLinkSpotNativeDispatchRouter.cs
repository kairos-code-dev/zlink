namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkSpotNativeDispatchRouter
{
    public static void Attach(
        IZLinkBackendSpot nativeSpot,
        Action<IReadOnlyList<Received>> routeReadable,
        Action<Action?> channelReplyReadable,
        Action actorJoinReadable,
        Action<IReadOnlyList<ZLinkBackendActorPart>> actorPartsReadable)
    {
        nativeSpot.OnDispatchEvent(info =>
        {
            switch (info.Event)
            {
                case ZLinkBackendSpotDispatchEvent.RouteReadable:
                    routeReadable(info.RoutedMessages ?? []);
                    break;
                case ZLinkBackendSpotDispatchEvent.ChannelReplyReadable:
                    channelReplyReadable(info.DrainChannelReply);
                    break;
                case ZLinkBackendSpotDispatchEvent.ActorJoinReadable:
                    actorJoinReadable();
                    break;
                case ZLinkBackendSpotDispatchEvent.ActorReadable
                    when info.ActorParts is { Count: > 0 } actorParts:
                    actorPartsReadable(actorParts);
                    break;
            }
        });
    }
}
