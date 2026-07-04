package systems.zlink.framework.spots;

import systems.zlink.framework.messaging.ZLinkMessage;

public record ZLinkSpotActorJoinResponse(
    boolean accepted,
    ZLinkMessage reply) {
    public static ZLinkSpotActorJoinResponse accept() {
        return from(ZLinkSpotAcceptRejectResult.accept());
    }

    public static ZLinkSpotActorJoinResponse accept(ZLinkMessage reply) {
        return from(ZLinkSpotAcceptRejectResult.accept(reply));
    }

    public static ZLinkSpotActorJoinResponse accept(Object reply) {
        return from(ZLinkSpotAcceptRejectResult.accept(reply));
    }

    public static ZLinkSpotActorJoinResponse reject() {
        return from(ZLinkSpotAcceptRejectResult.reject());
    }

    public static ZLinkSpotActorJoinResponse reject(ZLinkMessage reply) {
        return from(ZLinkSpotAcceptRejectResult.reject(reply));
    }

    public static ZLinkSpotActorJoinResponse reject(Object reply) {
        return from(ZLinkSpotAcceptRejectResult.reject(reply));
    }

    private static ZLinkSpotActorJoinResponse from(ZLinkSpotAcceptRejectResult result) {
        return new ZLinkSpotActorJoinResponse(result.accepted(), result.reply());
    }
}
