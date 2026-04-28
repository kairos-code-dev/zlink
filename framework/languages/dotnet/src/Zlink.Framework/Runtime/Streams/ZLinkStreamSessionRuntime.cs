using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend;
using System.Threading;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkStreamSessionRuntime : IAsyncDisposable
{
    private readonly AsyncServiceScope _scope;
    private readonly IZLinkBackendStreamSocket _socket;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly object _dispatchSync = new();
    private readonly object _handler;
    private readonly ZlinkStreamHeaderCodec _headerCodec = new();
    private int _connected;
    private Task _dispatchTail = Task.CompletedTask;

    public ZLinkStreamSessionRuntime(
        AsyncServiceScope scope,
        IZLinkBackendStreamSocket socket,
        global::Zlink.RoutingId routingId,
        Type? headerSessionType)
    {
        _scope = scope;
        _socket = socket;
        Stream = new ZLinkManagedStream(socket, routingId);
        _handler = scope.ServiceProvider.GetRequiredService(headerSessionType!);
    }

    public ZLinkManagedStream Stream { get; }

    public void EnqueueConnected(string localAddr, string remoteAddr)
    {
        Enqueue(() => MarkConnectedAsync(localAddr, remoteAddr).AsTask());
    }

    public void EnqueuePacket(global::Zlink.Message header, global::Zlink.Message body)
    {
        Enqueue(() => DispatchPacketAsync(header, body).AsTask());
    }

    public void EnqueueDisconnected(ZLinkStreamError error)
    {
        Enqueue(() => MarkDisconnectedAsync(error).AsTask());
    }

    public async ValueTask MarkConnectedAsync(string localAddr, string remoteAddr)
    {
        await _gate.WaitAsync();
        try
        {
            Stream.UpdateAddresses(localAddr, remoteAddr);
            if (Interlocked.Exchange(ref _connected, 1) != 0)
            {
                return;
            }

            await ((IZLinkStreamHeaderSession)_handler).OnConnectedAsync(Stream, CancellationToken.None);
        }
        finally
        {
            _gate.Release();
        }
    }

    public async ValueTask DispatchPacketAsync(
        global::Zlink.Message header,
        global::Zlink.Message body)
    {
        using (header)
        using (body)
        {
            await _gate.WaitAsync();
            try
            {
                await EnsureConnectedAsync();
                var decoded = _headerCodec.Decode(header.AsReadOnlyMemory());
                await ((IZLinkStreamHeaderSession)_handler).OnDispatchAsync(
                    Stream,
                    decoded,
                    body.Move(),
                    CancellationToken.None);
            }
            finally
            {
                _gate.Release();
            }
        }
    }

    public async ValueTask MarkDisconnectedAsync(ZLinkStreamError error)
    {
        await _gate.WaitAsync();
        try
        {
            await ((IZLinkStreamHeaderSession)_handler).OnErrorAsync(Stream, error, CancellationToken.None);
            await ((IZLinkStreamHeaderSession)_handler).OnDisconnectedAsync(Stream, CancellationToken.None);
        }
        finally
        {
            _gate.Release();
        }
    }

    public async ValueTask DisposeAsync()
    {
        Task pending;
        lock (_dispatchSync)
        {
            pending = _dispatchTail;
        }

        try
        {
            await pending;
        }
        catch (ObjectDisposedException)
        {
        }

        await _scope.DisposeAsync();
        _gate.Dispose();
    }

    private void Enqueue(Func<Task> work)
    {
        lock (_dispatchSync)
        {
            _dispatchTail = _dispatchTail.ContinueWith(
                static async (_, state) =>
                {
                    await ((Func<Task>)state!).Invoke().ConfigureAwait(false);
                },
                work,
                CancellationToken.None,
                TaskContinuationOptions.None,
                TaskScheduler.Default).Unwrap();
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

        return ((IZLinkStreamHeaderSession)_handler).OnConnectedAsync(Stream, CancellationToken.None);
    }
}
