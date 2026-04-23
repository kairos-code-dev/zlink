using Microsoft.Extensions.DependencyInjection;
using System.Threading;

namespace Zlink.Framework;

internal sealed class ZLinkStreamNodeRuntime : IAsyncDisposable
{
    private readonly IServiceProvider _services;
    private readonly Type? _packetSessionType;
    private readonly Type? _rawSessionType;
    private readonly Dictionary<string, ZLinkStreamSessionRuntime> _sessions = [];
    private readonly Queue<(string LocalAddr, string RemoteAddr)> _pendingConnectionMetadata = [];
    private readonly object _gate = new();
    private readonly CancellationTokenSource _stopSource = new();
    private Task? _monitorLoop;

    public ZLinkStreamNodeRuntime(
        string nodeName,
        IServiceProvider services,
        global::Zlink.StreamSocket socket,
        Type? packetSessionType,
        Type? rawSessionType)
    {
        NodeName = nodeName;
        _services = services;
        Socket = socket;
        _packetSessionType = packetSessionType;
        _rawSessionType = rawSessionType;
        Monitor = socket.MonitorOpen(global::Zlink.SocketEvent.All);
    }

    public string NodeName { get; }

    public global::Zlink.StreamSocket Socket { get; }

    public global::Zlink.SocketMonitor Monitor { get; }

    public void Start()
    {
        RegisterWithoutSynchronizationContext(() =>
        {
            if (_packetSessionType is not null)
            {
                Socket.OnFramedPacket(OnFramedPacket);
            }
            else
            {
                Socket.OnPacket(OnRawPacket);
            }
            return 0;
        });

        _monitorLoop = Task.Run(
            () => RunMonitorLoopAsync(_stopSource.Token),
            CancellationToken.None);
    }

    public async ValueTask DisposeAsync()
    {
        _stopSource.Cancel();
        await Monitor.DisposeAsync();
        await Socket.DisposeAsync();

        if (_monitorLoop is not null)
        {
            try
            {
                await _monitorLoop;
            }
            catch (OperationCanceledException)
            {
            }
            catch (ObjectDisposedException)
            {
            }
        }

        foreach (var session in _sessions.Values.ToArray())
        {
            await session.DisposeAsync();
        }

        _stopSource.Dispose();
    }

    private int OnRawPacket(string routingIdText, global::Zlink.Message payload)
    {
        var routingId = ParsePublicRoutingId(routingIdText);
        var session = GetOrCreateSession(routingId);
        ApplyPendingConnectionMetadata(session);
        session.DispatchRawAsync(payload).AsTask().GetAwaiter().GetResult();
        return 0;
    }

    private void OnFramedPacket(
        string routingIdText,
        global::Zlink.Message header,
        global::Zlink.Message body)
    {
        var routingId = ParsePublicRoutingId(routingIdText);
        var session = GetOrCreateSession(routingId);
        ApplyPendingConnectionMetadata(session);
        session.DispatchPacketAsync(header, body).AsTask().GetAwaiter().GetResult();
    }

    private void OnMonitorEvent(global::Zlink.MonitorEvent monitorEvent)
    {
        switch (monitorEvent.Event)
        {
            case global::Zlink.MonitorEventType.ConnectionReady:
            case global::Zlink.MonitorEventType.Accepted:
                if (monitorEvent.RoutingId is global::Zlink.RoutingId readyRoutingId)
                {
                    var session = GetOrCreateSession(readyRoutingId);
                    session.MarkConnectedAsync(monitorEvent.LocalAddr, monitorEvent.RemoteAddr)
                        .AsTask()
                        .GetAwaiter()
                        .GetResult();
                }
                else
                {
                    lock (_gate)
                    {
                        _pendingConnectionMetadata.Enqueue((monitorEvent.LocalAddr, monitorEvent.RemoteAddr));
                    }
                }
                break;
            case global::Zlink.MonitorEventType.Disconnected:
                if (TryResolveSessionForMonitorEvent(monitorEvent.RoutingId, out var disconnectedSession))
                {
                    disconnectedSession.MarkDisconnectedAsync(
                            new ZLinkStreamError(
                                ZLinkStreamSessionError.TransportError,
                                new ZLinkStreamDiagnostic((int)monitorEvent.Value, monitorEvent.Event.ToString())))
                        .AsTask()
                        .GetAwaiter()
                        .GetResult();

                    if (monitorEvent.RoutingId is global::Zlink.RoutingId disconnectedRoutingId)
                    {
                        RemoveSession(disconnectedRoutingId);
                    }
                    else
                    {
                        RemoveSession(disconnectedSession.Stream.SessionId);
                    }
                }
                break;
        }
    }

