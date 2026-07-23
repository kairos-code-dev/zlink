namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotSerialExecutor : IAsyncDisposable
{
    private readonly ZLinkSpotActivation _activation;
    private readonly Func<bool> _isDisposed;
    private readonly Func<bool> _flowCaptureEnabled;
    private readonly ZLinkSerialExecutionQueue _queue;

    public ZLinkSpotSerialExecutor(
        ZLinkSpotActivation activation,
        Func<bool> isDisposed,
        CancellationToken stopToken,
        IZLinkRuntimeErrorSink errorSink,
        Func<bool>? flowCaptureEnabled = null,
        object? executionOwner = null)
    {
        _activation = activation;
        _isDisposed = isDisposed;
        _flowCaptureEnabled = flowCaptureEnabled ?? AlwaysDisabled;
        _queue = new ZLinkSerialExecutionQueue(
            new ZLinkRuntimeTaskRunner(
                errorSink,
                stopToken,
                executionOwner ?? activation?.RuntimeExecutionOwner ?? new object()),
            errorSink,
            stopToken,
            spotMetricKind: "user");
    }

    private static bool AlwaysDisabled() => false;

    public async ValueTask DisposeAsync()
    {
        await _queue.DisposeAsync().ConfigureAwait(false);
    }

    public void RequestStop() => _queue.Complete();

    public async ValueTask ExecuteAsync(
        Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation,
        CancellationToken cancellationToken)
    {
        if (_isDisposed()) return;

        await _queue.RunAsync(
                ct => ExecuteOperationAsync(operation, null, ct),
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask ExecuteLifecycleAsync(
        Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation,
        CancellationToken cancellationToken)
    {
        if (_isDisposed()) return;

        await _queue.RunAsync(
                _ => ExecuteLifecycleOperationAsync(operation, cancellationToken),
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask ExecuteLifecycleOperationAsync(
        Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation,
        CancellationToken cancellationToken)
    {
        using var flow = ZLinkFlowContext.Enter(
            null,
            null,
            _flowCaptureEnabled(),
            ZLinkFlowOrigin.Lifecycle);
        await ExecuteOperationAsync(operation, null, cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask ExecuteAsync<TState>(
        Func<ZLinkSpotActivation, TState, CancellationToken, ValueTask> operation,
        TState state,
        CancellationToken cancellationToken)
    {
        if (_isDisposed()) return;

        await _queue.RunAsync(
                ct => ExecuteOperationAsync(operation, state, ct),
                cancellationToken)
            .ConfigureAwait(false);
    }

    public bool Queue(
        Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation,
        Action? onSkipped = null)
    {
        return _queue.TryPost(ct => ExecuteOperationAsync(operation, onSkipped, ct), out _);
    }

    public bool QueueAccepted(
        ReadOnlyMemory<byte> acceptedJournalRecord,
        Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation,
        Action relocationRelease,
        out Task completion)
    {
        if (_queue.TryPostAccepted(
            acceptedJournalRecord,
            ct => ExecuteOperationAsync(operation, relocationRelease, ct),
            relocationRelease,
            out var item))
        {
            completion = item.Completion;
            return true;
        }
        completion = Task.CompletedTask;
        return false;
    }

    internal bool TrySealRelocation(out ZLinkSerialRelocationSeal seal)
    {
        return _queue.TrySealRelocation(out seal);
    }

    internal ValueTask<ZLinkSerialRelocationSeal> SealRelocationAsync(
        CancellationToken cancellationToken)
    {
        return _queue.SealRelocationAsync(cancellationToken);
    }

    internal bool TryAbortRelocation(ZLinkSerialRelocationSeal seal)
    {
        return _queue.TryAbortRelocation(seal);
    }

    internal bool TryCommitRelocation(
        ZLinkSerialRelocationSeal seal,
        out IReadOnlyList<ZLinkAcceptedWorkRecord> held)
    {
        return _queue.TryCommitRelocation(seal, out held);
    }

    private async ValueTask ExecuteOperationAsync(
        Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation,
        Action? onSkipped,
        CancellationToken cancellationToken)
    {
        if (_isDisposed())
        {
            onSkipped?.Invoke();
            return;
        }

        using var _ = ZLinkSpotAmbientContext.Push(_activation);
        await operation(_activation, cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask ExecuteOperationAsync<TState>(
        Func<ZLinkSpotActivation, TState, CancellationToken, ValueTask> operation,
        TState state,
        CancellationToken cancellationToken)
    {
        if (_isDisposed()) return;

        using var _ = ZLinkSpotAmbientContext.Push(_activation);
        await operation(_activation, state, cancellationToken).ConfigureAwait(false);
    }
}
