// SPDX-License-Identifier: MPL-2.0

using System;
using System.Text;

namespace Zlink;

public readonly struct RoutingId : IEquatable<RoutingId>
{
    private static readonly UTF8Encoding StrictUtf8 = new(false, true);
    private readonly byte[]? _bytes;

    private RoutingId(byte[] bytes, bool takeOwnership)
    {
        _bytes = takeOwnership ? bytes : bytes.ToArray();
    }

    public static RoutingId FromBytes(ReadOnlySpan<byte> bytes)
    {
        Validate(bytes, nameof(bytes));
        return new RoutingId(bytes.ToArray(), takeOwnership: true);
    }

    public static RoutingId FromBytes(byte[] bytes)
    {
        if (bytes == null)
            throw new ArgumentNullException(nameof(bytes));
        Validate(bytes, nameof(bytes));
        return new RoutingId(bytes, takeOwnership: false);
    }

    public static RoutingId FromUInt32(uint value)
    {
        return new RoutingId(
            RoutingIdCodec.FromUInt32(value), takeOwnership: true);
    }

    public static RoutingId FromU32(uint value)
    {
        return FromUInt32(value);
    }

    public static RoutingId FromString(string value)
    {
        return FromText(value);
    }

    public static RoutingId FromText(string value)
    {
        if (value == null)
            throw new ArgumentNullException(nameof(value));
        byte[] bytes = StrictUtf8.GetBytes(value);
        Validate(bytes, nameof(value));
        return new RoutingId(bytes, takeOwnership: true);
    }

    public int Size => _bytes?.Length ?? 0;

    public bool IsEmpty => Size == 0;

    public ReadOnlySpan<byte> ToBytes()
    {
        return _bytes ?? ReadOnlySpan<byte>.Empty;
    }

    public ReadOnlySpan<byte> AsSpan()
    {
        return ToBytes();
    }

    public byte[] ToByteArray()
    {
        return _bytes?.ToArray() ?? Array.Empty<byte>();
    }

    public string ToHex()
    {
        return Convert.ToHexString(ToBytes()).ToLowerInvariant();
    }

    public bool TryToUInt32(out uint value)
    {
        return RoutingIdCodec.TryToUInt32(ToBytes(), out value);
    }

    public bool TryToU32(out uint value)
    {
        return TryToUInt32(out value);
    }

    public uint ToU32()
    {
        if (!TryToUInt32(out uint value))
            throw new InvalidOperationException(
                "routing id is not a 4-byte STREAM routing id.");
        return value;
    }

    public string ToPublicString()
    {
        return RoutingIdCodec.ToPublicString(ToBytes());
    }

    public string ToText()
    {
        return StrictUtf8.GetString(ToBytes());
    }

    public override string ToString()
    {
        return ToHex();
    }

    public bool Equals(RoutingId other)
    {
        return ToBytes().SequenceEqual(other.ToBytes());
    }

    public override bool Equals(object? obj)
    {
        return obj is RoutingId other && Equals(other);
    }

    public override int GetHashCode()
    {
        HashCode hash = new();
        foreach (byte value in ToBytes())
            hash.Add(value);
        return hash.ToHashCode();
    }

    public static bool operator ==(RoutingId left, RoutingId right)
    {
        return left.Equals(right);
    }

    public static bool operator !=(RoutingId left, RoutingId right)
    {
        return !left.Equals(right);
    }

    internal static RoutingId? FromOptionalBytes(ReadOnlySpan<byte> bytes)
    {
        return bytes.Length == 0 ? null : FromBytes(bytes);
    }

    private static void Validate(ReadOnlySpan<byte> bytes, string paramName)
    {
        if (bytes.Length > 255)
        {
            throw new ArgumentOutOfRangeException(paramName,
                "routingId length must be between 0 and 255 bytes.");
        }
    }
}
