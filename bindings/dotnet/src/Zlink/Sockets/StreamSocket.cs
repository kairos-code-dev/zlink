// SPDX-License-Identifier: MPL-2.0

namespace Zlink;

public sealed class StreamSocket : RoutedMessageSocketBase
{
    public StreamSocketOptions StreamOptions { get; }

    public StreamSocket(Context context)
        : base(context, SocketType.Stream)
    {
        StreamOptions = new StreamSocketOptions(this);
    }

    public void OnPacket(StreamPacketHandler handler)
    {
        Kernel.AttachStreamRaw(handler);
    }

    public void OnPacket(StreamUInt32PacketHandler handler)
    {
        Kernel.AttachStreamRaw(handler);
    }

    public void OnFramedPacket(StreamFramedPacketHandler handler)
    {
        Kernel.AttachStreamPacket(handler);
    }

    public void OnFramedPacket(StreamUInt32FramedPacketHandler handler)
    {
        Kernel.AttachStreamPacket(handler);
    }

    public void Send(uint routingId, Message message,
        SendFlags flags = SendFlags.None)
    {
        Kernel.Send(routingId, message, flags);
    }

    public void Send(uint routingId, byte[] payload,
        SendFlags flags = SendFlags.None)
    {
        Kernel.SendBorrowedSingle(routingId, payload, (int)flags);
    }

    public void DetachStream()
    {
        Kernel.DetachStream();
    }
}
