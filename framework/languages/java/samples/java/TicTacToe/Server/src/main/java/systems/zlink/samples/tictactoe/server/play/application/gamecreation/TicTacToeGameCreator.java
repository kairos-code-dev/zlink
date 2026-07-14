package systems.zlink.samples.tictactoe.server.play.application.gamecreation;

import java.util.concurrent.atomic.AtomicInteger;
import systems.zlink.samples.tictactoe.server.configuration.SampleSettings;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameRes;
import systems.zlink.samples.tictactoe.shared.contracts.PlayNodeInfo;

public final class TicTacToeGameCreator {
    private final AtomicInteger sequence = new AtomicInteger();
    private final SampleSettings settings;

    public TicTacToeGameCreator(SampleSettings settings) {
        this.settings = settings;
    }

    public GameRoom nextRoom(String gameName) {
        String normalized = gameName == null || gameName.isBlank() ? "tic-tac-toe" : gameName;
        return new GameRoom(
            "ttt-%s-room-%03d".formatted(settings.playSpotNodeRid(), sequence.incrementAndGet()),
            normalized);
    }

    public CreateGameRes created(GameRoom room) {
        return new CreateGameRes(
            room.roomId(),
            room.gameName(),
            settings.playEndpoint(),
            settings.playEndpoints(),
            settings.playEndpoints().stream()
                .map(endpoint -> new PlayNodeInfo(
                    endpoint,
                    "play-node-" + (settings.playEndpoints().indexOf(endpoint) + 1)))
                .toList(),
            SampleNames.RequiredLevel);
    }

    public record GameRoom(String roomId, String gameName) {
    }

}
