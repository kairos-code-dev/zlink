using Microsoft.Extensions.Logging;

namespace Zlink.Framework.AspNetCore;

internal sealed class ZLinkFrameworkDrainExecutor : IZLinkDrainExecutor
{
    private static readonly TimeSpan SchedulerJitterBudget = TimeSpan.FromMilliseconds(100);
    private readonly ZLinkDrainExecutionOperations _operations;
    private readonly ZLinkLocationOptions _locationOptions;
    private readonly ILogger<ZLinkFrameworkDrainExecutor>? _logger;

    public ZLinkFrameworkDrainExecutor(
        ZLinkFrameworkRuntime runtime,
        ZLinkLocationOptions locationOptions,
        ZLinkLocationAutoConnectHost? autoConnect,
        ZLinkLocationRuntime? locationRuntime,
        ILogger<ZLinkFrameworkDrainExecutor>? logger = null)
        : this(
            ZLinkDrainExecutionOperations.Create(
                runtime,
                autoConnect,
                locationRuntime),
            locationOptions,
            logger)
    {
    }

    internal ZLinkFrameworkDrainExecutor(
        ZLinkDrainExecutionOperations operations,
        ZLinkLocationOptions locationOptions,
        ILogger<ZLinkFrameworkDrainExecutor>? logger = null)
    {
        _operations = operations;
        _locationOptions = locationOptions;
        _logger = logger;
    }

    public async ValueTask<ZLinkDrainForceReason?> ExecuteAsync(
        TimeSpan deadline,
        CancellationToken deadlineToken)
    {
        if (!await PublishDrainingMarkerAsync(deadlineToken).ConfigureAwait(false))
            return ZLinkDrainForceReason.DrainingStatePublishFailed;

        var propagationDelay = CalculatePropagationDelay(_locationOptions);
        if (_operations.HasAutoConnect)
            _logger?.LogInformation(
                "ZLink drain propagation bound polling={PollingInterval} storeReadTimeout={StoreReadTimeout} schedulerJitterBudget={SchedulerJitterBudget} total={PropagationBound}",
                _locationOptions.PollingInterval,
                ZLinkLocationStoreRead.Timeout,
                SchedulerJitterBudget,
                propagationDelay);
        var propagation = !_operations.HasAutoConnect
            ? Task.CompletedTask
            : Task.Delay(propagationDelay, deadlineToken);
        await propagation.ConfigureAwait(false);
        _operations.SealRequestAdmissions();
        _operations.BeginSpotDrain();
        await _operations.WaitForAcceptedActorHandoffs(deadlineToken).ConfigureAwait(false);

        var actorsDrained = false;
        var spotsDrained = false;
        while (!actorsDrained || !spotsDrained)
        {
            actorsDrained = await _operations.DrainActors(deadlineToken).ConfigureAwait(false);
            spotsDrained = await _operations.DrainSpots(deadlineToken).ConfigureAwait(false);
            if (!actorsDrained || !spotsDrained)
                await Task.Delay(_locationOptions.PollingInterval, deadlineToken).ConfigureAwait(false);
        }
        await _operations.StopAndWaitOperations().WaitAsync(deadlineToken)
            .ConfigureAwait(false);

        if (!await _operations.DrainStreamSessions(deadlineToken).ConfigureAwait(false))
            return ZLinkDrainForceReason.TeardownFailed;

        if (_operations.HasAutoConnect)
            await _operations.FreezeOwnerWrites(deadlineToken).ConfigureAwait(false);
        if (!await CleanupOwnerAsync(deadlineToken).ConfigureAwait(false))
            return ZLinkDrainForceReason.OwnerCleanupFailed;
        return null;
    }

