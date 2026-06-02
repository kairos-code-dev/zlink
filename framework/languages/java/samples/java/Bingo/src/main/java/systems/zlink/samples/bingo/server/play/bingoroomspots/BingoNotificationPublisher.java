package systems.zlink.samples.bingo.server.play.bingoroomspots;

import java.util.List;
import java.util.concurrent.CompletionStage;
import systems.zlink.samples.bingo.client.BingoPlayerClient;

public final class BingoNotificationPublisher {
    public CompletionStage<Void> publishWinnerAsync(
        BingoPlayerClient client,
        List<String> winners,
        String roomId) {
        return client.pushAsync("BingoWinner", "Winner:" + String.join(",", winners), roomId);
    }
}
