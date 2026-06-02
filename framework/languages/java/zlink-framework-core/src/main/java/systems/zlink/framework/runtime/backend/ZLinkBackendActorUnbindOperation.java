package systems.zlink.framework.runtime.backend;

import java.time.Duration;
import java.util.concurrent.CompletionStage;

public interface ZLinkBackendActorUnbindOperation {
    CompletionStage<Void> submitAsync(Duration timeout);
}
