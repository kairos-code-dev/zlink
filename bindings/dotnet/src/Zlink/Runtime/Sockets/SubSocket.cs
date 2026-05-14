// SPDX-License-Identifier: MPL-2.0


namespace Systems.Zlink;

public sealed class SubSocket : SubscriberSocketBase, ISubSocket
{
    public new SubSocketOptions Options { get; }

    public SubSocket(Context context)
        : base(context, SocketType.Sub)
    {
        Options = new SubSocketOptions(this);
    }

    public void AttachDiscovery(IDiscovery discovery)
    {
        Kernel.AttachDiscovery(SocketInterop.RequireDiscovery(discovery,
            nameof(discovery)));
    }
}
