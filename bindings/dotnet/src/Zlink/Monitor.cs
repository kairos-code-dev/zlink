// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers.Binary;
using Zlink.Native;

namespace Zlink;

public sealed class MonitorSocket : IDisposable
{
    private readonly Socket _socket;

    internal MonitorSocket(Socket socket)
    {
        _socket = socket;
    }

    public MonitorEvent Receive(ReceiveFlags flags = ReceiveFlags.None)
    {
        int rc = NativeMethods.zlink_monitor_recv(_socket.Handle, out var evt,
            (int)flags);
        ZlinkException.ThrowIfError(rc);
        return MonitorEvent.FromNative(ref evt);
    }

    public void Dispose()
    {
        _socket.Dispose();
        GC.SuppressFinalize(this);
    }

    ~MonitorSocket()
    {
        Dispose();
    }
}

public readonly struct MonitorEvent
{
    public MonitorEvent(SocketEvent @event, ulong value, string routingId,
        uint? streamRoutingId,
        string localAddress, string remoteAddress, ulong rawEvent)
    {
        Event = @event;
        Value = value;
        RoutingId = routingId;
        StreamRoutingId = streamRoutingId;
        LocalAddress = localAddress;
        RemoteAddress = remoteAddress;
        RawEvent = rawEvent;
    }

    public SocketEvent Event { get; }
    public ulong Value { get; }
    public string RoutingId { get; }
    public uint? StreamRoutingId { get; }
    public string LocalAddress { get; }
    public string RemoteAddress { get; }
    public ulong RawEvent { get; }

    internal static MonitorEvent FromNative(ref ZlinkMonitorEvent evt)
    {
        byte[] routing = NativeHelpers.ReadRoutingId(ref evt.RoutingId);
        string routingId = RoutingIdCodec.ToPublicString(routing);
        uint? streamRoutingId = routing.Length == sizeof(uint)
            ? BinaryPrimitives.ReadUInt32BigEndian(routing)
            : null;
        string local;
        string remote;
        unsafe
        {
            fixed (byte* localPtr = evt.LocalAddr)
            fixed (byte* remotePtr = evt.RemoteAddr)
            {
                local = NativeHelpers.ReadString(localPtr, 256);
                remote = NativeHelpers.ReadString(remotePtr, 256);
            }
        }
        SocketEvent @event = (SocketEvent)(evt.Event & 0xFFFFFFFFuL);
        return new MonitorEvent(@event, evt.Value, routingId, streamRoutingId,
            local, remote, evt.Event);
    }
}
