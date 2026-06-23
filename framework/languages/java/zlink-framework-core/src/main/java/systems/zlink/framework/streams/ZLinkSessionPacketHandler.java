package systems.zlink.framework.streams;

import systems.zlink.framework.messaging.ZLinkMessage;

public interface ZLinkSessionPacketHandler<TSessionContext extends ZLinkSessionContext> {
    String packetName();

    void handle(
        TSessionContext context,
        ZLinkStreamHeader header,
        ZLinkMessage payload);
}
