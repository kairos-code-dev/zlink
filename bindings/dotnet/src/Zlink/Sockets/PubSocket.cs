// SPDX-License-Identifier: MPL-2.0

namespace Zlink;

public sealed class PubSocket : PublisherSocketBase
{
    public PubSocket(Context context)
        : base(context, SocketType.Pub)
    {
    }

    public void SetNoDrop(bool enabled)
    {
        SetOption(SocketOptions.XPubNoDrop, enabled ? 1 : 0);
    }
}
