package systems.zlink.framework;

import java.util.concurrent.CompletionStage;

public interface ZLinkHandlerFilter {
    <T> CompletionStage<T> invoke(
        ZLinkMessageContext context,
        ZLinkHandlerFilterNext<T> next);
}
