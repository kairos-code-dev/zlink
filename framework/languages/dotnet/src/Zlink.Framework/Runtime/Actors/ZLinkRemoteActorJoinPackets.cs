namespace Zlink.Framework.Runtime.Actors;

internal static class ZLinkRemoteActorJoinPackets
{
    public const string RequestPacketName = "__zlink.actor.join_spot.request";
    public const string BoundSessionBindPacketName = "zlink.framework.actor.bound_session.bind";
    public const string SessionDisconnectedPacketName = "zlink.framework.actor.session_disconnected";
}

internal sealed record ZLinkRemoteActorJoinRequest(
    string ActorId,
    string ActorType,
    byte[]? BoundSessionNodeRid,
    byte[]? BoundSessionRid,
    string RequestContentType,
    byte[] Request);

internal sealed record ZLinkRemoteActorJoinReply(
    bool Accepted,
    byte[] ActorNodeRid,
    string ActorId,
    ulong ActorGeneration,
    string ReplyContentType,
    byte[] Reply);
