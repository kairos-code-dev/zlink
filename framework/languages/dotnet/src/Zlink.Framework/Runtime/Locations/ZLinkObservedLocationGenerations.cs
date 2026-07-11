using Zlink.Framework.Internal.Locations;

namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Highest generation this runtime has accepted per location key, shared by
/// every read surface (resolvers and the runtime query). A read whose
/// generation is strictly older than the recorded one is a lagging replica
/// view and never counts as a success result. Entries remain for the runtime
/// lifetime because forgetting one would allow a lagging replica value to
/// become current again.
/// </summary>
internal sealed class ZLinkObservedLocationGenerations
{
    private readonly Observed<ZLinkPeerLocationKey> _peers = new();
    private readonly Observed<ZLinkSpotLocationKey> _spots = new();
    private readonly Observed<ZLinkActorLocationKey> _actors = new();
    private readonly Observed<ZLinkRouteLocationKey> _routes = new();

    internal bool AcceptPeer(ZLinkPeerLocation row)
    {
        if (!ZLinkCanonicalLocationKeyFormatter.IsKnown(row.AutoConnectType)
            || !ZLinkCanonicalLocationKeyFormatter.IsKnown(row.Role))
        {
            ZLinkFrameworkDebugLog.SpotDiscovery(
                $"peer row ignored: unknown auto-connect type '{row.AutoConnectType}' "
                + $"or role '{row.Role}' (mesh '{row.MeshName}', endpoint '{row.Endpoint}')");
            return false;
        }

        return _peers.Accept(
            new ZLinkPeerLocationKey(
                row.AutoConnectType, row.MeshName, row.Role, row.NodeRid, row.Endpoint),
            row.Generation);
    }

    internal void ObservePeer(ZLinkPeerLocation row) =>
        _peers.Observe(
            new ZLinkPeerLocationKey(
                row.AutoConnectType, row.MeshName, row.Role, row.NodeRid, row.Endpoint),
            row.Generation);

    internal void ObservePeer(ZLinkPeerLocationKey key, long generation) =>
        _peers.Observe(key, generation);

    internal bool AcceptSpot(ZLinkSpotLocation row) =>
        _spots.Accept(new ZLinkSpotLocationKey(row.MeshName, row.SpotRid), row.Generation);

    internal void ObserveSpot(ZLinkSpotLocation row) =>
        _spots.Observe(new ZLinkSpotLocationKey(row.MeshName, row.SpotRid), row.Generation);

    internal void ObserveSpot(ZLinkSpotLocationKey key, long generation) =>
        _spots.Observe(key, generation);

    internal bool AcceptActor(ZLinkActorLocation row) =>
        _actors.Accept(new ZLinkActorLocationKey(row.ActorId), row.Generation);

    internal void ObserveActor(ZLinkActorLocation row) =>
        _actors.Observe(new ZLinkActorLocationKey(row.ActorId), row.Generation);

    internal void ObserveActor(ZLinkActorLocationKey key, long generation) =>
        _actors.Observe(key, generation);

    internal bool AcceptRoute(ZLinkRouteLocation row) =>
        _routes.Accept(new ZLinkRouteLocationKey(row.RouteKind, row.RouteKey), row.Generation);

    internal void ObserveRoute(ZLinkRouteLocation row) =>
        _routes.Observe(new ZLinkRouteLocationKey(row.RouteKind, row.RouteKey), row.Generation);

    internal void ObserveRoute(ZLinkRouteLocationKey key, long generation) =>
        _routes.Observe(key, generation);

    private sealed class Observed<TKey>
        where TKey : notnull
    {
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

                _generations[key] = generation;
                return true;
            }
        }

        internal void Observe(TKey key, long generation)
        {
            lock (_gate)
            {
                if (!_generations.TryGetValue(key, out var observed) || generation > observed)
                    _generations[key] = generation;
            }
        }
    }
}
