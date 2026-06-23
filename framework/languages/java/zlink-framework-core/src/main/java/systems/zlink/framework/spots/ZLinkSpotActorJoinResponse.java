package systems.zlink.framework.spots;

import systems.zlink.framework.messaging.ZLinkMessage;

public record ZLinkSpotActorJoinResponse(
    boolean accepted,
    ZLinkMessage reply) {
    public static ZLinkSpotActorJoinResponse accept() {
        return new ZLinkSpotActorJoinResponse(true, null);
    }

    public static ZLinkSpotActorJoinResponse accept(ZLinkMessage reply) {
        return new ZLinkSpotActorJoinResponse(true, reply);
    }

    public static ZLinkSpotActorJoinResponse accept(Object reply) {
        return new ZLinkSpotActorJoinResponse(true, ZLinkMessage.of(reply));
    }

    public static ZLinkSpotActorJoinResponse reject() {
        return new ZLinkSpotActorJoinResponse(false, null);
    }

    public static ZLinkSpotActorJoinResponse reject(ZLinkMessage reply) {
        return new ZLinkSpotActorJoinResponse(false, reply);
    }

    public static ZLinkSpotActorJoinResponse reject(Object reply) {
        return new ZLinkSpotActorJoinResponse(false, ZLinkMessage.of(reply));
    }
}
