// SPDX-License-Identifier: MPL-2.0

using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed partial class Spot : ISpot
{
    public void SetRoutingId(RoutingId routingId)
    {
        EnsureNotDisposed();
        var routingIdBytes = routingId.ToByteArray();
        unsafe
        {
            fixed (byte* routingIdPtr = routingIdBytes)
            {
                var rc = NativeMethods.zlink_set_routing_id(Handle,
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
            var rc = NativeMethods.zlink_get_routing_id(Handle,
                out var routingId);
            ZlinkException.ThrowConfigIfError(rc);
            return RoutingId.From(
                NativeHelpers.ReadRoutingId(ref routingId));
        }
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

    internal unsafe void SetOption(SpotOption option, int value)
    {
        EnsureNotDisposed();
        var local = value;
        var rc = NativeMethods.zlink_set_spot_option(Handle, option,
            (IntPtr)(&local), sizeof(int));
        ZlinkException.ThrowConfigIfError(rc);
    }

    internal unsafe int GetOption(SpotOption option)
    {
        EnsureNotDisposed();
        var value = 0;
        var size = (nuint)sizeof(int);
        var rc = NativeMethods.zlink_get_spot_option(Handle, option,
            (IntPtr)(&value), ref size);
        ZlinkException.ThrowConfigIfError(rc);
        return value;
    }

    internal unsafe void SetSocketOption(SocketOptionKey<int> option, int value)
    {
        EnsureNotDisposed();
        var local = value;
        var rc = NativeMethods.zlink_set_option(Handle, (int)option.Option,
            (IntPtr)(&local), sizeof(int));
        ZlinkException.ThrowConfigIfError(rc);
    }

    internal unsafe int GetSocketOption(SocketOptionKey<int> option)
    {
        EnsureNotDisposed();
        var value = 0;
        var size = (nuint)sizeof(int);
        var rc = NativeMethods.zlink_get_option(Handle, (int)option.Option,
            (IntPtr)(&value), ref size);
        ZlinkException.ThrowConfigIfError(rc);
        return value;
    }
}