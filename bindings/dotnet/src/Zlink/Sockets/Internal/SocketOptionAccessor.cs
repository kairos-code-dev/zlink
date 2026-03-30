// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Text;
using Zlink.Native;

namespace Zlink.Sockets.Internal;

internal sealed class SocketOptionAccessor
{
    private readonly Func<IntPtr> _handleProvider;

    public SocketOptionAccessor(Func<IntPtr> handleProvider)
    {
        _handleProvider = handleProvider
            ?? throw new ArgumentNullException(nameof(handleProvider));
    }

    public unsafe void SetInt32(SocketOption option, int value)
    {
        int tmp = value;
        IntPtr ptr = new IntPtr(&tmp);
        int rc = SetCore(option, ptr, (nuint)sizeof(int));
        ZlinkException.ThrowIfError(rc);
    }

    public unsafe void SetInt64(SocketOption option, long value)
    {
        long tmp = value;
        IntPtr ptr = new IntPtr(&tmp);
        int rc = SetCore(option, ptr, (nuint)sizeof(long));
        ZlinkException.ThrowIfError(rc);
    }

    public unsafe void SetUInt64(SocketOption option, ulong value)
    {
        ulong tmp = value;
        IntPtr ptr = new IntPtr(&tmp);
        int rc = SetCore(option, ptr, (nuint)sizeof(ulong));
        ZlinkException.ThrowIfError(rc);
    }

    public unsafe void SetBytes(SocketOption option, ReadOnlySpan<byte> value)
    {
        IntPtr handle = _handleProvider();
        if (option == SocketOption.RoutingId)
        {
            int rc;
            fixed (byte* ptr = value)
            {
                rc = NativeMethods.zlink_set_routing_id(handle, (IntPtr)ptr,
                    (nuint)value.Length);
            }
            ZlinkException.ThrowIfError(rc);
            return;
        }

        fixed (byte* ptr = value)
        {
            int rc = SetCore(option, (IntPtr)ptr, (nuint)value.Length);
            ZlinkException.ThrowIfError(rc);
        }
    }

    public void SetString(SocketOption option, string value)
    {
        if (value == null)
            throw new ArgumentNullException(nameof(value));

        IntPtr handle = _handleProvider();
        if (option == SocketOption.Subscribe)
        {
            int rc = NativeMethods.zlink_set_subscription(handle, value);
            ZlinkException.ThrowIfError(rc);
            return;
        }

        if (option == SocketOption.Unsubscribe)
        {
            int rc = NativeMethods.zlink_unset_subscription(handle, value);
            ZlinkException.ThrowIfError(rc);
            return;
        }

        int maxByteCount = Encoding.UTF8.GetMaxByteCount(value.Length);
        if (maxByteCount <= 512)
        {
            Span<byte> buffer = stackalloc byte[maxByteCount];
            int byteCount = Encoding.UTF8.GetBytes(value.AsSpan(), buffer);
            SetBytes(option, buffer.Slice(0, byteCount));
            return;
        }

        byte[] rented = ArrayPool<byte>.Shared.Rent(maxByteCount);
        try
        {
            int byteCount = Encoding.UTF8.GetBytes(value, rented);
            SetBytes(option, rented.AsSpan(0, byteCount));
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(rented);
        }
    }

    public unsafe int GetInt32(SocketOption option)
    {
        int value = 0;
        nuint size = (nuint)sizeof(int);
        IntPtr ptr = new IntPtr(&value);
        int rc = GetCore(option, ptr, ref size);
        ZlinkException.ThrowIfError(rc);
        return value;
    }

    public unsafe long GetInt64(SocketOption option)
    {
        long value = 0;
        nuint size = (nuint)sizeof(long);
        IntPtr ptr = new IntPtr(&value);
        int rc = GetCore(option, ptr, ref size);
        ZlinkException.ThrowIfError(rc);
        return value;
    }

    public unsafe ulong GetUInt64(SocketOption option)
    {
        ulong value = 0;
        nuint size = (nuint)sizeof(ulong);
        IntPtr ptr = new IntPtr(&value);
        int rc = GetCore(option, ptr, ref size);
        ZlinkException.ThrowIfError(rc);
        return value;
    }