    public async ValueTask ForceStopAsync(
        ZLinkDrainForceReason reason,
        CancellationToken cancellationToken)
    {
        _ = reason;
        var forcedSessions = _operations.GetRemainderCounts().Sessions;
        using var notificationBound = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        notificationBound.CancelAfter(TimeSpan.FromSeconds(2));
        try
        {
            _ = await _operations.DrainStreamSessions(notificationBound.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (notificationBound.IsCancellationRequested)
        {
        }

        var remaining = _operations.GetRemainderCounts();
        ZLinkRuntimeMetrics.RecordDrainForced("actor", remaining.Actors);
        ZLinkRuntimeMetrics.RecordDrainForced("spot", remaining.Spots);
        ZLinkRuntimeMetrics.RecordDrainForced("request", remaining.Requests);
        ZLinkRuntimeMetrics.RecordDrainForced("session", forcedSessions);

        var failures = new List<Exception>();
        await CaptureAsync(() => _operations.StopRuntime(CancellationToken.None), failures).ConfigureAwait(false);
        if (_operations.HasAutoConnect)
            await CaptureAsync(() => _operations.StopAutoConnect(CancellationToken.None), failures).ConfigureAwait(false);
        var ownerCleanupFailed = false;
        if (_operations.HasLocationRuntime && !_locationOptions.AllocatedRoutingIdsEnabled)
        {
            using var cleanupBound = new CancellationTokenSource(TimeSpan.FromSeconds(2));
            try
            {
                await _operations.CleanupOwner(cleanupBound.Token).ConfigureAwait(false);
            }
            catch (Exception error)
            {
                ownerCleanupFailed = true;
                failures.Add(error);
            }
            await CaptureAsync(() => _operations.StopLocation(CancellationToken.None), failures).ConfigureAwait(false);
        }
        if (ownerCleanupFailed)
            throw new ZLinkDrainForceException(ZLinkDrainForceReason.OwnerCleanupFailed, failures);
        if (failures.Count == 1) throw failures[0];
        if (failures.Count > 1) throw new AggregateException(failures);
    }

    private async ValueTask<bool> PublishDrainingMarkerAsync(CancellationToken cancellationToken)
    {
        if (!_operations.HasAutoConnect) return true;
        while (true)
        {
            try
            {
                if (await _operations.MarkDraining(cancellationToken).ConfigureAwait(false))
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
                await Task.Delay(_locationOptions.PollingInterval, cancellationToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                return false;
            }
        }
    }

    private async ValueTask<bool> CleanupOwnerAsync(CancellationToken cancellationToken)
    {
        if (!_operations.HasLocationRuntime) return true;
        // Allocated routing ids require socket close -> location row removal -> slot release ->
        // owner lease removal. The hosted stop sequence owns that ordering after drain.
        if (_locationOptions.AllocatedRoutingIdsEnabled) return true;
        while (true)
        {
            try
            {
                await _operations.CleanupOwner(cancellationToken).ConfigureAwait(false);
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
                    await Task.Delay(_locationOptions.PollingInterval, cancellationToken).ConfigureAwait(false);
                }
                catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
                {
                    return false;
                }
            }
        }
    }

    internal static TimeSpan CalculatePropagationDelay(ZLinkLocationOptions options) =>
        options.PollingInterval
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

internal sealed record ZLinkDrainExecutionOperations(
    bool HasAutoConnect,
    bool HasLocationRuntime,
    Func<CancellationToken, ValueTask<bool>> MarkDraining,
    Action SealRequestAdmissions,
    Action BeginSpotDrain,
    Func<CancellationToken, Task> WaitForAcceptedActorHandoffs,
    Func<CancellationToken, ValueTask<bool>> DrainActors,
    Func<CancellationToken, ValueTask<bool>> DrainSpots,
    Func<Task> StopAndWaitOperations,
    Func<CancellationToken, ValueTask<bool>> DrainStreamSessions,
    Func<CancellationToken, ValueTask> FreezeOwnerWrites,
    Func<CancellationToken, ValueTask> CleanupOwner,
    Func<ZLinkDrainRemainderCounts> GetRemainderCounts,
    Func<CancellationToken, ValueTask> StopRuntime,
    Func<CancellationToken, ValueTask> StopAutoConnect,
    Func<CancellationToken, ValueTask> StopLocation)
{
    internal static ZLinkDrainExecutionOperations Create(
        ZLinkFrameworkRuntime runtime,
        ZLinkLocationAutoConnectHost? autoConnect,
        ZLinkLocationRuntime? locationRuntime) => new(
        autoConnect is not null,
        locationRuntime is not null,
        autoConnect is null
            ? static _ => ValueTask.FromResult(true)
            : autoConnect.MarkDrainingAsync,
        runtime.SealRequestAdmissionsForDrain,
        runtime.BeginSpotDrain,
        runtime.WaitForAcceptedActorHandoffsAsync,
        runtime.DrainActorsAsync,
        runtime.TryDrainSpotsAsync,
        runtime.StopAndWaitOperationsForDrainAsync,
        runtime.DrainStreamSessionsAsync,
        autoConnect is null
            ? static _ => ValueTask.CompletedTask
            : autoConnect.FreezeOwnerWritesAsync,
        locationRuntime is null
            ? static _ => ValueTask.CompletedTask
            : locationRuntime.CleanupOwnerForDrainAsync,
        runtime.GetDrainRemainderCounts,
        runtime.StopAsync,
        autoConnect is null
            ? static _ => ValueTask.CompletedTask
            : autoConnect.StopAsync,
        locationRuntime is null
            ? static _ => ValueTask.CompletedTask
            : locationRuntime.StopAsync);
}
