using Microsoft.Extensions.Logging;

namespace Zlink.Framework.AspNetCore;

internal sealed class ZLinkFrameworkDrainExecutor : IZLinkDrainExecutor
{
    private static readonly TimeSpan SchedulerJitterBudget = TimeSpan.FromMilliseconds(100);
    private readonly ZLinkDrainExecutionOperations _operations;
    private readonly ZLinkLocationOptions _locationOptions;
    private readonly ILogger<ZLinkFrameworkDrainExecutor>? _logger;
    private readonly Action _stopMeshMonitoring;

    public ZLinkFrameworkDrainExecutor(
        ZLinkFrameworkRuntime runtime,
        ZLinkRouteMeshRuntimeService routeMeshRuntime,
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
            logger,
            routeMeshRuntime.Stop)
    {
    }

    internal ZLinkFrameworkDrainExecutor(
        ZLinkDrainExecutionOperations operations,
        ZLinkLocationOptions locationOptions,
        ILogger<ZLinkFrameworkDrainExecutor>? logger = null,
        Action? stopMeshMonitoring = null)
    {
        _operations = operations;
        _locationOptions = locationOptions;
        _logger = logger;
        _stopMeshMonitoring = stopMeshMonitoring ?? Noop;
    }

    private static void Noop()
    {
    }

    public ValueTask<ZLinkDrainForceReason?> ExecuteAsync(
        TimeSpan deadline,
        CancellationToken deadlineToken) =>
        ExecuteAsync(
            ZLinkFrameworkTerminationIntent.Shutdown,
            deadline,
            deadlineToken);

    public async ValueTask<ZLinkDrainForceReason?> ExecuteAsync(
        ZLinkFrameworkTerminationIntent intent,
        TimeSpan deadline,
        CancellationToken deadlineToken)
    {
        if (intent == ZLinkFrameworkTerminationIntent.Shutdown)
            _operations.SealApplicationAdmissions();

        if (!await PublishDrainingMarkerAsync(deadlineToken).ConfigureAwait(false))
            return ZLinkDrainForceReason.DrainingStatePublishFailed;

        // The published state excludes placement and channel weight excludes
        // new select-one load. Retire keeps admissions open for the bounded
        // propagation interval; Shutdown sealed them before publication.
        if (!await PublishServingWeightAsync(deadlineToken).ConfigureAwait(false))
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
        if (intent == ZLinkFrameworkTerminationIntent.Retire)
            _operations.SealApplicationAdmissions();
        await _operations.WaitForAcceptedOperations().WaitAsync(deadlineToken)
            .ConfigureAwait(false);
        await _operations.WaitForAcceptedActorHandoffs(deadlineToken).ConfigureAwait(false);

        var actorsDrained = false;
        while (!actorsDrained)
        {
            actorsDrained = await _operations.DrainActors(deadlineToken).ConfigureAwait(false);
            if (!actorsDrained)
                await Task.Delay(_locationOptions.PollingInterval, deadlineToken).ConfigureAwait(false);
        }

        if (!await _operations.DrainStreamSessions(deadlineToken).ConfigureAwait(false))
            return ZLinkDrainForceReason.TeardownFailed;

        var spotsDrained = false;
        while (!spotsDrained)
        {
            spotsDrained = await _operations.DrainSpots(
                    intent == ZLinkFrameworkTerminationIntent.Retire,
                    deadlineToken)
                .ConfigureAwait(false);
            if (!spotsDrained)
                await Task.Delay(_locationOptions.PollingInterval, deadlineToken).ConfigureAwait(false);
        }

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
        Capture(_stopMeshMonitoring, failures);
        await CaptureAsync(() => _operations.ForceStopRuntime(CancellationToken.None), failures)
            .ConfigureAwait(false);
        if (_operations.HasAutoConnect)
            await CaptureAsync(() => _operations.StopAutoConnect(CancellationToken.None), failures).ConfigureAwait(false);
        var ownerCleanupFailed = false;
        if (_operations.HasLocationRuntime)
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

        static void Capture(Action operation, ICollection<Exception> failures)
        {
            try
            {
                operation();
            }
            catch (Exception error)
            {
                failures.Add(error);
            }
        }
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

    private async ValueTask<bool> PublishServingWeightAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            if (await _operations.QuiesceServingChannels(cancellationToken).ConfigureAwait(false))
                return true;
            try
            {
                await Task.Delay(_locationOptions.PollingInterval, cancellationToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                return false;
            }
        }

        return false;
    }

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
    Func<CancellationToken, ValueTask<bool>> QuiesceServingChannels,
    Func<CancellationToken, ValueTask<bool>> MarkDraining,
    Action SealApplicationAdmissions,
    Func<Task> WaitForAcceptedOperations,
    Func<CancellationToken, Task> WaitForAcceptedActorHandoffs,
    Func<CancellationToken, ValueTask<bool>> DrainActors,
    Func<bool, CancellationToken, ValueTask<bool>> DrainSpots,
    Func<CancellationToken, ValueTask<bool>> DrainStreamSessions,
    Func<CancellationToken, ValueTask> FreezeOwnerWrites,
    Func<CancellationToken, ValueTask> CleanupOwner,
    Func<ZLinkDrainRemainderCounts> GetRemainderCounts,
    Func<CancellationToken, ValueTask> ForceStopRuntime,
    Func<CancellationToken, ValueTask> StopAutoConnect,
    Func<CancellationToken, ValueTask> StopLocation)
{
    internal static ZLinkDrainExecutionOperations Create(
        ZLinkFrameworkRuntime runtime,
        ZLinkLocationAutoConnectHost? autoConnect,
        ZLinkLocationRuntime? locationRuntime) => new(
        autoConnect is not null,
        locationRuntime is not null,
        cancellationToken => runtime.QuiesceServingChannelsForDrainAsync(autoConnect, cancellationToken),
        autoConnect is null
            ? static _ => ValueTask.FromResult(true)
            : autoConnect.MarkDrainingAsync,
        runtime.SealApplicationAdmissionsForDrain,
        runtime.WaitForAcceptedOperationsForDrainAsync,
        runtime.WaitForAcceptedActorHandoffsAsync,
        runtime.DrainActorsAsync,
        runtime.TryDrainSpotsAsync,
        runtime.DrainStreamSessionsAsync,
        autoConnect is null
            ? static _ => ValueTask.CompletedTask
            : autoConnect.FreezeOwnerWritesAsync,
        locationRuntime is null
            ? static _ => ValueTask.CompletedTask
            : locationRuntime.CleanupOwnerForDrainAsync,
        runtime.GetDrainRemainderCounts,
        runtime.ForceStopAsync,
        autoConnect is null
            ? static _ => ValueTask.CompletedTask
            : autoConnect.StopAsync,
        locationRuntime is null
            ? static _ => ValueTask.CompletedTask
            : locationRuntime.StopAsync);
}
