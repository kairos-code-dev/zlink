package systems.zlink.samples.bingo.server.play.bingoroomspots;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;

public final class BingoRoomState {
    private final String roomId;
    private final List<Integer> drawSequence;
    private final BingoNotificationPublisher publisher = new BingoNotificationPublisher();
    private final List<BingoWinnerSink> clients = new ArrayList<>();
    private final Map<String, BingoCard> cards = new HashMap<>();
    private String host = "";

    public BingoRoomState(String roomId, List<Integer> drawSequence) {
        this.roomId = roomId;
        this.drawSequence = drawSequence;
    }

    public void join(BingoWinnerSink client) {
        if (clients.isEmpty()) {
            host = client.playerId();
        }
        clients.add(client);
        cards.put(client.playerId(), cardFor(client.playerId()));
    }

    public String host() {
        return host;
    }

    public CompletionStage<Boolean> startAsync(String playerId) {
        if (!host.equals(playerId)) {
            return CompletableFuture.completedFuture(false);
        }
        List<CompletionStage<Void>> pushes = new ArrayList<>();
        List<String> winners = winners();
        for (BingoWinnerSink client : clients) {
            pushes.add(publisher.publishWinnerAsync(client, winners, roomId));
        }
        return allOf(pushes).thenApply(ignored -> true);
    }

    public List<String> winners() {
        List<String> winners = new ArrayList<>();
        for (Map.Entry<String, BingoCard> entry : cards.entrySet()) {
            if (drawSequence.containsAll(entry.getValue().numbers())) {
                winners.add(entry.getKey());
            }
        }
        winners.sort(String::compareTo);
        return winners;
    }

    private BingoCard cardFor(String playerId) {
        return BingoRoomSpot.cardFor(playerId);
    }

    private static CompletionStage<Void> allOf(List<CompletionStage<Void>> stages) {
        CompletableFuture<Void> done = new CompletableFuture<>();
        if (stages.isEmpty()) {
            done.complete(null);
            return done;
        }
        final int[] remaining = {stages.size()};
        for (CompletionStage<Void> stage : stages) {
            stage.whenComplete((ignored, error) -> {
                if (error != null) {
                    done.completeExceptionally(error);
                } else if (--remaining[0] == 0) {
                    done.complete(null);
                }
            });
        }
        return done;
    }
}
