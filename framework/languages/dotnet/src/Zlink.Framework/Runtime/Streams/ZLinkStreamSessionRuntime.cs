using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend.Contracts;
using System.Threading.Channels;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkStreamSessionRuntime : IAsyncDisposable
{
    private readonly AsyncServiceScope _scope;
    private readonly IZLinkBackendStreamSocket _socket;
    private readonly Action<string> _removeSession;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly Channel<Func<ValueTask>> _dispatchQueue =
        Channel.CreateUnbounded<Func<ValueTask>>(new UnboundedChannelOptions
        {
            SingleReader = true,
            SingleWriter = false
        });
    private readonly IZLinkSession _handler;
    private readonly ZLinkSessionContext _context;
    private readonly ZlinkStreamHeaderCodec _headerCodec = new();
    private readonly Task _dispatchPump;
    private int _connected;
    private int _disconnected;

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
        _dispatchPump = Task.Run(RunDispatchQueueAsync);
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
        await _gate.WaitAsync();
        try
        {
            Stream.UpdateAddresses(localAddr, remoteAddr);
            if (Interlocked.Exchange(ref _connected, 1) != 0)
            {
                return;
            }

            await _handler.OnConnectedAsync(CancellationToken.None);
        }
        finally
        {
            _gate.Release();
        }
    }

    private async ValueTask DispatchPacketAsync(
        Message header,
        Message body)
    {
        using (header)
        using (body)
        {
            await _gate.WaitAsync();
            try
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
                    await _handler.OnDispatchAsync(
                        decoded,
                        body.Move(),
                        CancellationToken.None);
                }
                finally
                {
                    _context.ExitDispatch();
                }
            }
            finally
            {
                _gate.Release();
            }
        }
    }

    private async ValueTask MarkDisconnectedAsync(ZLinkStreamError error)
    {
        await _gate.WaitAsync();
        try
        {
            await _handler.OnErrorAsync(error, CancellationToken.None);
            await _handler.OnDisconnectedAsync(CancellationToken.None);
        }
        finally
        {
            _gate.Release();
        }
    }

    private async ValueTask MarkClosedAsync()
    {
        await _gate.WaitAsync();
        try
        {
            await _handler.OnDisconnectedAsync(CancellationToken.None);
            _removeSession(Stream.SessionId);
        }
        finally
        {
            _gate.Release();
        }
    }

    public async ValueTask DisposeAsync()
    {
        _dispatchQueue.Writer.TryComplete();

        try
        {
            await _dispatchPump;
        }
        catch (ObjectDisposedException)
        {
        }

        await _scope.DisposeAsync();
        _gate.Dispose();
    }

    private void Enqueue(Func<ValueTask> work, Action? onRejected = null)
    {
        if (!_dispatchQueue.Writer.TryWrite(work))
        {
            onRejected?.Invoke();
        }
    }

    private async Task RunDispatchQueueAsync()
    {
        await foreach (var work in _dispatchQueue.Reader.ReadAllAsync().ConfigureAwait(false))
        {
            await work().ConfigureAwait(false);
        }
    }

    private ValueTask EnsureConnectedAsync()
    {
        if (Interlocked.CompareExchange(ref _connected, 1, 1) != 0)
        {
            return ValueTask.CompletedTask;
        }

        if (string.IsNullOrWhiteSpace(Stream.LocalAddr)
            || string.IsNullOrWhiteSpace(Stream.RemoteAddr))
        {
            return ValueTask.CompletedTask;
        }

        Interlocked.Exchange(ref _connected, 1);

        return _handler.OnConnectedAsync(CancellationToken.None);
    }
}
