// SPDX-License-Identifier: MPL-2.0

using System.Runtime.CompilerServices;
using System.Text;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal static class RoutingIdCodec
{
    private const string HexPrefix = "hex:";
    private const int ThreadCacheMaxEntries = 256;
    private const int SharedCacheMaxKeys = 4096;
    private static readonly object PublicCacheLock = new();
    private static readonly object CanonicalCacheLock = new();
    private static readonly object RoutingCacheLock = new();

    [ThreadStatic] private static Dictionary<RouteCacheKey, List<byte[]>>? t_ownedBytesCache;

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

        var key = RouteCacheKey.Create(routingId);
        lock (PublicCacheLock)
        {
            if (ByteToPublicCache.TryGetValue(key, out var entries))
                for (var i = 0; i < entries.Count; i++)
                    if (routingId.SequenceEqual(entries[i].Bytes))
                        return entries[i].Public;
        }

        var utf8 = Encoding.UTF8.GetString(routingId);
        var publicValue = IsPrintableUtf8Roundtrip(utf8, routingId)
            ? utf8
            : HexPrefix + Convert.ToHexString(routingId);
        var copy = routingId.ToArray();

        lock (PublicCacheLock)
        {
            TrimPublicCachesIfNeeded();
            if (!ByteToPublicCache.TryGetValue(key, out var entries))
            {
                entries = new List<RouteCacheEntry>(1);
                ByteToPublicCache[key] = entries;
            }

            for (var i = 0; i < entries.Count; i++)
                if (copy.AsSpan().SequenceEqual(entries[i].Bytes))
                    return entries[i].Public;

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

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
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

        var bytes = new byte[size];
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

    internal static unsafe ZlinkRoutingId ToNative(uint routingId)
    {
        ZlinkRoutingId native = default;
        native.Size = 4;
        native.Data[0] = (byte)(routingId >> 24);
        native.Data[1] = (byte)(routingId >> 16);
        native.Data[2] = (byte)(routingId >> 8);
        native.Data[3] = (byte)routingId;
        return native;
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
            throw new ArgumentOutOfRangeException(paramName,
                "routingId must not be empty.");

        lock (PublicCacheLock)
        {
            if (PublicToByteCache.TryGetValue(routingId, out var cached))
                return cached;
        }

        byte[] bytes;
        if (routingId.StartsWith(HexPrefix, StringComparison.OrdinalIgnoreCase))
        {
            var hex = routingId.Substring(HexPrefix.Length);
            if (hex.Length == 0 || (hex.Length & 1) != 0)
                throw new ArgumentException(
                    "Hex routingId must contain an even number of digits.",
                    paramName);

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
                throw new ArgumentOutOfRangeException(paramName,
                    "routingId length must be between 1 and 255 bytes.");
        }
        else
        {
            var byteCount = Encoding.UTF8.GetByteCount(routingId);
            if (byteCount <= 0 || byteCount > 255)
                throw new ArgumentOutOfRangeException(paramName,
                    "routingId UTF-8 length must be between 1 and 255 bytes.");

            bytes = new byte[byteCount];
            Encoding.UTF8.GetBytes(routingId, bytes.AsSpan());
        }

        lock (PublicCacheLock)
        {
            TrimPublicCachesIfNeeded();
            if (!PublicToByteCache.TryGetValue(routingId, out var cached))
                PublicToByteCache[routingId] = bytes;
        }

        return bytes;
    }

    private static bool IsPrintableUtf8Roundtrip(string text,
        ReadOnlySpan<byte> original)
    {
        for (var i = 0; i < text.Length; i++)
            if (char.IsControl(text[i]))
                return false;

        var byteCount = Encoding.UTF8.GetByteCount(text);
        if (byteCount != original.Length)
            return false;

        var roundtrip = new byte[byteCount];
        Encoding.UTF8.GetBytes(text, roundtrip.AsSpan());
        return roundtrip.AsSpan().SequenceEqual(original);
    }

    private static byte[] Canonicalize(ReadOnlySpan<byte> routingId)
    {
        var key = RouteCacheKey.Create(routingId);
        lock (CanonicalCacheLock)
        {
            TrimCanonicalCacheIfNeeded();
            if (ByteCanonicalCache.TryGetValue(key,
                    out var cachedEntries))
                for (var i = 0; i < cachedEntries.Count; i++)
                    if (routingId.SequenceEqual(cachedEntries[i]))
                        return cachedEntries[i];

            var copy = routingId.ToArray();
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
        var key = RouteCacheKey.Create(routingId);
        var cache =
            t_ownedBytesCache ??= new Dictionary<RouteCacheKey, List<byte[]>>();
        if (cache.TryGetValue(key, out var cachedEntries))
            for (var i = 0; i < cachedEntries.Count; i++)
                if (routingId.SequenceEqual(cachedEntries[i]))
                    return cachedEntries[i];

        var copy = routingId.ToArray();
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
        var canonical = Canonicalize(routingId);
        var key = RouteCacheKey.Create(canonical);
        lock (RoutingCacheLock)
        {
            TrimRoutingCacheIfNeeded();
            if (ByteToRoutingCache.TryGetValue(key,
                    out var cachedEntries))
                for (var i = 0; i < cachedEntries.Count; i++)
                    if (ReferenceEquals(canonical, cachedEntries[i].Bytes)
                        || canonical.AsSpan().SequenceEqual(cachedEntries[i].Bytes))
                        return cachedEntries[i].RoutingId;

            var created = RoutingId.FromOwnedOptionalBytes(canonical)
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

    private static void TrimPublicCachesIfNeeded()
    {
        if (ByteToPublicCache.Count < SharedCacheMaxKeys
            && PublicToByteCache.Count < SharedCacheMaxKeys)
            return;

        ByteToPublicCache.Clear();
        PublicToByteCache.Clear();
    }

    private static void TrimCanonicalCacheIfNeeded()
    {
        if (ByteCanonicalCache.Count < SharedCacheMaxKeys)
            return;

        ByteCanonicalCache.Clear();
    }

    private static void TrimRoutingCacheIfNeeded()
    {
        if (ByteToRoutingCache.Count < SharedCacheMaxKeys)
            return;

        ByteToRoutingCache.Clear();
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
            return new RouteCacheKey(bytes.Length, RouteHash.Fnv1a(bytes));
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