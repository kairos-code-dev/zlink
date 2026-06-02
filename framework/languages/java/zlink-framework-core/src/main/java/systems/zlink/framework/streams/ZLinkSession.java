package systems.zlink.framework.streams;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;

public interface ZLinkSession {
    ZLinkSessionContext context();

    CompletionStage<Void> onConnectedAsync();

    CompletionStage<Void> onDisconnectedAsync();

    CompletionStage<Void> onErrorAsync(ZLinkStreamError error);

    default CompletionStage<Void> onDispatchAsync(
        ZLinkStreamHeader header,
        Message payload) {
        return CompletableFuture.completedFuture(null);
    }
}
