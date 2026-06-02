package systems.zlink.framework.streams;

import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;

public interface ZLinkSessionPacketDispatcher<TSessionContext extends ZLinkSessionContext> {
    CompletionStage<Boolean> tryHandleAsync(
        TSessionContext context,
        ZLinkStreamHeader header,
        Message payload);
}
