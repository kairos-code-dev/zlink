// SPDX-License-Identifier: MPL-2.0

namespace Zlink;

public sealed class StreamSocket : RoutedMessageSocketBase
{
    public StreamSocket(Context context)
        : base(context, SocketType.Stream)
    {
    }

    public void SetNotify(bool enabled)
    {
        SetOption(SocketOptions.StreamNotify, enabled ? 1 : 0);
    }

    public bool GetNotify()
    {
        return GetOption(SocketOptions.StreamNotify) != 0;
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
