/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.messaging;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public final class RecvScratch {
    public static final int TOPIC_CAPACITY = 256;

    public final Arena arena = Arena.ofAuto();
    public final MemorySegment sourceRidOut = arena.allocate(ValueLayout.ADDRESS);
    public final MemorySegment hasMoreOut = arena.allocate(ValueLayout.JAVA_INT);

    // Subscribe hot path: persistent topic-out buffers (C uses a stack
    // char[256] with zero per-message allocation) plus a last-topic cache so
    // a steady stream on one constant topic does not re-decode a String per
    // message.
    public final MemorySegment topicOut = arena.allocate(TOPIC_CAPACITY);
    public final MemorySegment topicLenOut = arena.allocate(ValueLayout.JAVA_LONG);
    public byte[] cachedTopicBytes;
    public String cachedTopicString = "";
}
