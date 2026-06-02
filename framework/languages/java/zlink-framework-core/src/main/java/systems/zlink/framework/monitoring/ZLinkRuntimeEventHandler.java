package systems.zlink.framework.monitoring;

import java.util.concurrent.CompletionStage;

public interface ZLinkRuntimeEventHandler<TEvent extends ZLinkRuntimeEvent> {
    CompletionStage<Void> handleAsync(TEvent event);
}
