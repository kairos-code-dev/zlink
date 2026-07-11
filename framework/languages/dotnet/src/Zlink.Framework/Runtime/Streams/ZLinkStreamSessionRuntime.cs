using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkStreamSessionRuntime : IAsyncDisposable
{
    private readonly ZLinkSessionContext _context;
    private readonly ZLinkMessageFlowTracer _flow;
    private readonly ZLinkStreamSessionLiveness _liveness;
    private IZLinkSession _handler = null!;
    private readonly Action<string> _removeSession;
    private readonly ZLinkFrameworkRuntime _runtime;
    private readonly AsyncServiceScope _scope;
    private readonly ZLinkStreamSessionSerialExecutor _serial;
    private readonly IZLinkBackendStreamSocket _socket;
    private readonly object _disposeGate = new();
    private readonly object _terminalGate = new();
    private int _connected;
    private readonly TaskCompletionSource<bool> _completion =
        new(TaskCreationOptions.RunContinuationsAsynchronously);
    private readonly TaskCompletionSource<bool> _transportClosed =
        new(TaskCreationOptions.RunContinuationsAsynchronously);
    private readonly CancellationTokenSource _terminalCallbackStop = new();
    private TerminalClose? _terminalClose;
    private Task? _disposeTask;
    private int _streamMetricActive;
    private int _terminalSucceeded = 1;
    private int _retainApplicationScope;

    public static async ValueTask<ZLinkStreamSessionRuntime> CreateAsync(
        IServiceProvider services,
        IZLinkBackendStreamSocket socket,
        RoutingId routingId,
        Type? headerSessionType,
        Action<string> removeSession,
        string transport,
        TimeProvider timeProvider)
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
                removeSession,
                transport,
                timeProvider);
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
        Action<string> removeSession,
        string transport,
        TimeProvider timeProvider)
    {
        _scope = scope;
        _socket = socket;
        _removeSession = removeSession;
        _runtime = scope.ServiceProvider.GetRequiredService<ZLinkFrameworkRuntime>();
        Stream = new ZLinkManagedStream(socket, routingId, _runtime.Registration.Codecs, transport);
        _flow = new ZLinkMessageFlowTracer(
            _runtime.Registration.DispatchOptions,
            scope.ServiceProvider.GetService<ILogger<ZLinkStreamSessionRuntime>>(),
            _runtime);
        _liveness = new ZLinkStreamSessionLiveness(timeProvider);
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
        Handlers.BindSession(_handler);
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

    public ValueTask DisposeAsync()
    {
        lock (_disposeGate)
            return new ValueTask(_disposeTask ??= DisposeCoreAsync());
    }

    private async Task DisposeCoreAsync()
    {
        var disposeOwnsDisconnect = ClaimCloseForDisposal();
        var failures = new List<Exception>();
        await CaptureAsync(_serial.DisposeAsync).ConfigureAwait(false);
        if (disposeOwnsDisconnect)
        {
            if (await TryCloseTransportAsync().ConfigureAwait(false))
                RecordStreamClosedMetric(Volatile.Read(ref _terminalClose)!.Reason);
            else
                MarkTerminalFailed();
            await CaptureAsync(InvokeDisconnectedLifecycleAsync).ConfigureAwait(false);
        }
        if (Volatile.Read(ref _retainApplicationScope) == 0)
            await CaptureAsync(() => AwaitCleanupAsync(
                    _context.CleanupAsync(_terminalCallbackStop.Token),
                    "stream-session-context-cleanup"))
                .ConfigureAwait(false);
        Capture(() => _removeSession(Stream.SessionId));
        if (Volatile.Read(ref _retainApplicationScope) == 0)
            await CaptureAsync(() => AwaitCleanupAsync(
                    _scope.DisposeAsync(),
                    "stream-session-scope-dispose"))
                .ConfigureAwait(false);
        if (failures.Count > 0) MarkTerminalFailed();
        _completion.TrySetResult(
            failures.Count == 0 && Volatile.Read(ref _terminalSucceeded) != 0);
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
        Enqueue(cancellationToken => MarkConnectedAsync(localAddr, remoteAddr, cancellationToken));
    }

    public void EnqueuePacket(Message header, Message payload)
    {
        RecordInboundLiveness(header, payload);
        Enqueue(
            cancellationToken => DispatchPacketAsync(header, payload, cancellationToken),
            () =>
            {
                header.Dispose();
                payload.Dispose();
            });
    }

    public void CheckLiveness()
    {
        if (IsClosing) return;
        switch (_liveness.Evaluate())
        {
            case ZLinkStreamLivenessDecision.None:
                return;
            case ZLinkStreamLivenessDecision.SendHeartbeat:
                try
                {
                    ZLinkStreamControlFrames.SendHeartbeatPing(Stream);
                    _liveness.RecordHeartbeatPing();
                }
                catch (Exception error)
                {
                    TryScheduleTerminal(
                        "transport_error",
                        () => CloseForTransportErrorAsync(error));
                }
                return;
            case ZLinkStreamLivenessDecision.IdleTimeout:
                TryScheduleTerminal(
                    "idle_timeout",
                    () => CloseForLivenessTimeoutAsync(
                        ZLinkStreamSessionClosingCodec.EncodeIdleTimeout()));
                return;
            case ZLinkStreamLivenessDecision.HeartbeatTimeout:
                TryScheduleTerminal(
                    "heartbeat_timeout",
                    () => CloseForLivenessTimeoutAsync(
                        ZLinkStreamSessionClosingCodec.EncodeHeartbeatTimeout()));
                return;
            default:
                throw new InvalidOperationException("Unknown STREAM liveness decision.");
        }
    }

    public void EnqueueDisconnected(ZLinkStreamError error)
    {
        TryScheduleTerminal("transport_error", () => MarkDisconnectedAsync(error));
    }

    public async ValueTask CloseAsync()
    {
        if (!TryScheduleTerminal(
                "client_close",
                () => CompleteAfterTransportClosedAsync(notifyDisconnected: true)))
            return;

        try
        {
            await Stream.CloseAsync().ConfigureAwait(false);
            _transportClosed.TrySetResult(true);
        }
        catch
        {
            _transportClosed.TrySetResult(false);
            throw;
        }
    }

    public async ValueTask CloseByProxyAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (!TryScheduleTerminal(
                "client_close",
                () => CompleteAfterTransportClosedAsync(notifyDisconnected: false)))
            return;

        try
        {
            await Stream.CloseAsync().AsTask().WaitAsync(cancellationToken).ConfigureAwait(false);
            _transportClosed.TrySetResult(true);
        }
        catch
        {
            _transportClosed.TrySetResult(false);
            throw;
        }
    }

    internal async ValueTask<bool> CloseForDrainAsync(CancellationToken cancellationToken)
    {
        var scheduled = TryScheduleTerminal("server_drain", CloseForDrainCoreAsync);
        if (!scheduled && !IsClosing) return false;
        try
        {
            return await _completion.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            return false;
        }
        catch
        {
            return false;
        }
    }

    private async ValueTask CloseForDrainCoreAsync()
    {
        var transportClosed = false;
        try
        {
            try
            {
                var payload = ZLinkStreamSessionClosingCodec.EncodeServerDrain();
                ZLinkStreamFrameWriter.Write(
                    Stream,
                    ZLinkStreamSessionClosingCodec.CreateHeader(),
                    payload.AsMemory(),
                    "Could not submit the session-closing control packet.");
            }
            catch
            {
                MarkTerminalFailed();
            }

            await Stream.CloseAsync().ConfigureAwait(false);
            transportClosed = true;
        }
        finally
        {
            _transportClosed.TrySetResult(transportClosed);
            await CompleteSessionAsync(null, true).ConfigureAwait(false);
        }
    }

    private async ValueTask MarkConnectedAsync(
        string localAddr,
        string remoteAddr,
        CancellationToken cancellationToken)
    {
        Stream.UpdateAddresses(localAddr, remoteAddr);
        if (Interlocked.Exchange(ref _connected, 1) != 0) return;

        RecordStreamOpenedMetric();
        await InvokeConnectedLifecycleAsync(cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask DispatchPacketAsync(
        Message header,
        Message payload,
        CancellationToken cancellationToken)
    {
        using (header)
        using (payload)
        {
            if (IsClosing) return;
            await EnsureConnectedAsync(cancellationToken).ConfigureAwait(false);
            ZlinkStreamHeader decoded;
            try
            {
                decoded = ZLinkStreamProtocolDefaults.DecodeHeader(header.AsReadOnlyMemory());
                if (decoded.Kind == ZlinkStreamMessageKind.Control)
                {
                    ZLinkStreamControlFrames.Dispatch(Stream, decoded, payload.AsReadOnlyMemory());
                    return;
                }
            }
            catch (Exception protocolError)
            {
                await CloseForProtocolErrorAsync(protocolError).ConfigureAwait(false);
                return;
            }

            using var currentFlow = ZLinkFlowContext.Enter(
                decoded.FlowId,
                decoded.FlowOrigin is { } streamOrigin ? (ZLinkFlowOrigin)(byte)streamOrigin : null,
                _flow.GenerationEnabled,
                ZLinkFlowOrigin.Inbound);

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
                    cancellationToken);

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
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
            }
            catch (Exception ex)
            {
                try
                {
                    await _context.ReplyErrorAsync(decoded, ex, cancellationToken)
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
        _transportClosed.TrySetResult(true);
        await CompleteSessionAsync(error, true);
    }

    private async ValueTask CloseForProtocolErrorAsync(Exception error)
    {
        TryScheduleTerminal(
            "protocol_error",
            () => CloseForProtocolErrorCoreAsync(error));
        await ValueTask.CompletedTask;
    }

    private async ValueTask CloseForProtocolErrorCoreAsync(Exception error)
    {
        try
        {
            ZLinkStreamFrameWriter.Write(
                Stream,
                ZLinkStreamSessionClosingCodec.CreateHeader(),
                ZLinkStreamSessionClosingCodec.EncodeProtocolError().AsMemory(),
                "Could not submit the protocol-error session-closing control packet.");
        }
        catch
        {
        }
        try
        {
            await Stream.CloseAsync().ConfigureAwait(false);
            _transportClosed.TrySetResult(true);
        }
        catch
        {
            _transportClosed.TrySetResult(false);
        }
        await CompleteSessionAsync(
                new ZLinkStreamError(
                    ZLinkStreamSessionError.Internal,
                    new ZLinkStreamDiagnostic(0, error.Message)),
                notifyDisconnected: true)
            .ConfigureAwait(false);
    }

    private async ValueTask CloseForLivenessTimeoutAsync(byte[] payload)
    {
        try
        {
            ZLinkStreamFrameWriter.Write(
                Stream,
                ZLinkStreamSessionClosingCodec.CreateHeader(),
                payload.AsMemory(),
                "Could not submit the liveness session-closing control packet.");
        }
        catch
        {
        }
        try
        {
            await Stream.CloseAsync().ConfigureAwait(false);
            _transportClosed.TrySetResult(true);
        }
        catch
        {
            _transportClosed.TrySetResult(false);
        }
        await CompleteSessionAsync(null, notifyDisconnected: true).ConfigureAwait(false);
    }

    private async ValueTask CloseForTransportErrorAsync(Exception error)
    {
        try
        {
            await Stream.CloseAsync().ConfigureAwait(false);
            _transportClosed.TrySetResult(true);
        }
        catch
        {
            _transportClosed.TrySetResult(false);
        }

        await CompleteSessionAsync(
                new ZLinkStreamError(
                    ZLinkStreamSessionError.TransportError,
                    new ZLinkStreamDiagnostic(0, error.Message)),
                notifyDisconnected: true)
            .ConfigureAwait(false);
    }

    private async ValueTask CompleteAfterTransportClosedAsync(bool notifyDisconnected)
    {
        _ = await _transportClosed.Task.ConfigureAwait(false);
        await CompleteSessionAsync(null, notifyDisconnected).ConfigureAwait(false);
    }

    internal async ValueTask ForceCloseForShutdownAsync()
    {
        ClaimCloseForDisposal();
        Volatile.Write(ref _retainApplicationScope, 1);
        _terminalCallbackStop.Cancel();
        _serial.ForceStop();
        MarkTerminalFailed();
        _transportClosed.TrySetResult(false);
        _completion.TrySetResult(false);
        try
        {
            await Stream.CloseAsync().ConfigureAwait(false);
            RecordStreamClosedMetric(Volatile.Read(ref _terminalClose)!.Reason);
        }
        catch
        {
        }
    }

    internal void ConfirmNodeTransportDisposed()
    {
        RecordStreamClosedMetric(Volatile.Read(ref _terminalClose)?.Reason ?? "transport_error");
    }

    private async ValueTask CompleteSessionAsync(
        ZLinkStreamError? error,
        bool notifyDisconnected)
    {
        if (await _transportClosed.Task.ConfigureAwait(false))
            RecordStreamClosedMetric(Volatile.Read(ref _terminalClose)!.Reason);
        else
            MarkTerminalFailed();

        Exception? callbackFailure = null;
        if (Volatile.Read(ref _retainApplicationScope) == 0
            && error is { } streamError)
            try
            {
                using var flow = ZLinkFlowContext.Enter(
                    null,
                    null,
                    _flow.GenerationEnabled,
                    ZLinkFlowOrigin.Lifecycle);
                await InvokeTerminalCallbackAsync(
                        cancellationToken => _handler.OnErrorAsync(streamError, cancellationToken),
                        "stream-session-error-callback")
                    .ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                callbackFailure = exception;
            }

        if (notifyDisconnected && Volatile.Read(ref _retainApplicationScope) == 0)
            try
            {
                await InvokeDisconnectedLifecycleAsync().ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                MarkTerminalFailed();
                callbackFailure = callbackFailure is null
                    ? exception
                    : new AggregateException(callbackFailure, exception);
            }

        ZLinkUnawaitedSubmit.Observe(
            DisposeAsync(),
            $"stream-session-dispose:{Stream.SessionId}");

        if (callbackFailure is not null)
        {
            MarkTerminalFailed();
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(callbackFailure).Throw();
        }
    }

    private bool IsClosing => Volatile.Read(ref _terminalClose) is not null;

    private bool TryScheduleTerminal(string reason, Func<ValueTask> finalWork)
    {
        lock (_terminalGate)
        {
            if (_terminalClose is not null) return false;

            _terminalClose = new TerminalClose(reason);
            if (_serial.EnqueueFinal(finalWork)) return true;

            _terminalClose = null;
            return false;
        }
    }

    private bool ClaimCloseForDisposal()
    {
        lock (_terminalGate)
        {
            if (_terminalClose is not null) return false;
            _terminalClose = new TerminalClose("transport_error");
            return true;
        }
    }

    private void RecordInboundLiveness(Message header, Message payload)
    {
        try
        {
            var decoded = ZLinkStreamProtocolDefaults.DecodeHeader(header.AsReadOnlyMemory());
            if (decoded.Kind != ZlinkStreamMessageKind.Control)
            {
                _liveness.RecordApplicationInbound();
                return;
            }

            if (payload.AsReadOnlyMemory().Length == 0
                && ZLinkStreamControlFrames.IsHeartbeatPong(decoded))
                _liveness.RecordHeartbeatPong();
        }
        catch
        {
        }
    }

    private void Enqueue(
        Func<CancellationToken, ValueTask> work,
        Action? onRejected = null)
    {
        if (!_serial.Enqueue(work)) onRejected?.Invoke();
    }

    private async ValueTask EnsureConnectedAsync(CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(Stream.LocalAddr)
            || string.IsNullOrWhiteSpace(Stream.RemoteAddr))
            return;

        if (Interlocked.CompareExchange(ref _connected, 1, 0) != 0) return;

        RecordStreamOpenedMetric();
        await InvokeConnectedLifecycleAsync(cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask InvokeConnectedLifecycleAsync(CancellationToken cancellationToken)
    {
        using var flow = ZLinkFlowContext.Enter(
            null,
            null,
            _flow.GenerationEnabled,
            ZLinkFlowOrigin.Lifecycle);
        await _handler.OnConnectedAsync(cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask InvokeDisconnectedLifecycleAsync()
    {
        using var flow = ZLinkFlowContext.Enter(
            null,
            null,
            _flow.GenerationEnabled,
            ZLinkFlowOrigin.Lifecycle);
        await InvokeTerminalCallbackAsync(
                _handler.OnDisconnectedAsync,
                "stream-session-disconnected-callback")
            .ConfigureAwait(false);
    }

    private async ValueTask<bool> TryCloseTransportAsync()
    {
        try
        {
            await Stream.CloseAsync().ConfigureAwait(false);
            _transportClosed.TrySetResult(true);
            return true;
        }
        catch
        {
            _transportClosed.TrySetResult(false);
            return false;
        }
    }

    private async ValueTask InvokeTerminalCallbackAsync(
        Func<CancellationToken, ValueTask> callback,
        string operationName)
    {
        var operation = callback(_terminalCallbackStop.Token);
        if (operation.IsCompletedSuccessfully) return;

        var task = operation.AsTask();
        try
        {
            await task.WaitAsync(_terminalCallbackStop.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (_terminalCallbackStop.IsCancellationRequested)
        {
            ZLinkUnawaitedSubmit.Observe(new ValueTask(task), operationName);
            throw;
        }
    }

    private async ValueTask AwaitCleanupAsync(ValueTask cleanup, string operationName)
    {
        if (cleanup.IsCompletedSuccessfully) return;

        var task = cleanup.AsTask();
        try
        {
            await task.WaitAsync(_terminalCallbackStop.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (_terminalCallbackStop.IsCancellationRequested)
        {
            ZLinkUnawaitedSubmit.Observe(new ValueTask(task), operationName);
            throw;
        }
    }

    private void MarkTerminalFailed()
    {
        Volatile.Write(ref _terminalSucceeded, 0);
    }

    private void RecordStreamOpenedMetric()
    {
        if (Interlocked.Exchange(ref _streamMetricActive, 1) == 0)
            ZLinkRuntimeMetrics.RecordStreamOpened();
    }

    private void RecordStreamClosedMetric(string reason)
    {
        if (Interlocked.Exchange(ref _streamMetricActive, 0) != 0)
            ZLinkRuntimeMetrics.RecordStreamClosed(reason);
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

    private sealed record TerminalClose(string Reason);
}
