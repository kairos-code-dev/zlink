package systems.zlink.framework.runtime.spots;

import java.nio.charset.StandardCharsets;
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
