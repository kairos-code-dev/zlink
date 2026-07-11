using Microsoft.Extensions.Hosting;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Runtime.Configuration;
using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.AspNetCore;

internal sealed class ZLinkSpotHandleWatchHost(
    IZLinkLocationWatchStore? watchStore,
    ZLinkStoreLocationResolvers rows,
    ZLinkSpotHandleRegistry handles,
    ZLinkLocationOptions options,
    ZLinkObservedLocationGenerations? observed = null) : IHostedService, IAsyncDisposable
{
    private readonly object _lifecycleGate = new();
    private CancellationTokenSource? _stop;
    private Task[] _watches = [];
    private Task? _shutdown;

    public Task StartAsync(CancellationToken cancellationToken)
    {
        lock (_lifecycleGate)
        {
            if (_stop is not null || _shutdown is not null)
                throw new InvalidOperationException("The location watch host cannot be started more than once.");

            _stop = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            var watches = new List<Task> { PollAsync(_stop.Token) };
            if (watchStore is not null)
            {
                watches.Add(WatchAsync(ZLinkLocationKind.Peer, _stop.Token));
                watches.Add(WatchAsync(ZLinkLocationKind.Spot, _stop.Token));
                watches.Add(WatchAsync(ZLinkLocationKind.Actor, _stop.Token));
                watches.Add(WatchAsync(ZLinkLocationKind.Route, _stop.Token));
            }
            _watches = watches.ToArray();
        }
        return Task.CompletedTask;
    }

    public async Task StopAsync(CancellationToken cancellationToken)
    {
        await ShutdownAsync().WaitAsync(cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask DisposeAsync()
    {
        await ShutdownAsync().ConfigureAwait(false);
    }

    private Task ShutdownAsync()
    {
        lock (_lifecycleGate)
        {
            if (_shutdown is not null) return _shutdown;
            var stop = _stop;
            var watches = _watches;
            _stop = null;
            _watches = [];
            return _shutdown = stop is null
                ? Task.CompletedTask
                : ShutdownCoreAsync(stop, watches);
        }
    }

    private static async Task ShutdownCoreAsync(
        CancellationTokenSource stop,
        Task[] watches)
    {
        try
        {
            await stop.CancelAsync().ConfigureAwait(false);
            try
            {
                await Task.WhenAll(watches).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
            }
        }
        finally
        {
            stop.Dispose();
        }
    }

    private async Task WatchAsync(ZLinkLocationKind kind, CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                await foreach (var change in watchStore!.WatchAsync(
                                   new ZLinkLocationWatchFilter(kind), cancellationToken)
                                   .ConfigureAwait(false))
                    await ApplyAsync(change, cancellationToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                return;
            }
            catch
            {
                var retryDelay = options.PollingInterval > TimeSpan.Zero
                    ? options.PollingInterval
                    : TimeSpan.FromMilliseconds(100);
                await Task.Delay(retryDelay, cancellationToken).ConfigureAwait(false);
            }
        }
    }

    private async Task PollAsync(CancellationToken cancellationToken)
    {
        var interval = options.PollingInterval > TimeSpan.Zero
            ? options.PollingInterval
            : TimeSpan.FromMilliseconds(100);
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                await Task.Delay(interval, cancellationToken).ConfigureAwait(false);
                var keys = handles.SnapshotLiveKeys();
                foreach (var key in keys.Spots)
                {
                    var row = await rows.ResolveSpotRowAsync(key.LocationKey, cancellationToken).ConfigureAwait(false);
                    if (row is null)
                        handles.RemoveSpotIfUnchanged(key.LocationKey, key.Generation);
                    else
                        handles.UpdateSpot(row);
                }

                foreach (var liveActor in keys.Actors)
                {
                    var key = new ZLinkActorLocationKey(liveActor.ActorId);
                    var row = await rows.ResolveActorRowAsync(key, cancellationToken).ConfigureAwait(false);
                    if (row is null) handles.RemoveActorIfUnchanged(liveActor.ActorId, liveActor.Generation);
                    else handles.UpdateActor(row);
                }
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                return;
            }
            catch
            {
                // The next polling interval retries every still-live key.
            }
        }
    }

    internal async ValueTask ApplyAsync(
        ZLinkLocationChanged change,
        CancellationToken cancellationToken)
    {
        if (change.Key is ZLinkLocationKey.Peer(var peerKey))
        {
            observed?.ObservePeer(peerKey, change.Generation);
            return;
        }

        if (change.Key is ZLinkLocationKey.Spot(var spotKey))
        {
            observed?.ObserveSpot(spotKey, change.Generation);
            if (change.ChangeType is ZLinkLocationChangeType.Removed or ZLinkLocationChangeType.Expired)
            {
                handles.RemoveSpot(spotKey, change.Generation);
                return;
            }

            var row = await rows.ResolveSpotRowAsync(spotKey, cancellationToken).ConfigureAwait(false);
            if (row is null || row.Generation < change.Generation)
                handles.MarkSpotPending(spotKey, change.Generation);
            else handles.UpdateSpot(row);
            return;
        }

        if (change.Key is ZLinkLocationKey.Route(var routeKey))
        {
            observed?.ObserveRoute(routeKey, change.Generation);
            return;
        }

        if (change.Key is not ZLinkLocationKey.Actor(var actorKey)) return;
        observed?.ObserveActor(actorKey, change.Generation);
        if (change.ChangeType is ZLinkLocationChangeType.Removed or ZLinkLocationChangeType.Expired)
        {
            handles.RemoveActor(actorKey, change.Generation);
            return;
        }

        var actor = await rows.ResolveActorRowAsync(actorKey, cancellationToken).ConfigureAwait(false);
        if (actor is null || actor.Generation < change.Generation)
            handles.MarkActorPending(actorKey, change.Generation);
        else handles.UpdateActor(actor);
    }
}
