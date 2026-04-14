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
        try
        {
            Kernel.Connect(address);
        }
        catch (ZlinkException ex)
        {
            throw ZlinkException.CreateConnectException(ex.InternalErrno);
        }
    }

    public void Disconnect(string address)
    {
        try
        {
            Kernel.Disconnect(address);
        }
        catch (ZlinkException ex)
        {
            throw ZlinkException.CreateConnectException(ex.InternalErrno);
        }
    }
}
