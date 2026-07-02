namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Highest generation this runtime has accepted per location key, shared by
/// every read surface (resolvers and the runtime query). A read whose
/// generation is strictly older than the recorded one is a lagging replica
/// view and never counts as a success result (draft 8.1). Bounded; evicting
/// an entry only weakens the guard for that key, never the correctness of
/// fencing (the store still enforces owner/generation on writes).
/// </summary>
internal sealed class ZLinkObservedLocationGenerations
{
    private readonly Observed<ZLinkPeerLocationKey> _peers = new();
    private readonly Observed<ZLinkSpotLocationKey> _spots = new();
    private readonly Observed<ZLinkActorLocationKey> _actors = new();
    private readonly Observed<ZLinkRouteLocationKey> _routes = new();

    internal bool AcceptPeer(ZLinkPeerLocation row) =>
        _peers.Accept(
            new ZLinkPeerLocationKey(
                row.AutoConnectType, row.MeshName, row.Role, row.NodeRid, row.Endpoint),
            row.Generation);

    internal bool AcceptSpot(ZLinkSpotLocation row) =>
        _spots.Accept(new ZLinkSpotLocationKey(row.MeshName, row.SpotRid), row.Generation);

    internal bool AcceptActor(ZLinkActorLocation row) =>
        _actors.Accept(new ZLinkActorLocationKey(row.ActorType, row.ActorId), row.Generation);

    internal bool AcceptRoute(ZLinkRouteLocation row) =>
        _routes.Accept(new ZLinkRouteLocationKey(row.RouteKind, row.RouteKey), row.Generation);

    private sealed class Observed<TKey>
        where TKey : notnull
    {
        private const int MaxEntries = 8192;
        private readonly object _gate = new();
        private readonly Dictionary<TKey, long> _generations = [];

        internal bool Accept(TKey key, long generation)
        {
            lock (_gate)
            {
                if (_generations.TryGetValue(key, out var observed) && generation < observed)
                {
                    return false;
                }

                if (!_generations.ContainsKey(key) && _generations.Count >= MaxEntries)
                {
                    _generations.Clear();
                }

                _generations[key] = generation;
                return true;
            }
        }
    }
}
