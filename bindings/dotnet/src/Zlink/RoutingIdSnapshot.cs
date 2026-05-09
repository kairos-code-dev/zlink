// SPDX-License-Identifier: MPL-2.0

using System;
using Systems.Zlink.Native;

namespace Systems.Zlink;

internal unsafe struct RoutingIdSnapshot
{
    private const int InlineCapacity = 16;

    private byte _size;
    private ulong _lo;
    private ulong _hi;
    private byte[]? _large;

    internal bool HasValue => _size > 0;

    internal static RoutingIdSnapshot FromPointer(IntPtr routingIdPtr)
    {
        if (routingIdPtr == IntPtr.Zero)
            return default;

        return FromNative(ref *(ZlinkRoutingId*)routingIdPtr);
    }

    internal static RoutingIdSnapshot FromBytes(byte[]? routingId)
    {
        if (routingId == null || routingId.Length == 0)
            return default;

        return FromSpan(routingId);
    }

    internal static RoutingIdSnapshot FromNative(ref ZlinkRoutingId routingId)
    {
        int size = routingId.Size;
        if (size <= 0)
            return default;

        fixed (byte* src = routingId.Data)
        {
            return FromSpan(new ReadOnlySpan<byte>(src, size));
        }
    }

    private static RoutingIdSnapshot FromSpan(ReadOnlySpan<byte> routingId)
    {
        if (routingId.Length <= 0)
            return default;

        var snapshot = new RoutingIdSnapshot
        {
            _size = checked((byte)routingId.Length)
        };

        if (routingId.Length <= InlineCapacity)
        {
            for (int i = 0; i < routingId.Length && i < 8; i++)
                snapshot._lo |= (ulong)routingId[i] << (i * 8);
            for (int i = 8; i < routingId.Length; i++)
                snapshot._hi |= (ulong)routingId[i] << ((i - 8) * 8);
        }
        else
        {
            snapshot._large = routingId.ToArray();
        }

        return snapshot;
    }

    internal RoutingId? ToRoutingId()
    {
        byte[]? bytes = ToByteArray();
        return bytes == null ? null : RoutingId.FromOwnedOptionalBytes(bytes);
    }

    internal byte[]? ToByteArray()
    {
        if (_size <= 0)
            return null;
        if (_large != null)
            return _large;

        byte[] bytes = new byte[_size];
        for (int i = 0; i < _size && i < 8; i++)
            bytes[i] = (byte)(_lo >> (i * 8));
        for (int i = 8; i < _size; i++)
            bytes[i] = (byte)(_hi >> ((i - 8) * 8));
        return bytes;
    }
}
