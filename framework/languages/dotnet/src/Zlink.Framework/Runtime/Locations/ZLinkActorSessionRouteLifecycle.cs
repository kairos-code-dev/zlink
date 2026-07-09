namespace Zlink.Framework.Runtime.Locations;

internal sealed class ZLinkActorSessionRouteLifecycle(ZLinkLocationRuntime runtime)
{
    private readonly object _gate = new();
    private readonly Dictionary<string, long> _routes = new(StringComparer.Ordinal);

    internal void OnActorSessionBound(
        RoutingId sessionRid,
        string actorId,
        RoutingId ownerNodeRid)
    {
        _ = RunGuardedAsync(() => BindAsync(
            sessionRid,
            actorId,
            ownerNodeRid,
            CancellationToken.None));
    }

    internal void OnActorSessionUnbound(RoutingId sessionRid)
    {
        _ = RunGuardedAsync(() => RemoveAsync(sessionRid, CancellationToken.None));
    }

    internal async ValueTask BindAsync(
        RoutingId sessionRid,
        string actorId,
        RoutingId ownerNodeRid,
        CancellationToken cancellationToken = default)
    {
        var routeKey = sessionRid.ToHex();
        var row = new ZLinkRouteLocation(
            ZLinkRouteKind.ActorSession,
            routeKey,
            ownerNodeRid,
            OwnerId: string.Empty,
            Generation: 0,
            Value: System.Text.Encoding.UTF8.GetBytes(actorId),
            UpdatedAt: default);
        var result = await runtime.WriteRouteAsync(row, ZLinkLocationWriteIntent.NewClaim, cancellationToken)
            .ConfigureAwait(false);
        if (result.Status == ZLinkLocationWriteStatus.RejectedConflict)
        {
            // A session rebind moves the route to this node; replacing the
            // previous owner's row is an owner change, hence Takeover.
            result = await runtime.WriteRouteAsync(row, ZLinkLocationWriteIntent.Takeover, cancellationToken)
                .ConfigureAwait(false);
        }

        if (result.Status != ZLinkLocationWriteStatus.Stored)
        {
            return;
        }

        var canonical = ZLinkLocationKeyCodec.EncodeRouteKey(
            new ZLinkRouteLocationKey(ZLinkRouteKind.ActorSession, routeKey));
        lock (_gate)
        {
            _routes[canonical] = result.Generation;
        }
    }

    internal async ValueTask RemoveAsync(
        RoutingId sessionRid,
        CancellationToken cancellationToken = default)
    {
        var key = new ZLinkRouteLocationKey(ZLinkRouteKind.ActorSession, sessionRid.ToHex());
        var canonical = ZLinkLocationKeyCodec.EncodeRouteKey(key);
        long generation;
        lock (_gate)
        {
            if (!_routes.Remove(canonical, out generation))
            {
                return;
            }
        }

        await runtime.RemoveRouteAsync(key, generation, cancellationToken).ConfigureAwait(false);
    }

    internal void OnOwnershipLost(string canonicalKey)
    {
        lock (_gate)
        {
            _routes.Remove(canonicalKey);
        }
    }

    private static async Task RunGuardedAsync(Func<ValueTask> operation)
    {
        try
        {
            await operation().ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            ZLinkFrameworkDebugLog.SpotDiscovery($"actor session route lifecycle error: {exception.Message}");
        }
    }
}
