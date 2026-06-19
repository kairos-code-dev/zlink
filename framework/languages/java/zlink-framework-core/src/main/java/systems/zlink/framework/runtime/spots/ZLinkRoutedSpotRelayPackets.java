package systems.zlink.framework.runtime.spots;

import java.nio.charset.StandardCharsets;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.errors.ZLinkConfigurationException;

public final class ZLinkRoutedSpotRelayPackets {
    public static final String SEND_PACKET_NAME = "__zlink.routed_spot.egress.send";
    public static final String REQUEST_PACKET_NAME = "__zlink.routed_spot.egress.request";

    private ZLinkRoutedSpotRelayPackets() {
    }

    public static List<Message> createSendRelayParts(RoutingId targetSpotRid, List<Message> spotParts) {
        return createRelayParts(SEND_PACKET_NAME, targetSpotRid, spotParts);
    }

    public static List<Message> createRequestRelayParts(RoutingId targetSpotRid, List<Message> spotParts) {
        return createRelayParts(REQUEST_PACKET_NAME, targetSpotRid, spotParts);
    }

    public static RoutingId decodeTargetSpotRid(List<Message> parts) {
        if (parts.size() < 3) {
            throw new ZLinkConfigurationException(
                "routed SPOT relay packet requires metadata and payload parts");
        }
        return RoutingId.from(parts.get(1).toByteArray());
    }

    public static List<Message> copySpotPayloadParts(List<Message> parts) {
        if (parts.size() < 3) {
            throw new ZLinkConfigurationException(
                "routed SPOT relay packet requires payload parts");
        }
        List<Message> payload = new ArrayList<>(parts.size() - 2);
        for (int index = 2; index < parts.size(); index++) {
            payload.add(Message.from(parts.get(index)));
        }
        return payload;
    }

    public static List<Message> copyReplyParts(List<Message> parts) {
        List<Message> reply = new ArrayList<>(parts.size());
        for (Message part : parts) {
            reply.add(Message.from(part));
        }
        return reply;
    }

    public static List<Message> createReplyParts(List<Message> parts) {
        int size = Integer.BYTES;
        List<byte[]> payloads = new ArrayList<>(parts.size());
        for (Message part : parts) {
            byte[] payload = part.toByteArray();
            payloads.add(payload);
            size += Integer.BYTES + payload.length;
        }
        ByteBuffer buffer = ByteBuffer.allocate(size);
        buffer.putInt(payloads.size());
        for (byte[] payload : payloads) {
            buffer.putInt(payload.length);
            buffer.put(payload);
        }
        return List.of(Message.from(buffer.array()));
    }

    public static List<Message> decodeReplyParts(List<Message> parts) {
        if (parts.size() != 1) {
            return copyReplyParts(parts);
        }
        ByteBuffer buffer = ByteBuffer.wrap(parts.get(0).toByteArray());
        if (buffer.remaining() < Integer.BYTES) {
            throw new ZLinkConfigurationException("routed SPOT relay reply is incomplete");
        }
        int count = buffer.getInt();
        if (count < 0) {
            throw new ZLinkConfigurationException("routed SPOT relay reply has invalid part count");
        }
        List<Message> reply = new ArrayList<>(count);
        try {
            for (int index = 0; index < count; index++) {
                if (buffer.remaining() < Integer.BYTES) {
                    throw new ZLinkConfigurationException("routed SPOT relay reply is truncated");
                }
                int length = buffer.getInt();
                if (length < 0 || buffer.remaining() < length) {
                    throw new ZLinkConfigurationException("routed SPOT relay reply has invalid part length");
                }
                byte[] payload = new byte[length];
                buffer.get(payload);
                reply.add(Message.from(payload));
            }
            if (buffer.hasRemaining()) {
                throw new ZLinkConfigurationException("routed SPOT relay reply has trailing bytes");
            }
            return reply;
        } catch (RuntimeException ex) {
            reply.forEach(Message::close);
            throw ex;
        }
    }

    private static List<Message> createRelayParts(
        String packetName,
        RoutingId targetSpotRid,
        List<Message> spotParts) {
        List<Message> relayParts = new ArrayList<>(spotParts.size() + 2);
        relayParts.add(Message.from(packetName.getBytes(StandardCharsets.UTF_8)));
        relayParts.add(Message.from(targetSpotRid.toBytes()));
        for (Message spotPart : spotParts) {
            relayParts.add(Message.from(spotPart));
        }
        return relayParts;
    }
}
