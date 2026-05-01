using Zlink;
using Zlink.Framework.Streams;

namespace TicTacToe.SessionActorDispatch.Infrastructure;

internal sealed class RegistryPlayRouteStore(
    IRegistryDiscoveryMetadata registry,
    string metadataNamespace) : IZLinkActorPlayRouteResolver
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

    public ValueTask BindMatchAsync(
        string matchId,
        ZLinkActorRoute route,
        CancellationToken cancellationToken)
    {
        return registry.PutAsync(
            MatchKey(matchId),
            ToMetadata(route),
            cancellationToken);
    }

    public async ValueTask<ZLinkActorRoute> ResolveMatchAsync(
        string matchId,
        CancellationToken cancellationToken)
    {
        var entry = await registry.GetRequiredAsync(
            MatchKey(matchId),
            cancellationToken).ConfigureAwait(false);

        return ReadPlayRoute(entry);
    }

    private string ActorPlayKey(string actorId)
    {
        return $"{metadataNamespace}/actor-play/{actorId}";
    }

    private string MatchKey(string matchId)
    {
        return $"{metadataNamespace}/match/{matchId}";
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
