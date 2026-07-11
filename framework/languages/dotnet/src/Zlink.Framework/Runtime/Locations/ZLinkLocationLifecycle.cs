namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Owns the location lifecycle subdomains for this runtime and routes
/// ownership-loss events to the subdomain that tracks the affected row kind.
/// </summary>
internal sealed class ZLinkLocationLifecycle : IDisposable, IAsyncDisposable
{
    private readonly ZLinkLocationRuntime _runtime;
    private readonly object _backgroundGate = new();
    private readonly SemaphoreSlim _backgroundDrainGate = new(1, 1);
    private readonly HashSet<Task> _backgroundTasks = [];
    private CancellationTokenSource _backgroundStop = new();
    private bool _backgroundStopping;
    private int _disposed;

    internal ZLinkLocationLifecycle(
        ZLinkLocationRuntime runtime,
        ZLinkStoreLocationResolvers resolver)
    {
        _runtime = runtime;
        _runtime.OwnershipLost += OnOwnershipLost;
        ActorOwnership = new ZLinkActorOwnershipCoordinator(runtime, resolver);
        ActorSessionRoutes = new ZLinkActorSessionRouteLifecycle(runtime, TryRunBackground);
        SpotLocations = new ZLinkSpotLocationLifecycle(runtime, resolver);
    }

    internal ZLinkActorOwnershipCoordinator ActorOwnership { get; }

    internal ZLinkActorSessionRouteLifecycle ActorSessionRoutes { get; }

    internal ZLinkSpotLocationLifecycle SpotLocations { get; }

    public void Dispose()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0) return;
        _runtime.OwnershipLost -= OnOwnershipLost;
        lock (_backgroundGate)
        {
            _backgroundStopping = true;
            _backgroundStop.Cancel();
        }
        ActorOwnership.Dispose();
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref _disposed, 1) == 0)
            _runtime.OwnershipLost -= OnOwnershipLost;
        await PauseBackgroundWorkCoreAsync().ConfigureAwait(false);
        await ActorOwnership.DisposeAsync().ConfigureAwait(false);
        _backgroundStop.Dispose();
    }

    internal ValueTask PauseBackgroundWorkAsync()
        => PauseBackgroundWorkCoreAsync();

    internal void ResumeBackgroundWork()
    {
        CancellationTokenSource? stopped = null;
        lock (_backgroundGate)
        {
            if (_disposed != 0 || !_backgroundStopping) return;
            if (_backgroundStop.IsCancellationRequested)
            {
                stopped = _backgroundStop;
                _backgroundStop = new CancellationTokenSource();
            }
            _backgroundStopping = false;
        }
        stopped?.Dispose();
        ActorOwnership.ResumeBackgroundWork();
    }

    internal void ResetGeneration()
    {
        ActorSessionRoutes.ResetGeneration();
        SpotLocations.ResetGeneration();
        ActorOwnership.ResetGeneration();
    }

    private void OnOwnershipLost(ZLinkLocationKind kind, string canonicalKey)
    {
        Func<CancellationToken, ValueTask>? deactivate = null;
        if (kind == ZLinkLocationKind.Actor)
        {
            deactivate = ActorOwnership.TakeOwnershipLostDeactivation(canonicalKey);
        }
        else if (kind == ZLinkLocationKind.Spot)
        {
            deactivate = SpotLocations.TakeOwnershipLostDeactivation(canonicalKey);
        }
        else if (kind == ZLinkLocationKind.Route)
        {
            ActorSessionRoutes.OnOwnershipLost(canonicalKey);
        }

        if (deactivate is not null)
            TryRunBackground(deactivate);
    }

    private async ValueTask PauseBackgroundWorkCoreAsync()
    {
        await _backgroundDrainGate.WaitAsync(CancellationToken.None).ConfigureAwait(false);
        try
        {
            Task[] tasks;
            CancellationTokenSource stop;
            lock (_backgroundGate)
            {
                _backgroundStopping = true;
                stop = _backgroundStop;
                stop.Cancel();
                tasks = _backgroundTasks.ToArray();
            }

            if (tasks.Length != 0) await Task.WhenAll(tasks).ConfigureAwait(false);
            await ActorOwnership.PauseBackgroundWorkAsync().ConfigureAwait(false);

            lock (_backgroundGate)
            {
                _backgroundTasks.RemoveWhere(static task => task.IsCompleted);
                _backgroundStopping = true;
            }
        }
        finally
        {
            _backgroundDrainGate.Release();
        }
    }

    private void RemoveCompletedTasks()
    {
        lock (_backgroundGate)
            _backgroundTasks.RemoveWhere(static task => task.IsCompleted);
    }

    private bool TryRunBackground(Func<CancellationToken, ValueTask> operation)
    {
        lock (_backgroundGate)
        {
            if (_disposed != 0 || _backgroundStopping) return false;
            var stopToken = _backgroundStop.Token;
            var task = RunGuardedAsync(() => operation(stopToken), stopToken);
            _backgroundTasks.Add(task);
            _ = task.ContinueWith(
                static (_, state) => ((ZLinkLocationLifecycle)state!).RemoveCompletedTasks(),
                this,
                CancellationToken.None,
                TaskContinuationOptions.ExecuteSynchronously,
                TaskScheduler.Default);
            return true;
        }
    }

    private static async Task RunGuardedAsync(
        Func<ValueTask> operation,
        CancellationToken cancellationToken)
    {
        try
        {
            await operation().ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
        }
        catch (Exception exception)
        {
            ZLinkFrameworkDebugLog.SpotDiscovery($"location lifecycle error: {exception.Message}");
        }
    }
}
