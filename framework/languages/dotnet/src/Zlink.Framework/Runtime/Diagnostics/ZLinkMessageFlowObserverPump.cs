using System.Threading.Channels;
using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Diagnostics;

internal sealed class ZLinkMessageFlowObserverPump(
    ZLinkDispatchOptionsModel options,
    IServiceProvider services,
    ZLinkRuntimeTaskRunner taskRunner) : IAsyncDisposable
{
    internal IZLinkRuntimeFailureReporter ErrorSink => taskRunner.ErrorSink;

    private readonly AsyncServiceScope _scope = services.CreateAsyncScope();
    private readonly Channel<ZLinkMessageFlowEvent> _events = Channel.CreateBounded<ZLinkMessageFlowEvent>(
        new BoundedChannelOptions(1024)
        {
            // Preserve events that were already accepted. When the observer is
            // slower than dispatch, the event being submitted is the one dropped.
            FullMode = BoundedChannelFullMode.DropWrite,
            SingleReader = true,
            SingleWriter = false
        },
        static dropped => ZLinkRuntimeMetrics.RecordObserverOverflow(
            dropped.Outcome switch
            {
                ZLinkMessageFlowOutcome.Received => "received",
                ZLinkMessageFlowOutcome.Dispatched => "dispatched",
                ZLinkMessageFlowOutcome.Replied => "replied",
                ZLinkMessageFlowOutcome.Dropped => "dropped",
                ZLinkMessageFlowOutcome.Sent => "sent",
                ZLinkMessageFlowOutcome.ReplyReceived => "reply_received",
                ZLinkMessageFlowOutcome.Error => "error",
                _ => "unknown"
            }));
    private readonly Channel<ZLinkRuntimeMessageFlowEvent> _runtimeEvents =
        Channel.CreateBounded<ZLinkRuntimeMessageFlowEvent>(
            new BoundedChannelOptions(1024)
            {
                FullMode = BoundedChannelFullMode.DropWrite,
                SingleReader = true,
                SingleWriter = false
            },
            static dropped => ZLinkRuntimeMetrics.RecordObserverOverflow(
                dropped.Phase ?? dropped.EventId));
    private IZLinkMessageFlowObserver? _observer;
    private bool _ownsObserver;
    private IZLinkRuntimeMessageFlowObserver? _runtimeObserver;
    private bool _ownsRuntimeObserver;
    private Zlink.Framework.Contracts.Dispatch.IZLinkRuntimeErrorSink?
        _runtimeErrorSink;
    private bool _ownsRuntimeErrorSink;
    private readonly object _disposeGate = new();
    private Task? _disposeTask;
    private int _draining;
    private int _runtimeDraining;
    private int _disposed;

    public bool Enqueue(ZLinkMessageFlowEvent flow)
    {
        if (Volatile.Read(ref _disposed) != 0 || !_events.Writer.TryWrite(flow)) return false;
        StartDrain();
        return true;
    }

    public bool EnqueueRuntime(ZLinkRuntimeMessageFlowEvent flow)
    {
        if (Volatile.Read(ref _disposed) != 0
            || !_runtimeEvents.Writer.TryWrite(flow))
            return false;
        StartRuntimeDrain();
        return true;
    }

    public ValueTask DisposeAsync()
    {
        Task task;
        TaskCompletionSource? start = null;
        lock (_disposeGate)
        {
            if (_disposeTask is null)
            {
                Volatile.Write(ref _disposed, 1);
                start = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                _disposeTask = DisposeCoreAsync(start.Task);
            }
            task = _disposeTask;
        }
        start?.TrySetResult();
        return new ValueTask(task);
    }

    private async Task DisposeCoreAsync(Task started)
    {
        await started.ConfigureAwait(false);
        while (_events.Reader.TryRead(out _))
        {
        }
        while (_runtimeEvents.Reader.TryRead(out _))
        {
        }

        var failures = new ZLinkFailureCollector();
        if (_ownsObserver && _observer is IAsyncDisposable asyncDisposable)
            await failures.CaptureAsync(asyncDisposable.DisposeAsync).ConfigureAwait(false);
        else if (_ownsObserver && _observer is IDisposable disposable)
        {
            try
            {
                disposable.Dispose();
            }
            catch (Exception exception)
            {
                failures = new ZLinkFailureCollector(exception);
            }
        }
        _observer = null;
        await DisposeOwnedAsync(
                _ownsRuntimeObserver ? _runtimeObserver : null,
                failures)
            .ConfigureAwait(false);
        await DisposeOwnedAsync(
                _ownsRuntimeErrorSink ? _runtimeErrorSink : null,
                failures)
            .ConfigureAwait(false);
        _runtimeObserver = null;
        _runtimeErrorSink = null;
        await failures.CaptureAsync(_scope.DisposeAsync).ConfigureAwait(false);
        failures.ThrowIfAny();
    }

    private static async ValueTask DisposeOwnedAsync(
        object? instance,
        ZLinkFailureCollector failures)
    {
        if (instance is IAsyncDisposable asyncDisposable)
            await failures.CaptureAsync(asyncDisposable.DisposeAsync)
                .ConfigureAwait(false);
        else if (instance is IDisposable disposable)
            await failures.CaptureAsync(() =>
                {
                    disposable.Dispose();
                    return ValueTask.CompletedTask;
                })
                .ConfigureAwait(false);
    }

    private async ValueTask DrainAsync(CancellationToken cancellationToken)
    {
        while (true)
        {
            while (_events.Reader.TryRead(out var flow))
            {
                try
                {
                    var observer = ResolveObserver();
                    if (observer is not null)
                        await observer.OnMessageFlowAsync(flow, cancellationToken).ConfigureAwait(false);
                }
                catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
                {
                    DropQueuedEvents();
                    return;
                }
                catch (Exception exception)
                {
                    taskRunner.ErrorSink.ReportUnhandledCallbackException(exception);
                }
            }

            Interlocked.Exchange(ref _draining, 0);
            if (!_events.Reader.TryPeek(out _)
                || Interlocked.CompareExchange(ref _draining, 1, 0) != 0)
                return;
        }
    }

    private void StartDrain()
    {
        if (Interlocked.CompareExchange(ref _draining, 1, 0) != 0) return;
        if (taskRunner.TryRunDetached("message-flow-observer", DrainAsync)) return;
        DropQueuedEvents();
    }

    private async ValueTask DrainRuntimeAsync(
        CancellationToken cancellationToken)
    {
        while (true)
        {
            while (_runtimeEvents.Reader.TryRead(out var flow))
            {
                try
                {
                    var observer = ResolveRuntimeObserver();
                    if (observer is not null)
                        await observer.OnMessageFlowAsync(flow, cancellationToken)
                            .ConfigureAwait(false);
                }
                catch (OperationCanceledException)
                    when (cancellationToken.IsCancellationRequested)
                {
                    DropQueuedRuntimeEvents();
                    return;
                }
                catch (Exception exception)
                {
                    ReportRuntimeObserverFailure(exception);
                }
            }

            Interlocked.Exchange(ref _runtimeDraining, 0);
            if (!_runtimeEvents.Reader.TryPeek(out _)
                || Interlocked.CompareExchange(
                    ref _runtimeDraining,
                    1,
                    0) != 0)
                return;
        }
    }

    private void StartRuntimeDrain()
    {
        if (Interlocked.CompareExchange(ref _runtimeDraining, 1, 0) != 0)
            return;
        if (taskRunner.TryRunDetached(
                "runtime-message-flow-observer",
                DrainRuntimeAsync))
            return;
        DropQueuedRuntimeEvents();
    }

    private void DropQueuedEvents()
    {
        Interlocked.Exchange(ref _draining, 0);
        while (_events.Reader.TryRead(out _))
        {
        }
    }

    private void DropQueuedRuntimeEvents()
    {
        Interlocked.Exchange(ref _runtimeDraining, 0);
        while (_runtimeEvents.Reader.TryRead(out _))
        {
        }
    }

    private IZLinkMessageFlowObserver? ResolveObserver()
    {
        if (_observer is not null) return _observer;
        if (options.MessageFlowObserver is { } configured)
            return _observer = configured;
        if (options.MessageFlowObserverType is not { } observerType) return null;

        if (_scope.ServiceProvider.GetService(observerType) is IZLinkMessageFlowObserver registered)
            return _observer = registered;

        _ownsObserver = true;
        return _observer = (IZLinkMessageFlowObserver)ActivatorUtilities.CreateInstance(
            _scope.ServiceProvider,
            observerType);
    }

    private IZLinkRuntimeMessageFlowObserver? ResolveRuntimeObserver()
    {
        if (_runtimeObserver is not null) return _runtimeObserver;
        if (options.RuntimeMessageFlowObserver is { } configured)
            return _runtimeObserver = configured;
        if (options.RuntimeMessageFlowObserverType is not { } observerType)
            return null;

        if (_scope.ServiceProvider.GetService(observerType)
            is IZLinkRuntimeMessageFlowObserver registered)
            return _runtimeObserver = registered;

        _ownsRuntimeObserver = true;
        return _runtimeObserver =
            (IZLinkRuntimeMessageFlowObserver)ActivatorUtilities.CreateInstance(
                _scope.ServiceProvider,
                observerType);
    }

    private Zlink.Framework.Contracts.Dispatch.IZLinkRuntimeErrorSink?
        ResolveRuntimeErrorSink()
    {
        if (_runtimeErrorSink is not null) return _runtimeErrorSink;
        if (options.RuntimeErrorSink is { } configured)
            return _runtimeErrorSink = configured;
        if (options.RuntimeErrorSinkType is not { } sinkType) return null;

        if (_scope.ServiceProvider.GetService(sinkType)
            is Zlink.Framework.Contracts.Dispatch.IZLinkRuntimeErrorSink
                registered)
            return _runtimeErrorSink = registered;

        _ownsRuntimeErrorSink = true;
        return _runtimeErrorSink =
            (Zlink.Framework.Contracts.Dispatch.IZLinkRuntimeErrorSink)
            ActivatorUtilities.CreateInstance(_scope.ServiceProvider, sinkType);
    }

    private void ReportRuntimeObserverFailure(Exception exception)
    {
        var sink = ResolveRuntimeErrorSink();
        if (sink is null)
        {
            ZLinkFrameworkDebugLog.UnhandledCallbackFailure(exception);
            return;
        }

        var reason = $"{exception.GetType().Name}: {exception.Message}";
        if (reason.Length > 1024) reason = reason[..1024];
        var error = new ZLinkRuntimeErrorEvent(
            "zlink.runtime_error",
            DateTimeOffset.UtcNow,
            "observer_failed",
            "message_flow_observer",
            reason);
        if (!taskRunner.TryRunDetached(
                "runtime-error-sink",
                async cancellationToken =>
                {
                    try
                    {
                        await sink.OnRuntimeErrorAsync(error, cancellationToken)
                            .ConfigureAwait(false);
                    }
                    catch (Exception sinkFailure)
                    {
                        ZLinkFrameworkDebugLog.UnhandledCallbackFailure(
                            sinkFailure);
                    }
                }))
            ZLinkFrameworkDebugLog.UnhandledCallbackFailure(exception);
    }
}
