using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Diagnostics;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkStreamSessionRuntime : IAsyncDisposable
{
    private readonly AsyncServiceScope _scope;
    private readonly IZLinkBackendStreamSocket _socket;
    private readonly Action<string> _removeSession;
    private readonly ZLinkStreamSessionSerialExecutor _serial = new();
    private readonly IZLinkSession _handler;
    private readonly ZLinkSessionContext _context;
    private readonly ZLinkMessageFlowTracer _flow;
    private int _connected;
    private int _disconnected;
    private int _disposed;

    public ZLinkStreamSessionRuntime(
        AsyncServiceScope scope,
        IZLinkBackendStreamSocket socket,
        RoutingId routingId,
        Type? headerSessionType,
        Action<string> removeSession)
    {
        _scope = scope;
        _socket = socket;
        _removeSession = removeSession;
        Stream = new ZLinkManagedStream(socket, routingId);
        var runtime = scope.ServiceProvider.GetRequiredService<ZLinkFrameworkRuntime>();
        _flow = new ZLinkMessageFlowTracer(
            runtime.Registration.DispatchOptions,
            scope.ServiceProvider,
            scope.ServiceProvider.GetService<ILogger<ZLinkStreamSessionRuntime>>());
        _context = new ZLinkSessionContext(
            runtime,
            Stream,
            CloseAsync,
            CloseByProxyAsync);
        _handler = (IZLinkSession)ActivatorUtilities.CreateInstance(
            scope.ServiceProvider,
            headerSessionType!,
            _context);
        if (!ReferenceEquals(_handler.Context, _context))
        {
            throw new InvalidOperationException(
                $"Session '{_handler.GetType().FullName}' must expose the context provided by the runtime.");
        }
    }

    public ZLinkManagedStream Stream { get; }

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
        if (Interlocked.Exchange(ref _disconnected, 1) != 0)
        {
            return;
        }

        Enqueue(() => MarkDisconnectedAsync(error));
    }

    public async ValueTask CloseAsync()
    {
        await Stream.CloseAsync();

        if (Interlocked.Exchange(ref _disconnected, 1) != 0)
        {
            return;
        }

        Enqueue(MarkClosedAsync);
    }

    public async ValueTask CloseByProxyAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (Interlocked.Exchange(ref _disconnected, 1) != 0)
        {
            return;
        }

        await Stream.CloseAsync();
        Enqueue(MarkProxyClosedAsync);
    }

    private async ValueTask MarkConnectedAsync(string localAddr, string remoteAddr)
    {
        Stream.UpdateAddresses(localAddr, remoteAddr);
        if (Interlocked.Exchange(ref _connected, 1) != 0)
        {
            return;
        }

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
                return;
            }

            if (_flow.Enabled(ZLinkMessageFlowPhase.Received))
            {
                _flow.Trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowPhase.Received,
                    ZLinkDispatchErrorSurface.StreamSession,
                    decoded.RequestSeq.HasValue
                        ? ZLinkDispatchMessageKind.Request
                        : ZLinkDispatchMessageKind.Send,
                    PacketName: decoded.Name,
                    CorrelationId: decoded.RequestSeq?.ToString()));
            }

            _context.EnterDispatch(decoded);
            try
            {
                await _handler.OnDispatchAsync(
                    decoded,
                    payload,
                    CancellationToken.None);

                if (_flow.Enabled(ZLinkMessageFlowPhase.Dispatched))
                {
                    _flow.Trace(new ZLinkMessageFlowEvent(
                        ZLinkMessageFlowPhase.Dispatched,
                        ZLinkDispatchErrorSurface.StreamSession,
                        decoded.RequestSeq.HasValue
                            ? ZLinkDispatchMessageKind.Request
                            : ZLinkDispatchMessageKind.Send,
                        PacketName: decoded.Name,
                        CorrelationId: decoded.RequestSeq?.ToString()));
                }
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
        await CompleteSessionAsync(error, notifyDisconnected: true);
    }

    private async ValueTask MarkClosedAsync()
    {
        await CompleteSessionAsync(error: null, notifyDisconnected: true);
    }

    private async ValueTask MarkProxyClosedAsync()
    {
        await CompleteSessionAsync(error: null, notifyDisconnected: false);
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0)
        {
            return;
        }

        await _serial.DisposeAsync();
        try
        {
            if (Interlocked.Exchange(ref _disconnected, 1) == 0)
            {
                await _handler.OnDisconnectedAsync(CancellationToken.None);
            }

            await CleanupSessionAsync();
        }
        finally
        {
            await _scope.DisposeAsync();
        }
    }

    private async ValueTask CompleteSessionAsync(
        ZLinkStreamError? error,
        bool notifyDisconnected)
    {
        if (error is { } streamError)
        {
            await _handler.OnErrorAsync(streamError, CancellationToken.None);
        }

        if (notifyDisconnected)
        {
            await _handler.OnDisconnectedAsync(CancellationToken.None);
        }

        await CleanupSessionAsync();
    }

    private async ValueTask CleanupSessionAsync()
    {
        await _context.CleanupAsync(CancellationToken.None);
        _removeSession(Stream.SessionId);
    }

    private void Enqueue(Func<ValueTask> work, Action? onRejected = null)
    {
        if (!_serial.Enqueue(work))
        {
            onRejected?.Invoke();
        }
    }

    private ValueTask EnsureConnectedAsync()
    {
        if (string.IsNullOrWhiteSpace(Stream.LocalAddr)
            || string.IsNullOrWhiteSpace(Stream.RemoteAddr))
        {
            return ValueTask.CompletedTask;
        }

        if (Interlocked.CompareExchange(ref _connected, 1, 0) != 0)
        {
            return ValueTask.CompletedTask;
        }

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
