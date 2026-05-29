/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.nativeapi;

import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import systems.zlink.contracts.core.RoutingId;

public final class NativeRoutingIds {
    private NativeRoutingIds() {}

    public static RoutingId readOut(MemorySegment nativeRidOut) {
        MemorySegment nativeRid = nativeRidOut.get(ValueLayout.ADDRESS, 0);
        if (nativeRid.address() == 0) {
            return null;
        }
        nativeRid = nativeRid.reinterpret(
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize());
        int size = nativeRid.get(ValueLayout.JAVA_BYTE,
            NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
        if (size == 0) {
            return null;
        }
        if (size <= 16) {
            long lo = nativeRid.get(ValueLayout.JAVA_LONG_UNALIGNED,
                NativeLayouts.ROUTING_ID_DATA_OFFSET);
            long hi = size > 8
                ? nativeRid.get(ValueLayout.JAVA_LONG_UNALIGNED,
                    NativeLayouts.ROUTING_ID_DATA_OFFSET + 8)
                : 0L;
            int loBits = (size >= 8 ? 8 : size) * 8;
            long loMask = loBits == 64 ? -1L : ((1L << loBits) - 1L);
            lo &= loMask;
            int hiBytes = size > 8 ? size - 8 : 0;
            int hiBits = hiBytes * 8;
            long hiMask = hiBits == 64 ? -1L
                : (hiBits == 0 ? 0L : ((1L << hiBits) - 1L));
            hi &= hiMask;
            RoutingId cached = RoutingId.tryFromInlineCached(size, lo, hi);
            if (cached != null) {
                return cached;
            }
        }
        return read(nativeRid);
    }

    public static byte[] readBytesOut(MemorySegment nativeRidOut) {
        MemorySegment nativeRid = nativeRidOut.get(ValueLayout.ADDRESS, 0);
        if (nativeRid.address() == 0) {
            return null;
        }
        nativeRid = nativeRid.reinterpret(
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize());
        int size = nativeRid.get(ValueLayout.JAVA_BYTE,
            NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
        if (size == 0) {
            return null;
        }
        byte[] value = new byte[size];
        MemorySegment.copy(nativeRid, NativeLayouts.ROUTING_ID_DATA_OFFSET,
            MemorySegment.ofArray(value), 0, size);
        return value;
    }

    public static RoutingId read(MemorySegment nativeRid) {
        if (nativeRid == null || nativeRid.address() == 0) {
            return null;
        }
        if (nativeRid.byteSize() < NativeLayouts.ROUTING_ID_LAYOUT.byteSize()) {
            nativeRid = nativeRid.reinterpret(
                NativeLayouts.ROUTING_ID_LAYOUT.byteSize());
        }
        int size = nativeRid.get(ValueLayout.JAVA_BYTE,
            NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
        if (size == 0) {
            return null;
        }
        byte[] value = new byte[size];
        MemorySegment.copy(nativeRid, NativeLayouts.ROUTING_ID_DATA_OFFSET,
            MemorySegment.ofArray(value), 0, size);
        return InternalAccess.routingIdFromTrusted(value);
    }
}
