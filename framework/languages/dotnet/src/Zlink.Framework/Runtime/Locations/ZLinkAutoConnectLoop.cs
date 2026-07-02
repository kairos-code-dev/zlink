namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Drives one reconciler: polling is the correctness path, a change stamp
/// makes empty ticks O(1), and watch events (when the store supports them)
/// only wake the next tick early. Event loss is tolerated because the next
/// polling tick reaches the same state.
/// </summary>
internal sealed class ZLinkAutoConnectLoop : IAsyncDisposable
{
    private readonly ZLinkAutoConnectReconciler _reconciler;
    private readonly ZLinkLocationOptions _options;
    private readonly ZLinkLocationChangeStampScope _stampScope;
    private readonly IZLinkLocationChangeStampStore? _stampStore;
    private readonly IZLinkLocationWatchStore? _watchStore;
    private readonly TimeProvider _time;
    private readonly SemaphoreSlim _wake = new(0, 1);
    private CancellationTokenSource? _cts;
    private Task? _loop;
    private Task? _watch;
    private long? _lastStamp;
    private bool _lastTickFailed;

    internal ZLinkAutoConnectLoop(
        ZLinkAutoConnectReconciler reconciler,
        ZLinkAutoConnectLocal local,
        ZLinkLocationOptions options,
        IZLinkLocationChangeStampStore? stampStore = null,
        IZLinkLocationWatchStore? watchStore = null,
        TimeProvider? timeProvider = null)
    {
        _reconciler = reconciler;
        _options = options;
        _stampScope = new ZLinkLocationChangeStampScope(ZLinkLocationKind.Peer, local.MeshName);
        _stampStore = stampStore;
        _watchStore = watchStore;
        _time = timeProvider ?? TimeProvider.System;
    }

    internal async ValueTask StartAsync(CancellationToken cancellationToken = default)
    {
        await TickAsync(cancellationToken).ConfigureAwait(false);
        _cts = new CancellationTokenSource();
        _loop = Task.Run(() => LoopAsync(_cts.Token), CancellationToken.None);
        if (_watchStore is not null)
        {
            _watch = Task.Run(() => WatchAsync(_cts.Token), CancellationToken.None);
        }
    }

    internal async ValueTask StopAsync(CancellationToken cancellationToken = default)
    {
        if (_cts is not null)
        {
            await _cts.CancelAsync().ConfigureAwait(false);
        }

        foreach (var task in new[] { _loop, _watch })
        {
            if (task is null)
            {
                continue;
            }

            try
            {
                await task.ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
            }
        }

        await _reconciler.ShutdownAsync(cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask DisposeAsync()
    {
        await StopAsync().ConfigureAwait(false);
        _cts?.Dispose();
        _wake.Dispose();
    }

    /// <summary>Runs one reconcile tick, letting the change stamp skip the
    /// list read when nothing changed since the last successful tick.</summary>
    internal async ValueTask TickAsync(CancellationToken cancellationToken = default)
    {
        if (_stampStore is not null && !_lastTickFailed)
        {
            try
            {
                var stamp = await _stampStore.GetChangeStampAsync(_stampScope, cancellationToken)
                    .ConfigureAwait(false);
                if (_lastStamp == stamp)
                {
                    return;
                }

                await RunReconcileAsync(cancellationToken).ConfigureAwait(false);
                if (!_lastTickFailed)
                {
                    _lastStamp = stamp;
                }

                return;
            }
            catch (Exception)
            {
                // Stamp read failure falls through to a full tick, which
                // applies the fail-static policy itself.
            }
        }

        await RunReconcileAsync(cancellationToken).ConfigureAwait(false);
        _lastStamp = null;
    }

    private async ValueTask RunReconcileAsync(CancellationToken cancellationToken)
    {
        var before = _reconciler.StoreFailed;
        await _reconciler.TickAsync(cancellationToken).ConfigureAwait(false);
        _lastTickFailed = _reconciler.StoreFailed;
        _ = before;
    }

    private async Task LoopAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                var delay = Task.Delay(_options.PollingInterval, _time, cancellationToken);
                var woken = _wake.WaitAsync(cancellationToken);
                await Task.WhenAny(delay, woken).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                return;
            }

            if (cancellationToken.IsCancellationRequested)
            {
                return;
            }

            await TickAsync(cancellationToken).ConfigureAwait(false);
        }
    }

    private async Task WatchAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                await foreach (var _ in _watchStore!.WatchAsync(
                    new ZLinkLocationWatchFilter(ZLinkLocationKind.Peer, _stampScope.MeshName),
                    cancellationToken).ConfigureAwait(false))
                {
                    // Wake the loop; the tick re-reads the store, so a lost
                    // or duplicated event can never corrupt the state.
                    if (_wake.CurrentCount == 0)
                    {
                        _wake.Release();
                    }
                }
            }
            catch (OperationCanceledException)
            {
                return;
            }
            catch (Exception)
            {
                // A broken watch stream degrades to pure polling until the
                // next successful subscription.
                try
                {
                    await Task.Delay(_options.PollingInterval, _time, cancellationToken)
                        .ConfigureAwait(false);
                }
                catch (OperationCanceledException)
                {
                    return;
                }
            }
        }
    }
}
