package systems.zlink.samples.bingo.client;

import java.util.ArrayList;
import java.util.List;
import systems.zlink.samples.bingo.server.play.bingoroomspots.BingoRoomState;

public final class BingoClientApp {
    private final BingoClientOptions options;

    public BingoClientApp(BingoClientOptions options) {
        this.options = options;
    }

    public void run(BingoRoomState room) throws Exception {
        List<BingoPlayerClient> clients = new ArrayList<>();
        for (int i = 1; i <= options.playerCount(); i++) {
            BingoPlayerClient client = new BingoPlayerClient("player-" + i);
            SampleAsync.await(client.connectAsync());
            clients.add(client);
        }

        for (BingoPlayerClient client : clients) {
            room.join(client);
        }
        require(room.host().equals("player-1"), "first joiner must be host");
        require(!SampleAsync.await(room.startAsync("player-2")), "non-host start must be rejected");
        require(SampleAsync.await(room.startAsync("player-1")), "host start must succeed");

        List<String> winners = room.winners();
        require(winners.equals(List.of("player-2", "player-3")),
            "same-sequence deterministic winners mismatch: " + winners);
        for (BingoPlayerClient client : clients) {
            awaitNotification(client, "Winner:player-2,player-3");
        }
    }

    private static void awaitNotification(BingoPlayerClient client, String expected)
        throws Exception {
        long deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(5);
        while (System.nanoTime() < deadline) {
            SampleAsync.await(client.dispatchAsync());
            if (client.inbox().events().contains(expected)) {
                return;
            }
            Thread.onSpinWait();
        }
        throw new IllegalStateException("bound push did not arrive at " + client.playerId());
    }

    private static void require(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }
}
