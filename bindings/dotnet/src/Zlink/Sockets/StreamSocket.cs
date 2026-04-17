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

    public void DetachStream()
    {
        Kernel.DetachStream();
    }
}
