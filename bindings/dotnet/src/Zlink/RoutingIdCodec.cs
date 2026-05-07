// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Text;
using Systems.Zlink.Native;

namespace Systems.Zlink;

internal static class RoutingIdCodec
{
    private const string HexPrefix = "hex:";
    private const int ThreadCacheMaxEntries = 256;
    private static readonly object ByteCacheLock = new();
    [ThreadStatic]
    private static Dictionary<RouteCacheKey, List<byte[]>>? t_ownedBytesCache;
    private static readonly Dictionary<RouteCacheKey, List<RouteCacheEntry>>
        ByteToPublicCache = new();
    private static readonly Dictionary<RouteCacheKey, List<byte[]>>
        ByteCanonicalCache = new();
    private static readonly Dictionary<RouteCacheKey, List<RouteRoutingEntry>>
        ByteToRoutingCache = new();
    private static readonly Dictionary<string, byte[]> PublicToByteCache =
        new(StringComparer.Ordinal);

    internal static string ToPublicString(ReadOnlySpan<byte> routingId)
    {
        if (routingId.Length == 0)
            return string.Empty;

        RouteCacheKey key = RouteCacheKey.Create(routingId);
        lock (ByteCacheLock)
        {
            if (ByteToPublicCache.TryGetValue(key, out List<RouteCacheEntry>? entries))
            {
                for (int i = 0; i < entries.Count; i++)
                {
                    if (routingId.SequenceEqual(entries[i].Bytes))
                        return entries[i].Public;
                }
            }
        }

        string utf8 = Encoding.UTF8.GetString(routingId);
        string publicValue = IsPrintableUtf8Roundtrip(utf8, routingId)
            ? utf8
            : HexPrefix + Convert.ToHexString(routingId);
        byte[] copy = routingId.ToArray();

        lock (ByteCacheLock)
        {
            if (!ByteToPublicCache.TryGetValue(key, out List<RouteCacheEntry>? entries))
            {
                entries = new List<RouteCacheEntry>(1);
                ByteToPublicCache[key] = entries;
            }

            for (int i = 0; i < entries.Count; i++)
            {
                if (copy.AsSpan().SequenceEqual(entries[i].Bytes))
                    return entries[i].Public;
            }

            entries.Add(new RouteCacheEntry(copy, publicValue));
            if (!PublicToByteCache.ContainsKey(publicValue))
                PublicToByteCache[publicValue] = copy;
        }

        return publicValue;
    }

    internal static RoutingId? ToRoutingId(ReadOnlySpan<byte> routingId)
    {
        return routingId.Length == 0 ? null : ToRoutingIdCached(routingId);
    }

    internal static unsafe RoutingId? ToRoutingId(ref ZlinkRoutingId routingId)
    {
        int size = routingId.Size;
        if (size <= 0)
            return null;

        fixed (byte* src = routingId.Data)
        {
            return ToRoutingIdCached(new ReadOnlySpan<byte>(src, size));
        }
    }

    internal static RoutingId? ToRoutingId(byte[] routingId)
    {
        return routingId.Length == 0 ? null : ToRoutingIdCached(routingId);
    }

    internal static byte[] FromRoutingId(RoutingId routingId)
    {
        return routingId.AsByteArrayUnsafe();
    }

    internal static unsafe byte[]? ToOwnedBytes(ref ZlinkRoutingId routingId)
    {
        int size = routingId.Size;
        if (size <= 0)
            return null;

        fixed (byte* src = routingId.Data)
        {
            return CanonicalizeThreadLocal(new ReadOnlySpan<byte>(src, size));
        }
    }

    internal static unsafe byte[]? CopyOwnedBytes(ref ZlinkRoutingId routingId)
    {
        int size = routingId.Size;
        if (size <= 0)
            return null;

        byte[] bytes = new byte[size];
        fixed (byte* src = routingId.Data)
        {
            new ReadOnlySpan<byte>(src, size).CopyTo(bytes);
        }
        return bytes;
    }

    internal static byte[] FromUInt32(uint routingId)
    {
        return
        [
            (byte)(routingId >> 24),
            (byte)(routingId >> 16),
            (byte)(routingId >> 8),
            (byte)routingId
        ];
    }

    internal static bool TryToUInt32(ReadOnlySpan<byte> routingId,
        out uint value)
    {
        if (routingId.Length != 4)
        {
            value = 0;
            return false;
        }

        value = ((uint)routingId[0] << 24)
              | ((uint)routingId[1] << 16)
              | ((uint)routingId[2] << 8)
              | routingId[3];
        return true;
    }

