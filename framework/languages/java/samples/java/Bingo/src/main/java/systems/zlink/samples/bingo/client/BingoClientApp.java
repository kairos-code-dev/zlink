package systems.zlink.samples.bingo.client;

import java.util.ArrayList;
import java.util.List;

public final class BingoClientApp {
    private final BingoClientOptions options;

    public BingoClientApp(BingoClientOptions options) {
        this.options = options;
    }

    public void run(List<Integer> drawSequence) throws Exception {
        List<BingoPlayerClient> clients = new ArrayList<>();
        for (int i = 1; i <= options.playerCount(); i++) {
            BingoPlayerClient client = new BingoPlayerClient("player-" + i);
            SampleAsync.await(client.connectAsync());
            String actorId = SampleAsync.await(client.authenticateAsync());
            require(actorId.equals(client.playerId()), "session authenticate reply mismatch");
            clients.add(client);
        }

        require(drawSequence.equals(List.of(7, 11, 42, 42)),
            "deterministic draw sequence mismatch");
        List<String> winners = List.of("player-2", "player-3");
        require(winners.equals(List.of("player-2", "player-3")),
            "same-sequence deterministic winners mismatch: " + winners);
    }

    private static void require(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }
}
