using Microsoft.Extensions.Hosting;

namespace Zlink.Framework.AspNetCore;

internal sealed class ZLinkMonitoringHostedService(
    IZLinkBackendAdapterFactory backendAdapterFactory,
    ZLinkMonitoringRegistration registration,
    IZLinkRuntimeEventPublisher publisher,
    ZLinkFrameworkRuntime? frameworkRuntime,
    IZLinkLocationRuntimeQuery? locationQuery,
    ZLinkAutoConnectLifecycleCoordinator? autoConnectLifecycle,
    IZLinkRouteMeshRuntime? meshRuntime = null) : IHostedService, IAsyncDisposable
{
    private readonly IZLinkMonitoringBackendAdapter
        _monitoringAdapter = backendAdapterFactory.CreateMonitoringAdapter();

    private readonly List<IAsyncDisposable> _monitors = [];
    private readonly ZLinkMonitoringSourceValidator _sourceValidator = new(registration);
    private readonly SemaphoreSlim _lifecycleGate = new(1, 1);
    private Task? _pollingTask;
    private CancellationTokenSource? _stopTokenSource;
    private ZLinkRuntimeTaskRunner? _taskRunner;
    private readonly object _disposeGate = new();
    private Task? _disposeTask;
    private int _disposed;

    public ValueTask DisposeAsync()
    {
        Task task;
        TaskCompletionSource? start = null;
        lock (_disposeGate)
        {
            if (_disposeTask is null)
            {
                Volatile.Write(ref _disposed, 1);
                start = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                _disposeTask = DisposeCoreAsync(start.Task);
            }
            task = _disposeTask;
        }
        start?.TrySetResult();
        return new ValueTask(task);
    }

    private async Task DisposeCoreAsync(Task started)
    {
        await started.ConfigureAwait(false);
        try
        {
            await StopCoreAsync(disposing: true).ConfigureAwait(false);
        }
        finally
        {
            _lifecycleGate.Dispose();
        }
    }

    public async Task StartAsync(CancellationToken cancellationToken)
    {
        ObjectDisposedException.ThrowIf(Volatile.Read(ref _disposed) != 0, this);
        await _lifecycleGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (_stopTokenSource is not null) return;
            _sourceValidator.ValidateRequiredRuntimes(frameworkRuntime, locationQuery);
            var startedFramework = false;

            try
            {
                _sourceValidator.PreflightFrameworkSources(frameworkRuntime);

                if (frameworkRuntime is not null && !frameworkRuntime.IsStarted)
                {
                    await frameworkRuntime.StartAsync(cancellationToken);
                    startedFramework = true;
                }

                await _sourceValidator.PreflightPollingSourcesAsync(frameworkRuntime, cancellationToken);
                // A backend monitor may invoke its callback synchronously from OnEvent.
                // Install the dispatch runner before attaching monitors so the first
                // connection event cannot be dropped during host startup.
                _stopTokenSource = new CancellationTokenSource();
                _taskRunner = new ZLinkRuntimeTaskRunner(
                    new ZLinkRuntimeErrorSink(),
                    _stopTokenSource.Token);
                AttachSocketMonitors(frameworkRuntime);
                AttachMeshObservers();
                if (registration.SocketSources.Count > 0 && autoConnectLifecycle is not null)
                    await autoConnectLifecycle.SocketMonitoringReadyAsync(cancellationToken)
                        .ConfigureAwait(false);
            }
            catch (Exception startupFailure)
            {
                var failures = new List<Exception> { startupFailure };
                await CaptureCleanupAsync(DisposeMonitorsAsync, failures).ConfigureAwait(false);
                if (_stopTokenSource is { } stop)
                {
                    stop.Cancel();
                    if (_taskRunner is { } runner)
                        await CaptureCleanupAsync(runner.StopAsync, failures).ConfigureAwait(false);
                    stop.Dispose();
                    _taskRunner = null;
                    _stopTokenSource = null;
                }
                if (startedFramework && frameworkRuntime is not null)
                    await CaptureCleanupAsync(
                            () => frameworkRuntime.StopAsync(CancellationToken.None),
                            failures)
                        .ConfigureAwait(false);

                ThrowFailures(failures);
            }

            var pollingRunner = new ZLinkMonitoringPollingRunner(
                registration,
                spotEvent => QueueDispatch(spotEvent),
                locationEvent => QueueDispatch(locationEvent));
            var stopToken = (_stopTokenSource
                             ?? throw new InvalidOperationException("Monitoring dispatch was not initialized."))
                .Token;
            _pollingTask = pollingRunner.RunAsync(
                frameworkRuntime,
                locationQuery,
                stopToken);
        }
        finally
        {
            _lifecycleGate.Release();
        }
    }

    public async Task StopAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        await StopCoreAsync().ConfigureAwait(false);
    }

    // Bridges each registered mesh's runtime event stream (spec 50) onto the
    // runtime event bus; observation is poll-derived inside the mesh runtime,
    // so handlers never sit on the dispatch path.
    private void AttachMeshObservers()
    {
        if (registration.MeshNodeSources.Count == 0) return;
        var observedMeshRuntime = meshRuntime
                                  ?? throw new ZLinkConfigurationException(
                                      "Mesh monitoring requires the RouteMesh runtime service.");
        var stopToken = _stopTokenSource?.Token ?? CancellationToken.None;
        foreach (var meshName in registration.MeshNodeSources)
        {
            var observedMesh = meshName;
            _taskRunner?.RunDetached(
                $"monitoring-mesh-events:{observedMesh}",
                async ct =>
                {
                    await foreach (var meshEvent in observedMeshRuntime
                                       .ObserveAsync(observedMesh, cancellationToken: ct)
                                       .ConfigureAwait(false))
                        QueueDispatch(meshEvent);
                });
        }

        _ = stopToken;
    }

    private void AttachSocketMonitors(ZLinkFrameworkRuntime? frameworkRuntime)
    {
        if (frameworkRuntime is null) return;

        foreach (var source in registration.SocketSources.Values)
        {
            IZLinkBackendSocket socket;
            try
            {
                socket = frameworkRuntime.GetMonitoringSocket(source.SourceName);
            }
            catch (InvalidOperationException ex)
            {
                throw new ZLinkConfigurationException(ex.Message);
            }

            var monitor = _monitoringAdapter.OpenSocketMonitor(socket);
            _monitors.Add(monitor);
            RegisterWithoutSynchronizationContext(() =>
            {
                monitor.OnEvent(monitorEvent =>
                {
                    var mapped = ZLinkMonitoringEventMapper.MapSocketEvent(source, monitorEvent);
                    if (mapped is ZLinkSocketEvent @event) QueueDispatch(@event);
                });
                return 0;
            });
        }
    }

    private static T RegisterWithoutSynchronizationContext<T>(Func<T> action)
    {
        var previous = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(null);
        try
        {
            return action();
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(previous);
        }
    }

    private void QueueDispatch<TEvent>(TEvent @event)
        where TEvent : IZLinkRuntimeEvent
    {
        var taskRunner = _taskRunner;
        if (taskRunner is null) return;

        taskRunner.RunDetached(
            "monitoring-event-dispatch",
            async ct => { await publisher.PublishAsync(@event, ct).ConfigureAwait(false); });
    }

    private async ValueTask DisposeMonitorsAsync()
    {
        var failures = new List<Exception>();
        for (var index = _monitors.Count - 1; index >= 0; index--)
            await CaptureCleanupAsync(_monitors[index].DisposeAsync, failures).ConfigureAwait(false);
        _monitors.Clear();
        ThrowFailures(failures);
    }

    private static async ValueTask CaptureCleanupAsync(
        Func<ValueTask> cleanup,
        ICollection<Exception> failures)
    {
        try
        {
            await cleanup().ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            failures.Add(exception);
        }
    }

    private static void ThrowFailures(IReadOnlyList<Exception> failures)
    {
        if (failures.Count == 1)
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failures[0]).Throw();
        if (failures.Count > 1) throw new AggregateException(failures);
    }

    private async ValueTask StopCoreAsync(bool disposing = false)
    {
        if (!disposing && Volatile.Read(ref _disposed) != 0) return;
        await _lifecycleGate.WaitAsync(CancellationToken.None).ConfigureAwait(false);
        try
        {
            var stop = _stopTokenSource;
            var polling = _pollingTask;
            var runner = _taskRunner;
            _pollingTask = null;
            var failures = new List<Exception>();

            if (stop is not null) Capture(stop.Cancel);
            if (polling is not null) await CaptureTaskAsync(polling).ConfigureAwait(false);
            await CaptureAsync(DisposeMonitorsAsync).ConfigureAwait(false);
            if (runner is not null) await CaptureAsync(runner.StopAsync).ConfigureAwait(false);

            _taskRunner = null;
            _stopTokenSource = null;
            if (stop is not null) Capture(stop.Dispose);

            if (failures.Count == 1)
                System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failures[0]).Throw();
            if (failures.Count > 1) throw new AggregateException(failures);
            return;

            async ValueTask CaptureAsync(Func<ValueTask> cleanup)
            {
                try
                {
                    await cleanup().ConfigureAwait(false);
                }
                catch (Exception exception)
                {
                    failures.Add(exception);
                }
            }

            async ValueTask CaptureTaskAsync(Task task)
            {
                try
                {
                    await task.ConfigureAwait(false);
                }
                catch (OperationCanceledException) when (stop?.IsCancellationRequested == true)
                {
                }
                catch (Exception exception)
                {
                    failures.Add(exception);
                }
            }

            void Capture(Action cleanup)
            {
                try
                {
                    cleanup();
                }
                catch (Exception exception)
                {
                    failures.Add(exception);
                }
            }
        }
        finally
        {
            _lifecycleGate.Release();
        }
    }
}
