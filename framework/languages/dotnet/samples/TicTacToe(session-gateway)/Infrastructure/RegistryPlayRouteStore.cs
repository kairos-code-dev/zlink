using Systems.Zlink;
using Zlink.Framework.Streams;

namespace TicTacToe.SessionGateway.Infrastructure;

public sealed class RegistryPlayRouteStore(
    IRegistryDiscoveryMetadata registry,
    string metadataNamespace)
    : IZLinkActorPlayRouteResolver,
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
