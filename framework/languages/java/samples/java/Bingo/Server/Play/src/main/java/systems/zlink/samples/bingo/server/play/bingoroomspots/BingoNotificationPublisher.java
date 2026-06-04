package systems.zlink.samples.bingo.server.play.bingoroomspots;

import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;

public final class BingoNotificationPublisher {
    public CompletionStage<Void> publishWinnerAsync(
        List<String> winners,
        String roomId) {
        return CompletableFuture.completedFuture(null);
    }
}
