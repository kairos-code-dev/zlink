/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;


import java.lang.foreign.MemorySegment;

@FunctionalInterface
public interface StreamUInt32RawNativeHandler {
    /**
     * Handles one raw STREAM chunk using callback-local borrowed native message
     * storage.
     *
     * <p>The {@code message} handle is valid only during this callback.
     */
    void onPacket(int routingId, MemorySegment message);
}
