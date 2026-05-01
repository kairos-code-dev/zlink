using Zlink;

namespace TicTacToe.SessionActorDispatch.Infrastructure;

internal sealed class RegistryPlayRouteStore(
    IRegistryDiscoveryMetadata registry,
    string metadataNamespace)
{
    public ValueTask BindActorPlayAsync(
        string actorId,
        ZLinkPlayRoute route,
        CancellationToken cancellationToken)
    {
        return registry.PutAsync(
            ActorPlayKey(actorId),
            ToMetadata(route),
            cancellationToken);
    }

    public async ValueTask<ZLinkPlayRoute> ResolveActorPlayAsync(
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
        ZLinkPlayRoute route,
        CancellationToken cancellationToken)
    {
        return registry.PutAsync(
            MatchKey(matchId),
            ToMetadata(route),
            cancellationToken);
    }

    public async ValueTask<ZLinkPlayRoute> ResolveMatchAsync(
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

    private static Dictionary<string, string> ToMetadata(ZLinkPlayRoute route)
    {
        return new Dictionary<string, string>(StringComparer.Ordinal)
        {
            ["routerChannelId"] = route.RouterChannelId,
            ["playRouterId"] = route.PlayRouterId.ToHex(),
        };
    }

    private static ZLinkPlayRoute ReadPlayRoute(IRegistryMetadataEntry entry)
    {
        return new ZLinkPlayRoute(
            entry.Require("routerChannelId"),
            RoutingId.FromString(entry.Require("playRouterId")));
    }
}
