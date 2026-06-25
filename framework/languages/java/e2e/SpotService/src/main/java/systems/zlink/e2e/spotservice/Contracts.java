package systems.zlink.e2e.spotservice;

import java.util.List;

public final class Contracts {
    public static final String ROUTE_CHANNEL = "spot.service.route";
    public static final String ROUTE_PACKET = "RoutePing";
    public static final String EGRESS_CHANNEL = "spot.service.egress";
    public static final String INGRESS_CHANNEL = "spot.service.ingress";
    public static final String SPOT_MESH = "spot.service.mesh";
    public static final String SPOT_NODE = "play";

    private Contracts() {
    }

    public record StateRequest(String op) {
    }

    public record StateReply(
        String spotRid,
        String nodeRid,
        String value) {
    }

    public record StateCommand(String value) {
    }

    public record SlowRequest(String value) {
    }

    public record OutboundRequest(String value) {
    }

    public record OutboundReply(
        String spotRid,
        String nodeRid,
        String channelReply) {
    }

    public record OutboundCommand(String value) {
    }

    public record MeshEvent(String value) {
    }

    public record TimerActivity(String value) {
    }

    public record TimerStatus(
        String spotRid,
        String value) {
    }

    public record RoutePing(String value) {
    }

    public record RoutePong(
        String value,
        String nodeRid,
        String routeRid) {
    }

    public record ActorProfile(
        String displayName,
        int level,
        List<String> tags) {
    }

    public record ActorAuthRequest(
        String actorId,
        ActorProfile profile) {
    }

    public record ActorAuthReply(
        String actorId,
        String nodeRid,
        int boundCount) {
    }

    public record ActorJoinRequest(
        String spotRid,
        ActorProfile profile,
        List<String> tags) {
    }

    public record ActorJoinReply(
        String actorId,
        String spotRid,
        String nodeRid,
        String displayName,
        int level,
        List<String> tags) {
    }

    public record ActorEchoRequest(
        String value,
        int seq,
        ActorProfile profile) {
    }

    public record ActorEchoReply(
        String actorId,
        String spotRid,
        String nodeRid,
        String value,
        int requestSeq,
        int handlerSeq,
        String displayName,
        int level,
        List<String> tags) {
    }

    public record ActorPush(
        String actorId,
        String spotRid,
        String value,
        int requestSeq,
        int handlerSeq) {
    }

    public record EvidenceEntry(
        String marker,
        String nodeRid,
        String spotRid,
        String value) {
    }

    public record EvidenceSnapshot(
        String nodeRid,
        List<EvidenceEntry> entries) {
    }
}
