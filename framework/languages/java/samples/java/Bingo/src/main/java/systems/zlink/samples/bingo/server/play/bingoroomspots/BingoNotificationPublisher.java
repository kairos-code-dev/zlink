package systems.zlink.samples.bingo.server.play.bingoroomspots;

import java.util.List;
import java.util.concurrent.CompletionStage;

public final class BingoNotificationPublisher {
    public CompletionStage<Void> publishWinnerAsync(
        BingoWinnerSink client,
        List<String> winners,
        String roomId) {
        return client.publishWinnerAsync(String.join(",", winners), roomId);
    }
}
