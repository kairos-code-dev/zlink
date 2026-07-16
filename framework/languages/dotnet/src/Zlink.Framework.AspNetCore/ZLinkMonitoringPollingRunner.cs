namespace Zlink.Framework.AspNetCore;

internal sealed class ZLinkMonitoringPollingRunner(
    ZLinkMonitoringRegistration registration,
    Action<ZLinkSpotEvent> dispatchSpotEvent,
    Action<ZLinkLocationRuntimeEvent> dispatchLocationRuntimeEvent)
{
    public async Task RunAsync(
        ZLinkFrameworkRuntime? frameworkRuntime,
        IZLinkLocationRuntimeQuery? locationQuery,
        CancellationToken cancellationToken)
    {
        var tasks = new List<Task>(
            registration.SpotSources.Count + registration.LocationRuntimeSources.Count);

        if (frameworkRuntime is not null)
            foreach (var source in registration.SpotSources.Values)
                tasks.Add(RunSpotLoopAsync(source, frameworkRuntime, cancellationToken));

        if (locationQuery is not null)
            foreach (var source in registration.LocationRuntimeSources.Values)
                tasks.Add(RunLocationRuntimeLoopAsync(source, locationQuery, cancellationToken));

        if (tasks.Count > 0) await Task.WhenAll(tasks);
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
            catch (InvalidOperationException)
            {
                // Source configuration is validated before this loop starts.
                // A later failure means the runtime generation ended between
                // polls because of drain or host shutdown.
                return;
            }

            var timestamp = DateTimeOffset.UtcNow;
            diff.DispatchChanges(snapshot, timestamp, dispatchSpotEvent);
            await Task.Delay(source.Interval, cancellationToken);
        }
    }

    private async Task RunLocationRuntimeLoopAsync(
        ZLinkPollingMonitoringRegistration source,
        IZLinkLocationRuntimeQuery locationQuery,
        CancellationToken cancellationToken)
    {
        var diff = new ZLinkLocationRuntimePollingEventDiff(source.SourceName);

        while (!cancellationToken.IsCancellationRequested)
        {
            var timestamp = DateTimeOffset.UtcNow;
            try
            {
                var snapshot = await ZLinkLocationRuntimePollingSnapshot.CaptureAsync(
                        locationQuery,
                        cancellationToken)
                    .ConfigureAwait(false);
                diff.DispatchChanges(snapshot, timestamp, dispatchLocationRuntimeEvent);
            }
            catch (Exception error) when (error is not OperationCanceledException)
            {
                // A store outage must not kill the source (fail-static):
                // it degrades to a StoreFailure event and the loop
                // keeps polling for recovery.
                diff.DispatchCaptureFailure(timestamp, dispatchLocationRuntimeEvent);
            }

            await Task.Delay(source.Interval, cancellationToken);
        }
    }
}
