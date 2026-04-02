// SPDX-License-Identifier: MPL-2.0

namespace Zlink;

public sealed class DealerSocket : MessageSocketBase
{
    public DealerSocketOptions DealerOptions { get; }

    public DealerSocket(Context context)
        : base(context, SocketType.Dealer)
    {
        DealerOptions = new DealerSocketOptions(this);
    }
}
