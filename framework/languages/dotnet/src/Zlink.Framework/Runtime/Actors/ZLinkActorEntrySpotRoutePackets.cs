namespace Zlink.Framework.Runtime.Actors;

internal static class ZLinkActorEntrySpotRoutePackets
{
    public const string JoinEntrySpotPacketName = "__zlink.actor.joinEntrySpot";
}

internal sealed record ZLinkActorEntrySpotRouteJoinRequest(
    string ActorId,
    string ActorType,
    string SourceNodeRid,
    ulong SourceGeneration);

internal sealed record ZLinkActorEntrySpotRouteJoinReply(
    string ActorId,
    string ActorType,
    string TargetNodeRid,
    ulong ActorGeneration);
