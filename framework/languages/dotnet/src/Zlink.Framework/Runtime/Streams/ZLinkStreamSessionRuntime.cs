using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkStreamSessionRuntime : IAsyncDisposable
{
    private const string HeartbeatPingName = "$zlink.heartbeat.ping";
    private const string HeartbeatPongName = "$zlink.heartbeat.pong";

    private readonly AsyncServiceScope _scope;
    private readonly IZLinkBackendStreamSocket _socket;
    private readonly Action<string> _removeSession;
    private readonly ZLinkStreamSessionSerialExecutor _serial = new();
    private readonly IZLinkSession _handler;
    private readonly ZLinkSessionContext _context;
    private readonly IZlinkStreamHeaderCodec _headerCodec;
    private int _connected;
    private int _disconnected;
    private int _disposed;

    public ZLinkStreamSessionRuntime(
        AsyncServiceScope scope,
        IZLinkBackendStreamSocket socket,
        RoutingId routingId,
        Type? headerSessionType,
        IZlinkStreamHeaderCodec headerCodec,
        Action<string> removeSession)
    {
        _scope = scope;
        _socket = socket;
        _removeSession = removeSession;
        _headerCodec = headerCodec;
        Stream = new ZLinkManagedStream(socket, routingId, headerCodec);
        _handler = (IZLinkSession)scope.ServiceProvider.GetRequiredService(headerSessionType!);
        _context = new ZLinkSessionContext(
            scope.ServiceProvider.GetRequiredService<ZLinkFrameworkRuntime>(),
            scope.ServiceProvider.GetRequiredService<IZLinkClient>(),
            Stream,
            headerCodec,
            CloseAsync);
        _handler.Context = _context;
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
        Message payload)
    {
        using (header)
        using (payload)
        {
            await EnsureConnectedAsync();
            var decoded = _headerCodec.Decode(header.AsReadOnlyMemory());
            if (decoded.Kind == ZlinkStreamMessageKind.Control)
            {
                DispatchControlFrame(decoded, payload.AsReadOnlyMemory());
                return;
            }

            if (_context.TryCompleteResponse(decoded, payload))
            {
                return;
            }

            _context.EnterDispatch(decoded);
            try
            {
                using var dispatchPayload = payload.Move();
                await _handler.OnDispatchAsync(
                    decoded,
                    dispatchPayload,
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

    private void DispatchControlFrame(
        ZlinkStreamHeader header,
        ReadOnlyMemory<byte> payload)
    {
        if (payload.Length != 0)
        {
            throw new InvalidOperationException("Stream control packet payload must be empty.");
        }

        if (header.Name == HeartbeatPingName)
        {
            var pong = new ZlinkStreamHeader(
                ZlinkStreamMessageKind.Control,
                ZlinkStreamCodec.Raw,
                ZlinkStreamHeaderFlags.None,
                null,
                HeartbeatPongName,
                ZlinkStreamMetadata.Empty);
            ZLinkStreamFrameWriter.Write(
                Stream,
                pong,
                ReadOnlySpan<byte>.Empty,
                "Stream heartbeat pong send failed.");
            return;
        }

        if (header.Name == HeartbeatPongName)
        {
            return;
        }

        throw new InvalidOperationException("Unknown stream control packet.");
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
