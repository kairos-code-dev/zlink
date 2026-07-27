namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Drives one reconciler: polling is the correctness path, a change stamp
/// makes empty ticks O(1), and watch events (when the store supports them)
/// only wake the next tick early. Event loss is tolerated because the next
/// polling tick reaches the same state. The desired target set is a join of
/// peer rows and owner leases, so the skip requires both an unchanged stamp
/// and an unchanged live owner set: an owner appearing or expiring changes
/// the join without any row write.
/// </summary>
internal sealed class ZLinkAutoConnectLoop : IAsyncDisposable
{
    private readonly ZLinkAutoConnectReconciler _reconciler;
    private readonly ZLinkLocationOptions _options;
    private readonly string _meshName;
    private readonly IZLinkLocationStore _store;
    private readonly IZLinkLocationWatchStore? _watchStore;
    private readonly ZLinkOwnerLeaseTracker? _leaseTracker;
    private readonly TimeProvider _time;
    private readonly SemaphoreSlim _wake = new(0, 1);
    private readonly object _disposeGate = new();
    private Task? _disposeTask;
    private CancellationTokenSource? _cts;
    private Task? _loop;
    private Task? _watch;
    private ulong? _lastStamp;
    private long? _lastLiveOwnerSetVersion;
    private bool _lastTickFailed;

    internal ZLinkAutoConnectLoop(
        ZLinkAutoConnectReconciler reconciler,
        ZLinkAutoConnectLocal local,
        ZLinkLocationOptions options,
        IZLinkLocationStore store,
        IZLinkLocationWatchStore? watchStore = null,
        TimeProvider? timeProvider = null,
        ZLinkOwnerLeaseTracker? leaseTracker = null)
    {
        _reconciler = reconciler;
        _options = options;
        _meshName = local.MeshName;
        _store = store;
        _watchStore = watchStore;
        _leaseTracker = leaseTracker;
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
        var cts = _cts;
        var loop = _loop;
        var watch = _watch;
        _cts = null;
        _loop = null;
        _watch = null;
        var failures = new List<Exception>();
        if (cts is not null)
            await CaptureAsync(async () => await cts.CancelAsync().ConfigureAwait(false)).ConfigureAwait(false);

        foreach (var task in new[] { loop, watch })
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
            catch (Exception exception)
            {
                failures.Add(exception);
            }
        }

        await CaptureAsync(() => _reconciler.ShutdownAsync(cancellationToken)).ConfigureAwait(false);
        cts?.Dispose();
        if (failures.Count == 1)
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failures[0]).Throw();
        if (failures.Count > 1) throw new AggregateException(failures);
        return;

        async ValueTask CaptureAsync(Func<ValueTask> operation)
        {
            try
            {
                await operation().ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }
        }
    }

    public ValueTask DisposeAsync()
    {
        lock (_disposeGate)
            return new ValueTask(_disposeTask ??= DisposeCoreAsync());
    }

    private async Task DisposeCoreAsync()
    {
        Exception? failure = null;
        try
        {
            await StopAsync().ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            failure = exception;
        }
        finally
        {
            _wake.Dispose();
        }

        if (failure is not null)
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failure).Throw();
    }

    /// <summary>Runs one reconcile tick, letting the change stamp skip the
    /// list read when neither the rows nor the live owner set changed since
    /// the last successful tick.</summary>
    internal async ValueTask TickAsync(CancellationToken cancellationToken = default)
    {
        if (!_lastTickFailed)
        {
            try
            {
                var stamp = await _store.GetMeshNodeChangeStampAsync(
                        _meshName,
                        cancellationToken)
                    .ConfigureAwait(false);
                if (stamp is not null)
                {
                    var liveOwners = _leaseTracker is null
                        ? 0
                        : await _leaseTracker.GetLiveOwnerSetVersionAsync(cancellationToken)
                            .ConfigureAwait(false);
                    if (_lastStamp == stamp
                        && _lastLiveOwnerSetVersion == liveOwners
                        && !_reconciler.HasPendingTargets)
                    {
                        return;
                    }

                    await RunReconcileAsync(cancellationToken).ConfigureAwait(false);
                    if (!_lastTickFailed)
                    {
                        _lastStamp = stamp;
                        _lastLiveOwnerSetVersion = liveOwners;
                    }

                    return;
                }
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                throw;
            }
            catch (Exception)
            {
                // The stamp is only an optimization, so still perform the
                // full correctness read. Record the failed preflight first:
                // if the store recovers between the two reads, an incomplete
                // recovery snapshot must get the same disconnect deferral as
                // any other first successful read after an outage.
                await _reconciler.NoteStoreFailureAsync(cancellationToken).ConfigureAwait(false);
            }
        }

        await RunReconcileAsync(cancellationToken).ConfigureAwait(false);
        _lastStamp = null;
        _lastLiveOwnerSetVersion = null;
    }

    private async ValueTask RunReconcileAsync(CancellationToken cancellationToken)
    {
        await _reconciler.TickAsync(cancellationToken).ConfigureAwait(false);
        _lastTickFailed = _reconciler.StoreFailed;
    }

    private async Task LoopAsync(CancellationToken cancellationToken)
    {
        // One live semaphore waiter across iterations: a fresh WaitAsync per
        // tick would leave the losing waiter queued, and the next wake
        // signal would be consumed by that abandoned waiter and lost.
        Task? woken = null;
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                var delay = Task.Delay(_options.PollingInterval, _time, cancellationToken);
                woken ??= _wake.WaitAsync(cancellationToken);
                await Task.WhenAny(delay, woken).ConfigureAwait(false);
                if (woken.IsCompleted) woken = null;
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
                    new ZLinkLocationWatchFilter(
                        ZLinkLocationKind.MeshNode,
                        _meshName),
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
