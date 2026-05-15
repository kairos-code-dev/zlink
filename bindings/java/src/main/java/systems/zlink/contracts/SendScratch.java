/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

import systems.zlink.runtime.nativebridge.NativeLayouts;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;

final class SendScratch {
    static final int ROUTING_ID_CACHE_CAPACITY = 256;

    final Arena arena = Arena.ofConfined();
    final MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
    final MemorySegment nativeRoutingId = arena.allocate(
        NativeLayouts.ROUTING_ID_LAYOUT);
    byte[] cachedRoutingIdBytes;
    byte[][] cachedRoutingIdKeys;
    MemorySegment[] cachedRoutingIdSegments;
    MemorySegment cachedRoutingIdSegment;
}
