using System.Runtime.CompilerServices;
using System.Threading.Channels;

namespace Zlink.Framework.AspNetCore;

internal sealed class ZLinkFrameworkMaintenanceRuntime : IZLinkFrameworkRuntime, IDisposable
{
    private static readonly TimeSpan DefaultDeadline = TimeSpan.FromSeconds(30);

    private readonly ZLinkDrainCoordinator _shutdown;
    private readonly Func<CancellationToken, ValueTask<ZLinkFrameworkTerminationReason?>> _retirePreflight;
    private readonly Func<ZLinkDrainRemainderCounts> _pending;
    private readonly IDisposable _metricRegistration;
    private readonly object _gate = new();
    private readonly List<Channel<ZLinkFrameworkRuntimeEvent>> _observers = [];
    private Task<ZLinkFrameworkTerminationResult>? _operation;
    private ZLinkFrameworkRuntimeState _state = ZLinkFrameworkRuntimeState.Preparing;
    private ZLinkFrameworkTerminationIntent? _effectiveIntent;
    private DateTimeOffset? _deadline;
    private ZLinkFrameworkTerminationReason? _blocker;
    private ZLinkFrameworkTerminationResult? _terminal;
    private CancellationTokenSource? _preflightCancellation;
    private ulong _sequence;
    private long _operationStartedTimestamp;
    private bool _workSealed;
    private bool _shutdownRequestedDuringPreflight;
    private int _disposed;

    internal ZLinkFrameworkMaintenanceRuntime(
        ZLinkDrainCoordinator shutdown,
        Func<CancellationToken, ValueTask<ZLinkFrameworkTerminationReason?>> retirePreflight,
        Func<ZLinkDrainRemainderCounts> pending)
    {
        _shutdown = shutdown;
        _retirePreflight = retirePreflight;
        _pending = pending;
        _metricRegistration = ZLinkRuntimeMetrics.RegisterTerminationState(
            () => State.ToString().ToLowerInvariant());
    }

    public ZLinkFrameworkRuntimeState State
    {
        get
        {
            lock (_gate) return _state;
        }
    }

    public bool IsReady => State == ZLinkFrameworkRuntimeState.Serving;

    internal void MarkServing()
    {
        lock (_gate)
        {
            if (_state != ZLinkFrameworkRuntimeState.Preparing)
                return;
            TransitionUnderLock(ZLinkFrameworkRuntimeState.Serving);
        }
    }

    internal void MarkError()
    {
        lock (_gate)
        {
            if (_state is ZLinkFrameworkRuntimeState.Stopped or ZLinkFrameworkRuntimeState.Draining)
                return;
            TransitionUnderLock(ZLinkFrameworkRuntimeState.Error);
        }
    }

    public ZLinkFrameworkRuntimeSnapshot Snapshot()
    {
        lock (_gate)
        {
            var pending = _pending();
            return new ZLinkFrameworkRuntimeSnapshot(
                _state,
                _effectiveIntent,
                _deadline,
                _workSealed,
                _blocker,
                checked((ulong)Math.Max(0, pending.Requests)),
                checked((ulong)Math.Max(0, pending.Actors + pending.Spots)),
                checked((ulong)Math.Max(0, pending.Sessions)),
                _terminal,
                _sequence,
                DateTimeOffset.UtcNow);
        }
    }

    public async IAsyncEnumerable<ZLinkFrameworkRuntimeEvent> ObserveAsync(
        int capacity = 1024,
        [EnumeratorCancellation] CancellationToken cancellationToken = default)
    {
        if (capacity <= 0)
            throw new ArgumentOutOfRangeException(nameof(capacity));

        var observer = Channel.CreateBounded<ZLinkFrameworkRuntimeEvent>(
            new BoundedChannelOptions(capacity)
            {
                SingleReader = true,
                SingleWriter = false,
                FullMode = BoundedChannelFullMode.DropOldest,
                AllowSynchronousContinuations = false
            });
        lock (_gate)
        {
            ThrowIfDisposed();
            _observers.Add(observer);
        }
        try
        {
            await foreach (var item in observer.Reader.ReadAllAsync(cancellationToken)
                               .ConfigureAwait(false))
                yield return item;
        }
        finally
        {
            lock (_gate) _observers.Remove(observer);
        }
    }

