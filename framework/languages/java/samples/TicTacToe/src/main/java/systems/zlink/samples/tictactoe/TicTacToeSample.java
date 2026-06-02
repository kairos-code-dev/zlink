package systems.zlink.samples.tictactoe;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.ZLinkFramework;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotOutbound;
import systems.zlink.framework.spots.ZLinkTimer;
import systems.zlink.framework.spots.ZLinkTimerOptions;

public final class TicTacToeSample {
    private static final String CHANNEL_NAME = "tictactoe-api";
    private static final String CREATE_GAME_PACKET = "CreateGame";
    private static final String PLAY_ENDPOINT =
        "inproc://zlink-java-sample-tictactoe-play";
    private static final GameRoomSpot ROOM = new GameRoomSpot("room-1");

    public static void main(String[] args) {
        String created;
        try (ZLinkFramework framework = ZLinkFramework.start(options -> {
            options.addClientServerChannel(CHANNEL_NAME, channel -> {
                channel.enableServer(server -> server.bind(PLAY_ENDPOINT));
                channel.enableClient(client -> client.useManualConnections(
                    endpoints -> endpoints.connect(PLAY_ENDPOINT)));
                channel.addRequestHandler(
                    CreateGameHandler.class,
                    String.class,
                    String.class,
                    CREATE_GAME_PACKET);
            });
            options.addSpotMesh("tictactoe", mesh ->
                mesh.addNode("play", node -> node.addSpotFactory(GameRoomSpot.class)));
            options.addStreamNode("play-stream", stream -> {
                stream.bind("inproc://zlink-java-sample-tictactoe-stream");
                stream.registerSession(HeaderSession.class);
            });
        })) {
            framework.spotManager()
                .createAsync(GameRoomSpot.class, RoutingId.from("room-1"))
                .toCompletableFuture()
                .join();
            created = framework.client()
                .requestToChannel(CHANNEL_NAME, "alice:bob")
                .packetName(CREATE_GAME_PACKET)
                .submitAsync(String.class)
                .toCompletableFuture()
                .join();
        }

        require("room-1".equals(created),
            "channel request did not create expected room: " + created);

        ROOM.place("alice", 0);
        ROOM.place("bob", 4);
        ROOM.place("alice", 1);
        ROOM.place("bob", 8);
        ROOM.place("alice", 2);

        require("alice".equals(ROOM.winner()), "direct TicTacToe winner mismatch");
        require(ROOM.pushes().contains("GameWon:alice"), "room Spot did not publish winner push");
        System.out.println("TicTacToe sample self-check passed");
    }

    private static void require(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }

    public static final class CreateGameHandler
        implements ZLinkRequestHandler<String, String> {
        @Override
        public CompletionStage<String> handleAsync(
            String request,
            ZLinkRequestContext context) {
            String[] players = request.split(":", 2);
            if (players.length != 2) {
                throw new IllegalArgumentException("expected host:guest request");
            }
            ROOM.join(players[0]);
            ROOM.join(players[1]);
            return CompletableFuture.completedFuture(ROOM.spotId());
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

    public static final class HeaderSession implements systems.zlink.framework.streams.ZLinkSession {
        @Override
        public systems.zlink.framework.streams.ZLinkSessionContext context() {
            return null;
        }

        @Override
        public CompletionStage<Void> onConnectedAsync() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDisconnectedAsync() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onErrorAsync(
            systems.zlink.framework.streams.ZLinkStreamError error) {
            return CompletableFuture.completedFuture(null);
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