    public byte[] GetBytes(SocketOption option, int initialSize = 256)
    {
        IntPtr handle = _handleProvider();
        if (option == SocketOption.RoutingId)
        {
            int rc = NativeMethods.zlink_get_routing_id(handle, out var routingId);
            ZlinkException.ThrowIfError(rc);
            return NativeHelpers.ReadRoutingId(ref routingId);
        }

        if (initialSize <= 0)
            throw new ArgumentOutOfRangeException(nameof(initialSize));

        byte[] rented = ArrayPool<byte>.Shared.Rent(initialSize);
        try
        {
            while (true)
            {
                unsafe
                {
                    fixed (byte* ptr = rented)
                    {
                        nuint size = (nuint)rented.Length;
                        int rc = GetCore(option, (IntPtr)ptr, ref size);
                        if (rc == 0)
                            return CopyBytes(rented, size);

                        if (size > (nuint)rented.Length)
                        {
                            ArrayPool<byte>.Shared.Return(rented);
                            rented = ArrayPool<byte>.Shared.Rent(checked((int)size));
                            continue;
                        }

                        ZlinkException.ThrowIfError(rc);
                    }
                }
            }
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(rented);
        }
    }

    public unsafe int GetBytesInto(SocketOption option, Span<byte> destination)
    {
        fixed (byte* ptr = destination)
        {
            nuint size = (nuint)destination.Length;
            int rc = GetCore(option, (IntPtr)ptr, ref size);
            ZlinkException.ThrowIfError(rc);
            return checked((int)size);
        }
    }

    public string GetString(SocketOption option, int initialSize = 256)
    {
        byte[] bytes = GetBytes(option, initialSize);
        int len = Array.IndexOf(bytes, (byte)0);
        if (len < 0)
            len = bytes.Length;
        return Encoding.UTF8.GetString(bytes, 0, len);
    }

    public static SocketType ReadSocketType(IntPtr handle)
    {
        unsafe
        {
            int value = 0;
            nuint size = (nuint)sizeof(int);
            int rc = NativeMethods.zlink_get_option(handle, (int)SocketOption.Type,
                new IntPtr(&value), ref size);
            ZlinkException.ThrowIfError(rc);
            return (SocketType)value;
        }
    }

    private int SetCore(SocketOption option, IntPtr value, nuint length)
    {
        IntPtr handle = _handleProvider();
        int code = (int)option;
        if ((code & 0xFF00) == 0x3100)
            return NativeMethods.zlink_set_router_option(handle, code, value,
                length);
        if ((code & 0xFF00) == 0x3200)
            return NativeMethods.zlink_set_dealer_option(handle, code, value,
                length);
        if ((code & 0xFF00) == 0x3300)
            return NativeMethods.zlink_set_pub_option(handle, code, value,
                length);
        if ((code & 0xFF00) == 0x3400)
            return NativeMethods.zlink_set_sub_option(handle, code, value,
                length);
        if ((code & 0xFF00) == 0x3500)
            return NativeMethods.zlink_set_stream_option(handle, code, value,
                length);
        return NativeMethods.zlink_set_option(handle, code, value, length);
    }

    private int GetCore(SocketOption option, IntPtr value, ref nuint length)
    {
        IntPtr handle = _handleProvider();
        int code = (int)option;
        if ((code & 0xFF00) == 0x3100)
            return NativeMethods.zlink_get_router_option(handle, code, value,
                ref length);
        if ((code & 0xFF00) == 0x3300)
            return NativeMethods.zlink_get_pub_option(handle, code, value,
                ref length);
        if ((code & 0xFF00) == 0x3400)
            return NativeMethods.zlink_get_sub_option(handle, code, value,
                ref length);
        if ((code & 0xFF00) == 0x3500)
            return NativeMethods.zlink_get_stream_option(handle, code, value,
                ref length);
        return NativeMethods.zlink_get_option(handle, code, value, ref length);
    }

    private static byte[] CopyBytes(byte[] source, nuint size)
    {
        int actual = checked((int)size);
        byte[] result = new byte[actual];
        if (actual == 0)
            return result;

        int toCopy = actual;
        if (toCopy > source.Length)
            toCopy = source.Length;
        Array.Copy(source, result, toCopy);
        return result;
    }
}