    public ValueTask<ZLinkFrameworkTerminationResult> RetireAsync(
        TimeSpan? deadline = null,
        CancellationToken cancellationToken = default) =>
        TerminateAsync(ZLinkFrameworkTerminationIntent.Retire, deadline, cancellationToken);

    public ValueTask<ZLinkFrameworkTerminationResult> ShutdownAsync(
        TimeSpan? deadline = null,
        CancellationToken cancellationToken = default) =>
        TerminateAsync(ZLinkFrameworkTerminationIntent.Shutdown, deadline, cancellationToken);

    private async ValueTask<ZLinkFrameworkTerminationResult> TerminateAsync(
        ZLinkFrameworkTerminationIntent intent,
        TimeSpan? deadline,
        CancellationToken cancellationToken)
    {
        var timeout = deadline ?? DefaultDeadline;
        if (timeout <= TimeSpan.Zero)
            throw new ArgumentOutOfRangeException(
                nameof(deadline),
                deadline,
                "Termination deadline must be greater than zero.");

        Task<ZLinkFrameworkTerminationResult> operation;
        lock (_gate)
        {
            ThrowIfDisposed();
            if (_terminal is { } terminal)
                return terminal;
            if (_operation is null)
            {
                if (intent == ZLinkFrameworkTerminationIntent.Retire
                    && _state != ZLinkFrameworkRuntimeState.Serving)
                {
                    var metricStarted = ZLinkRuntimeMetrics.StartTermination();
                    var blocked = new ZLinkFrameworkTerminationResult(
                        intent,
                        ZLinkFrameworkTerminationOutcome.Blocked,
                        ZLinkFrameworkTerminationReason.RuntimeNotReady);
                    PublishUnderLock(
                        "zlink.runtime.host.termination_changed",
                        blocked.Outcome,
                        blocked.Reason);
                    RecordCompletion(metricStarted, blocked);
                    return blocked;
                }

                _effectiveIntent = intent;
                _deadline = DateTimeOffset.UtcNow + timeout;
                _blocker = null;
                _operationStartedTimestamp = ZLinkRuntimeMetrics.StartTermination();
                _operation = ExecuteAsync(intent, _deadline.Value);
            }
            else if (intent == ZLinkFrameworkTerminationIntent.Shutdown
                     && _state == ZLinkFrameworkRuntimeState.Serving
                     && _effectiveIntent == ZLinkFrameworkTerminationIntent.Retire)
            {
                _shutdownRequestedDuringPreflight = true;
                _effectiveIntent = ZLinkFrameworkTerminationIntent.Shutdown;
                _preflightCancellation?.Cancel();
            }
            operation = _operation;
        }

        return await operation.WaitAsync(cancellationToken).ConfigureAwait(false);
    }

