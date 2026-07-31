using System.Runtime.CompilerServices;
using System.Threading.Channels;

namespace Zlink.Framework.AspNetCore;

internal sealed class ZLinkFrameworkMaintenanceRuntime :
    IZLinkFrameworkRuntime,
    IZLinkRuntimeTerminalFailureSink,
    IDisposable
{
    private static readonly TimeSpan DefaultDeadline = TimeSpan.FromSeconds(30);

    private readonly ZLinkDrainCoordinator _lifecycle;
    private readonly ZLinkFrameworkHostLifecycleState _hostLifecycle;
    private readonly Func<
            ZLinkFrameworkRelocationMode,
            long,
            CancellationToken,
            ValueTask<ZLinkFrameworkRelocationReason?>>
        _relocationPreflight;
    private readonly Func<CancellationToken, ValueTask<bool>> _publishRelocating;
    private readonly long _sourceApplicationVersion;
    private readonly IDisposable _metricRegistration;
    private readonly IDisposable _inboundMetricRegistration;
    private readonly Func<ZLinkInboundDispatchStatus> _inboundDispatchSnapshot;
    private readonly object _gate = new();
    private readonly List<Channel<ZLinkFrameworkRuntimeStatus>> _observers = [];

    private Task<ZLinkFrameworkRelocationResult>? _relocationOperation;
    private Task<ZLinkFrameworkTerminationResult>? _shutdownOperation;
    private CancellationTokenSource? _relocationCancellation;
    private ZLinkFrameworkRuntimeState? _relocationOriginState;
    private ZLinkFrameworkRelocationResult? _relocationResult;
    private ZLinkFrameworkTerminationResult? _terminationResult;
    private DateTimeOffset? _deadline;
    private ZLinkFrameworkRelocationMode? _activeMode;
    private long? _activeTargetVersion;
    private ulong _sequence;
    private int _disposed;

    internal ZLinkFrameworkMaintenanceRuntime(
        ZLinkDrainCoordinator lifecycle,
        ZLinkFrameworkHostLifecycleState hostLifecycle,
        Func<
                ZLinkFrameworkRelocationMode,
                long,
                CancellationToken,
                ValueTask<ZLinkFrameworkRelocationReason?>>
            relocationPreflight,
        Func<CancellationToken, ValueTask<bool>> publishRelocating,
        long sourceApplicationVersion = 0,
        Func<ZLinkInboundDispatchStatus>? inboundDispatchSnapshot = null)
    {
        _lifecycle = lifecycle;
        _hostLifecycle = hostLifecycle;
        _relocationPreflight = relocationPreflight;
        _publishRelocating = publishRelocating;
        _sourceApplicationVersion = sourceApplicationVersion;
        _inboundDispatchSnapshot = inboundDispatchSnapshot ?? EmptyInboundDispatchSnapshot;
        _metricRegistration = ZLinkRuntimeMetrics.RegisterHostState(
            () => HostStateMetricValue(_hostLifecycle.State));
        _inboundMetricRegistration = ZLinkRuntimeMetrics.RegisterHostInboundDispatch(
            _inboundDispatchSnapshot);
    }

    public ZLinkFrameworkRuntimeStatus Status
    {
        get
        {
            lock (_gate)
                return CreateStatusUnderLock(DateTimeOffset.UtcNow);
        }
    }

    internal void MarkServing()
    {
        lock (_gate)
        {
            if (_hostLifecycle.State == ZLinkFrameworkRuntimeState.Preparing)
                TransitionUnderLock(ZLinkFrameworkRuntimeState.Serving);
        }
    }

    internal void MarkError()
    {
        lock (_gate)
        {
            if (_hostLifecycle.State != ZLinkFrameworkRuntimeState.Stopped)
                TransitionUnderLock(ZLinkFrameworkRuntimeState.Error);
        }
    }

    void IZLinkRuntimeTerminalFailureSink.SealError(Exception error)
    {
        ArgumentNullException.ThrowIfNull(error);
        MarkError();
    }

    public async IAsyncEnumerable<ZLinkFrameworkRuntimeStatus> ObserveAsync(
        [EnumeratorCancellation] CancellationToken cancellationToken = default)
    {
        var observer = Channel.CreateBounded<ZLinkFrameworkRuntimeStatus>(
            new BoundedChannelOptions(1024)
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
            observer.Writer.TryWrite(CreateStatusUnderLock(DateTimeOffset.UtcNow));
        }
        try
        {
            await foreach (var status in observer.Reader.ReadAllAsync(cancellationToken)
                               .ConfigureAwait(false))
                yield return status;
        }
        finally
        {
            lock (_gate)
                _observers.Remove(observer);
        }
    }

    public ValueTask<ZLinkFrameworkRelocationResult> RelocateAsync(
        ZLinkFrameworkRelocationOptions options,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(options);
        var (effectiveTargetVersion, timeout) = Validate(options);

        Task<ZLinkFrameworkRelocationResult> operation;
        lock (_gate)
        {
            ThrowIfDisposed();
            if (_shutdownOperation is not null
                || _hostLifecycle.State is ZLinkFrameworkRuntimeState.Draining
                    or ZLinkFrameworkRuntimeState.Stopped)
            {
                if (_relocationResult is { } shutdownCompleted
                    && Matches(
                        shutdownCompleted,
                        options.Mode,
                        effectiveTargetVersion))
                    return ValueTask.FromResult(shutdownCompleted);
                return ValueTask.FromResult(Blocked(
                    options.Mode,
                    effectiveTargetVersion,
                    ZLinkFrameworkRelocationReason.ShutdownRequested));
            }
            if (_relocationOperation is not null)
            {
                if (_activeMode != options.Mode
                    || _activeTargetVersion != effectiveTargetVersion)
                    return ValueTask.FromResult(Blocked(
                        options.Mode,
                        effectiveTargetVersion,
                        ZLinkFrameworkRelocationReason.OperationInProgress));
                operation = _relocationOperation;
            }
            else
            {
                if (_hostLifecycle.State == ZLinkFrameworkRuntimeState.Relocated
                    && _relocationResult is { } completed)
                    return ValueTask.FromResult(completed);
                if (_hostLifecycle.State != ZLinkFrameworkRuntimeState.Serving)
                    return ValueTask.FromResult(Blocked(
                        options.Mode,
                        effectiveTargetVersion,
                        ZLinkFrameworkRelocationReason.RuntimeNotReady));
                _relocationOriginState = _hostLifecycle.State;
                _relocationResult = null;
                _activeMode = options.Mode;
                _activeTargetVersion = effectiveTargetVersion;
                _deadline = DateTimeOffset.UtcNow + timeout;
                _relocationCancellation = new CancellationTokenSource();
                _relocationOperation = ExecuteRelocationAsync(
                    options.Mode,
                    effectiveTargetVersion,
                    _deadline.Value,
                    _relocationCancellation);
                operation = _relocationOperation;
            }
        }
        return new ValueTask<ZLinkFrameworkRelocationResult>(
            operation.WaitAsync(cancellationToken));
    }

    public ValueTask<ZLinkFrameworkTerminationResult> ShutdownAsync(
        TimeSpan? deadline = null,
        CancellationToken cancellationToken = default)
    {
        var timeout = deadline ?? DefaultDeadline;
        if (timeout <= TimeSpan.Zero)
            throw new ArgumentOutOfRangeException(
                nameof(deadline),
                deadline,
                "Shutdown deadline must be greater than zero.");

        Task<ZLinkFrameworkTerminationResult> operation;
        CancellationTokenSource? relocationCancellation = null;
        lock (_gate)
        {
            ThrowIfDisposed();
            if (_terminationResult is { } completed)
                return ValueTask.FromResult(completed);
            if (_shutdownOperation is null)
            {
                var absoluteDeadline = DateTimeOffset.UtcNow + timeout;
                _deadline = absoluteDeadline;
                TransitionUnderLock(ZLinkFrameworkRuntimeState.Draining);
                _lifecycle.RequestShutdown(timeout);
                relocationCancellation = _relocationCancellation;
                _shutdownOperation = ExecuteShutdownAsync(
                    absoluteDeadline,
                    timeout);
            }
            operation = _shutdownOperation;
        }
        relocationCancellation?.Cancel();
        return new ValueTask<ZLinkFrameworkTerminationResult>(
            operation.WaitAsync(cancellationToken));
    }

    private async Task<ZLinkFrameworkRelocationResult> ExecuteRelocationAsync(
        ZLinkFrameworkRelocationMode mode,
        long targetApplicationVersion,
        DateTimeOffset absoluteDeadline,
        CancellationTokenSource shutdownCancellation)
    {
        await Task.Yield();
        using var shutdownSignal = shutdownCancellation;
        var metricStarted = ZLinkRuntimeMetrics.StartHostRelocation(
            mode == ZLinkFrameworkRelocationMode.PlannedMaintenance
                ? "planned_maintenance"
                : "rolling_update");
        using var deadline = CreateDeadline(absoluteDeadline);
        using var operationCancellation =
            CancellationTokenSource.CreateLinkedTokenSource(
                deadline.Token,
                shutdownCancellation.Token);
        ZLinkFrameworkRelocationReason? blocker;
        try
        {
            blocker = await _relocationPreflight(
                    mode,
                    targetApplicationVersion,
                    operationCancellation.Token)
                .ConfigureAwait(false);
            if (shutdownCancellation.IsCancellationRequested)
                blocker = ZLinkFrameworkRelocationReason.ShutdownRequested;
        }
        catch (OperationCanceledException)
            when (shutdownCancellation.IsCancellationRequested)
        {
            blocker = ZLinkFrameworkRelocationReason.ShutdownRequested;
        }
        catch (OperationCanceledException)
            when (deadline.IsCancellationRequested)
        {
            blocker = ZLinkFrameworkRelocationReason.DeadlineExceeded;
        }
        catch
        {
            blocker = ZLinkFrameworkRelocationReason.StoreUnavailable;
        }
        if (blocker is { } preflightBlocker)
            return CompleteBlocked(
                mode,
                targetApplicationVersion,
                preflightBlocker,
                metricStarted);

        bool published;
        try
        {
            published = await _publishRelocating(operationCancellation.Token)
                .ConfigureAwait(false);
        }
        catch (OperationCanceledException)
            when (shutdownCancellation.IsCancellationRequested)
        {
            return CompleteBlocked(
                mode,
                targetApplicationVersion,
                ZLinkFrameworkRelocationReason.ShutdownRequested,
                metricStarted);
        }
        catch (OperationCanceledException) when (deadline.IsCancellationRequested)
        {
            return CompleteBlocked(
                mode,
                targetApplicationVersion,
                ZLinkFrameworkRelocationReason.DeadlineExceeded,
                metricStarted);
        }
        catch
        {
            return CompleteBlocked(
                mode,
                targetApplicationVersion,
                ZLinkFrameworkRelocationReason.StoreUnavailable,
                metricStarted);
        }
        if (!published)
            return CompleteBlocked(
                mode,
                targetApplicationVersion,
                ZLinkFrameworkRelocationReason.StoreUnavailable,
                metricStarted);

        lock (_gate)
            TransitionUnderLock(ZLinkFrameworkRuntimeState.Relocating);

        ZLinkDrainResult result;
        try
        {
            var remaining = absoluteDeadline - DateTimeOffset.UtcNow;
            if (remaining <= TimeSpan.Zero)
                remaining = TimeSpan.FromTicks(1);
            result = await _lifecycle.DrainAsync(
                    ZLinkFrameworkLifecycleIntent.Relocate,
                    remaining,
                    MarkRelocated,
                    CancellationToken.None)
                .ConfigureAwait(false);
        }
        catch
        {
            result = new ForceStopped(ZLinkDrainForceReason.RelocationFailed);
        }

        if (result is DrainBlocked blocked)
            return CompleteBlocked(
                mode,
                targetApplicationVersion,
                blocked.Reason,
                metricStarted);
        if (result is not Drained)
        {
            lock (_gate)
                TransitionUnderLock(ZLinkFrameworkRuntimeState.Error);
            return CompleteBlocked(
                mode,
                targetApplicationVersion,
                ZLinkFrameworkRelocationReason.RelocationFailed,
                metricStarted,
                restoreServing: false);
        }

        lock (_gate)
        {
            var completed = new ZLinkFrameworkRelocationResult(
                mode,
                targetApplicationVersion,
                ZLinkFrameworkRelocationOutcome.Relocated,
                ZLinkFrameworkRelocationReason.None);
            _relocationResult = completed;
            _deadline = null;
            _relocationOperation = null;
            _relocationCancellation = null;
            _relocationOriginState = null;
            if (_hostLifecycle.State != ZLinkFrameworkRuntimeState.Relocated)
                TransitionUnderLock(ZLinkFrameworkRuntimeState.Relocated);
            else
                PublishUnderLock();
            RecordRelocationCompletion(metricStarted, completed);
            return completed;
        }
    }

    private async Task<ZLinkFrameworkTerminationResult> ExecuteShutdownAsync(
        DateTimeOffset absoluteDeadline,
        TimeSpan teardownBound)
    {
        await Task.Yield();
        var metricStarted = ZLinkRuntimeMetrics.StartHostShutdown();
        using var deadline = CreateDeadline(absoluteDeadline);

        Task<ZLinkFrameworkRelocationResult>? relocation;
        lock (_gate)
            relocation = _relocationOperation;
        ZLinkDrainResult drained;
        try
        {
            if (relocation is not null)
                await relocation.WaitAsync(deadline.Token)
                    .ConfigureAwait(false);
            var remaining = absoluteDeadline - DateTimeOffset.UtcNow;
            if (remaining <= TimeSpan.Zero)
                throw new OperationCanceledException(deadline.Token);
            drained = await _lifecycle.DrainAsync(
                    ZLinkFrameworkLifecycleIntent.Shutdown,
                    remaining,
                    cancellationToken: CancellationToken.None)
                .ConfigureAwait(false);
        }
        catch (OperationCanceledException)
            when (deadline.IsCancellationRequested)
        {
            drained = await _lifecycle.ForceStopAsync(
                    ZLinkDrainForceReason.DeadlineExceeded,
                    teardownBound)
                .ConfigureAwait(false);
        }
        catch
        {
            drained = new ForceStopped(ZLinkDrainForceReason.TeardownFailed);
        }

        var result = drained switch
        {
            Drained => new ZLinkFrameworkTerminationResult(
                ZLinkFrameworkTerminationOutcome.Stopped,
                ZLinkFrameworkTerminationReason.None),
            ForceStopped forced => new ZLinkFrameworkTerminationResult(
                ZLinkFrameworkTerminationOutcome.ForceStopped,
                forced.Reason == ZLinkDrainForceReason.DeadlineExceeded
                    ? ZLinkFrameworkTerminationReason.DeadlineExceeded
                    : ZLinkFrameworkTerminationReason.TeardownFailed),
            _ => new ZLinkFrameworkTerminationResult(
                ZLinkFrameworkTerminationOutcome.ForceStopped,
                ZLinkFrameworkTerminationReason.TeardownFailed)
        };
        lock (_gate)
        {
            _terminationResult = result;
            _deadline = null;
            TransitionUnderLock(ZLinkFrameworkRuntimeState.Stopped);
            metricStarted.Complete(
                result.Outcome == ZLinkFrameworkTerminationOutcome.Stopped
                    ? "stopped"
                    : "force_stopped",
                result.Reason == ZLinkFrameworkTerminationReason.None
                    ? "none"
                    : result.Reason == ZLinkFrameworkTerminationReason.DeadlineExceeded
                        ? "deadline_exceeded"
                        : "teardown_failed");
            return result;
        }
    }

    private void MarkRelocated()
    {
        lock (_gate)
        {
            if (_hostLifecycle.State == ZLinkFrameworkRuntimeState.Relocating)
                TransitionUnderLock(ZLinkFrameworkRuntimeState.Relocated);
        }
    }

    private ZLinkFrameworkRelocationResult CompleteBlocked(
        ZLinkFrameworkRelocationMode mode,
        long targetApplicationVersion,
        ZLinkFrameworkRelocationReason reason,
        ZLinkRuntimeMetrics.ZLinkHostMetricOperation metricStarted,
        bool restoreServing = true)
    {
        lock (_gate)
        {
            var result = Blocked(mode, targetApplicationVersion, reason);
            var originState = _relocationOriginState
                              ?? ZLinkFrameworkRuntimeState.Serving;
            if (reason == ZLinkFrameworkRelocationReason.ShutdownRequested)
                _relocationResult = result;
            _deadline = null;
            _activeMode = null;
            _activeTargetVersion = null;
            _relocationOperation = null;
            _relocationCancellation = null;
            _relocationOriginState = null;
            if (restoreServing
                && _hostLifecycle.State == ZLinkFrameworkRuntimeState.Relocating)
                TransitionUnderLock(originState);
            else
                PublishUnderLock();
            RecordRelocationCompletion(metricStarted, result);
            return result;
        }
    }

    private (long EffectiveTargetVersion, TimeSpan Timeout) Validate(
        ZLinkFrameworkRelocationOptions options)
    {
        if (!Enum.IsDefined(options.Mode))
            throw new ArgumentOutOfRangeException(nameof(options.Mode));
        var timeout = options.Deadline ?? DefaultDeadline;
        if (timeout <= TimeSpan.Zero)
            throw new ArgumentOutOfRangeException(nameof(options.Deadline));

        return options.Mode switch
        {
            ZLinkFrameworkRelocationMode.PlannedMaintenance
                when options.TargetApplicationVersion is not null =>
                throw new ArgumentException(
                    "PlannedMaintenance does not accept TargetApplicationVersion.",
                    nameof(options)),
            ZLinkFrameworkRelocationMode.PlannedMaintenance =>
                (_sourceApplicationVersion, timeout),
            ZLinkFrameworkRelocationMode.RollingUpdate
                when options.TargetApplicationVersion is not { } target
                     || target <= _sourceApplicationVersion =>
                throw new ArgumentException(
                    "RollingUpdate requires a target application version greater than the source version.",
                    nameof(options)),
            ZLinkFrameworkRelocationMode.RollingUpdate =>
                (options.TargetApplicationVersion!.Value, timeout),
            _ => throw new ArgumentOutOfRangeException(nameof(options.Mode))
        };
    }

    private static ZLinkFrameworkRelocationResult Blocked(
        ZLinkFrameworkRelocationMode mode,
        long targetApplicationVersion,
        ZLinkFrameworkRelocationReason reason) =>
        new(
            mode,
            targetApplicationVersion,
            ZLinkFrameworkRelocationOutcome.Blocked,
            reason);

    private static bool Matches(
        ZLinkFrameworkRelocationResult result,
        ZLinkFrameworkRelocationMode mode,
        long targetApplicationVersion) =>
        result.Mode == mode
        && result.TargetApplicationVersion == targetApplicationVersion;

    private static CancellationTokenSource CreateDeadline(
        DateTimeOffset absoluteDeadline)
    {
        var remaining = absoluteDeadline - DateTimeOffset.UtcNow;
        return new CancellationTokenSource(
            remaining > TimeSpan.Zero ? remaining : TimeSpan.FromTicks(1));
    }

    private ZLinkFrameworkRuntimeStatus CreateStatusUnderLock(DateTimeOffset observedAt)
    {
        var state = _hostLifecycle.State;
        var accepting = state == ZLinkFrameworkRuntimeState.Serving;
        return new ZLinkFrameworkRuntimeStatus(
            state,
            state == ZLinkFrameworkRuntimeState.Serving,
            accepting,
            _deadline,
            _relocationResult,
            _terminationResult,
            _sequence,
            observedAt,
            _inboundDispatchSnapshot());
    }

    private void TransitionUnderLock(ZLinkFrameworkRuntimeState state)
    {
        _hostLifecycle.TransitionTo(state);
        PublishUnderLock();
    }

    private void PublishUnderLock()
    {
        _sequence = checked(_sequence + 1);
        var status = CreateStatusUnderLock(DateTimeOffset.UtcNow);
        foreach (var observer in _observers)
            observer.Writer.TryWrite(status);
    }

    private static void RecordRelocationCompletion(
        ZLinkRuntimeMetrics.ZLinkHostMetricOperation started,
        ZLinkFrameworkRelocationResult result) =>
        started.Complete(
            result.Outcome == ZLinkFrameworkRelocationOutcome.Relocated
                ? "relocated"
                : "blocked",
            RelocationReasonMetricValue(result.Reason));

    private static string HostStateMetricValue(ZLinkFrameworkRuntimeState state) =>
        state switch
        {
            ZLinkFrameworkRuntimeState.Preparing => "preparing",
            ZLinkFrameworkRuntimeState.Serving => "serving",
            ZLinkFrameworkRuntimeState.Relocating => "relocating",
            ZLinkFrameworkRuntimeState.Relocated => "relocated",
            ZLinkFrameworkRuntimeState.Draining => "draining",
            ZLinkFrameworkRuntimeState.Stopped => "stopped",
            ZLinkFrameworkRuntimeState.Error => "error",
            _ => "error"
        };

    private static ZLinkInboundDispatchStatus EmptyInboundDispatchSnapshot() => default;

    private static string RelocationReasonMetricValue(
        ZLinkFrameworkRelocationReason reason) =>
        reason switch
        {
            ZLinkFrameworkRelocationReason.None => "none",
            ZLinkFrameworkRelocationReason.TargetUnavailable => "target_unavailable",
            ZLinkFrameworkRelocationReason.StoreUnavailable => "store_unavailable",
            ZLinkFrameworkRelocationReason.RelocationDisabled => "relocation_disabled",
            ZLinkFrameworkRelocationReason.StateIncompatible => "state_incompatible",
            ZLinkFrameworkRelocationReason.DeadlineExceeded => "deadline_exceeded",
            ZLinkFrameworkRelocationReason.RelocationFailed => "relocation_failed",
            ZLinkFrameworkRelocationReason.RuntimeNotReady => "runtime_not_ready",
            ZLinkFrameworkRelocationReason.ManualTopologyUnsupported => "manual_topology_unsupported",
            ZLinkFrameworkRelocationReason.ShutdownRequested => "shutdown_requested",
            ZLinkFrameworkRelocationReason.OperationInProgress => "operation_in_progress",
            _ => "relocation_failed"
        };

    private void ThrowIfDisposed() =>
        ObjectDisposedException.ThrowIf(Volatile.Read(ref _disposed) != 0, this);

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
        _inboundMetricRegistration.Dispose();
    }
}
