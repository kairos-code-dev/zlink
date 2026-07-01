package systems.zlink.e2e.spotservice.shared;

import java.util.List;

public final class Contracts {
    public static final String ROUTE_CHANNEL = "spot.service.route";
    public static final String ROUTE_PACKET = "RouteReq";
    public static final String EGRESS_CHANNEL = "spot.service.egress";
    public static final String INGRESS_CHANNEL = "spot.service.ingress";
    public static final String SPOT_MESH = "spot.service.mesh";
    public static final String SPOT_NODE = "play";

    private Contracts() {
    }

    public record StateReq(String op) {
    }

    public record StateRes(
        String spotRid,
        String nodeRid,
        String value) {
    }

    public record StateMsg(String value) {
    }

    public record SlowReq(String value) {
    }

    public record OutboundReq(String value) {
    }

    public record OutboundRes(
        String spotRid,
        String nodeRid,
        String channelReply) {
    }

    public record OutboundMsg(String value) {
    }

    public record MeshMsg(String value) {
    }

    public record TimerActivityReq(String value) {
    }

    public record TimerActivityRes(
        String spotRid,
        String value) {
    }

    public record RouteReq(String value) {
    }

    public record RouteRes(
        String value,
        String nodeRid,
        String routeRid) {
    }

    public record ActorProfile(
        String displayName,
        int level,
        List<String> tags) {
    }

    public record ActorAuthReq(
        String actorId,
        ActorProfile profile) {
    }

    public record ActorAuthRes(
        String actorId,
        String nodeRid,
        int boundCount,
        String displayName,
        int level,
        List<String> tags) {
    }

    public record ActorJoinReq(
        String spotRid,
        ActorProfile profile,
        List<String> tags) {
    }

    public record ActorJoinRes(
        String actorId,
        String spotRid,
        String nodeRid,
        String displayName,
        int level,
        List<String> tags) {
    }

    public record LeaveActorReq(String actorId) {
    }

    public record LeaveActorRes(
        String actorId,
        boolean accepted) {
    }

    public record ActorEchoReq(
        String value,
        int seq,
        ActorProfile profile) {
    }

    public record ActorEchoRes(
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

    public record ActorPushNotify(
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

    public record EvidenceWaitReq(
        List<String> containsAll,
        int timeoutMilliseconds) {
    }

    public record EvidenceSnapshot(
        String nodeRid,
        List<EvidenceEntry> entries) {
    }
}
