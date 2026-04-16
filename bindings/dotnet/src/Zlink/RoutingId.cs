// SPDX-License-Identifier: MPL-2.0

using System;
using System.Text;

namespace Zlink;

public readonly struct RoutingId : IEquatable<RoutingId>
{
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

    public static RoutingId FromString(string value)
    {
        return new RoutingId(
            RoutingIdCodec.FromPublicString(value, nameof(value)),
            takeOwnership: true);
    }

    public int Size => _bytes?.Length ?? 0;

    public bool IsEmpty => Size == 0;

    public ReadOnlySpan<byte> ToBytes()
    {
        return _bytes ?? ReadOnlySpan<byte>.Empty;
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

    public string ToPublicString()
    {
        return RoutingIdCodec.ToPublicString(ToBytes());
    }

    public override string ToString()
    {
        return Encoding.UTF8.GetString(ToBytes());
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
        if (bytes.Length <= 0 || bytes.Length > 255)
        {
            throw new ArgumentOutOfRangeException(paramName,
                "routingId length must be between 1 and 255 bytes.");
        }
    }
}
