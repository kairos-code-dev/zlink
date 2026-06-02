package systems.zlink.framework.runtime;

import java.time.Duration;
import java.util.concurrent.CompletionStage;

public interface ZLinkBackendActorBindOperation {
    CompletionStage<Void> submitAsync(Duration timeout);
}
