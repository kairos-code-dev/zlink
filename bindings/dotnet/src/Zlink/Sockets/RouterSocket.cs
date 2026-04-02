// SPDX-License-Identifier: MPL-2.0

namespace Zlink;

public sealed class RouterSocket : ConnectableRoutedMessageSocketBase
{
    public RouterSocketOptions RouterOptions { get; }

    public RouterSocket(Context context)
        : base(context, SocketType.Router)
    {
        RouterOptions = new RouterSocketOptions(this);
    }
}
