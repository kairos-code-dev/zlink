using Microsoft.Extensions.Hosting;

namespace Zlink.Framework.AspNetCore;

internal sealed class ZLinkMonitoringHostedService(
    IZLinkBackendAdapterFactory backendAdapterFactory,
    ZLinkMonitoringRegistration registration,
    ZLinkRuntimeEventDispatcher dispatcher,
    ZLinkFrameworkRuntime? frameworkRuntime,
    IZLinkLocationRuntimeQuery? locationQuery) : IHostedService, IAsyncDisposable
{
    private readonly IZLinkMonitoringBackendAdapter
        _monitoringAdapter = backendAdapterFactory.CreateMonitoringAdapter();

    private readonly List<IAsyncDisposable> _monitors = [];
    private readonly ZLinkMonitoringSourceValidator _sourceValidator = new(registration);
    private Task? _pollingTask;
    private CancellationTokenSource? _stopTokenSource;
    private ZLinkRuntimeTaskRunner? _taskRunner;

    public async ValueTask DisposeAsync()
    {
        if (_stopTokenSource is not null) _stopTokenSource.Cancel();

        if (_pollingTask is not null)
            try
            {
                await _pollingTask;
            }
            catch (OperationCanceledException)
            {
            }

        await DisposeMonitorsAsync();
        _stopTokenSource?.Dispose();
        _stopTokenSource = null;
        _taskRunner = null;
    }

    public async Task StartAsync(CancellationToken cancellationToken)
    {
        _sourceValidator.ValidateRequiredRuntimes(frameworkRuntime, locationQuery);

        try
        {
            if (frameworkRuntime is not null) await frameworkRuntime.StartAsync(cancellationToken);

            await _sourceValidator.PreflightPollingSourcesAsync(frameworkRuntime, cancellationToken);
            AttachSocketMonitors(frameworkRuntime);
        }
        catch
        {
            await DisposeMonitorsAsync();
            if (frameworkRuntime is not null && frameworkRuntime.IsStarted)
                await frameworkRuntime.StopAsync(CancellationToken.None);

            throw;
        }

        _stopTokenSource = new CancellationTokenSource();
        _taskRunner = new ZLinkRuntimeTaskRunner(
            new ZLinkRuntimeErrorSink(),
            _stopTokenSource.Token);
        var pollingRunner = new ZLinkMonitoringPollingRunner(
            registration,
            spotEvent => QueueDispatch(spotEvent),
            locationEvent => QueueDispatch(locationEvent));
        _pollingTask = pollingRunner.RunAsync(
            frameworkRuntime,
            locationQuery,
            _stopTokenSource.Token);
    }

    public async Task StopAsync(CancellationToken cancellationToken)
    {
        if (_stopTokenSource is not null) _stopTokenSource.Cancel();

        if (_pollingTask is not null)
            try
            {
                await _pollingTask.WaitAsync(cancellationToken);
            }
            catch (OperationCanceledException)
            {
            }

        await DisposeMonitorsAsync();

        _stopTokenSource?.Dispose();
        _stopTokenSource = null;
        _taskRunner = null;
        _pollingTask = null;
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
            RegisterWithoutSynchronizationContext(() =>
            {
                monitor.OnEvent(monitorEvent =>
                {
                    var mapped = ZLinkMonitoringEventMapper.MapSocketEvent(source, monitorEvent);
                    if (mapped is ZLinkSocketEvent @event) QueueDispatch(@event);
                });
                return 0;
            });
            _monitors.Add(monitor);
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
            async ct => { await dispatcher.DispatchAsync(@event, ct).ConfigureAwait(false); });
    }

    private async ValueTask DisposeMonitorsAsync()
    {
        for (var index = _monitors.Count - 1; index >= 0; index--) await _monitors[index].DisposeAsync();

        _monitors.Clear();
    }
}