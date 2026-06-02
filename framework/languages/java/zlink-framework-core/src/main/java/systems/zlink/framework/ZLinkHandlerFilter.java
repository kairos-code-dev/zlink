package systems.zlink.framework;

import java.util.concurrent.CompletionStage;

public interface ZLinkHandlerFilter {
    <T> CompletionStage<T> invokeAsync(
        ZLinkInvocationContext context,
        ZLinkNext<T> next);
}
