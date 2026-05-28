/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.nativeapi;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;

public final class SendScratch {
    public static final int ROUTING_ID_CACHE_CAPACITY = 256;

    public final Arena arena = Arena.ofConfined();
    public final MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
    public final MemorySegment nativeRoutingId = arena.allocate(
        NativeLayouts.ROUTING_ID_LAYOUT);
    public byte[] cachedRoutingIdBytes;
    public byte[][] cachedRoutingIdKeys;
    public MemorySegment[] cachedRoutingIdSegments;
    public MemorySegment cachedRoutingIdSegment;

    // Publish hot path: the topic id is almost always a small constant string
    // reused for every message. C passes a const char* with zero per-call
    // allocation; cache the last-encoded topic so steady-state publishes do
    // not re-encode UTF-8 / re-allocate native memory per message.
    public String cachedTopicString;
    public MemorySegment cachedTopicSegment;
}
