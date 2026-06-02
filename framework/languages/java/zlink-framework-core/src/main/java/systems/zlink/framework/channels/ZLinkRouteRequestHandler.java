package systems.zlink.framework.channels;

import java.util.concurrent.CompletionStage;

public interface ZLinkRouteRequestHandler<TRequest, TReply> {
    CompletionStage<TReply> handleAsync(
        TRequest request,
        ZLinkRouteRequestContext context);
}
