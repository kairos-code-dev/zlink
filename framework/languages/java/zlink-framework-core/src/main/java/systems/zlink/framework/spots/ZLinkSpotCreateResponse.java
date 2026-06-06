package systems.zlink.framework.spots;

import systems.zlink.contracts.messaging.Message;

public record ZLinkSpotCreateResponse(
    boolean accepted,
    Message reply) {
    public static ZLinkSpotCreateResponse accept() {
        return new ZLinkSpotCreateResponse(true, null);
    }

    public static ZLinkSpotCreateResponse accept(Message reply) {
        return new ZLinkSpotCreateResponse(true, reply);
    }

    public static ZLinkSpotCreateResponse reject() {
        return new ZLinkSpotCreateResponse(false, null);
    }

    public static ZLinkSpotCreateResponse reject(Message reply) {
        return new ZLinkSpotCreateResponse(false, reply);
    }
}
