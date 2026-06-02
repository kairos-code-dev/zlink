package systems.zlink.samples.bingo;

import java.net.URI;
import java.time.Duration;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory;
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions;
import systems.zlink.stream.connector.ZLinkStreamDispatchMode;
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload;

public final class BingoSample {
    public static void main(String[] args) throws Exception {
        List<PlayerClient> clients = new ArrayList<>();
        for (int i = 1; i <= 4; i++) {
            PlayerClient client = new PlayerClient("player-" + i);
            client.connect();
            clients.add(client);
        }

        BingoRoom room = new BingoRoom("room-1", List.of(7, 11, 42, 42));
        for (PlayerClient client : clients) {
            room.join(client);
        }
        require(room.host().equals("player-1"), "first joiner must be host");
        require(!room.start("player-2"), "non-host start must be rejected");
        require(room.start("player-1"), "host start must succeed");

        List<String> winners = room.winners();
        require(winners.equals(List.of("player-2", "player-3")),
            "same-sequence deterministic winners mismatch: " + winners);
        for (PlayerClient client : clients) {
            client.dispatch();
            require(client.events().contains("Winner:player-2,player-3"),
                "bound push did not arrive at " + client.playerId());
        }
        System.out.println("Bingo sample self-check passed");
    }

    private static void require(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }

    private static final class BingoRoom {
        private final String roomId;
        private final List<Integer> drawSequence;
        private final List<PlayerClient> clients = new ArrayList<>();
        private final Map<String, List<Integer>> cards = new HashMap<>();
        private String host = "";

        BingoRoom(String roomId, List<Integer> drawSequence) {
            this.roomId = roomId;
            this.drawSequence = drawSequence;
        }

        void join(PlayerClient client) {
            if (clients.isEmpty()) {
                host = client.playerId();
            }
            clients.add(client);
            cards.put(client.playerId(), cardFor(client.playerId()));
        }

        String host() {
            return host;
        }

        boolean start(String playerId) {
            if (!host.equals(playerId)) {
                return false;
            }
            List<String> winners = winners();
            String payload = "Winner:" + String.join(",", winners);
            for (PlayerClient client : clients) {
                client.push("BingoWinner", payload, roomId);
            }
            return true;
        }

        List<String> winners() {
            List<String> winners = new ArrayList<>();
            for (Map.Entry<String, List<Integer>> entry : cards.entrySet()) {
                if (drawSequence.containsAll(entry.getValue())) {
                    winners.add(entry.getKey());
                }
            }
            winners.sort(String::compareTo);
            return winners;
        }

        private List<Integer> cardFor(String playerId) {
            return switch (playerId) {
                case "player-2", "player-3" -> List.of(7, 11, 42);
                default -> List.of(1, 2, 3);
            };
        }
    }

    private static final class PlayerClient implements AutoCloseable {
        private final String playerId;
        private final ZLinkStreamConnector connector;
        private final List<String> events = new ArrayList<>();

        PlayerClient(String playerId) {
            this.playerId = playerId;
            this.connector = ZLinkStreamConnectorFactory.create(new ZLinkStreamConnectorOptions(
                URI.create("tcp://127.0.0.1:29100/" + playerId),
                ZLinkStreamDispatchMode.MANUAL,
                Duration.ofSeconds(3),
                2));
        }

        void connect() throws Exception {
            connector.on("BingoWinner", message -> {
                events.add(message.payload().payload().toUtf8String());
                return java.util.concurrent.CompletableFuture.completedFuture(null);
            });
            connector.connectAsync().toCompletableFuture().join();
        }

        void push(String packetName, String payload, String roomId) {
            connector.send(new ZLinkStreamEncodedPayload(
                    packetName,
                    Message.from(payload),
                    Map.of("roomId", roomId)))
                .packetName(packetName)
                .metadata("playerId", playerId)
                .submitAsync()
                .toCompletableFuture()
                .join();
        }

        void dispatch() {
            connector.dispatchAsync().toCompletableFuture().join();
        }

        String playerId() {
            return playerId;
        }

        List<String> events() {
            return List.copyOf(events);
        }

        @Override
        public void close() {
            connector.close();
        }
    }
}
