using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend;

namespace Zlink.Framework.AspNetCore;

internal sealed class ZLinkMonitoringHostedService(
    IServiceProvider services,
    IZLinkBackendAdapterFactory backendAdapterFactory,
    ZLinkMonitoringRegistration registration,
    ZLinkRuntimeEventDispatcher dispatcher,
    ZLinkFrameworkRuntime? frameworkRuntime,
    ZLinkRegistryRuntime? registryRuntime) : IHostedService, IAsyncDisposable
{
    private readonly IZLinkMonitoringBackendAdapter _monitoringAdapter = backendAdapterFactory.CreateMonitoringAdapter();
    private readonly List<IAsyncDisposable> _monitors = [];
    private CancellationTokenSource? _stopTokenSource;
    private Task? _pollingTask;

    public async Task StartAsync(CancellationToken cancellationToken)
    {
        if (frameworkRuntime is null
            && (registration.SocketSources.Count > 0
                || registration.DiscoverySources.Count > 0
                || registration.SpotSources.Count > 0))
        {
            throw new ZLinkConfigurationException(
                "Monitoring socket, discovery, or spot sources require AddZLinkFramework(...).");
        }

        if (registryRuntime is null && registration.RegistrySources.Count > 0)
        {
            throw new ZLinkConfigurationException(
                "Monitoring registry sources require AddZLinkRegistry(...).");
        }

        if (frameworkRuntime is not null)
        {
            await frameworkRuntime.StartAsync(cancellationToken);
        }

        if (registryRuntime is not null)
        {
            await registryRuntime.StartAsync(cancellationToken);
        }

        PreflightPollingSources(frameworkRuntime, registryRuntime);

        try
        {
            AttachSocketMonitors(frameworkRuntime);
            AttachDiscoveryMonitors(frameworkRuntime);
        }
        catch
        {
            await DisposeMonitorsAsync();
            throw;
        }

        _stopTokenSource = new CancellationTokenSource();
        _pollingTask = RunPollingAsync(
            frameworkRuntime,
            registryRuntime,
            _stopTokenSource.Token);
    }

    public async Task StopAsync(CancellationToken cancellationToken)
    {
        if (_stopTokenSource is not null)
        {
            _stopTokenSource.Cancel();
        }

        if (_pollingTask is not null)
        {
            try
            {
                await _pollingTask.WaitAsync(cancellationToken);
            }
            catch (OperationCanceledException)
            {
            }
        }

        await DisposeMonitorsAsync();

        _stopTokenSource?.Dispose();
        _stopTokenSource = null;
        _pollingTask = null;
    }

    public async ValueTask DisposeAsync()
    {
        if (_stopTokenSource is not null)
        {
            _stopTokenSource.Cancel();
        }

        if (_pollingTask is not null)
        {
            try
            {
                await _pollingTask;
            }
            catch (OperationCanceledException)
            {
            }
        }

        await DisposeMonitorsAsync();
        _stopTokenSource?.Dispose();
        _stopTokenSource = null;
    }

    private void AttachSocketMonitors(ZLinkFrameworkRuntime? frameworkRuntime)
    {
        if (frameworkRuntime is null)
        {
            return;
        }

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
            monitor.RequireNative<global::Zlink.SocketMonitor>().OnEvent(
                monitorEvent =>
                {
                    var mapped = MapSocketEvent(source, monitorEvent);
                    if (mapped is ZLinkSocketEvent @event)
                    {
                        QueueDispatch(@event);
                    }
                });
            _monitors.Add(monitor);
        }
    }

    private void AttachDiscoveryMonitors(ZLinkFrameworkRuntime? frameworkRuntime)
    {
        if (frameworkRuntime is null)
        {
            return;
        }

        foreach (var source in registration.DiscoverySources.Values)
        {
            IZLinkBackendDiscovery discovery;
            try
            {
                discovery = frameworkRuntime.GetMonitoringDiscovery(source.SourceName);
            }
            catch (InvalidOperationException ex)
            {
                throw new ZLinkConfigurationException(ex.Message);
            }

            var monitor = _monitoringAdapter.OpenDiscoveryMonitor(discovery);
            monitor.RequireNative<global::Zlink.ServiceMonitor>().OnEvent(
                serviceEvent =>
                {
                    var mapped = MapDiscoveryEvent(source, serviceEvent);
                    if (mapped is ZLinkDiscoveryEvent @event)
                    {
                        QueueDispatch(@event);
                    }
                });
            _monitors.Add(monitor);
        }
    }

    private void PreflightPollingSources(
        ZLinkFrameworkRuntime? frameworkRuntime,
        ZLinkRegistryRuntime? registryRuntime)
    {
        if (registryRuntime is not null && registration.RegistrySources.Count > 0)
        {
            _ = services.GetRequiredService<IZLinkRegistryQuery>().StatusSnapshot();
        }

        if (frameworkRuntime is null)
        {
            return;
        }

        foreach (var source in registration.SpotSources.Values)
        {
            try
            {
                _ = frameworkRuntime.GetSpotMonitoringSnapshot(source.SourceName);
            }
            catch (InvalidOperationException ex)
            {
                throw new ZLinkConfigurationException(ex.Message);
            }
        }
    }

    private async Task RunPollingAsync(
        ZLinkFrameworkRuntime? frameworkRuntime,
        ZLinkRegistryRuntime? registryRuntime,
        CancellationToken cancellationToken)
    {
        var tasks = new List<Task>(
            registration.RegistrySources.Count + registration.SpotSources.Count);

        if (registryRuntime is not null)
        {
            var registryQuery = services.GetRequiredService<IZLinkRegistryQuery>();
            foreach (var source in registration.RegistrySources.Values)
            {
                tasks.Add(RunRegistryLoopAsync(source, registryQuery, cancellationToken));
            }
        }

        if (frameworkRuntime is not null)
        {
            foreach (var source in registration.SpotSources.Values)
            {
                tasks.Add(RunSpotLoopAsync(source, frameworkRuntime, cancellationToken));
            }
        }

        if (tasks.Count == 0)
        {
            return;
        }

        await Task.WhenAll(tasks);
    }

    private async Task RunRegistryLoopAsync(
        ZLinkPollingMonitoringRegistration source,
        IZLinkRegistryQuery query,
        CancellationToken cancellationToken)
    {
        ZLinkRegistryStatus? previousStatus = null;
        IReadOnlyList<ZLinkRegistryTopologyEntry>? previousTopology = null;
        IReadOnlyList<ZLinkRegistryServiceSummaryEntry>? previousSummary = null;

        while (!cancellationToken.IsCancellationRequested)
        {
            var timestamp = DateTimeOffset.UtcNow;
            var status = query.StatusSnapshot();
            var topology = query.TopologySnapshot()
                .OrderBy(static entry => entry.ServiceName, StringComparer.Ordinal)
                .ThenBy(static entry => entry.Endpoint, StringComparer.Ordinal)
                .ThenBy(static entry => entry.RoutingId?.ToString(), StringComparer.Ordinal)
                .ToArray();
            var summary = query.ServiceSummarySnapshot()
                .OrderBy(static entry => entry.ServiceName, StringComparer.Ordinal)
                .ThenBy(static entry => entry.ServiceKind)
                .ThenBy(static entry => entry.ServiceRole)
                .ToArray();

            if (previousStatus is null || previousStatus != status)
            {
                previousStatus = status;
                QueueDispatch(new ZLinkRegistryEvent(
                    source.SourceName,
                    timestamp,
                    ZLinkRegistryEventKind.StatusChanged,
                    status,
                    null,
                    null));
            }

            if (previousTopology is null || !previousTopology.SequenceEqual(topology))
            {
                previousTopology = topology;
                QueueDispatch(new ZLinkRegistryEvent(
                    source.SourceName,
                    timestamp,
                    ZLinkRegistryEventKind.TopologyChanged,
                    null,
                    topology,
                    null));
            }

            if (previousSummary is null || !previousSummary.SequenceEqual(summary))
            {
                previousSummary = summary;
                QueueDispatch(new ZLinkRegistryEvent(
                    source.SourceName,
                    timestamp,
                    ZLinkRegistryEventKind.ServiceSummaryChanged,
                    null,
                    null,
                    summary));
            }

            await Task.Delay(source.Interval, cancellationToken);
        }
    }

    private async Task RunSpotLoopAsync(
        ZLinkPollingMonitoringRegistration source,
        ZLinkFrameworkRuntime frameworkRuntime,
        CancellationToken cancellationToken)
    {
        ZLinkSpotMonitoringSnapshot? previous = null;

        while (!cancellationToken.IsCancellationRequested)
        {
            ZLinkSpotMonitoringSnapshot snapshot;
            try
            {
                snapshot = frameworkRuntime.GetSpotMonitoringSnapshot(source.SourceName);
            }
            catch (InvalidOperationException ex)
            {
                throw new ZLinkConfigurationException(ex.Message);
            }

            var timestamp = DateTimeOffset.UtcNow;

            if (previous is null || previous.Status != snapshot.Status)
            {
                QueueDispatch(new ZLinkSpotEvent(
                    source.SourceName,
                    timestamp,
                    ZLinkSpotEventKind.StatusChanged,
                    snapshot.Status,
                    null,
                    null));
            }

            if (previous is null || !previous.Peers.SequenceEqual(snapshot.Peers))
            {
                QueueDispatch(new ZLinkSpotEvent(
                    source.SourceName,
                    timestamp,
                    ZLinkSpotEventKind.PeersChanged,
                    null,
                    snapshot.Peers,
                    null));
            }

            if (previous is null || !previous.Subjects.SequenceEqual(snapshot.Subjects))
            {
                QueueDispatch(new ZLinkSpotEvent(
                    source.SourceName,
                    timestamp,
                    ZLinkSpotEventKind.SubjectsChanged,
                    null,
                    null,
                    snapshot.Subjects));
            }

            previous = snapshot;
            await Task.Delay(source.Interval, cancellationToken);
        }
    }

    private void QueueDispatch<TEvent>(TEvent @event)
        where TEvent : IZLinkRuntimeEvent
    {
        var cancellationToken = _stopTokenSource?.Token ?? CancellationToken.None;
        _ = Task.Run(
            async () =>
            {
                try
                {
                    await dispatcher.DispatchAsync(@event, cancellationToken);
                }
                catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
                {
                }
            },
            cancellationToken);
    }

    private ZLinkSocketEvent? MapSocketEvent(
        ZLinkSocketMonitoringRegistration source,
        global::Zlink.MonitorEvent monitorEvent)
    {
        var eventKind = monitorEvent.Event switch
        {
            global::Zlink.MonitorEventType.Connected => ZLinkSocketEventKind.Connected,
            global::Zlink.MonitorEventType.ConnectionReady => ZLinkSocketEventKind.ConnectionReady,
            global::Zlink.MonitorEventType.Disconnected => ZLinkSocketEventKind.Disconnected,
            global::Zlink.MonitorEventType.HandshakeFailedNoDetail => ZLinkSocketEventKind.HandshakeFailed,
            global::Zlink.MonitorEventType.HandshakeFailedProtocol => ZLinkSocketEventKind.HandshakeFailed,
            global::Zlink.MonitorEventType.HandshakeFailedAuth => ZLinkSocketEventKind.HandshakeFailed,
            global::Zlink.MonitorEventType.PeerAdmissionChanged => ZLinkSocketEventKind.PeerAdmissionChanged,
            global::Zlink.MonitorEventType.Closed => ZLinkSocketEventKind.Closed,
            global::Zlink.MonitorEventType.CloseFailed => ZLinkSocketEventKind.Closed,
            global::Zlink.MonitorEventType.MonitorStopped => ZLinkSocketEventKind.Closed,
            _ => ZLinkSocketEventKind.Internal,
        };

        if (source.Events.Count > 0 && !source.Events.Contains(eventKind))
        {
            return null;
        }

        return new ZLinkSocketEvent(
            source.SourceName,
            DateTimeOffset.UtcNow,
            eventKind,
            monitorEvent.RoutingId,
            monitorEvent.LocalAddr,
            monitorEvent.RemoteAddr,
            new ZLinkSocketDiagnostic(
                (ZLinkSocketNativeEventType)monitorEvent.Event,
                monitorEvent.Value));
    }

    private ZLinkDiscoveryEvent? MapDiscoveryEvent(
        ZLinkDiscoveryMonitoringRegistration source,
        global::Zlink.ServiceEvent serviceEvent)
    {
        var eventKind = serviceEvent.EventType switch
        {
            global::Zlink.ServiceEventType.DiscoveryServiceUp => ZLinkDiscoveryEventKind.ServiceUp,
            global::Zlink.ServiceEventType.DiscoveryServiceDown => ZLinkDiscoveryEventKind.ServiceDown,
            global::Zlink.ServiceEventType.DiscoveryProvidersChanged => ZLinkDiscoveryEventKind.ProvidersChanged,
            global::Zlink.ServiceEventType.PeerAdmissionChanged => ZLinkDiscoveryEventKind.PeerAdmissionChanged,
            global::Zlink.ServiceEventType.Error => ZLinkDiscoveryEventKind.Error,
            global::Zlink.ServiceEventType.Closed => ZLinkDiscoveryEventKind.Closed,
            _ => ZLinkDiscoveryEventKind.Internal,
        };

        if (source.Events.Count > 0 && !source.Events.Contains(eventKind))
        {
            return null;
        }

        return new ZLinkDiscoveryEvent(
            source.SourceName,
            DateTimeOffset.UtcNow,
            eventKind,
            serviceEvent.ServiceName,
            serviceEvent.Endpoint,
            serviceEvent.RoutingId,
            serviceEvent.Subject,
            (ZLinkSubjectKind)serviceEvent.SubjectKind,
            new ZLinkDiscoveryDiagnostic(
                (ZLinkDiscoveryNativeEventType)serviceEvent.EventType,
                serviceEvent.Status,
                serviceEvent.ErrorCode,
                serviceEvent.Value,
                serviceEvent.DetailFlags));
    }

    private async ValueTask DisposeMonitorsAsync()
    {
        for (var index = _monitors.Count - 1; index >= 0; index--)
        {
            await _monitors[index].DisposeAsync();
        }

        _monitors.Clear();
    }
}
