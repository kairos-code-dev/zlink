package systems.zlink.samples.bingo.client;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletionException;
import systems.zlink.samples.bingo.shared.configuration.SampleTimings;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class BingoClientApp {
    private final BingoClientOptions options;

    public BingoClientApp(BingoClientOptions options) {
        this.options = options;
    }

    public void run(List<Integer> drawSequence) throws Exception {
        List<BingoPlayerClient> clients = new ArrayList<>();
        for (int i = 1; i <= options.playerCount(); i++) {
            BingoPlayerClient client = new BingoPlayerClient("player-" + i);
            await("connect " + client.playerId(), client.connectAsync());
            Messages.AuthenticateRes auth =
                await("authenticate " + client.playerId(), client.authenticateAsync());
            require(auth.actorId().equals(client.playerId()), "session authenticate reply mismatch");
            clients.add(client);
        }

        Messages.MatchBingoRes firstMatch =
            await("match " + clients.getFirst().playerId(), clients.getFirst().matchAsync());
        requireRejected(() -> await(
                "premature start " + clients.getFirst().playerId(),
                clients.getFirst().startAsync(firstMatch.roomId())),
            "host start must be rejected before four players join");

        List<Messages.MatchBingoRes> matches = new ArrayList<>();
        matches.add(firstMatch);
        for (int i = 1; i < clients.size(); i++) {
            matches.add(await("match " + clients.get(i).playerId(), clients.get(i).matchAsync()));
        }
        require(matches.stream().map(Messages.MatchBingoRes::roomId).distinct().count() == 1,
            "all match requests must return the same room");
        require(matches.getFirst().state().hostActorId().equals(clients.getFirst().playerId()),
            "first joined actor must become host");

        requireRejected(() -> await(
                "non-host start " + clients.get(1).playerId(),
                clients.get(1).startAsync(firstMatch.roomId())),
            "non-host start must be rejected");
        Messages.StartBingoGameRes started =
            await(
                "host start " + clients.getFirst().playerId(),
                clients.getFirst().startAsync(firstMatch.roomId()));
        require(started.state().status().equals("Running"),
            "host start must put room into Running status");

        Messages.BingoRoomState ended = waitForEnded(clients);
        require(ended.status().equals("Finished"), "room must finish through timer draws");
        require(ended.winners().size() > 1, "deterministic sample must include same-sequence winners");
        require(ended.players().stream().allMatch(player -> player.card().size() == 25),
            "each player card must contain 25 cells");
        require(ended.players().stream().allMatch(player -> Boolean.TRUE.equals(player.marks().get(12))),
            "center free cell must start marked");
        require(clients.stream().allMatch(client -> !client.inbox().started().isEmpty()),
            "each connector must receive game-start push");
        require(clients.stream().allMatch(client -> !client.inbox().drawn().isEmpty()),
            "each connector must receive draw push");
        require(clients.stream().allMatch(client -> !client.inbox().ended().isEmpty()),
            "each connector must receive game-ended push");
    }

    private static Messages.BingoRoomState waitForEnded(List<BingoPlayerClient> clients) throws Exception {
        long deadline = System.nanoTime() + SampleTimings.RequestTimeout.toNanos();
        while (System.nanoTime() < deadline) {
            for (BingoPlayerClient client : clients) {
                await("dispatch " + client.playerId(), client.dispatchAsync());
            }
            if (clients.stream().allMatch(client -> !client.inbox().ended().isEmpty())) {
                return clients.stream()
                    .flatMap(client -> client.inbox().ended().stream())
                    .reduce((first, second) -> second)
                    .orElseThrow()
                    .state();
            }
            Thread.onSpinWait();
        }
        throw new IllegalStateException("Timed out waiting for BingoGameEndedNotify");
    }

    private static void requireRejected(ThrowingRunnable action, String message) throws Exception {
        try {
            action.run();
        } catch (CompletionException | IllegalStateException ex) {
            return;
        }
        throw new IllegalStateException(message);
    }

    private static <T> T await(String action, java.util.concurrent.CompletionStage<T> stage)
        throws Exception {
        try {
            return SampleAsync.await(stage);
        } catch (Exception ex) {
            throw new IllegalStateException("Bingo sample failed during " + action, ex);
        }
    }

    private static void require(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }

    @FunctionalInterface
    private interface ThrowingRunnable {
        void run() throws Exception;
    }
}
