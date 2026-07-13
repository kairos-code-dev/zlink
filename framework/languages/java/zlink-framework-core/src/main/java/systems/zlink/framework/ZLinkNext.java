package systems.zlink.framework;

import java.util.concurrent.CompletionStage;

@FunctionalInterface
public interface ZLinkNext<T> {
    CompletionStage<T> invoke();
}