    private async Task<ZLinkFrameworkTerminationResult> ExecuteAsync(
        ZLinkFrameworkTerminationIntent intent,
        DateTimeOffset absoluteDeadline)
    {
        // The shared operation must be visible before a synchronously completed
        // preflight can clear a Blocked attempt and permit a later retry.
        await Task.Yield();

        var effectiveIntent = intent;
        if (effectiveIntent == ZLinkFrameworkTerminationIntent.Retire)
        {
            ZLinkFrameworkTerminationReason? blocker;
            CancellationTokenSource? preflightDeadline = null;
            lock (_gate)
            {
                if (_shutdownRequestedDuringPreflight)
                    effectiveIntent = ZLinkFrameworkTerminationIntent.Shutdown;
                else
                {
                    var preflightRemaining = absoluteDeadline - DateTimeOffset.UtcNow;
                    if (preflightRemaining <= TimeSpan.Zero)
                        return CompleteBlocked(ZLinkFrameworkTerminationReason.DeadlineExceeded);
                    preflightDeadline = new CancellationTokenSource(preflightRemaining);
                    _preflightCancellation = preflightDeadline;
                }
            }

            blocker = null;
            if (effectiveIntent == ZLinkFrameworkTerminationIntent.Retire)
            {
                try
                {
                    blocker = await _retirePreflight(preflightDeadline!.Token)
                        .ConfigureAwait(false);
                }
                catch (OperationCanceledException) when (preflightDeadline!.IsCancellationRequested)
                {
                    lock (_gate)
                        if (_shutdownRequestedDuringPreflight)
                            effectiveIntent = ZLinkFrameworkTerminationIntent.Shutdown;
                    if (effectiveIntent == ZLinkFrameworkTerminationIntent.Retire)
                        blocker = ZLinkFrameworkTerminationReason.DeadlineExceeded;
                }
                catch
                {
                    blocker = ZLinkFrameworkTerminationReason.StoreUnavailable;
                }
                finally
                {
                    lock (_gate)
                    {
                        if (ReferenceEquals(_preflightCancellation, preflightDeadline))
                            _preflightCancellation = null;
                        if (_shutdownRequestedDuringPreflight)
                            effectiveIntent = ZLinkFrameworkTerminationIntent.Shutdown;
                    }
                    preflightDeadline!.Dispose();
                }
            }

            if (effectiveIntent == ZLinkFrameworkTerminationIntent.Retire
                && blocker is { } reason)
                return CompleteBlocked(reason);

            if (effectiveIntent == ZLinkFrameworkTerminationIntent.Retire
                && absoluteDeadline <= DateTimeOffset.UtcNow)
                return CompleteBlocked(ZLinkFrameworkTerminationReason.DeadlineExceeded);

            if (effectiveIntent == ZLinkFrameworkTerminationIntent.Retire)
                lock (_gate)
                    TransitionUnderLock(ZLinkFrameworkRuntimeState.Retiring);
        }

        lock (_gate)
        {
            _workSealed = true;
            TransitionUnderLock(ZLinkFrameworkRuntimeState.Draining);
        }

        ZLinkDrainResult drained;
        try
        {
            var remaining = absoluteDeadline - DateTimeOffset.UtcNow;
            if (remaining <= TimeSpan.Zero)
                remaining = TimeSpan.FromTicks(1);
            drained = await _shutdown.DrainAsync(
                    effectiveIntent,
                    remaining,
                    CancellationToken.None)
                .ConfigureAwait(false);
        }
        catch
        {
            drained = new ForceStopped(ZLinkDrainForceReason.TeardownFailed);
        }

        var result = drained switch
        {
            Drained => new ZLinkFrameworkTerminationResult(
                effectiveIntent,
                ZLinkFrameworkTerminationOutcome.Stopped,
                ZLinkFrameworkTerminationReason.None),
            ForceStopped forced => new ZLinkFrameworkTerminationResult(
                effectiveIntent,
                ZLinkFrameworkTerminationOutcome.ForceStopped,
                MapForceReason(forced.Reason)),
            _ => throw new InvalidOperationException(
                $"Unknown drain result '{drained.GetType().Name}'.")
        };
        lock (_gate)
        {
            _terminal = result;
            TransitionUnderLock(ZLinkFrameworkRuntimeState.Stopped);
            PublishUnderLock(
                "zlink.runtime.host.termination_changed",
                result.Outcome,
                result.Reason);
            RecordCompletion(_operationStartedTimestamp, result);
        }
        return result;
    }

