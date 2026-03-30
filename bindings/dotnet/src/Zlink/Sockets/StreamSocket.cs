// SPDX-License-Identifier: MPL-2.0

namespace Zlink;

public sealed class StreamSocket : RoutedMessageSocketBase
{
    public StreamSocket(Context context)
        : base(context, SocketType.Stream)
    {
    }

    public void AttachStreamRaw(StreamPacketHandler handler)
    {
        Kernel.AttachStreamRaw(handler);
    }

    public void DetachStream()
    {
        Kernel.DetachStream();
    }
}