    private async Task RunMonitorLoopAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                var monitorEvent = Monitor.Recv();
                OnMonitorEvent(monitorEvent);
            }
            catch (Exception) when (cancellationToken.IsCancellationRequested)
            {
                return;
            }
            catch (ObjectDisposedException)
            {
                return;
            }
            catch (global::Zlink.ZlinkException ex)
                when (cancellationToken.IsCancellationRequested
                      || ex.InternalErrno is (int)global::Zlink.ErrorCode.EFault
                      or (int)global::Zlink.ErrorCode.EBadf)
            {
                return;
            }

            await Task.Yield();
        }
    }

    private ZLinkStreamSessionRuntime GetOrCreateSession(global::Zlink.RoutingId routingId)
    {
        var sessionId = routingId.ToHex();
        lock (_gate)
        {
            if (_sessions.TryGetValue(sessionId, out var existing))
            {
                return existing;
            }

            var created = new ZLinkStreamSessionRuntime(
                _services.CreateAsyncScope(),
                Socket,
                routingId,
                _packetSessionType,
                _rawSessionType);
            _sessions.Add(sessionId, created);
            return created;
        }
    }

    private void RemoveSession(global::Zlink.RoutingId routingId)
    {
        RemoveSession(routingId.ToHex());
    }

    private void RemoveSession(string sessionId)
    {
        lock (_gate)
        {
            _sessions.Remove(sessionId);
        }
    }

    private void ApplyPendingConnectionMetadata(ZLinkStreamSessionRuntime session)
    {
        (string LocalAddr, string RemoteAddr)? metadata = null;

        lock (_gate)
        {
            if (session.Stream.LocalAddr is null
                && session.Stream.RemoteAddr is null
                && _pendingConnectionMetadata.Count > 0)
            {
                metadata = _pendingConnectionMetadata.Dequeue();
            }
        }

        if (metadata is { } value)
        {
            session.MarkConnectedAsync(value.LocalAddr, value.RemoteAddr)
                .AsTask()
                .GetAwaiter()
                .GetResult();
        }
    }

    private bool TryResolveSessionForMonitorEvent(
        global::Zlink.RoutingId? routingId,
        out ZLinkStreamSessionRuntime session)
    {
        lock (_gate)
        {
            if (routingId is global::Zlink.RoutingId streamRoutingId)
            {
                var sessionId = streamRoutingId.ToHex();
                if (_sessions.TryGetValue(sessionId, out var existing))
                {
                    session = existing;
                    return true;
                }
            }

            if (_sessions.Count == 1)
            {
                session = _sessions.Values.First();
                return true;
            }
        }

        session = null!;
        return false;
    }

    private static global::Zlink.RoutingId ParsePublicRoutingId(string routingIdText)
    {
        if (routingIdText.StartsWith("hex:", StringComparison.OrdinalIgnoreCase))
        {
            return global::Zlink.RoutingId.FromBytes(
                Convert.FromHexString(routingIdText["hex:".Length..]));
        }

        return global::Zlink.RoutingId.FromBytes(
            System.Text.Encoding.UTF8.GetBytes(routingIdText));
    }

    private static T RegisterWithoutSynchronizationContext<T>(Func<T> action)
    {
        var previous = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(null);
        try
        {
            return action();
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(previous);
        }
    }
}

internal sealed class ZLinkStreamSessionRuntime : IAsyncDisposable
{
    private readonly AsyncServiceScope _scope;
    private readonly global::Zlink.StreamSocket _socket;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly object _handler;
    private readonly Type? _packetSessionType;
    private readonly Type? _rawSessionType;
    private int _connected;

