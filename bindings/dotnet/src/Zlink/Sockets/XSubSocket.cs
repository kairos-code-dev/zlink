// SPDX-License-Identifier: MPL-2.0

namespace Zlink;

public sealed class XSubSocket : SubscriberSocketBase
{
    public XSubSocket(Context context)
        : base(context, SocketType.XSub)
    {
    }
}
