using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkStreamSessionRuntime : IAsyncDisposable
{
    private readonly AsyncServiceScope _scope;
    private readonly IZLinkBackendStreamSocket _socket;
    private readonly Action<string> _removeSession;
    private readonly ZLinkStreamSessionSerialExecutor _serial = new();
    private readonly IZLinkSession _handler;
    private readonly ZLinkSessionContext _context;
    private readonly IZlinkStreamHeaderCodec _headerCodec = ZlinkStreamDefaultCodecs.Header();
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
        _handler = (IZLinkSession)scope.ServiceProvider.GetRequiredService(headerSessionType!);
        _context = new ZLinkSessionContext(
            scope.ServiceProvider.GetRequiredService<ZLinkFrameworkRuntime>(),
            scope.ServiceProvider.GetRequiredService<IZLinkClient>(),
            Stream,
            CloseAsync);
        _handler.Context = _context;
    }

    public ZLinkManagedStream Stream { get; }

    public void EnqueueConnected(string localAddr, string remoteAddr)
    {
        Enqueue(() => MarkConnectedAsync(localAddr, remoteAddr));
    }

    public void EnqueuePacket(Message header, Message body)
    {
        Enqueue(
            () => DispatchPacketAsync(header, body),
            () =>
            {
                header.Dispose();
                body.Dispose();
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

    public async ValueTask CloseAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        await Stream.CloseAsync(cancellationToken);

        if (Interlocked.Exchange(ref _disconnected, 1) != 0)
        {
            return;
        }

        Enqueue(MarkClosedAsync);
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
        Message body)
    {
        using (header)
        using (body)
        {
            await EnsureConnectedAsync();
            var decoded = _headerCodec.Decode(header.AsReadOnlyMemory());
            if (_context.TryCompleteResponse(decoded, body))
            {
                return;
            }

            _context.EnterDispatch(decoded);
            try
            {
                using var packet = new ZLinkSessionPacket(decoded, body.Move());
                await _handler.OnDispatchAsync(
                    packet,
                    CancellationToken.None);
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
        await _handler.OnErrorAsync(error, CancellationToken.None);
        await _handler.OnDisconnectedAsync(CancellationToken.None);
        await _context.CleanupAsync(CancellationToken.None);
        _removeSession(Stream.SessionId);
    }

    private async ValueTask MarkClosedAsync()
    {
        await _handler.OnDisconnectedAsync(CancellationToken.None);
        await _context.CleanupAsync(CancellationToken.None);
        _removeSession(Stream.SessionId);
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

            await _context.CleanupAsync(CancellationToken.None);
            _removeSession(Stream.SessionId);
        }
        finally
        {
            await _scope.DisposeAsync();
        }
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
