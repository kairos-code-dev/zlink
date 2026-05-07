// SPDX-License-Identifier: MPL-2.0

using System.ComponentModel;
using Systems.Zlink.Sockets.Internal;

namespace Systems.Zlink;

[EditorBrowsable(EditorBrowsableState.Never)]
public abstract class ConnectableRoutedMessageSocketBase : RoutedMessageSocketBase
{
    internal ConnectableRoutedMessageSocketBase(Context context, SocketType type)
        : base(context, type)
    {
    }

    internal ConnectableRoutedMessageSocketBase(SocketKernel kernel)
        : base(kernel)
    {
    }

    public void Connect(string address)
    {
        Kernel.Connect(address);
    }

    public void Disconnect(string address)
    {
        Kernel.Disconnect(address);
    }
}
