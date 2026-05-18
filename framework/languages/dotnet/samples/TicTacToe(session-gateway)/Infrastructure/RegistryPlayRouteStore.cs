using Systems.Zlink;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.SessionGateway.Infrastructure;

public sealed class RegistryPlayRouteStore(
    IRegistryDiscoveryMetadata registry,
    string metadataNamespace)
    : IZLinkActorPlayRouteResolver,
      IZLinkSpotRouteResolver,
      ISpotRouteResolver,
      ISpotRouteWriter
{
    public ValueTask BindActorPlayAsync(
        string actorId,
        ZLinkActorRoute route,
        CancellationToken cancellationToken)
    {
        return registry.PutAsync(
            ActorPlayKey(actorId),
            ToMetadata(route),
            cancellationToken);
    }

    public async ValueTask<ZLinkActorRoute> ResolvePlayRouteAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        var entry = await registry.GetRequiredAsync(
            ActorPlayKey(actorId),
            cancellationToken).ConfigureAwait(false);

        return ReadPlayRoute(entry);
    }

    public ValueTask BindSpotRouteAsync(
        RoutingId spotRid,
        ZLinkActorRoute route,
        CancellationToken cancellationToken)
    {
        return registry.PutAsync(
            SpotKey(spotRid),
            ToMetadata(route),
            cancellationToken);
    }

    public async ValueTask<ZLinkActorRoute> ResolveSpotRouteAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        var entry = await registry.GetRequiredAsync(
            SpotKey(spotRid),
            cancellationToken).ConfigureAwait(false);

        return ReadPlayRoute(entry);
    }

    public async ValueTask<ZLinkSpotRoute> ResolveSpotRouteAsync(
        string spotName,
        CancellationToken cancellationToken)
    {
        return await ResolveSpotRouteAsync(
            ZLinkSpotId.FromRoutingId(RoutingId.FromString(spotName)),
            cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask<ZLinkSpotRoute> ResolveSpotRouteAsync(
        ZLinkSpotId spotId,
        CancellationToken cancellationToken)
    {
        var spotRid = spotId.ToRoutingId();
        var entry = await registry.GetRequiredAsync(
            SpotKey(spotRid),
            cancellationToken).ConfigureAwait(false);
        var actorRoute = ReadPlayRoute(entry);
        return new ZLinkSpotRoute(
            actorRoute.RouterChannelId,
            actorRoute.TargetNodeRid,
            spotId);
    }

    private string ActorPlayKey(string actorId)
    {
        return $"{metadataNamespace}/actor-play/{actorId}";
    }

    private string SpotKey(RoutingId spotRid)
    {
        return $"{metadataNamespace}/spot/{spotRid.ToHex()}";
    }

    private static Dictionary<string, string> ToMetadata(ZLinkActorRoute route)
    {
        return new Dictionary<string, string>(StringComparer.Ordinal)
        {
            ["routerChannelId"] = route.RouterChannelId,
            ["targetNodeRid"] = route.TargetNodeRid.ToHex(),
        };
    }

    private static ZLinkActorRoute ReadPlayRoute(IRegistryMetadataEntry entry)
    {
        return new ZLinkActorRoute(
            entry.Require("routerChannelId"),
            RoutingId.FromString(entry.Require("targetNodeRid")));
    }
}
