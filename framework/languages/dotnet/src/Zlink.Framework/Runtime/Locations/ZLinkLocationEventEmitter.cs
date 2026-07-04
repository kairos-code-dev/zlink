using Zlink.Framework.Runtime.Configuration;

namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Publishes the push-based location monitoring events of draft 20.5:
/// row update/remove and resolve-miss events for the location-peer,
/// location-spot, location-actor and location-route sources, auto-connect
/// desired set changes, and cache invalidations under the location-runtime
/// source. Every method is a no-op unless the matching source is registered
/// through the monitoring options, so location flows pay nothing when
/// monitoring is off. Publish failures never break a location flow.
/// </summary>
internal sealed class ZLinkLocationEventEmitter
{
    /// <summary>Emitter used when monitoring is not configured; every
    /// method is a no-op.</summary>
    internal static readonly ZLinkLocationEventEmitter Disabled = new(null, null);

    private readonly IZLinkRuntimeEventPublisher? _publisher;
    private readonly IReadOnlyCollection<string> _runtimeSources;
    private readonly IReadOnlyCollection<string> _peerSources;
    private readonly IReadOnlyCollection<string> _spotSources;
    private readonly IReadOnlyCollection<string> _actorSources;
    private readonly IReadOnlyCollection<string> _routeSources;

    internal ZLinkLocationEventEmitter(
        ZLinkMonitoringRegistration? registration,
        IZLinkRuntimeEventPublisher? publisher)
    {
        _publisher = publisher;
        _runtimeSources = registration is null
            ? Array.Empty<string>()
            : registration.LocationRuntimeSources.Keys;
        _peerSources = registration?.LocationPeerSources ?? (IReadOnlyCollection<string>)Array.Empty<string>();
        _spotSources = registration?.LocationSpotSources ?? (IReadOnlyCollection<string>)Array.Empty<string>();
        _actorSources = registration?.LocationActorSources ?? (IReadOnlyCollection<string>)Array.Empty<string>();
        _routeSources = registration?.LocationRouteSources ?? (IReadOnlyCollection<string>)Array.Empty<string>();
    }

    internal ValueTask PeerRowUpdatedAsync(ZLinkPeerLocation peer, CancellationToken ct) =>
        EmitAsync(_peerSources, source => new ZLinkLocationPeerEvent(
            source, DateTimeOffset.UtcNow, ZLinkLocationPeerEventKind.RowUpdated,
            new ZLinkPeerLocationKey(
                peer.AutoConnectType, peer.MeshName, peer.Role, peer.NodeRid, peer.Endpoint),
            peer, null), ct);

    internal ValueTask PeerRowRemovedAsync(ZLinkPeerLocationKey key, CancellationToken ct) =>
        EmitAsync(_peerSources, source => new ZLinkLocationPeerEvent(
            source, DateTimeOffset.UtcNow, ZLinkLocationPeerEventKind.RowRemoved,
            key, null, null), ct);

    internal ValueTask DesiredSetChangedAsync(
        ZLinkAutoConnectDesiredSetChange change,
        CancellationToken ct) =>
        EmitAsync(_peerSources, source => new ZLinkLocationPeerEvent(
            source, DateTimeOffset.UtcNow, ZLinkLocationPeerEventKind.DesiredSetChanged,
            null, null, change), ct);

    internal ValueTask SpotRowUpdatedAsync(ZLinkSpotLocation spot, CancellationToken ct) =>
        EmitAsync(_spotSources, source => new ZLinkLocationSpotEvent(
            source, DateTimeOffset.UtcNow, ZLinkLocationSpotEventKind.RowUpdated,
            new ZLinkSpotLocationKey(spot.MeshName, spot.SpotRid), spot), ct);

    internal ValueTask SpotRowRemovedAsync(ZLinkSpotLocationKey key, CancellationToken ct) =>
        EmitAsync(_spotSources, source => new ZLinkLocationSpotEvent(
            source, DateTimeOffset.UtcNow, ZLinkLocationSpotEventKind.RowRemoved, key, null), ct);

    internal ValueTask SpotResolveMissAsync(ZLinkSpotLocationKey key, CancellationToken ct) =>
        EmitAsync(_spotSources, source => new ZLinkLocationSpotEvent(
            source, DateTimeOffset.UtcNow, ZLinkLocationSpotEventKind.ResolveMiss, key, null), ct);

    internal ValueTask ActorRowUpdatedAsync(ZLinkActorLocation actor, CancellationToken ct) =>
        EmitAsync(_actorSources, source => new ZLinkLocationActorEvent(
            source, DateTimeOffset.UtcNow, ZLinkLocationActorEventKind.RowUpdated,
            new ZLinkActorLocationKey(actor.ActorId), actor), ct);

    internal ValueTask ActorRowRemovedAsync(ZLinkActorLocationKey key, CancellationToken ct) =>
        EmitAsync(_actorSources, source => new ZLinkLocationActorEvent(
            source, DateTimeOffset.UtcNow, ZLinkLocationActorEventKind.RowRemoved, key, null), ct);

    internal ValueTask ActorResolveMissAsync(ZLinkActorLocationKey key, CancellationToken ct) =>
        EmitAsync(_actorSources, source => new ZLinkLocationActorEvent(
            source, DateTimeOffset.UtcNow, ZLinkLocationActorEventKind.ResolveMiss, key, null), ct);

    internal ValueTask RouteRowUpdatedAsync(ZLinkRouteLocation route, CancellationToken ct) =>
        EmitAsync(_routeSources, source => new ZLinkLocationRouteEvent(
            source, DateTimeOffset.UtcNow, ZLinkLocationRouteEventKind.RowUpdated,
            new ZLinkRouteLocationKey(route.RouteKind, route.RouteKey), route), ct);

    internal ValueTask RouteRowRemovedAsync(ZLinkRouteLocationKey key, CancellationToken ct) =>
        EmitAsync(_routeSources, source => new ZLinkLocationRouteEvent(
            source, DateTimeOffset.UtcNow, ZLinkLocationRouteEventKind.RowRemoved, key, null), ct);

    internal ValueTask RouteResolveMissAsync(ZLinkRouteLocationKey key, CancellationToken ct) =>
        EmitAsync(_routeSources, source => new ZLinkLocationRouteEvent(
            source, DateTimeOffset.UtcNow, ZLinkLocationRouteEventKind.ResolveMiss, key, null), ct);

    private async ValueTask EmitAsync<TEvent>(
        IReadOnlyCollection<string> sources,
        Func<string, TEvent> create,
        CancellationToken ct)
        where TEvent : IZLinkRuntimeEvent
    {
        if (_publisher is null || sources.Count == 0)
        {
            return;
        }

        foreach (var source in sources)
        {
            try
            {
                await _publisher.PublishAsync(create(source), ct).ConfigureAwait(false);
            }
            catch (Exception)
            {
                // Monitoring must never break a location flow; a failing
                // handler loses its own event only.
            }
        }
    }

}
