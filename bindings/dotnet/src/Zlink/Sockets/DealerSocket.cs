// SPDX-License-Identifier: MPL-2.0

namespace Zlink;

public sealed class DealerSocket : MessageSocketBase
{
    public DealerSocket(Context context)
        : base(context, SocketType.Dealer)
    {
    }
}