    private ZLinkFrameworkTerminationResult CompleteBlocked(
        ZLinkFrameworkTerminationReason reason)
    {
        lock (_gate)
        {
            var result = new ZLinkFrameworkTerminationResult(
                ZLinkFrameworkTerminationIntent.Retire,
                ZLinkFrameworkTerminationOutcome.Blocked,
                reason);
            _blocker = reason;
            _effectiveIntent = null;
            _deadline = null;
            _operation = null;
            PublishUnderLock(
                "zlink.runtime.host.termination_changed",
                result.Outcome,
                result.Reason);
            RecordCompletion(_operationStartedTimestamp, result);
            return result;
        }
    }

    private static ZLinkFrameworkTerminationReason MapForceReason(
        ZLinkDrainForceReason reason) =>
        reason switch
        {
            ZLinkDrainForceReason.DeadlineExceeded =>
                ZLinkFrameworkTerminationReason.DeadlineExceeded,
            ZLinkDrainForceReason.DrainingStatePublishFailed =>
                ZLinkFrameworkTerminationReason.RelocationFailed,
            ZLinkDrainForceReason.OwnerCleanupFailed =>
                ZLinkFrameworkTerminationReason.TeardownFailed,
            ZLinkDrainForceReason.TeardownFailed =>
                ZLinkFrameworkTerminationReason.TeardownFailed,
            _ => ZLinkFrameworkTerminationReason.TeardownFailed
        };

    private void TransitionUnderLock(ZLinkFrameworkRuntimeState state)
    {
        _state = state;
        PublishUnderLock("zlink.runtime.host.termination_changed", null, null);
    }

    private void PublishUnderLock(
        string identifier,
        ZLinkFrameworkTerminationOutcome? outcome,
        ZLinkFrameworkTerminationReason? reason)
    {
        var @event = new ZLinkFrameworkRuntimeEvent(
            identifier,
            checked(++_sequence),
            DateTimeOffset.UtcNow,
            _state,
            _effectiveIntent,
            outcome,
            reason);
        foreach (var observer in _observers)
            observer.Writer.TryWrite(@event);
    }

    private void ThrowIfDisposed() =>
        ObjectDisposedException.ThrowIf(Volatile.Read(ref _disposed) != 0, this);

    private static void RecordCompletion(
        long startedTimestamp,
        ZLinkFrameworkTerminationResult result) =>
        ZLinkRuntimeMetrics.CompleteTermination(
            startedTimestamp,
            result.EffectiveIntent == ZLinkFrameworkTerminationIntent.Retire
                ? "retire"
                : "shutdown",
            result.Outcome switch
            {
                ZLinkFrameworkTerminationOutcome.Stopped => "stopped",
                ZLinkFrameworkTerminationOutcome.Blocked => "blocked",
                ZLinkFrameworkTerminationOutcome.ForceStopped => "force_stopped",
                _ => "unknown"
            },
            result.Reason switch
            {
                ZLinkFrameworkTerminationReason.None => "none",
                ZLinkFrameworkTerminationReason.TargetUnavailable => "target_unavailable",
                ZLinkFrameworkTerminationReason.StoreUnavailable => "store_unavailable",
                ZLinkFrameworkTerminationReason.RelocationDisabled => "relocation_disabled",
                ZLinkFrameworkTerminationReason.StateIncompatible => "state_incompatible",
                ZLinkFrameworkTerminationReason.DeadlineExceeded => "deadline_exceeded",
                ZLinkFrameworkTerminationReason.RelocationFailed => "relocation_failed",
                ZLinkFrameworkTerminationReason.TeardownFailed => "teardown_failed",
                ZLinkFrameworkTerminationReason.RuntimeNotReady => "runtime_not_ready",
                _ => "unknown"
            });

    public void Dispose()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0)
            return;
        lock (_gate)
        {
            foreach (var observer in _observers)
                observer.Writer.TryComplete();
            _observers.Clear();
        }
        _metricRegistration.Dispose();
    }
}
