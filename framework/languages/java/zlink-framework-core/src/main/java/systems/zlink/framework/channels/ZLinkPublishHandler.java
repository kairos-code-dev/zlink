package systems.zlink.framework.channels;

import java.util.concurrent.CompletionStage;

public interface ZLinkPublishHandler<TMessage> {
    CompletionStage<Void> handleAsync(
        TMessage message,
        ZLinkPublishContext context);
}
