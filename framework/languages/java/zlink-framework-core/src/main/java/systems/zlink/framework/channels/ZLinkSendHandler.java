package systems.zlink.framework.channels;

import java.util.concurrent.CompletionStage;

public interface ZLinkSendHandler<TMessage> {
    CompletionStage<Void> handle(
        TMessage message,
        ZLinkSendContext context);
}
