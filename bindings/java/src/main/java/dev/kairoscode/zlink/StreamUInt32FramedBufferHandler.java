/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.nio.ByteBuffer;

@FunctionalInterface
public interface StreamUInt32FramedBufferHandler {
    /**
     * Handles one framed STREAM packet using callback-local borrowed views.
     *
     * <p>The {@code header} and {@code body} buffers are valid only during this
     * callback. Copy the bytes explicitly if they must outlive the callback.
     */
    void onPacket(int routingId, ByteBuffer header, ByteBuffer body);
}
