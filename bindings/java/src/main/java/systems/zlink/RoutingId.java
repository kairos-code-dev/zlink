/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Objects;

/** Immutable binary-safe routing id value object. */
public final class RoutingId {
    public static final int MAX_LENGTH = 255;
    private static final int THREAD_CACHE_MAX_ENTRIES = 256;
    private static final ThreadLocal<HashMap<RouteCacheKey, List<RouteCacheEntry>>>
        TRUSTED_CACHE = ThreadLocal.withInitial(HashMap::new);

    private final byte[] value;
    private final int hash;

    private RoutingId(byte[] value) {
        this.value = value;
        this.hash = Arrays.hashCode(value);
    }

    static RoutingId fromTrusted(byte[] value) {
        Objects.requireNonNull(value, "value");
        validateLength(value.length);
        RouteCacheKey key = RouteCacheKey.create(value);
        HashMap<RouteCacheKey, List<RouteCacheEntry>> cache = TRUSTED_CACHE.get();
        List<RouteCacheEntry> entries = cache.get(key);
        if (entries != null) {
            for (int i = 0; i < entries.size(); i++) {
                RouteCacheEntry entry = entries.get(i);
                if (Arrays.equals(value, entry.bytes)) {
                    return entry.routingId;
                }
            }
        }

        RoutingId created = new RoutingId(value);
        if (cache.size() >= THREAD_CACHE_MAX_ENTRIES) {
            cache.clear();
        }
        entries = cache.get(key);
        if (entries == null) {
            entries = new ArrayList<>(1);
            cache.put(key, entries);
        }
        entries.add(new RouteCacheEntry(value, created));
        return created;
    }

    // Inline-keyed cache lookup for the recv hot path. The caller already
    // has the routing id bytes packed into (lo, hi, size); doing the hash and
    // SequenceEqual against the cached entries directly off those words
    // avoids the byte[] allocation that is otherwise discarded on cache hits.
    // Returns null on miss; caller must fall back to allocating a byte[] and
    // calling fromTrusted(byte[]).
    static RoutingId tryFromInlineCached(int size, long lo, long hi) {
        if (size <= 0 || size > 16) {
            return null;
        }
        final long offset = 0xcbf29ce484222325L;
        final long prime = 0x100000001b3L;
        long hash = offset;
        for (int i = 0; i < size && i < 8; i++) {
            hash ^= ((lo >>> (i * 8)) & 0xFFL);
            hash *= prime;
        }
        for (int i = 8; i < size; i++) {
            hash ^= ((hi >>> ((i - 8) * 8)) & 0xFFL);
            hash *= prime;
        }
        RouteCacheKey key = new RouteCacheKey(size, hash);
        HashMap<RouteCacheKey, List<RouteCacheEntry>> cache = TRUSTED_CACHE.get();
        List<RouteCacheEntry> entries = cache.get(key);
        if (entries == null) {
            return null;
        }
        for (int i = 0; i < entries.size(); i++) {
            RouteCacheEntry entry = entries.get(i);
            if (entry.bytes.length != size) continue;
            if (!inlineMatchesBytes(size, lo, hi, entry.bytes)) continue;
            return entry.routingId;
        }
        return null;
    }

    private static boolean inlineMatchesBytes(int size, long lo, long hi,
                                              byte[] entryBytes) {
        for (int i = 0; i < size && i < 8; i++) {
            if (entryBytes[i] != (byte) (lo >>> (i * 8))) return false;
        }
        for (int i = 8; i < size; i++) {
            if (entryBytes[i] != (byte) (hi >>> ((i - 8) * 8))) return false;
        }
        return true;
    }

    /** Copies the full routing id byte array. */
    public static RoutingId fromBytes(byte[] value) {
        Objects.requireNonNull(value, "value");
        validateLength(value.length);
        return new RoutingId(Arrays.copyOf(value, value.length));
    }

    /** Copies the selected routing id byte range. */
    public static RoutingId fromBytes(byte[] value, int offset, int length) {
        Objects.requireNonNull(value, "value");
        if (offset < 0 || length < 0 || offset > value.length - length)
            throw new IndexOutOfBoundsException("value range out of bounds");
        validateLength(length);
        return new RoutingId(Arrays.copyOfRange(value, offset, offset + length));
    }

    /** Parses the lowercase or uppercase hex string returned by {@link #toHex()}. */
    public static RoutingId fromString(String value) {
        Objects.requireNonNull(value, "value");
        if (value.isEmpty() || (value.length() & 1) != 0) {
            throw new IllegalArgumentException(
                "routing id string must be a non-empty even-length hex string");
        }
        if (value.length() > MAX_LENGTH * 2) {
            throw new IllegalArgumentException(
                "routing id string must decode to at most 255 bytes");
        }
        byte[] bytes = new byte[value.length() / 2];
        for (int i = 0; i < bytes.length; i++) {
            int high = Character.digit(value.charAt(i * 2), 16);
            int low = Character.digit(value.charAt(i * 2 + 1), 16);
            if (high < 0 || low < 0) {
                throw new IllegalArgumentException(
                    "routing id string must contain only hex digits");
            }
            bytes[i] = (byte) ((high << 4) | low);
        }
        validateLength(bytes.length);
        return new RoutingId(bytes);
    }

    /** Creates a 4-byte big-endian routing id from an unsigned 32-bit value. */
    static RoutingId fromU32(int value) {
        return new RoutingId(new byte[] {
            (byte) (value >>> 24),
            (byte) (value >>> 16),
            (byte) (value >>> 8),
            (byte) value
        });
    }

    private static void validateLength(int length) {
        if (length > MAX_LENGTH) {
            throw new IllegalArgumentException(
                "routing id too long: " + length + " > " + MAX_LENGTH);
        }
    }

    /** Returns a defensive copy of the routing id bytes. */
    public byte[] toBytes() {
        return Arrays.copyOf(value, value.length);
    }

    byte[] trustedBytes() {
        return value;
    }

    /** Returns a read-only buffer view over the routing id bytes. */
    ByteBuffer asReadOnlyBuffer() {
        return ByteBuffer.wrap(value).asReadOnlyBuffer();
    }

    /** Returns the routing id byte length. */
    public int size() {
        return value.length;
    }

    /** Returns whether the routing id is empty. */
    boolean empty() {
        return value.length == 0;
    }

    public String toHex() {
        StringBuilder out = new StringBuilder(value.length * 2);
        for (byte b : value) {
            out.append(Character.forDigit((b >>> 4) & 0xF, 16));
            out.append(Character.forDigit(b & 0xF, 16));
        }
        return out.toString();
    }

    @Override
    public boolean equals(Object other) {
        if (this == other)
            return true;
        if (!(other instanceof RoutingId rid))
            return false;
        if (hash != rid.hash || value.length != rid.value.length)
            return false;
        return Arrays.equals(value, rid.value);
    }

    @Override
    public int hashCode() {
        return hash;
    }

    private record RouteCacheKey(int length, long hash) {
        private static RouteCacheKey create(byte[] bytes) {
            final long offset = 0xcbf29ce484222325L;
            final long prime = 0x100000001b3L;
            long hash = offset;
            for (byte b : bytes) {
                hash ^= (b & 0xFFL);
                hash *= prime;
            }
            return new RouteCacheKey(bytes.length, hash);
        }
    }

    private record RouteCacheEntry(byte[] bytes, RoutingId routingId) {
    }

}
