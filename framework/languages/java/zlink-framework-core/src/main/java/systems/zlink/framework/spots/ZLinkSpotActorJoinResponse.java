package systems.zlink.framework.spots;

import systems.zlink.contracts.messaging.Message;

public record ZLinkSpotActorJoinResponse(
    boolean accepted,
    Message reply) {
    public static ZLinkSpotActorJoinResponse accept() {
        return new ZLinkSpotActorJoinResponse(true, null);
    }

    public static ZLinkSpotActorJoinResponse accept(Message reply) {
        return new ZLinkSpotActorJoinResponse(true, reply);
    }

    public static ZLinkSpotActorJoinResponse reject() {
        return new ZLinkSpotActorJoinResponse(false, null);
    }

    public static ZLinkSpotActorJoinResponse reject(Message reply) {
        return new ZLinkSpotActorJoinResponse(false, reply);
    }
}
