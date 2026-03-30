// SPDX-License-Identifier: MPL-2.0

namespace Zlink;

public sealed class RouterSocket : RoutedMessageSocketBase
{
    public RouterSocket(Context context)
        : base(context, SocketType.Router)
    {
    }
}
