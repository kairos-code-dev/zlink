// SPDX-License-Identifier: MPL-2.0

namespace Zlink;

public sealed class SubSocket : SubscriberSocketBase
{
    public SubSocketOptions SubOptions { get; }

    public SubSocket(Context context)
        : base(context, SocketType.Sub)
    {
        SubOptions = new SubSocketOptions(this);
    }
}
