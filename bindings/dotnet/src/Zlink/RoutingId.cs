// SPDX-License-Identifier: MPL-2.0

using System;

namespace Zlink;

public readonly struct RoutingId : IEquatable<RoutingId>
{
    private readonly string _value;

    public RoutingId(string value)
    {
        _value = value ?? throw new ArgumentNullException(nameof(value));
        _ = RoutingIdCodec.FromPublicString(_value, nameof(value));
    }

    public string Value => _value ?? string.Empty;

    public bool IsEmpty => string.IsNullOrEmpty(_value);

    public override string ToString()
    {
        return Value;
    }

    public bool Equals(RoutingId other)
    {
        return string.Equals(Value, other.Value, StringComparison.Ordinal);
    }

    public override bool Equals(object? obj)
    {
        return obj is RoutingId other && Equals(other);
    }

    public override int GetHashCode()
    {
        return StringComparer.Ordinal.GetHashCode(Value);
    }

    public static bool operator ==(RoutingId left, RoutingId right)
    {
        return left.Equals(right);
    }

    public static bool operator !=(RoutingId left, RoutingId right)
    {
        return !left.Equals(right);
    }

    public static explicit operator string(RoutingId routingId)
    {
        return routingId.Value;
    }
}