    public ZLinkStreamSessionRuntime(
        AsyncServiceScope scope,
        global::Zlink.StreamSocket socket,
        global::Zlink.RoutingId routingId,
        Type? packetSessionType,
        Type? rawSessionType)
    {
        _scope = scope;
        _socket = socket;
        _packetSessionType = packetSessionType;
        _rawSessionType = rawSessionType;
        Stream = new ZLinkManagedStream(socket, routingId);
        _handler = scope.ServiceProvider.GetRequiredService(packetSessionType ?? rawSessionType!);
    }

    public ZLinkManagedStream Stream { get; }

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

            if (_packetSessionType is not null)
            {
                await ((IZLinkPacketStreamSession)_handler).OnConnectedAsync(Stream, CancellationToken.None);
            }
            else
            {
                await ((IZLinkRawStreamSession)_handler).OnConnectedAsync(Stream, CancellationToken.None);
            }
        }
        finally
        {
            _gate.Release();
        }
    }

    public async ValueTask DispatchRawAsync(global::Zlink.Message payload)
    {
        using (payload)
        {
            await _gate.WaitAsync();
            try
            {
                await EnsureConnectedAsync();
                await ((IZLinkRawStreamSession)_handler).OnRawAsync(Stream, payload.Move(), CancellationToken.None);
            }
            finally
            {
                _gate.Release();
            }
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
                await ((IZLinkPacketStreamSession)_handler).OnPacketAsync(
                    Stream,
                    header.Move(),
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
            if (_packetSessionType is not null)
            {
                await ((IZLinkPacketStreamSession)_handler).OnErrorAsync(Stream, error, CancellationToken.None);
                await ((IZLinkPacketStreamSession)_handler).OnDisconnectedAsync(Stream, CancellationToken.None);
            }
            else
            {
                await ((IZLinkRawStreamSession)_handler).OnErrorAsync(Stream, error, CancellationToken.None);
                await ((IZLinkRawStreamSession)_handler).OnDisconnectedAsync(Stream, CancellationToken.None);
            }
        }
        finally
        {
            _gate.Release();
        }
    }

    public async ValueTask DisposeAsync()
    {
        await _scope.DisposeAsync();
        _gate.Dispose();
    }

    private ValueTask EnsureConnectedAsync()
    {
        if (Interlocked.CompareExchange(ref _connected, 1, 1) != 0)
        {
            return ValueTask.CompletedTask;
        }

        Interlocked.Exchange(ref _connected, 1);

        return _packetSessionType is not null
            ? ((IZLinkPacketStreamSession)_handler).OnConnectedAsync(Stream, CancellationToken.None)
            : ((IZLinkRawStreamSession)_handler).OnConnectedAsync(Stream, CancellationToken.None);
    }
}

internal sealed class ZLinkManagedStream : IZLinkStream
{
    private readonly global::Zlink.StreamSocket _socket;
    private readonly global::Zlink.RoutingId _routingId;

    public ZLinkManagedStream(
        global::Zlink.StreamSocket socket,
        global::Zlink.RoutingId routingId)
    {
        _socket = socket;
        _routingId = routingId;
        SessionId = _routingId.ToHex();
    }

    public string SessionId { get; }

    public global::Zlink.RoutingId? RoutingId => _routingId;

    public string? LocalAddr { get; private set; }

    public string? RemoteAddr { get; private set; }

    public bool Write(
        global::Zlink.Message payload,
        global::Zlink.SendFlags flags = global::Zlink.SendFlags.None)
    {
        return _socket.Send(_routingId, payload, flags);
    }

    public bool Write(
        global::Zlink.Message header,
        global::Zlink.Message body,
        global::Zlink.SendFlags flags = global::Zlink.SendFlags.None)
    {
        return _socket.Send(_routingId, [header, body], flags);
    }

    public void UpdateAddresses(string? localAddr, string? remoteAddr)
    {
        LocalAddr = localAddr;
        RemoteAddr = remoteAddr;
    }
}
