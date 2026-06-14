// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

public readonly partial struct RoutingId
{
    private const int ThreadCacheMaxEntries = 256;
    private const int InlineDirectCacheEntries = 256;

    [ThreadStatic]
    private static Dictionary<RouteCacheKey, List<RouteCacheEntry>>? t_ownedCache;
    [ThreadStatic]
    private static InlineRouteCacheEntry[]? t_inlineDirectCache;

    internal static RoutingId? FromOptionalBytes(ReadOnlySpan<byte> bytes)
    {
        return bytes.Length == 0 ? null : From(bytes);
    }

    internal byte[] ToByteArray()
    {
        return _bytes?.ToArray() ?? Array.Empty<byte>();
    }

    internal static RoutingId? FromOwnedOptionalBytes(byte[] bytes)
    {
        if (bytes.Length == 0)
            return null;
        Validate(bytes, nameof(bytes));
        return FromOwnedBytesCached(bytes);
    }

    internal static RoutingId? TryFromInlineCached(int size, ulong lo, ulong hi)
    {
        if (size <= 0 || size > 16)
            return null;
        ulong hash = RouteHash.Fnv1aInline(size, lo, hi);
        RoutingId? direct = TryFromInlineDirectCache(size, lo, hi, hash);
        if (direct != null)
            return direct;
        RouteCacheKey key = RouteCacheKey.FromHash(size, hash);
        Dictionary<RouteCacheKey, List<RouteCacheEntry>>? cache = t_ownedCache;
        if (cache == null || !cache.TryGetValue(key,
                out List<RouteCacheEntry>? entries))
        {
            return null;
        }
        for (int i = 0; i < entries.Count; i++)
        {
            byte[] entryBytes = entries[i].Bytes;
            if (entryBytes.Length != size)
                continue;
            if (!InlineMatchesBytes(size, lo, hi, entryBytes))
                continue;
            StoreInlineDirectCache(size, lo, hi, hash, entries[i].RoutingId);
            return entries[i].RoutingId;
        }
        return null;
    }

    private static bool InlineMatchesBytes(int size, ulong lo, ulong hi,
        byte[] entryBytes)
    {
        for (int i = 0; i < size && i < 8; i++)
        {
            if (entryBytes[i] != (byte)(lo >> (i * 8)))
                return false;
        }
        for (int i = 8; i < size; i++)
        {
            if (entryBytes[i] != (byte)(hi >> ((i - 8) * 8)))
                return false;
        }
        return true;
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
        return NativeHelpers.WriteRoutingId(ToBytes());
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal ref ZlinkRoutingId ToNativeRef(ref ZlinkRoutingId fallback)
    {
        fallback = NativeHelpers.WriteRoutingId(ToBytes());
        return ref fallback;
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
        StoreInlineDirectCache(bytes, key.Hash, created);
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
        StoreInlineDirectCache(ownedBytes, key.Hash, created);
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
        internal ulong Hash { get; }

        internal static RouteCacheKey FromHash(int length, ulong hash)
        {
            return new RouteCacheKey(length, hash);
        }

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
        internal RouteCacheEntry(byte[] bytes, RoutingId routingId)
        {
            Bytes = bytes;
            RoutingId = routingId;
        }

        internal byte[] Bytes { get; }
        internal RoutingId RoutingId { get; }
    }

    private static RoutingId? TryFromInlineDirectCache(int size, ulong lo,
        ulong hi, ulong hash)
    {
        InlineRouteCacheEntry[]? cache = t_inlineDirectCache;
        if (cache == null)
            return null;

        ref InlineRouteCacheEntry entry = ref cache[(int)hash
            & (InlineDirectCacheEntries - 1)];
        if (entry.RoutingId == null || entry.Size != size
            || entry.Hash != hash || entry.Lo != lo || entry.Hi != hi)
        {
            return null;
        }
        return entry.RoutingId;
    }

    private static void StoreInlineDirectCache(byte[] bytes, ulong hash,
        RoutingId routingId)
    {
        if (bytes.Length <= 0 || bytes.Length > 16)
            return;

        ulong lo = 0;
        ulong hi = 0;
        for (int i = 0; i < bytes.Length && i < 8; i++)
            lo |= (ulong)bytes[i] << (i * 8);
        for (int i = 8; i < bytes.Length; i++)
            hi |= (ulong)bytes[i] << ((i - 8) * 8);
        StoreInlineDirectCache(bytes.Length, lo, hi, hash, routingId);
    }

    private static void StoreInlineDirectCache(int size, ulong lo, ulong hi,
        ulong hash, RoutingId routingId)
    {
        InlineRouteCacheEntry[] cache = t_inlineDirectCache
            ??= new InlineRouteCacheEntry[InlineDirectCacheEntries];
        cache[(int)hash & (InlineDirectCacheEntries - 1)] =
            new InlineRouteCacheEntry(size, hash, lo, hi, routingId);
    }

    private readonly record struct InlineRouteCacheEntry(int Size, ulong Hash,
        ulong Lo, ulong Hi, RoutingId? RoutingId);

    private sealed class NativeRoutingIdBox
    {
        internal NativeRoutingIdBox(ReadOnlySpan<byte> bytes)
        {
            Value = NativeHelpers.WriteRoutingId(bytes);
        }

        internal ZlinkRoutingId Value;

        internal ref ZlinkRoutingId RefValue
        {
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            get => ref Value;
        }
    }
}