    internal static byte[] FromPublicString(string routingId, string paramName)
    {
        if (routingId == null)
            throw new ArgumentNullException(paramName);
        if (routingId.Length == 0)
        {
            throw new ArgumentOutOfRangeException(paramName,
                "routingId must not be empty.");
        }

        lock (ByteCacheLock)
        {
            if (PublicToByteCache.TryGetValue(routingId, out byte[]? cached))
                return cached;
        }

        byte[] bytes;
        if (routingId.StartsWith(HexPrefix, StringComparison.OrdinalIgnoreCase))
        {
            string hex = routingId.Substring(HexPrefix.Length);
            if (hex.Length == 0 || (hex.Length & 1) != 0)
            {
                throw new ArgumentException(
                    "Hex routingId must contain an even number of digits.",
                    paramName);
            }

            try
            {
                bytes = Convert.FromHexString(hex);
            }
            catch (FormatException ex)
            {
                throw new ArgumentException(
                    "Invalid hex routingId format.",
                    paramName, ex);
            }

            if (bytes.Length == 0 || bytes.Length > 255)
            {
                throw new ArgumentOutOfRangeException(paramName,
                    "routingId length must be between 1 and 255 bytes.");
            }
        }
        else
        {
            int byteCount = Encoding.UTF8.GetByteCount(routingId);
            if (byteCount <= 0 || byteCount > 255)
            {
                throw new ArgumentOutOfRangeException(paramName,
                    "routingId UTF-8 length must be between 1 and 255 bytes.");
            }

            bytes = new byte[byteCount];
            Encoding.UTF8.GetBytes(routingId, bytes.AsSpan());
        }

        lock (ByteCacheLock)
        {
            if (!PublicToByteCache.TryGetValue(routingId, out byte[]? cached))
                PublicToByteCache[routingId] = bytes;
        }
        return bytes;
    }

    private static bool IsPrintableUtf8Roundtrip(string text,
        ReadOnlySpan<byte> original)
    {
        for (int i = 0; i < text.Length; i++)
        {
            if (char.IsControl(text[i]))
                return false;
        }

        int byteCount = Encoding.UTF8.GetByteCount(text);
        if (byteCount != original.Length)
            return false;

        byte[] roundtrip = new byte[byteCount];
        Encoding.UTF8.GetBytes(text, roundtrip.AsSpan());
        return roundtrip.AsSpan().SequenceEqual(original);
    }

    private static byte[] Canonicalize(ReadOnlySpan<byte> routingId)
    {
        RouteCacheKey key = RouteCacheKey.Create(routingId);
        lock (ByteCacheLock)
        {
            if (ByteCanonicalCache.TryGetValue(key,
                out List<byte[]>? cachedEntries))
            {
                for (int i = 0; i < cachedEntries.Count; i++)
                {
                    if (routingId.SequenceEqual(cachedEntries[i]))
                        return cachedEntries[i];
                }
            }

            byte[] copy = routingId.ToArray();
            if (!ByteCanonicalCache.TryGetValue(key, out cachedEntries))
            {
                cachedEntries = new List<byte[]>(1);
                ByteCanonicalCache[key] = cachedEntries;
            }

            cachedEntries.Add(copy);
            return copy;
        }
    }

    private static byte[] CanonicalizeThreadLocal(ReadOnlySpan<byte> routingId)
    {
        RouteCacheKey key = RouteCacheKey.Create(routingId);
        Dictionary<RouteCacheKey, List<byte[]>> cache =
            t_ownedBytesCache ??= new Dictionary<RouteCacheKey, List<byte[]>>();
        if (cache.TryGetValue(key, out List<byte[]>? cachedEntries))
        {
            for (int i = 0; i < cachedEntries.Count; i++)
            {
                if (routingId.SequenceEqual(cachedEntries[i]))
                    return cachedEntries[i];
            }
        }

        byte[] copy = routingId.ToArray();
        if (cache.Count >= ThreadCacheMaxEntries)
            cache.Clear();
        if (!cache.TryGetValue(key, out cachedEntries))
        {
            cachedEntries = new List<byte[]>(1);
            cache[key] = cachedEntries;
        }

        cachedEntries.Add(copy);
        return copy;
    }

    private static RoutingId ToRoutingIdCached(ReadOnlySpan<byte> routingId)
    {
        byte[] canonical = Canonicalize(routingId);
        RouteCacheKey key = RouteCacheKey.Create(canonical);
        lock (ByteCacheLock)
        {
            if (ByteToRoutingCache.TryGetValue(key,
                out List<RouteRoutingEntry>? cachedEntries))
            {
                for (int i = 0; i < cachedEntries.Count; i++)
                {
                    if (ReferenceEquals(canonical, cachedEntries[i].Bytes)
                        || canonical.AsSpan().SequenceEqual(cachedEntries[i].Bytes))
                    {
                        return cachedEntries[i].RoutingId;
                    }
                }
            }

            RoutingId created = RoutingId.FromOwnedOptionalBytes(canonical)
                ?? throw new InvalidOperationException("routingId must not be empty.");
            if (!ByteToRoutingCache.TryGetValue(key, out cachedEntries))
            {
                cachedEntries = new List<RouteRoutingEntry>(1);
                ByteToRoutingCache[key] = cachedEntries;
            }

            cachedEntries.Add(new RouteRoutingEntry(canonical, created));
            return created;
        }
    }

    private readonly struct RouteCacheKey : IEquatable<RouteCacheKey>
    {
        private RouteCacheKey(int length, ulong hash)
        {
            Length = length;
            Hash = hash;
        }

        internal int Length { get; }
        internal ulong Hash { get; }

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
        internal RouteCacheEntry(byte[] bytes, string @public)
        {
            Bytes = bytes;
            Public = @public;
        }

        internal byte[] Bytes { get; }
        internal string Public { get; }
    }

    private sealed class RouteRoutingEntry
    {
        internal RouteRoutingEntry(byte[] bytes, RoutingId routingId)
        {
            Bytes = bytes;
            RoutingId = routingId;
        }

        internal byte[] Bytes { get; }
        internal RoutingId RoutingId { get; }
    }
}
