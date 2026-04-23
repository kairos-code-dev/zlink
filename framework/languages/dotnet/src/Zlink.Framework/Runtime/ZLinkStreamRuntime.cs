using Microsoft.Extensions.DependencyInjection;
using System.Buffers.Binary;

namespace Zlink.Framework;

internal sealed class ZLinkStreamNodeRuntime : IAsyncDisposable
{
    private readonly IServiceProvider _services;
    private readonly Type? _packetSessionType;
    private readonly Type? _rawSessionType;
    private readonly Dictionary<uint, ZLinkStreamSessionRuntime> _sessions = [];
    private readonly object _gate = new();

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
        Socket.OnPacket(
            new global::Zlink.StreamUInt32PacketHandler(OnRawPacket));

        Monitor.OnEvent(OnMonitorEvent);
    }

    public async ValueTask DisposeAsync()
    {
        foreach (var session in _sessions.Values.ToArray())
        {
            await session.DisposeAsync();
        }

        await Monitor.DisposeAsync();
        await Socket.DisposeAsync();
    }

    private int OnRawPacket(uint routingId, global::Zlink.Message payload)
    {
        var session = GetOrCreateSession(routingId);
        session.DispatchRawAsync(payload).AsTask().GetAwaiter().GetResult();
        return 0;
    }

    private void OnMonitorEvent(global::Zlink.MonitorEvent monitorEvent)
    {
        if (monitorEvent.RoutingId is null
            || !global::Zlink.RoutingIdCodec.TryToUInt32(monitorEvent.RoutingId.Value.ToBytes(), out var streamRoutingId))
        {
            return;
        }

        var session = GetOrCreateSession(streamRoutingId);
        switch (monitorEvent.Event)
        {
            case global::Zlink.MonitorEventType.ConnectionReady:
            case global::Zlink.MonitorEventType.Accepted:
                session.MarkConnectedAsync(monitorEvent.LocalAddr, monitorEvent.RemoteAddr)
                    .AsTask()
                    .GetAwaiter()
                    .GetResult();
                break;
            case global::Zlink.MonitorEventType.Disconnected:
                session.MarkDisconnectedAsync(
                        new ZLinkStreamError(
                            ZLinkStreamSessionError.TransportError,
                            new ZLinkStreamDiagnostic((int)monitorEvent.Value, monitorEvent.Event.ToString())))
                    .AsTask()
                    .GetAwaiter()
                    .GetResult();
                RemoveSession(streamRoutingId);
                break;
        }
    }

    private ZLinkStreamSessionRuntime GetOrCreateSession(uint routingId)
    {
        lock (_gate)
        {
            if (_sessions.TryGetValue(routingId, out var existing))
            {
                return existing;
            }

            var created = new ZLinkStreamSessionRuntime(
                _services.CreateAsyncScope(),
                Socket,
                routingId,
                _packetSessionType,
                _rawSessionType);
            _sessions.Add(routingId, created);
            return created;
        }
    }

    private void RemoveSession(uint routingId)
    {
        lock (_gate)
        {
            _sessions.Remove(routingId);
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
    private readonly ZLinkLen32BeAccumulator _packetAccumulator = new();
    private int _connected;

    public ZLinkStreamSessionRuntime(
        AsyncServiceScope scope,
        global::Zlink.StreamSocket socket,
        uint routingId,
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
                if (Interlocked.CompareExchange(ref _connected, 1, 1) == 0)
                {
                    if (_rawSessionType is not null)
                    {
                        await ((IZLinkRawStreamSession)_handler).OnConnectedAsync(Stream, CancellationToken.None);
                    }

                    Interlocked.Exchange(ref _connected, 1);
                }

                if (_rawSessionType is not null)
                {
                    await ((IZLinkRawStreamSession)_handler).OnRawAsync(Stream, payload.Move(), CancellationToken.None);
                    return;
                }

                var frames = _packetAccumulator.AppendAndDrain(payload.AsReadOnlySpan());
                foreach (var frame in frames)
                {
                    _packetAccumulator.PendingFrames.Add(frame);
                }

                while (_packetAccumulator.PendingFrames.Count >= 2)
                {
                    using var header = _packetAccumulator.PendingFrames[0];
                    using var body = _packetAccumulator.PendingFrames[1];
                    _packetAccumulator.PendingFrames.RemoveRange(0, 2);
                    await ((IZLinkPacketStreamSession)_handler).OnPacketAsync(
                        Stream,
                        header.Move(),
                        body.Move(),
                        CancellationToken.None);
                }
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
}

internal sealed class ZLinkLen32BeAccumulator
{
    private byte[] _buffer = Array.Empty<byte>();
    private int _length;

    public List<global::Zlink.Message> PendingFrames { get; } = [];

    public global::Zlink.Message[] AppendAndDrain(ReadOnlySpan<byte> payload)
    {
        EnsureCapacity(_length + payload.Length);
        payload.CopyTo(_buffer.AsSpan(_length));
        _length += payload.Length;

        var frames = new List<global::Zlink.Message>();
        var offset = 0;
        while (_length - offset >= 4)
        {
            var frameLength = checked((int)BinaryPrimitives.ReadUInt32BigEndian(_buffer.AsSpan(offset, 4)));
            if (_length - offset < 4 + frameLength)
            {
                break;
            }

            frames.Add(global::Zlink.Message.FromBytes(_buffer.AsSpan(offset + 4, frameLength)));
            offset += 4 + frameLength;
        }

        if (offset > 0)
        {
            _buffer.AsSpan(offset, _length - offset).CopyTo(_buffer);
            _length -= offset;
        }

        return [.. frames];
    }

    private void EnsureCapacity(int size)
    {
        if (_buffer.Length >= size)
        {
            return;
        }

        Array.Resize(ref _buffer, Math.Max(size, Math.Max(256, _buffer.Length * 2)));
    }
}

internal sealed class ZLinkManagedStream : IZLinkStream
{
    private readonly global::Zlink.StreamSocket _socket;
    private readonly global::Zlink.RoutingId _routingId;

    public ZLinkManagedStream(global::Zlink.StreamSocket socket, uint routingId)
    {
        _socket = socket;
        _routingId = global::Zlink.RoutingId.FromBytes(global::Zlink.RoutingIdCodec.FromUInt32(routingId));
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
