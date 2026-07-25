using Zlink.Framework.Runtime.Configuration;

namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Publishes the push-based location monitoring events: row update/remove
/// and resolve-miss events for the location-peer (MeshNode descriptor),
/// location-spot and location-actor sources, plus auto-connect desired set
/// changes. Every method is a no-op unless the matching source is
/// registered through the monitoring options, so location flows pay nothing
/// when monitoring is off. Publish failures never break a location flow.
/// </summary>
internal sealed class ZLinkLocationEventEmitter
{
    /// <summary>Emitter used when monitoring is not configured; every
    /// method is a no-op.</summary>
    internal static readonly ZLinkLocationEventEmitter Disabled = new(null, null);

    private readonly IZLinkRuntimeEventPublisher? _publisher;
    private readonly IReadOnlyCollection<string> _peerSources;
    private readonly IReadOnlyCollection<string> _spotSources;
    private readonly IReadOnlyCollection<string> _actorSources;
    private readonly ZLinkSpotHandleRegistry? _handles;
    private readonly ZLinkObservedLocationGenerations? _observed;

    internal ZLinkLocationEventEmitter(
        ZLinkMonitoringRegistration? registration,
        IZLinkRuntimeEventPublisher? publisher,
        ZLinkObservedLocationGenerations? observed = null)
    {
        _publisher = publisher;
        _observed = observed;
        _peerSources = registration?.LocationPeerSources ?? (IReadOnlyCollection<string>)Array.Empty<string>();
        _spotSources = registration?.LocationSpotSources ?? (IReadOnlyCollection<string>)Array.Empty<string>();
        _actorSources = registration?.LocationActorSources ?? (IReadOnlyCollection<string>)Array.Empty<string>();
    }

    internal ZLinkLocationEventEmitter(
        ZLinkMonitoringRegistration? registration,
        IZLinkRuntimeEventPublisher? publisher,
        ZLinkSpotHandleRegistry? handles,
        ZLinkObservedLocationGenerations? observed = null)
        : this(registration, publisher, observed)
    {
        _handles = handles;
    }

    internal ValueTask DescriptorRowUpdatedAsync(ZLinkMeshNodeDescriptor descriptor, CancellationToken ct)
    {
        _observed?.ObserveDescriptor(descriptor);
        return EmitAsync(_peerSources, source => new ZLinkLocationPeerEvent.RowUpdated(
            source, DateTimeOffset.UtcNow,
            new ZLinkMeshNodeDescriptorKey(descriptor.MeshName, descriptor.Rid),
            descriptor), ct);
    }

    internal ValueTask DescriptorRowRemovedAsync(
        ZLinkMeshNodeDescriptorKey key,
        CancellationToken ct) =>
        EmitAsync(_peerSources, source => new ZLinkLocationPeerEvent.RowRemoved(
            source, DateTimeOffset.UtcNow, key), ct);

    internal ValueTask DesiredSetChangedAsync(
        ZLinkAutoConnectDesiredSetChange change,
        CancellationToken ct) =>
        EmitAsync(_peerSources, source => new ZLinkLocationPeerEvent.DesiredSetChanged(
            source, DateTimeOffset.UtcNow, change), ct);

    internal ValueTask SpotRowUpdatedAsync(ZLinkSpotLocation spot, CancellationToken ct)
    {
        return EmitAsync(_spotSources, source => new ZLinkLocationSpotEvent.RowUpdated(
            source, DateTimeOffset.UtcNow,
            new ZLinkSpotLocationKey(spot.SpotId), spot), ct);
    }

    internal ValueTask SpotRowRemovedAsync(
        ZLinkSpotLocationKey key,
        ulong spotGeneration,
        CancellationToken ct)
    {
        _handles?.RemoveSpot(key, spotGeneration);
        return EmitAsync(_spotSources, source => new ZLinkLocationSpotEvent.RowRemoved(
            source, DateTimeOffset.UtcNow, key), ct);
    }

    internal ValueTask SpotResolveMissAsync(ZLinkSpotLocationKey key, CancellationToken ct) =>
        EmitAsync(_spotSources, source => new ZLinkLocationSpotEvent.ResolveMiss(
            source, DateTimeOffset.UtcNow, key), ct);

    internal ValueTask ActorRowUpdatedAsync(ZLinkActorLocation actor, CancellationToken ct)
    {
        return EmitAsync(_actorSources, source => new ZLinkLocationActorEvent.RowUpdated(
            source, DateTimeOffset.UtcNow,
            new ZLinkActorLocationKey(actor.ActorId), actor), ct);
    }

    internal ValueTask ActorRowRemovedAsync(
        ZLinkActorLocationKey key,
        CancellationToken ct)
    {
        _handles?.RemoveActor(key);
        return EmitAsync(_actorSources, source => new ZLinkLocationActorEvent.RowRemoved(
            source, DateTimeOffset.UtcNow, key), ct);
    }

    internal ValueTask ActorResolveMissAsync(ZLinkActorLocationKey key, CancellationToken ct) =>
        EmitAsync(_actorSources, source => new ZLinkLocationActorEvent.ResolveMiss(
            source, DateTimeOffset.UtcNow, key), ct);

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
