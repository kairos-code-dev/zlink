// SPDX-License-Identifier: MPL-2.0


namespace Systems.Zlink;

internal sealed class PubSocket : PublisherSocketBase, IPubSocket
{
    public new PubSocketOptions Options { get; }

    public PubSocket(Context context)
        : base(context, SocketType.Pub)
    {
        Options = new PubSocketOptions(this);
    }

    public void AttachDiscovery(IDiscovery discovery)
    {
        Kernel.AttachDiscovery(SocketInterop.RequireDiscovery(discovery,
            nameof(discovery)));
    }
}
