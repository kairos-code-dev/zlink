// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Text;
using Systems.Zlink.Native;

namespace Systems.Zlink;

public readonly struct RoutingId : IEquatable<RoutingId>
{
    private const int MaxSize = 255;
    private const int MaxHexStringLength = MaxSize * 2;
    private const int ThreadCacheMaxEntries = 256;
    [ThreadStatic]
    private static Dictionary<RouteCacheKey, List<RouteCacheEntry>>? t_ownedCache;
    private readonly byte[]? _bytes;
    private readonly int _hash;
    private readonly NativeRoutingIdBox? _native;

    private RoutingId(byte[] bytes, bool takeOwnership)
    {
        _bytes = takeOwnership ? bytes : bytes.ToArray();
        _hash = ComputeHash(_bytes);
        _native = new NativeRoutingIdBox(_bytes);
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

    public static RoutingId FromString(string value)
    {
        if (value == null)
            throw new ArgumentNullException(nameof(value));
        if (value.Length == 0 || (value.Length & 1) != 0)
        {
            throw new ArgumentException(
                "routingId string must be a non-empty even-length hex string.",
                nameof(value));
        }
        if (value.Length > MaxHexStringLength)
        {
            throw new ArgumentOutOfRangeException(nameof(value),
                "routingId string must decode to at most 255 bytes.");
        }

        byte[] bytes;
        try
        {
            bytes = Convert.FromHexString(value);
        }
        catch (FormatException ex)
        {
            throw new ArgumentException(
                "routingId string must contain only hex digits.",
                nameof(value), ex);
        }

        Validate(bytes, nameof(value));
        return new RoutingId(bytes, takeOwnership: true);
    }

    public int Size => _bytes?.Length ?? 0;

    internal bool IsEmpty => Size == 0;

    public ReadOnlySpan<byte> ToBytes()
    {
        return _bytes ?? ReadOnlySpan<byte>.Empty;
    }

    internal byte[] ToByteArray()
    {
        return _bytes?.ToArray() ?? Array.Empty<byte>();
    }

    public string ToHex()
    {
        return Convert.ToHexString(ToBytes()).ToLowerInvariant();
    }

    public override string ToString()
    {
        return ToHex();
    }

    public bool Equals(RoutingId other)
    {
        if (_hash != other._hash)
            return false;
        return ToBytes().SequenceEqual(other.ToBytes());
    }

    public override bool Equals(object? obj)
    {
        return obj is RoutingId other && Equals(other);
    }

    public override int GetHashCode()
    {
        return _hash;
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

    internal static RoutingId? FromOwnedOptionalBytes(byte[] bytes)
    {
        if (bytes.Length == 0)
            return null;
        Validate(bytes, nameof(bytes));
        return FromOwnedBytesCached(bytes);
    }

    internal static unsafe RoutingId? FromNative(ref ZlinkRoutingId routingId)
    {
        int size = routingId.Size;
        if (size <= 0)
            return null;

        fixed (byte* src = routingId.Data)
        {
            return FromSpanCached(new ReadOnlySpan<byte>(src, size));
        }
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal byte[] AsByteArrayUnsafe()
    {
        return _bytes ?? Array.Empty<byte>();
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal ZlinkRoutingId ToNative()
    {
        NativeRoutingIdBox? native = _native;
        if (native != null)
            return native.Value;
        return NativeHelpers.WriteRoutingId(ToBytes());
    }

    private static int ComputeHash(ReadOnlySpan<byte> bytes)
    {
        HashCode hash = new();
        for (int i = 0; i < bytes.Length; i++)
            hash.Add(bytes[i]);
        return hash.ToHashCode();
    }

    private static void Validate(ReadOnlySpan<byte> bytes, string paramName)
    {
        if (bytes.Length <= 0 || bytes.Length > MaxSize)
        {
            throw new ArgumentOutOfRangeException(paramName,
                "routingId length must be between 1 and 255 bytes.");
        }
    }

    private static RoutingId FromOwnedBytesCached(byte[] bytes)
    {
        RouteCacheKey key = RouteCacheKey.Create(bytes);
        Dictionary<RouteCacheKey, List<RouteCacheEntry>> cache =
            t_ownedCache ??= new Dictionary<RouteCacheKey, List<RouteCacheEntry>>();
        if (cache.TryGetValue(key, out List<RouteCacheEntry>? entries))
        {
            for (int i = 0; i < entries.Count; i++)
            {
                if (bytes.AsSpan().SequenceEqual(entries[i].Bytes))
                    return entries[i].RoutingId;
            }
        }

        RoutingId created = new RoutingId(bytes, takeOwnership: true);
        if (cache.Count >= ThreadCacheMaxEntries)
            cache.Clear();
        if (!cache.TryGetValue(key, out entries))
        {
            entries = new List<RouteCacheEntry>(1);
            cache[key] = entries;
        }
        entries.Add(new RouteCacheEntry(bytes, created));
        return created;
    }

    private static RoutingId FromSpanCached(ReadOnlySpan<byte> bytes)
    {
        RouteCacheKey key = RouteCacheKey.Create(bytes);
        Dictionary<RouteCacheKey, List<RouteCacheEntry>> cache =
            t_ownedCache ??= new Dictionary<RouteCacheKey, List<RouteCacheEntry>>();
        if (cache.TryGetValue(key, out List<RouteCacheEntry>? entries))
        {
            for (int i = 0; i < entries.Count; i++)
            {
                if (bytes.SequenceEqual(entries[i].Bytes))
                    return entries[i].RoutingId;
            }
        }

        byte[] ownedBytes = bytes.ToArray();
        RoutingId created = new RoutingId(ownedBytes, takeOwnership: true);
        if (cache.Count >= ThreadCacheMaxEntries)
            cache.Clear();
        if (!cache.TryGetValue(key, out entries))
        {
            entries = new List<RouteCacheEntry>(1);
            cache[key] = entries;
        }
        entries.Add(new RouteCacheEntry(ownedBytes, created));
        return created;
    }

    private readonly struct RouteCacheKey : IEquatable<RouteCacheKey>
    {
        private RouteCacheKey(int length, ulong hash)
        {
            Length = length;
            Hash = hash;
        }

        private int Length { get; }
        private ulong Hash { get; }

        internal static RouteCacheKey Create(ReadOnlySpan<byte> bytes)
        {
            const ulong offset = 14695981039346656037UL;
            const ulong prime = 1099511628211UL;
            ulong hash = offset;
            for (int i = 0; i < bytes.Length; i++)
            {
                hash ^= bytes[i];
                hash *= prime;
            }
            return new RouteCacheKey(bytes.Length, hash);
        }

        public bool Equals(RouteCacheKey other)
        {
            return Length == other.Length && Hash == other.Hash;
        }

        public override bool Equals(object? obj)
        {
            return obj is RouteCacheKey other && Equals(other);
        }

        public override int GetHashCode()
        {
            return HashCode.Combine(Length, Hash);
        }
    }

    private sealed class RouteCacheEntry
    {
        internal RouteCacheEntry(byte[] bytes, RoutingId routingId)
        {
            Bytes = bytes;
            RoutingId = routingId;
        }

        internal byte[] Bytes { get; }
        internal RoutingId RoutingId { get; }
    }

    private sealed class NativeRoutingIdBox
    {
        internal NativeRoutingIdBox(ReadOnlySpan<byte> bytes)
        {
            Value = NativeHelpers.WriteRoutingId(bytes);
        }

        internal readonly ZlinkRoutingId Value;
    }
}
