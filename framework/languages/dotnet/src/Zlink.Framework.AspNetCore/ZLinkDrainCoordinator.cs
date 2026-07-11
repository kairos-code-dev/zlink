using Zlink.Framework.Runtime.Host;

namespace Zlink.Framework.AspNetCore;

internal interface IZLinkDrainExecutor
{
    ValueTask<ZLinkDrainForceReason?> ExecuteAsync(
        TimeSpan deadline,
        CancellationToken deadlineToken);

    ValueTask ForceStopAsync(
        ZLinkDrainForceReason reason,
        CancellationToken cancellationToken);
}

internal sealed class ZLinkDrainForceException(
    ZLinkDrainForceReason reason,
    IReadOnlyList<Exception> failures) : Exception(
    $"Forced drain teardown failed with '{reason}'.",
    failures.Count == 1 ? failures[0] : new AggregateException(failures))
{
    public ZLinkDrainForceReason Reason { get; } = reason;
}

internal sealed class ZLinkDrainCoordinator : IZLinkDrainControl, IDisposable
{
    internal static readonly TimeSpan DefaultDeadline = TimeSpan.FromSeconds(30);

    private readonly ZLinkDrainAdmissionGate _admission;
    private readonly IZLinkDrainExecutor _executor;
    private readonly IZLinkRuntimeEventPublisher? _events;
    private readonly Func<bool> _flowGenerationEnabled;
    private readonly object _gate = new();
    private readonly TaskCompletionSource<ZLinkDrainResult> _terminal =
        new(TaskCreationOptions.RunContinuationsAsynchronously);
    private readonly IDisposable _metricRegistration;
    private string _state = "serving";
    private Task<ZLinkDrainResult>? _operation;

    public ZLinkDrainCoordinator(
        ZLinkDrainAdmissionGate admission,
        IZLinkDrainExecutor executor,
        IZLinkRuntimeEventPublisher? events,
        Func<bool>? flowGenerationEnabled = null)
    {
        _admission = admission;
        _executor = executor;
        _events = events;
        _flowGenerationEnabled = flowGenerationEnabled ?? AlwaysDisabled;
        _metricRegistration = ZLinkRuntimeMetrics.RegisterDrainState(
            () => Volatile.Read(ref _state));
    }

    public bool IsReady => !_admission.IsDraining;

    public ValueTask<ZLinkDrainResult> DrainAsync(
        CancellationToken cancellationToken = default) =>
        DrainAsync(DefaultDeadline, cancellationToken);

    public async ValueTask<ZLinkDrainResult> DrainAsync(
        TimeSpan deadline,
        CancellationToken cancellationToken = default)
    {
        if (deadline <= TimeSpan.Zero)
            throw new ArgumentOutOfRangeException(
                nameof(deadline),
                deadline,
                "Drain deadline must be greater than zero.");

        Task<ZLinkDrainResult> operation;
        lock (_gate)
        {
            if (_operation is null)
            {
                _admission.BeginDrain();
                Volatile.Write(ref _state, "draining");
                _operation = ExecuteSharedAsync(deadline);
            }
            operation = _operation;
        }

        return await operation.WaitAsync(cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask<ZLinkDrainResult> AwaitDrainedAsync(
        CancellationToken cancellationToken = default)
    {
        return await _terminal.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
    }

    private async Task<ZLinkDrainResult> ExecuteSharedAsync(TimeSpan deadline)
    {
        using var flow = ZLinkFlowContext.Enter(
            null,
            null,
            _flowGenerationEnabled(),
            ZLinkFlowOrigin.Lifecycle);
        var metricStarted = ZLinkRuntimeMetrics.StartDrain();
        ZLinkDrainResult result;
        try
        {
            await PublishStateAsync(ZLinkDrainState.Draining).ConfigureAwait(false);
            using var deadlineSource = new CancellationTokenSource(deadline);
            var forceReason = await _executor.ExecuteAsync(deadline, deadlineSource.Token)
                .ConfigureAwait(false);
            result = forceReason is null
                ? new Drained()
                : await ForceStopAsync(forceReason.Value).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            result = await ForceStopAsync(ZLinkDrainForceReason.DeadlineExceeded)
                .ConfigureAwait(false);
        }
        catch
        {
            result = await ForceStopAsync(ZLinkDrainForceReason.TeardownFailed)
                .ConfigureAwait(false);
        }

        if (result is Drained)
        {
            Volatile.Write(ref _state, "drained");
            await PublishStateAsync(ZLinkDrainState.Drained).ConfigureAwait(false);
            ZLinkRuntimeMetrics.CompleteDrain(metricStarted, "drained");
        }
        else
        {
            ZLinkRuntimeMetrics.CompleteDrain(metricStarted, "force_stopped");
        }

        _terminal.TrySetResult(result);
        return result;
    }

    private async ValueTask<ZLinkDrainResult> ForceStopAsync(ZLinkDrainForceReason reason)
    {
        Volatile.Write(ref _state, "force_stopping");
        await PublishStateAsync(ZLinkDrainState.ForceStopping).ConfigureAwait(false);
        try
        {
            await _executor.ForceStopAsync(reason, CancellationToken.None).ConfigureAwait(false);
        }
        catch (ZLinkDrainForceException failure)
        {
            reason = failure.Reason;
        }
        catch
        {
            reason = ZLinkDrainForceReason.TeardownFailed;
        }
        return new ForceStopped(reason);
    }

    private ValueTask PublishStateAsync(ZLinkDrainState state)
    {
        return _events is null
            ? ValueTask.CompletedTask
            : _events.PublishAsync(
                new ZLinkDrainEvent(DateTimeOffset.UtcNow, state),
                CancellationToken.None);
    }

    private static bool AlwaysDisabled() => false;

    public void Dispose() => _metricRegistration.Dispose();
}
