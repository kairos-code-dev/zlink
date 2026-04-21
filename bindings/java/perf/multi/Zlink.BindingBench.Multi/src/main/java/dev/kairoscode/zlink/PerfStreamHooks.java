/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

/**
 * Perf-only accessors for non-public stream hooks.
 */
public final class PerfStreamHooks {
    private PerfStreamHooks() {
    }

    @FunctionalInterface
    public interface FramedPacketHandler {
        int onPacket(RoutingId routingId, Message header, Message body);
    }

    public static void attachFramedPacketHandler(StreamSocket socket,
                                                 FramedPacketHandler handler) {
        socket.onFramedPacket((StreamFramedPacketHandler)
            (routingId, header, body) ->
                handler.onPacket(routingId, header, body));
    }
}
