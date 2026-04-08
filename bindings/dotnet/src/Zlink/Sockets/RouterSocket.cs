// SPDX-License-Identifier: MPL-2.0

using Zlink.Service;

namespace Zlink;

public sealed class RouterSocket : ConnectableRoutedMessageSocketBase
{
    public RouterSocketOptions RouterOptions { get; }

    public RouterSocket(Context context)
        : base(context, SocketType.Router)
    {
        RouterOptions = new RouterSocketOptions(this);
    }

    public void AttachDiscovery(Discovery discovery)
    {
        Kernel.AttachDiscovery(discovery);
    }
}
