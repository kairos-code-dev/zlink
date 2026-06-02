package systems.zlink.framework.channels;

import java.util.concurrent.CompletionStage;

public interface ZLinkRequestHandler<TRequest, TReply> {
    CompletionStage<TReply> handleAsync(
        TRequest request,
        ZLinkRequestContext context);
}
