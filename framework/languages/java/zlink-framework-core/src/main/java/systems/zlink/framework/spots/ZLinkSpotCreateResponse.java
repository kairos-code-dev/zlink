package systems.zlink.framework.spots;

import systems.zlink.framework.messaging.ZLinkMessage;

public record ZLinkSpotCreateResponse(
    boolean accepted,
    ZLinkMessage reply) {
    public static ZLinkSpotCreateResponse accept() {
        return new ZLinkSpotCreateResponse(true, null);
    }

    public static ZLinkSpotCreateResponse accept(ZLinkMessage reply) {
        return new ZLinkSpotCreateResponse(true, reply);
    }

    public static ZLinkSpotCreateResponse accept(Object reply) {
        return new ZLinkSpotCreateResponse(true, ZLinkMessage.of(reply));
    }

    public static ZLinkSpotCreateResponse reject() {
        return new ZLinkSpotCreateResponse(false, null);
    }

    public static ZLinkSpotCreateResponse reject(ZLinkMessage reply) {
        return new ZLinkSpotCreateResponse(false, reply);
    }

    public static ZLinkSpotCreateResponse reject(Object reply) {
        return new ZLinkSpotCreateResponse(false, ZLinkMessage.of(reply));
    }
}
