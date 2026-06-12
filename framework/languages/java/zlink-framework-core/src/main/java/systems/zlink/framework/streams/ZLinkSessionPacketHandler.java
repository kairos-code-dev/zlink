package systems.zlink.framework.streams;

import systems.zlink.contracts.messaging.Message;

public interface ZLinkSessionPacketHandler<TSessionContext extends ZLinkSessionContext> {
    String packetName();

    void handle(
        TSessionContext context,
        ZLinkStreamHeader header,
        Message payload);
}
