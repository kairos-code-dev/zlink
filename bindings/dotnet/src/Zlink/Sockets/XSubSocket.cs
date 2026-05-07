// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

public sealed class XSubSocket : SubscriberSocketBase
{
    public SubSocketOptions SubOptions { get; }

    public XSubSocket(Context context)
        : base(context, SocketType.XSub)
    {
        SubOptions = new SubSocketOptions(this);
    }
}
