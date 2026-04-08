// SPDX-License-Identifier: MPL-2.0

using Zlink.Service;

namespace Zlink;

public sealed class PubSocket : PublisherSocketBase
{
    public PubSocketOptions PubOptions { get; }

    public PubSocket(Context context)
        : base(context, SocketType.Pub)
    {
        PubOptions = new PubSocketOptions(this);
    }

    public void AttachDiscovery(Discovery discovery)
    {
        Kernel.AttachDiscovery(discovery);
    }
}
