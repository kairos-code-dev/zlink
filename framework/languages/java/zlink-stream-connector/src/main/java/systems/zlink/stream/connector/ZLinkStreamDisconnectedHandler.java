package systems.zlink.stream.connector;

import java.util.concurrent.CompletionStage;

@FunctionalInterface
public interface ZLinkStreamDisconnectedHandler {
    CompletionStage<Void> handleAsync();
}
