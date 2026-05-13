/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

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
        socket.onPacket((routingId, header, body) ->
            handler.onPacket(routingId, header, body));
    }

    public static void sendFramedPacket(StreamSocket socket,
                                        RoutingId routingId,
                                        Message header,
                                        Message body,
                                        SendFlags flags) {
        Message packet = buildPacketFrame(header, body);
        try (packet) {
            SendFlags effectiveFlags =
                flags == SendFlags.NONE ? SendFlags.DONT_WAIT : flags;
            if (!socket.send(routingId)
                .message(packet)
                .flags(effectiveFlags)
                .submit()) {
                throw new SubmitException(SubmitResult.BACKPRESSURED);
            }
        }
    }

    private static Message buildPacketFrame(Message header, Message body) {
        int headerSize = header.size();
        int bodySize = body.size();
        Message packet = new Message(6 + headerSize + bodySize);
        packet.writeShortBe(0, (short) headerSize);
        packet.writeIntBe(2, bodySize);
        if (headerSize > 0) {
            packet.copyFrom(header, 0, 6, headerSize);
        }
        if (bodySize > 0) {
            packet.copyFrom(body, 0, 6 + headerSize, bodySize);
        }
        return packet;
    }
}
