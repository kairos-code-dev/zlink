// SPDX-License-Identifier: MPL-2.0


namespace Systems.Zlink;

public sealed class SubSocket : SubscriberSocketBase
{
    public new SubSocketOptions Options { get; }

    public SubSocket(Context context)
        : base(context, SocketType.Sub)
    {
        Options = new SubSocketOptions(this);
    }

    public void AttachDiscovery(Discovery discovery)
    {
        Kernel.AttachDiscovery(discovery);
    }
}
