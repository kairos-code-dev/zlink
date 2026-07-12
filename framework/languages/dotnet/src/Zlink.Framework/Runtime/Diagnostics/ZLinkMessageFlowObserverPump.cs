using System.Threading.Channels;
using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Diagnostics;

internal sealed class ZLinkMessageFlowObserverPump(
    ZLinkDispatchOptionsModel options,
    IServiceProvider services,
    ZLinkRuntimeTaskRunner taskRunner) : IAsyncDisposable
{
    internal IZLinkRuntimeErrorSink ErrorSink => taskRunner.ErrorSink;

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
    private IZLinkMessageFlowObserver? _observer;
    private bool _ownsObserver;
    private readonly object _disposeGate = new();
    private Task? _disposeTask;
    private int _draining;
    private int _disposed;

    public bool Enqueue(ZLinkMessageFlowEvent flow)
    {
        if (Volatile.Read(ref _disposed) != 0 || !_events.Writer.TryWrite(flow)) return false;
        StartDrain();
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
        await failures.CaptureAsync(_scope.DisposeAsync).ConfigureAwait(false);
        failures.ThrowIfAny();
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

    private void DropQueuedEvents()
    {
        Interlocked.Exchange(ref _draining, 0);
        while (_events.Reader.TryRead(out _))
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
}
