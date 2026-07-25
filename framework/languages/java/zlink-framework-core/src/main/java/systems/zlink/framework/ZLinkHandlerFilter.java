package systems.zlink.framework;

import java.util.concurrent.CompletionStage;

public interface ZLinkHandlerFilter {
    <T> CompletionStage<T> invoke(
        ZLinkHandlerInvocation invocation,
        ZLinkNext<T> next);
}
