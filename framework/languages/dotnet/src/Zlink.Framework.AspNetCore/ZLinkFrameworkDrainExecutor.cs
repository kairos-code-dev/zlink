using Microsoft.Extensions.Logging;

namespace Zlink.Framework.AspNetCore;

internal sealed class ZLinkFrameworkDrainExecutor(
    ZLinkFrameworkRuntime runtime,
    ZLinkLocationOptions locationOptions,
    ZLinkLocationAutoConnectHost? autoConnect,
    ZLinkLocationRuntime? locationRuntime,
    ILogger<ZLinkFrameworkDrainExecutor>? logger = null) : IZLinkDrainExecutor
{
    private static readonly TimeSpan SchedulerJitterBudget = TimeSpan.FromMilliseconds(100);

    public async ValueTask<ZLinkDrainForceReason?> ExecuteAsync(
        TimeSpan deadline,
        CancellationToken deadlineToken)
    {
        if (!await PublishDrainingMarkerAsync(deadlineToken).ConfigureAwait(false))
            return ZLinkDrainForceReason.DrainingStatePublishFailed;

        var propagationDelay = PropagationDelay();
        if (autoConnect is not null)
            logger?.LogInformation(
                "ZLink drain propagation bound polling={PollingInterval} storeReadTimeout={StoreReadTimeout} schedulerJitterBudget={SchedulerJitterBudget} total={PropagationBound}",
                locationOptions.PollingInterval,
                ZLinkLocationStoreRead.Timeout,
                SchedulerJitterBudget,
                propagationDelay);
        var propagation = autoConnect is null
            ? Task.CompletedTask
            : Task.Delay(propagationDelay, deadlineToken);
        await propagation.ConfigureAwait(false);
        runtime.SealRequestAdmissionsForDrain();
        await runtime.WaitForAcceptedActorHandoffsAsync(deadlineToken).ConfigureAwait(false);

        var actorsDrained = false;
        var spotsDrained = false;
        while (!actorsDrained || !spotsDrained)
        {
            actorsDrained = await runtime.DrainActorsAsync(deadlineToken).ConfigureAwait(false);
            spotsDrained = await runtime.TryDrainSpotsAsync(deadlineToken).ConfigureAwait(false);
            if (!actorsDrained || !spotsDrained)
                await Task.Delay(locationOptions.PollingInterval, deadlineToken).ConfigureAwait(false);
        }
        await runtime.StopAndWaitOperationsForDrainAsync().WaitAsync(deadlineToken)
            .ConfigureAwait(false);

        if (!await runtime.DrainStreamSessionsAsync(deadlineToken).ConfigureAwait(false))
            return ZLinkDrainForceReason.TeardownFailed;

        if (autoConnect is not null)
            await autoConnect.FreezeOwnerWritesAsync(deadlineToken).ConfigureAwait(false);
        if (!await CleanupOwnerAsync(deadlineToken).ConfigureAwait(false))
            return ZLinkDrainForceReason.OwnerCleanupFailed;
        return null;
    }

    public async ValueTask ForceStopAsync(
        ZLinkDrainForceReason reason,
        CancellationToken cancellationToken)
    {
        _ = reason;
        using var notificationBound = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        notificationBound.CancelAfter(TimeSpan.FromSeconds(2));
        try
        {
            _ = await runtime.DrainStreamSessionsAsync(notificationBound.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (notificationBound.IsCancellationRequested)
        {
        }

        var remaining = runtime.GetDrainRemainderCounts();
        ZLinkRuntimeMetrics.RecordDrainForced("actor", remaining.Actors);
        ZLinkRuntimeMetrics.RecordDrainForced("spot", remaining.Spots);
        ZLinkRuntimeMetrics.RecordDrainForced("request", remaining.Requests);
        ZLinkRuntimeMetrics.RecordDrainForced("session", remaining.Sessions);

        var failures = new List<Exception>();
        await CaptureAsync(() => runtime.StopAsync(CancellationToken.None), failures).ConfigureAwait(false);
        if (autoConnect is not null)
            await CaptureAsync(() => autoConnect.StopAsync(CancellationToken.None), failures).ConfigureAwait(false);
        var ownerCleanupFailed = false;
        if (locationRuntime is not null)
        {
            using var cleanupBound = new CancellationTokenSource(TimeSpan.FromSeconds(2));
            try
            {
                await locationRuntime.CleanupOwnerForDrainAsync(cleanupBound.Token).ConfigureAwait(false);
            }
            catch (Exception error)
            {
                ownerCleanupFailed = true;
                failures.Add(error);
            }
            await CaptureAsync(() => locationRuntime.StopAsync(CancellationToken.None), failures).ConfigureAwait(false);
        }
        if (ownerCleanupFailed)
            throw new ZLinkDrainForceException(ZLinkDrainForceReason.OwnerCleanupFailed, failures);
        if (failures.Count == 1) throw failures[0];
        if (failures.Count > 1) throw new AggregateException(failures);
    }

    private async ValueTask<bool> PublishDrainingMarkerAsync(CancellationToken cancellationToken)
    {
        if (autoConnect is null) return true;
        while (true)
        {
            try
            {
                if (await autoConnect.MarkDrainingAsync(cancellationToken).ConfigureAwait(false))
                    return true;
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                return false;
            }
            catch
            {
            }

            try
            {
                await Task.Delay(locationOptions.PollingInterval, cancellationToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                return false;
            }
        }
    }

    private async ValueTask<bool> CleanupOwnerAsync(CancellationToken cancellationToken)
    {
        if (locationRuntime is null) return true;
        while (true)
        {
            try
            {
                await locationRuntime.CleanupOwnerForDrainAsync(cancellationToken).ConfigureAwait(false);
                return true;
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                return false;
            }
            catch
            {
                try
                {
                    await Task.Delay(locationOptions.PollingInterval, cancellationToken).ConfigureAwait(false);
                }
                catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
                {
                    return false;
                }
            }
        }
    }

    private TimeSpan PropagationDelay() =>
        locationOptions.PollingInterval
        + ZLinkLocationStoreRead.Timeout
        + SchedulerJitterBudget;

    private static async ValueTask CaptureAsync(
        Func<ValueTask> operation,
        List<Exception> failures)
    {
        try
        {
            await operation().ConfigureAwait(false);
        }
        catch (Exception error)
        {
            failures.Add(error);
        }
    }
}
