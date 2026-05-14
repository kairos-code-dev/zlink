// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

public sealed class XSubSocket : SubscriberSocketBase, IXSubSocket
{
    public new SubSocketOptions Options { get; }

    public XSubSocket(Context context)
        : base(context, SocketType.XSub)
    {
        Options = new SubSocketOptions(this);
    }
}
