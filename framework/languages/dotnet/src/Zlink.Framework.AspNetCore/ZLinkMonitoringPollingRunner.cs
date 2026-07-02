namespace Zlink.Framework.AspNetCore;

internal sealed class ZLinkMonitoringPollingRunner(
    ZLinkMonitoringRegistration registration,
    Action<ZLinkSpotEvent> dispatchSpotEvent)
{
    public async Task RunAsync(
        ZLinkFrameworkRuntime? frameworkRuntime,
        CancellationToken cancellationToken)
    {
        var tasks = new ZLinkMonitoringPollingTasks(registration.SpotSources.Count);

        if (frameworkRuntime is not null)
            foreach (var source in registration.SpotSources.Values)
                tasks.Add(RunSpotLoopAsync(source, frameworkRuntime, cancellationToken));

        await tasks.WaitAsync();
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
