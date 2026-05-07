// SPDX-License-Identifier: MPL-2.0


namespace Systems.Zlink;

public sealed class SubSocket : SubscriberSocketBase
{
    public SubSocketOptions SubOptions { get; }

    public SubSocket(Context context)
        : base(context, SocketType.Sub)
    {
        SubOptions = new SubSocketOptions(this);
    }

    public void AttachDiscovery(Discovery discovery)
    {
        Kernel.AttachDiscovery(discovery);
    }
}
