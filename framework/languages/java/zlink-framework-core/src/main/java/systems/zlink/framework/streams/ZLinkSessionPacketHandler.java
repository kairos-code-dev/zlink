package systems.zlink.framework.streams;

import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;

public interface ZLinkSessionPacketHandler<TSessionContext extends ZLinkSessionContext> {
    String packetName();

    CompletionStage<Void> handleAsync(
        TSessionContext context,
        ZLinkStreamHeader header,
        Message payload);
}
