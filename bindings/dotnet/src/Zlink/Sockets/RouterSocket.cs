// SPDX-License-Identifier: MPL-2.0

namespace Zlink;

public sealed class RouterSocket : MessageSocketBase
{
    public RouterSocket(Context context)
        : base(context, SocketType.Router)
    {
    }
}
