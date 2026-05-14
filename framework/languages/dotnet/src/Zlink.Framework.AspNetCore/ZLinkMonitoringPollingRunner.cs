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
        var tasks = new ZLinkMonitoringPollingTasks(
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

        await tasks.WaitAsync();
    }

    private async Task RunRegistryLoopAsync(
        ZLinkPollingMonitoringRegistration source,
        IZLinkRegistryQuery query,
        CancellationToken cancellationToken)
    {
        var diff = new ZLinkRegistryPollingEventDiff(source.SourceName);

        while (!cancellationToken.IsCancellationRequested)
        {
            var timestamp = DateTimeOffset.UtcNow;
            var snapshot = await ZLinkRegistryPollingSnapshot.CaptureAsync(query, cancellationToken)
                .ConfigureAwait(false);
            diff.DispatchChanges(snapshot, timestamp, dispatchRegistryEvent);

            await Task.Delay(source.Interval, cancellationToken);
        }
    }

    private async Task RunSpotLoopAsync(
        ZLinkPollingMonitoringRegistration source,
        ZLinkFrameworkRuntime frameworkRuntime,
        CancellationToken cancellationToken)
    {
        var diff = new ZLinkSpotPollingEventDiff(source.SourceName);

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
            diff.DispatchChanges(snapshot, timestamp, dispatchSpotEvent);
            await Task.Delay(source.Interval, cancellationToken);
        }
    }
}
