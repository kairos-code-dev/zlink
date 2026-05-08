// SPDX-License-Identifier: MPL-2.0


namespace Systems.Zlink;

public sealed class PubSocket : PublisherSocketBase
{
    public new PubSocketOptions Options { get; }

    public PubSocket(Context context)
        : base(context, SocketType.Pub)
    {
        Options = new PubSocketOptions(this);
    }

    public void AttachDiscovery(Discovery discovery)
    {
        Kernel.AttachDiscovery(discovery);
    }
}
