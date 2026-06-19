package systems.zlink.framework.runtime.actors;

import java.nio.charset.StandardCharsets;
import java.util.Base64;
import java.util.List;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderCodec;
import systems.zlink.framework.streams.ZLinkStreamHeader;

public final class ZLinkActorSpotRoutePackets {
    public static final String JOIN_SPOT_PACKET_NAME = "__zlink.actor.joinSpot";
    public static final String BOUND_SESSION_SEND_PACKET_NAME = "__zlink.actor.boundSession.send";
    public static final String ACTOR_PACKET_NAME = "__zlink.actor.packet";

    private ZLinkActorSpotRoutePackets() {
    }

    public static Message encodeJoinRequest(
        String actorId,
        String actorType,
        ZLinkBackendActorRef actorRef,
        RoutingId sourceEntrySpotRid) {
        return Message.from(String.join(
            "\n",
            actorId,
            actorType,
            actorRef.nodeRid().toString(),
            Long.toUnsignedString(actorRef.epoch()),
            sourceEntrySpotRid.toString()).getBytes(StandardCharsets.UTF_8));
    }

    public static List<Message> createJoinRequestParts(
        String actorId,
        String actorType,
        ZLinkBackendActorRef actorRef,
        RoutingId sourceEntrySpotRid,
        Message joinPayload) {
        return List.of(
            Message.from(JOIN_SPOT_PACKET_NAME.getBytes(StandardCharsets.UTF_8)),
            encodeJoinRequest(actorId, actorType, actorRef, sourceEntrySpotRid),
            Message.from(joinPayload));
    }

    public static List<Message> createBoundSessionSendParts(
        ZLinkBackendActorRef actorRef,
        Message frame) {
        return List.of(
            Message.from(BOUND_SESSION_SEND_PACKET_NAME.getBytes(StandardCharsets.UTF_8)),
            Message.from(String.join(
                "\n",
                actorRef.nodeRid().toString(),
                actorRef.actorId(),
                Long.toUnsignedString(actorRef.epoch())).getBytes(StandardCharsets.UTF_8)),
            Message.from(frame));
    }

    public static List<Message> createActorPacketParts(
        ZLinkBackendActorRef actorRef,
        ZLinkStreamHeader header,
        Message payload) {
        return List.of(
            Message.from(ACTOR_PACKET_NAME.getBytes(StandardCharsets.UTF_8)),
            Message.from(String.join(
                "\n",
                actorRef.nodeRid().toString(),
                actorRef.actorId(),
                Long.toUnsignedString(actorRef.epoch())).getBytes(StandardCharsets.UTF_8)),
            Message.from(ZLinkStreamHeaderCodec.encode(header)),
            Message.from(payload));
    }

    public static BoundSessionSend decodeBoundSessionSend(List<Message> parts) {
        if (parts.size() < 3
            || !BOUND_SESSION_SEND_PACKET_NAME.equals(parts.get(0).toUtf8String())) {
            throw new ZLinkConfigurationException("invalid routed actor bound session send packet");
        }
        String[] fields = parts.get(1).toUtf8String().split("\n", -1);
        if (fields.length != 3
            || fields[0].isBlank()
            || fields[1].isBlank()) {
            throw new ZLinkConfigurationException("invalid routed actor bound session send metadata");
        }
        return new BoundSessionSend(
            new ZLinkBackendActorRef(
                RoutingId.from(fields[0]),
                fields[1],
                Long.parseUnsignedLong(fields[2])),
            Message.from(parts.get(2)));
    }

    public static ActorPacket decodeActorPacket(List<Message> parts) {
        if (parts.size() < 4
            || !ACTOR_PACKET_NAME.equals(parts.get(0).toUtf8String())) {
            throw new ZLinkConfigurationException("invalid routed actor packet");
        }
        String[] fields = parts.get(1).toUtf8String().split("\n", -1);
        if (fields.length != 3
            || fields[0].isBlank()
            || fields[1].isBlank()) {
            throw new ZLinkConfigurationException("invalid routed actor packet metadata");
        }
        return new ActorPacket(
            new ZLinkBackendActorRef(
                RoutingId.from(fields[0]),
                fields[1],
                Long.parseUnsignedLong(fields[2])),
            ZLinkStreamHeaderCodec.decodeOrPlain(parts.get(2).toByteArray()),
            Message.from(parts.get(3)));
    }

    public static JoinRequest decodeJoinRequest(Message message) {
        String[] fields = message.toUtf8String().split("\n", -1);
        if (fields.length != 5
            || fields[0].isBlank()
            || fields[1].isBlank()
            || fields[2].isBlank()
            || fields[4].isBlank()) {
            throw new ZLinkConfigurationException("invalid actor Spot route join request");
        }
        return new JoinRequest(
            fields[0],
            fields[1],
            RoutingId.from(fields[2]),
            Long.parseUnsignedLong(fields[3]),
            RoutingId.from(fields[4]));
    }

    public static Message encodeJoinReply(
        boolean accepted,
        ZLinkBackendActorRef actorRef,
        Message reply) {
        String encodedReply = reply == null
            ? ""
            : Base64.getEncoder().encodeToString(reply.toByteArray());
        return Message.from(String.join(
            "\n",
            Boolean.toString(accepted),
            actorRef.nodeRid().toString(),
            actorRef.actorId(),
            Long.toUnsignedString(actorRef.epoch()),
            encodedReply).getBytes(StandardCharsets.UTF_8));
    }

    public static JoinReply decodeJoinReply(Message message) {
        String[] fields = message.toUtf8String().split("\n", -1);
        if (fields.length != 5
            || fields[1].isBlank()
            || fields[2].isBlank()) {
            throw new ZLinkConfigurationException("invalid actor Spot route join reply");
        }
        Message reply = fields[4].isBlank()
            ? Message.from(new byte[0])
            : Message.from(Base64.getDecoder().decode(fields[4]));
        return new JoinReply(
            Boolean.parseBoolean(fields[0]),
            new ZLinkBackendActorRef(
                RoutingId.from(fields[1]),
                fields[2],
                Long.parseUnsignedLong(fields[3])),
            reply);
    }

    public record JoinRequest(
        String actorId,
        String actorType,
        RoutingId actorNodeRid,
        long actorGeneration,
        RoutingId sourceEntrySpotRid) {
        public ZLinkBackendActorRef actorRef() {
            return new ZLinkBackendActorRef(actorNodeRid, actorId, actorGeneration);
        }
    }

    public record JoinReply(
        boolean accepted,
        ZLinkBackendActorRef actorRef,
        Message reply) {
    }

    public record BoundSessionSend(
        ZLinkBackendActorRef actorRef,
        Message frame) implements AutoCloseable {
        @Override
        public void close() {
            frame.close();
        }
    }

    public record ActorPacket(
        ZLinkBackendActorRef actorRef,
        ZLinkStreamHeader header,
        Message payload) implements AutoCloseable {
        @Override
        public void close() {
            payload.close();
        }
    }
}
