package systems.zlink.samples.bingo.server.play.bingoroomspots;

import java.util.concurrent.CompletionStage;

public interface BingoWinnerSink {
    String playerId();

    CompletionStage<Void> publishWinnerAsync(String payload, String roomId);
}
