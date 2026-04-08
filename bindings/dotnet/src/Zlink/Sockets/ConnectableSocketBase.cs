// SPDX-License-Identifier: MPL-2.0

using System.ComponentModel;
using Zlink.Sockets.Internal;

namespace Zlink;

[EditorBrowsable(EditorBrowsableState.Never)]
public abstract class ConnectableSocketBase : SocketBase
{
    internal ConnectableSocketBase(Context context, SocketType type)
        : base(context, type)
    {
    }

    internal ConnectableSocketBase(SocketKernel kernel)
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
