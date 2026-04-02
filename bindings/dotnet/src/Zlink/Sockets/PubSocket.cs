// SPDX-License-Identifier: MPL-2.0

namespace Zlink;

public sealed class PubSocket : PublisherSocketBase
{
    public PubSocketOptions PubOptions { get; }

    public PubSocket(Context context)
        : base(context, SocketType.Pub)
    {
        PubOptions = new PubSocketOptions(this);
    }
}
