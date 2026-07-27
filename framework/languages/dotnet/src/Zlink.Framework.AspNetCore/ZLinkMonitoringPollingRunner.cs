namespace Zlink.Framework.AspNetCore;

internal sealed class ZLinkMonitoringPollingRunner(
    ZLinkMonitoringRegistration registration,
    Action<ZLinkLocationRuntimeEvent> dispatchLocationRuntimeEvent)
{
    public async Task RunAsync(
        IZLinkLocationRuntimeQuery? locationQuery,
        CancellationToken cancellationToken)
    {
        var tasks = new List<Task>(registration.LocationRuntimeSources.Count);

        if (locationQuery is not null)
            foreach (var source in registration.LocationRuntimeSources.Values)
                tasks.Add(RunLocationRuntimeLoopAsync(source, locationQuery, cancellationToken));

        if (tasks.Count > 0) await Task.WhenAll(tasks);
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
