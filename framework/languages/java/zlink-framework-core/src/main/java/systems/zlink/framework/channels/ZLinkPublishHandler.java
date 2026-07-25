package systems.zlink.framework.channels;

import java.util.concurrent.CompletionStage;

public interface ZLinkPublishHandler<TMessage> {
    CompletionStage<Void> handle(
        TMessage message,
        ZLinkPublishMessageContext context);
}
