using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkStreamSessionRuntime : IAsyncDisposable
{
    private readonly ZLinkSessionContext _context;
    private readonly ZLinkMessageFlowTracer _flow;
    private IZLinkSession _handler = null!;
    private readonly Action<string> _removeSession;
    private readonly ZLinkFrameworkRuntime _runtime;
    private readonly AsyncServiceScope _scope;
    private readonly ZLinkStreamSessionSerialExecutor _serial;
    private readonly IZLinkBackendStreamSocket _socket;
    private int _connected;
    private int _disconnected;
    private int _disposed;

    public static async ValueTask<ZLinkStreamSessionRuntime> CreateAsync(
        IServiceProvider services,
        IZLinkBackendStreamSocket socket,
        RoutingId routingId,
        Type? headerSessionType,
        Action<string> removeSession)
    {
        AsyncServiceScope scope = default;
        var scopeCreated = false;
        ZLinkStreamSessionRuntime? session = null;
        try
        {
            scope = services.CreateAsyncScope();
            scopeCreated = true;
            session = new ZLinkStreamSessionRuntime(
                scope,
                socket,
                routingId,
                removeSession);
            session.Initialize(headerSessionType);
            return session;
        }
        catch (Exception initializationFailure)
        {
            var failures = new ZLinkFailureCollector(initializationFailure);
            if (session is not null)
                await failures.CaptureAsync(session.DisposeInitializationAsync).ConfigureAwait(false);
            else if (scopeCreated)
                await failures.CaptureAsync(scope.DisposeAsync).ConfigureAwait(false);
            failures.ThrowIfAny();
            throw new InvalidOperationException("Unreachable after session initialization cleanup.");
        }
    }

    private ZLinkStreamSessionRuntime(
        AsyncServiceScope scope,
        IZLinkBackendStreamSocket socket,
        RoutingId routingId,
        Action<string> removeSession)
    {
        _scope = scope;
        _socket = socket;
        _removeSession = removeSession;
        _runtime = scope.ServiceProvider.GetRequiredService<ZLinkFrameworkRuntime>();
        Stream = new ZLinkManagedStream(socket, routingId, _runtime.Registration.Codecs);
        _flow = new ZLinkMessageFlowTracer(
            _runtime.Registration.DispatchOptions,
            scope.ServiceProvider.GetService<ILogger<ZLinkStreamSessionRuntime>>(),
            _runtime);
        var handlers = new ZLinkSessionHandlerRegistry(scope.ServiceProvider);
        _context = new ZLinkSessionContext(
            _runtime,
            Stream,
            handlers,
            CloseAsync,
            CloseByProxyAsync);
        Handlers = handlers;
        _serial = new ZLinkStreamSessionSerialExecutor(_runtime.ExecutionOwner);
    }

    public ZLinkManagedStream Stream { get; }

    private ZLinkSessionHandlerRegistry Handlers { get; }

    internal void RequestStop() => _serial.RequestStop();

    private void Initialize(Type? headerSessionType)
    {
        Handlers.BindContext(_context);
        _handler = (IZLinkSession)ActivatorUtilities.CreateInstance(
            _scope.ServiceProvider,
            headerSessionType!,
            _context);
        if (!ReferenceEquals(_handler.Context, _context))
            throw new InvalidOperationException(
                $"Session '{_handler.GetType().FullName}' must expose the context provided by the runtime.");
        Handlers.AddScannedHandlers(_runtime.Registration.EnumerateHandlerScanAssemblies());
        _handler.Configure();
        Handlers.Bind();
    }

    private async ValueTask DisposeInitializationAsync()
    {
        var failures = new ZLinkFailureCollector();
        await failures.CaptureAsync(_serial.DisposeAsync).ConfigureAwait(false);
        await failures.CaptureAsync(_scope.DisposeAsync).ConfigureAwait(false);
        failures.ThrowIfAny();
    }

    internal async ValueTask DisposeUncommittedAsync()
    {
        var failures = new ZLinkFailureCollector();
        await failures.CaptureAsync(_serial.DisposeAsync).ConfigureAwait(false);
        await failures.CaptureAsync(() => _context.CleanupAsync(CancellationToken.None)).ConfigureAwait(false);
        await failures.CaptureAsync(_scope.DisposeAsync).ConfigureAwait(false);
        failures.ThrowIfAny();
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0) return;

        var failures = new List<Exception>();
        await CaptureAsync(_serial.DisposeAsync).ConfigureAwait(false);
        if (Interlocked.Exchange(ref _disconnected, 1) == 0)
            await CaptureAsync(() => _handler.OnDisconnectedAsync(CancellationToken.None)).ConfigureAwait(false);
        await CaptureAsync(() => _context.CleanupAsync(CancellationToken.None)).ConfigureAwait(false);
        Capture(() => _removeSession(Stream.SessionId));
        await CaptureAsync(_scope.DisposeAsync).ConfigureAwait(false);
        if (failures.Count == 1)
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failures[0]).Throw();
        if (failures.Count > 1) throw new AggregateException(failures);
        return;

        async ValueTask CaptureAsync(Func<ValueTask> cleanup)
        {
            try
            {
                await cleanup().ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }
        }

        void Capture(Action cleanup)
        {
            try
            {
                cleanup();
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }
        }
    }

    public void EnqueueConnected(string localAddr, string remoteAddr)
    {
        Enqueue(() => MarkConnectedAsync(localAddr, remoteAddr));
    }

    public void EnqueuePacket(Message header, Message payload)
    {
        Enqueue(
            () => DispatchPacketAsync(header, payload),
            () =>
            {
                header.Dispose();
                payload.Dispose();
            });
    }

    public void EnqueueDisconnected(ZLinkStreamError error)
    {
        if (Interlocked.Exchange(ref _disconnected, 1) != 0) return;

        Enqueue(() => MarkDisconnectedAsync(error));
    }

    public async ValueTask CloseAsync()
    {
        await Stream.CloseAsync();

        if (Interlocked.Exchange(ref _disconnected, 1) != 0) return;

        Enqueue(MarkClosedAsync);
    }

    public async ValueTask CloseByProxyAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (Interlocked.Exchange(ref _disconnected, 1) != 0) return;

        await Stream.CloseAsync();
        Enqueue(MarkProxyClosedAsync);
    }

    private async ValueTask MarkConnectedAsync(string localAddr, string remoteAddr)
    {
        Stream.UpdateAddresses(localAddr, remoteAddr);
        if (Interlocked.Exchange(ref _connected, 1) != 0) return;

        await _handler.OnConnectedAsync(CancellationToken.None);
    }

    private async ValueTask DispatchPacketAsync(
        Message header,
        Message payload)
    {
        using (header)
        using (payload)
        {
            await EnsureConnectedAsync();
            var decoded = ZLinkStreamProtocolDefaults.DecodeHeader(header.AsReadOnlyMemory());
            if (decoded.Kind == ZlinkStreamMessageKind.Control)
            {
                ZLinkStreamControlFrames.Dispatch(Stream, decoded, payload.AsReadOnlyMemory());
                return;
            }

            if (_context.TryCompleteResponse(decoded, payload))
            {
                if (_flow.Enabled(ZLinkMessageFlowOutcome.ReplyReceived))
                    _flow.Trace(new ZLinkMessageFlowEvent(
                        ZLinkMessageFlowOutcome.ReplyReceived,
                        ZLinkDispatchErrorSurface.StreamSession,
                        ZLinkDispatchMessageKind.Response,
                        decoded.Name,
                        CorrelationId: decoded.CorrelationId ?? decoded.RequestSeq?.ToString()));

                return;
            }

            if (_flow.Enabled(ZLinkMessageFlowOutcome.Received))
                _flow.Trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowOutcome.Received,
                    ZLinkDispatchErrorSurface.StreamSession,
                    decoded.RequestSeq.HasValue
                        ? ZLinkDispatchMessageKind.Request
                        : ZLinkDispatchMessageKind.Send,
                    decoded.Name,
                    CorrelationId: decoded.CorrelationId ?? decoded.RequestSeq?.ToString()));

            var dispatch = _context.EnterDispatch(decoded);
            try
            {
                await _handler.OnDispatchAsync(
                    dispatch,
                    ZLinkStreamPacketPayloadCodec.DecodeMessage(
                        decoded,
                        payload,
                        _runtime.Registration.Codecs,
                        _runtime.Registration.StreamCompressionCodec),
                    CancellationToken.None);

                if (_flow.Enabled(ZLinkMessageFlowOutcome.Dispatched))
                    _flow.Trace(new ZLinkMessageFlowEvent(
                        ZLinkMessageFlowOutcome.Dispatched,
                        ZLinkDispatchErrorSurface.StreamSession,
                        decoded.RequestSeq.HasValue
                            ? ZLinkDispatchMessageKind.Request
                            : ZLinkDispatchMessageKind.Send,
                        decoded.Name,
                        CorrelationId: decoded.CorrelationId ?? decoded.RequestSeq?.ToString()));
            }
            catch (Exception ex)
            {
                try
                {
                    await _context.ReplyErrorAsync(decoded, ex, CancellationToken.None)
                        .ConfigureAwait(false);
                }
                catch (Exception replyException) when (IsClosedReplyFailure(replyException))
                {
                }
            }
            finally
            {
                _context.ExitDispatch();
            }
        }
    }

    private async ValueTask MarkDisconnectedAsync(ZLinkStreamError error)
    {
        await CompleteSessionAsync(error, true);
    }

    private async ValueTask MarkClosedAsync()
    {
        await CompleteSessionAsync(null, true);
    }

    private async ValueTask MarkProxyClosedAsync()
    {
        await CompleteSessionAsync(null, false);
    }

    private async ValueTask CompleteSessionAsync(
        ZLinkStreamError? error,
        bool notifyDisconnected)
    {
        Exception? callbackFailure = null;
        if (error is { } streamError)
            try
            {
                await _handler.OnErrorAsync(streamError, CancellationToken.None).ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                callbackFailure = exception;
            }

        if (notifyDisconnected)
            try
            {
                await _handler.OnDisconnectedAsync(CancellationToken.None).ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                callbackFailure = callbackFailure is null
                    ? exception
                    : new AggregateException(callbackFailure, exception);
            }

        _runtime.TryRunDetached(
            $"stream-session-dispose:{Stream.SessionId}",
            _ => DisposeAsync());

        if (callbackFailure is not null)
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(callbackFailure).Throw();
    }

    private void Enqueue(Func<ValueTask> work, Action? onRejected = null)
    {
        if (!_serial.Enqueue(work)) onRejected?.Invoke();
    }

    private ValueTask EnsureConnectedAsync()
    {
        if (string.IsNullOrWhiteSpace(Stream.LocalAddr)
            || string.IsNullOrWhiteSpace(Stream.RemoteAddr))
            return ValueTask.CompletedTask;

        if (Interlocked.CompareExchange(ref _connected, 1, 0) != 0) return ValueTask.CompletedTask;

        return _handler.OnConnectedAsync(CancellationToken.None);
    }

    private static bool IsClosedReplyFailure(Exception exception)
    {
        return exception is ObjectDisposedException
                   or ZlinkCloseException
               || exception is ZlinkSubmitException
               {
                   Result: ZlinkSubmitException.ErrorCode.NotConnected
                   or ZlinkSubmitException.ErrorCode.Terminated
                   or ZlinkSubmitException.ErrorCode.InvalidHandle
               };
    }
}
