/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.lang.foreign.MemorySegment;

@FunctionalInterface
public interface StreamUInt32FramedNativeHandler {
    /**
     * Handles one framed STREAM packet using callback-local borrowed native
     * message handles.
     *
     * <p>The {@code headerMsg} and {@code bodyMsg} handles are valid only
     * during this callback. Copy payload bytes explicitly if they must outlive
     * the callback.
     */
    void onPacket(int routingId, MemorySegment headerMsg, MemorySegment bodyMsg);
}
