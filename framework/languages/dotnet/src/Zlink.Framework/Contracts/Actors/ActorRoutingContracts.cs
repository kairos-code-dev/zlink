namespace Zlink.Framework.Contracts.Actors;

public interface IZLinkActorRemoteAddressResolver
{
    ValueTask<ZLinkActorRemoteLocation> ResolveActorRemoteAddressAsync(
        string actorId,
        CancellationToken cancellationToken);
}

public readonly record struct ZLinkActorRemoteLocation(
    string ActorId,
    ZLinkActorRemoteAddress RemoteAddress,
    RoutingId CurrentSpotRid,
    ZLinkSpotKind CurrentSpotKind);

public readonly record struct ZLinkActorRemoteAddress(
    string RouterChannelId,
    RoutingId TargetNodeRid,
    ulong ActorGeneration);
