// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Systems.Zlink.Native;
using Systems.Zlink.Sockets.Internal;

namespace Systems.Zlink;

internal sealed partial class Spot : ISpot
{
    public void SetRoutingId(RoutingId routingId)
    {
        EnsureNotDisposed();
        byte[] routingIdBytes = routingId.ToByteArray();
        unsafe
        {
            fixed (byte* routingIdPtr = routingIdBytes)
            {
                int rc = NativeMethods.zlink_set_routing_id(_handle,
                    (IntPtr)routingIdPtr, (nuint)routingIdBytes.Length);
                ZlinkException.ThrowConfigIfError(rc);
            }
        }
    }

    public RoutingId RoutingId
    {
        get
        {
            EnsureNotDisposed();
            int rc = NativeMethods.zlink_get_routing_id(_handle,
                out ZlinkRoutingId routingId);
            ZlinkException.ThrowConfigIfError(rc);
            return RoutingId.From(
                NativeHelpers.ReadRoutingId(ref routingId));
        }
    }

    internal unsafe void SetOption(SpotOption option, int value)
    {
        EnsureNotDisposed();
        int local = value;
        int rc = NativeMethods.zlink_set_spot_option(_handle, option,
            (IntPtr)(&local), (nuint)sizeof(int));
        ZlinkException.ThrowConfigIfError(rc);
    }

    internal unsafe int GetOption(SpotOption option)
    {
        EnsureNotDisposed();
        int value = 0;
        nuint size = (nuint)sizeof(int);
        int rc = NativeMethods.zlink_get_spot_option(_handle, option,
            (IntPtr)(&value), ref size);
        ZlinkException.ThrowConfigIfError(rc);
        return value;
    }

    internal unsafe void SetSocketOption(SocketOptionKey<int> option, int value)
    {
        EnsureNotDisposed();
        int local = value;
        int rc = NativeMethods.zlink_set_option(_handle, (int)option.Option,
            (IntPtr)(&local), (nuint)sizeof(int));
        ZlinkException.ThrowConfigIfError(rc);
    }

    internal unsafe int GetSocketOption(SocketOptionKey<int> option)
    {
        EnsureNotDisposed();
        int value = 0;
        nuint size = (nuint)sizeof(int);
        int rc = NativeMethods.zlink_get_option(_handle, (int)option.Option,
            (IntPtr)(&value), ref size);
        ZlinkException.ThrowConfigIfError(rc);
        return value;
    }

    public TimeSpan? RequestTimeout
    {
        get => Options.RequestTimeout;
        set => Options.RequestTimeout = value;
    }

    public int SendHighWaterMark
    {
        get => Options.SendHighWaterMark;
        set => Options.SendHighWaterMark = value;
    }

    public int ReceiveHighWaterMark
    {
        get => Options.ReceiveHighWaterMark;
        set => Options.ReceiveHighWaterMark = value;
    }

    public int SendBufferSize
    {
        get => Options.SendBufferSize;
        set => Options.SendBufferSize = value;
    }

    public int ReceiveBufferSize
    {
        get => Options.ReceiveBufferSize;
        set => Options.ReceiveBufferSize = value;
    }

    public TimeSpan? SendTimeout
    {
        get => Options.SendTimeout;
        set => Options.SendTimeout = value;
    }

    public TimeSpan? ReceiveTimeout
    {
        get => Options.ReceiveTimeout;
        set => Options.ReceiveTimeout = value;
    }

    public TimeSpan? Linger
    {
        get => Options.Linger;
        set => Options.Linger = value;
    }
}
