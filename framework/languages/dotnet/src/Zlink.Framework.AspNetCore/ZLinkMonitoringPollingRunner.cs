using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.AspNetCore;

internal sealed class ZLinkMonitoringPollingRunner(
    IServiceProvider services,
    ZLinkMonitoringRegistration registration,
    Action<ZLinkRegistryEvent> dispatchRegistryEvent,
    Action<ZLinkSpotEvent> dispatchSpotEvent)
{
    public async Task RunAsync(
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
            var status = await query.StatusSnapshotAsync(cancellationToken);
            var topology = (await query.TopologySnapshotAsync(cancellationToken))
                .OrderBy(static entry => entry.ChannelName, StringComparer.Ordinal)
                .ThenBy(static entry => entry.Endpoint, StringComparer.Ordinal)
                .ThenBy(static entry => entry.RoutingId?.ToString(), StringComparer.Ordinal)
                .ToArray();
            var summary = (await query.ServiceSummarySnapshotAsync(cancellationToken: cancellationToken))
                .OrderBy(static entry => entry.ChannelName, StringComparer.Ordinal)
                .ThenBy(static entry => entry.AutoConnectType)
                .ThenBy(static entry => entry.ServiceRole)
                .ToArray();

            if (previousStatus is null || previousStatus != status)
            {
                previousStatus = status;
                dispatchRegistryEvent(new ZLinkRegistryEvent(
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
                dispatchRegistryEvent(new ZLinkRegistryEvent(
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
                dispatchRegistryEvent(new ZLinkRegistryEvent(
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
                dispatchSpotEvent(new ZLinkSpotEvent(
                    source.SourceName,
                    timestamp,
                    ZLinkSpotEventKind.StatusChanged,
                    snapshot.Status,
                    null,
                    null));
            }

            if (previous is null || !previous.Peers.SequenceEqual(snapshot.Peers))
            {
                dispatchSpotEvent(new ZLinkSpotEvent(
                    source.SourceName,
                    timestamp,
                    ZLinkSpotEventKind.PeersChanged,
                    null,
                    snapshot.Peers,
                    null));
            }

            if (previous is null || !previous.Subjects.SequenceEqual(snapshot.Subjects))
            {
                dispatchSpotEvent(new ZLinkSpotEvent(
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
}
