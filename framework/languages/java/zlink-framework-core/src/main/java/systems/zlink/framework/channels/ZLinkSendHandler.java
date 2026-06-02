package systems.zlink.framework.channels;

import java.util.concurrent.CompletionStage;

public interface ZLinkSendHandler<TMessage> {
    CompletionStage<Void> handleAsync(
        TMessage message,
        ZLinkSendContext context);
}
