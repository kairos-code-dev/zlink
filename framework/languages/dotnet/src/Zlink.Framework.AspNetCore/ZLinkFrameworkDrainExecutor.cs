namespace Zlink.Framework.AspNetCore;

internal sealed class ZLinkFrameworkDrainExecutor(
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration,
    ZLinkLocationOptions locationOptions,
    ZLinkLocationAutoConnectHost? autoConnect,
    ZLinkLocationRuntime? locationRuntime) : IZLinkDrainExecutor
{
    public async ValueTask<ZLinkDrainForceReason?> ExecuteAsync(
        TimeSpan deadline,
        CancellationToken deadlineToken)
    {
        if (!await PublishDrainingMarkerAsync(deadlineToken).ConfigureAwait(false))
            return ZLinkDrainForceReason.DrainingStatePublishFailed;

        var propagation = autoConnect is null
            ? Task.CompletedTask
            : Task.Delay(PropagationDelay(), deadlineToken);
        await propagation.ConfigureAwait(false);
        runtime.SealRequestAdmissionsForDrain();
        await runtime.WaitForAcceptedActorHandoffsAsync(deadlineToken).ConfigureAwait(false);

        var actorsDrained = await runtime.DrainActorsAsync(deadlineToken).ConfigureAwait(false);
        await runtime.DrainSpotsAsync(deadlineToken).ConfigureAwait(false);
        while (!actorsDrained)
        {
            await Task.Delay(locationOptions.PollingInterval, deadlineToken).ConfigureAwait(false);
            actorsDrained = await runtime.DrainActorsAsync(deadlineToken).ConfigureAwait(false);
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
        + registration.DefaultRequestTimeout
        + TimeSpan.FromMilliseconds(100);

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
