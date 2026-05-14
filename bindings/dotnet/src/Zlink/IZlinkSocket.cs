// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

public interface IZlinkSocket
{
}

internal static class SocketInterop
{
    internal static SocketBase RequireSocket(IZlinkSocket socket, string paramName)
    {
        if (socket == null)
            throw new ArgumentNullException(paramName);
        if (socket is not SocketBase concrete)
        {
            throw new ArgumentException(
                "socket must be a concrete zlink socket instance",
                paramName);
        }
        return concrete;
    }

    internal static IntPtr RequirePollableHandle(IZlinkSocket socket,
        string paramName)
    {
        if (socket == null)
            throw new ArgumentNullException(paramName);
        return socket switch
        {
            SocketBase concrete => concrete.Handle,
            Spot spot => spot.Handle,
            _ => throw new ArgumentException(
                "socket must be a concrete zlink socket or spot instance",
                paramName)
        };
    }
}
