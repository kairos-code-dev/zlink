// SPDX-License-Identifier: MPL-2.0

using System.ComponentModel;
using Systems.Zlink.Runtime.Sockets.Internal;

namespace Systems.Zlink;

[EditorBrowsable(EditorBrowsableState.Never)]
internal abstract class ConnectableRoutedMessageSocketBase : RoutedMessageSocketBase,
    IConnectableRoutedMessageSocket
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
        try
        {
            Kernel.Connect(address);
        }
        catch (ZlinkException ex)
        {
            throw ZlinkException.CreateConnectException(ex.NativeErrno);
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
            throw ZlinkException.CreateConnectException(ex.NativeErrno);
        }
    }

    public void DisconnectRid(RoutingId peerRid)
    {
        try
        {
            Kernel.DisconnectRid(peerRid);
        }
        catch (ZlinkException ex)
        {
            throw ZlinkException.CreateConnectException(ex.NativeErrno);
        }
    }
}
