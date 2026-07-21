package systems.zlink.framework.runtime.backend;

import systems.zlink.contracts.core.RoutingId;

/** Internal identity of one send-capacity domain. */
public record ZLinkBackendAdmissionKey(
    Kind kind,
    RoutingId nodeRid,
    RoutingId spotRid,
    String actorId,
    long actorGeneration,
    String channelName) {

    public enum Kind {
        SOCKET,
        NODE,
        CHANNEL,
        SPOT,
        ACTOR,
        BOUND_SESSION
    }

    public static ZLinkBackendAdmissionKey socket() {
        return new ZLinkBackendAdmissionKey(Kind.SOCKET, null, null, null, 0L, null);
    }

    public static ZLinkBackendAdmissionKey node(RoutingId nodeRid) {
        return new ZLinkBackendAdmissionKey(Kind.NODE, nodeRid, null, null, 0L, null);
    }

    public static ZLinkBackendAdmissionKey channel(String channelName) {
        return new ZLinkBackendAdmissionKey(
            Kind.CHANNEL, null, null, null, 0L, channelName);
    }

    public static ZLinkBackendAdmissionKey spot(RoutingId nodeRid, RoutingId spotRid) {
        return new ZLinkBackendAdmissionKey(
            Kind.SPOT, nodeRid, spotRid, null, 0L, null);
    }

    public static ZLinkBackendAdmissionKey actor(
        RoutingId nodeRid,
        String actorId,
        long generation) {
        return new ZLinkBackendAdmissionKey(
            Kind.ACTOR, nodeRid, null, actorId, generation, null);
    }

    public static ZLinkBackendAdmissionKey boundSession(
        RoutingId nodeRid,
        String actorId,
        long generation) {
        return new ZLinkBackendAdmissionKey(
            Kind.BOUND_SESSION, nodeRid, null, actorId, generation, null);
    }
}
