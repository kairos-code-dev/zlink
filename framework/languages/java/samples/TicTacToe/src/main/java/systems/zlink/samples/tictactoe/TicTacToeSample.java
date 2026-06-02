package systems.zlink.samples.tictactoe;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotOutbound;
import systems.zlink.framework.spots.ZLinkTimer;
import systems.zlink.framework.spots.ZLinkTimerOptions;

public final class TicTacToeSample {
    public static void main(String[] args) {
        GameRoomSpot room = new GameRoomSpot("room-1");
        String created = new CreateGameHandler(room)
            .handleAsync(new CreateGame("alice", "bob"), null)
            .toCompletableFuture()
            .join();
        require("room-1".equals(created), "channel request did not create expected room");

        room.join("alice");
        room.join("bob");
        room.place("alice", 0);
        room.place("bob", 4);
        room.place("alice", 1);
        room.place("bob", 8);
        room.place("alice", 2);

        require("alice".equals(room.winner()), "direct TicTacToe winner mismatch");
        require(room.pushes().contains("GameWon:alice"), "room Spot did not publish winner push");
        System.out.println("TicTacToe sample self-check passed");
    }

    private static void require(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }

    private record CreateGame(String host, String guest) {
    }

    private static final class CreateGameHandler
        implements ZLinkRequestHandler<CreateGame, String> {
        private final GameRoomSpot room;

        CreateGameHandler(GameRoomSpot room) {
            this.room = room;
        }

        @Override
        public CompletionStage<String> handleAsync(
            CreateGame request,
            ZLinkRequestContext context) {
            room.join(request.host());
            room.join(request.guest());
            return CompletableFuture.completedFuture(room.spotId());
        }
    }

    private static final class GameRoomSpot implements ZLinkSpot {
        private final String spotId;
        private final char[] board = new char[9];
        private final List<String> players = new ArrayList<>();
        private final List<String> pushes = new ArrayList<>();
        private String winner = "";

        GameRoomSpot(String spotId) {
            this.spotId = spotId;
        }

        public String spotId() {
            return spotId;
        }

        @Override
        public ZLinkSpotContext context() {
            return new SampleSpotContext();
        }

        @Override
        public CompletionStage<Void> onCreateAsync(List<Message> createParts) {
            return CompletableFuture.completedFuture(null);
        }

        void join(String player) {
            if (!players.contains(player)) {
                players.add(player);
                pushes.add("Joined:" + player);
            }
        }

        void place(String player, int cell) {
            char mark = players.indexOf(player) == 0 ? 'X' : 'O';
            if (board[cell] != 0) {
                throw new IllegalStateException("cell is already occupied");
            }
            board[cell] = mark;
            if (line(0, 1, 2, mark) || line(3, 4, 5, mark)
                || line(6, 7, 8, mark) || line(0, 3, 6, mark)
                || line(1, 4, 7, mark) || line(2, 5, 8, mark)
                || line(0, 4, 8, mark) || line(2, 4, 6, mark)) {
                winner = player;
                pushes.add("GameWon:" + player);
            }
        }

        String winner() {
            return winner;
        }

        List<String> pushes() {
            return List.copyOf(pushes);
        }

        private boolean line(int a, int b, int c, char mark) {
            return board[a] == mark && board[b] == mark && board[c] == mark;
        }
    }

    private static final class SampleSpotContext implements ZLinkSpotContext {
        @Override
        public RoutingId spotRid() {
            return RoutingId.from("room-1");
        }

        @Override
        public RoutingId nodeRid() {
            return RoutingId.from("play-node");
        }

        @Override
        public ZLinkSpotOutbound outbound() {
            throw new UnsupportedOperationException("not needed by sample");
        }

        @Override
        public CompletionStage<Void> leaveActorAsync(
            systems.zlink.framework.actors.ZLinkActor actor) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<ZLinkTimer> addTimer(
            String name,
            java.time.Duration period,
            Class<?> handlerType,
            ZLinkTimerOptions options) {
            throw new UnsupportedOperationException("not needed by sample");
        }
    }
}
