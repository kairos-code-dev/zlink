package systems.zlink.samples.tictactoe.server.play.sessions;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher;
import systems.zlink.framework.streams.ZLinkStreamError;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.samples.tictactoe.server.play.gamespots.TicTacToeGameDirectory;
import systems.zlink.samples.tictactoe.shared.contracts.GameState;

public final class PlaySession implements ZLinkSession {
    private final ZLinkSessionContext context;

    public PlaySession(ZLinkSessionContext context) {
        this.context = context;
    }

    @Override
    public ZLinkSessionContext context() {
        return context;
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
    public CompletionStage<Void> onErrorAsync(ZLinkStreamError error) {
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> onDispatchAsync(ZLinkStreamHeader header, Message payload) {
        String body = payload.toUtf8String();
        try {
            return switch (header.packetName()) {
                case "AuthenticateReq" -> context.client().reply(body.trim()).submitAsync();
                case "JoinGameReq" -> {
                    String[] parts = body.split("\\|", 2);
                    GameState state = TicTacToeGameDirectory.get(parts[0]).join(parts[1]).state();
                    yield context.client().reply(reply(state)).submitAsync();
                }
                case "PlaceMarkReq" -> {
                    String[] parts = body.split("\\|", 3);
                    var game = TicTacToeGameDirectory.get(parts[0]);
                    GameState state = game.placeMark(parts[1], Integer.parseInt(parts[2])).state();
                    yield context.client().reply(reply(state.winner(), game.pushes())).submitAsync();
                }
                default -> CompletableFuture.failedFuture(new IllegalArgumentException(
                    "unsupported stream packet: " + header.packetName()));
            };
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    private static String reply(GameState state) {
        return reply(state.winner(), java.util.List.of());
    }

    private static String reply(String winner, java.util.List<String> pushes) {
        return (winner == null ? "" : winner) + "|" + String.join(",", pushes);
    }
}
