package systems.zlink.samples.tictactoe.server.play.application.gamecreation;

import java.util.UUID;
import systems.zlink.samples.tictactoe.server.configuration.PlaySettings;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameRes;
import systems.zlink.samples.tictactoe.shared.contracts.PlayNodeInfo;

public final class TicTacToeGameCreator {
    private final PlaySettings settings;

    public TicTacToeGameCreator(PlaySettings settings) {
        this.settings = settings;
    }

    public GameRoom nextRoom(String gameName) {
        String normalized = gameName == null || gameName.isBlank() ? "tic-tac-toe" : gameName;
        return new GameRoom(
            "ttt-room-" + UUID.randomUUID(),
            normalized);
    }

    public CreateGameRes created(GameRoom room) {
        return new CreateGameRes(
            room.roomId(),
            room.gameName(),
            settings.playEndpoint(),
            settings.playEndpoints(),
            settings.playEndpoints().stream()
                .map(PlayNodeInfo::new)
                .toList(),
            SampleNames.RequiredLevel);
    }

    public record GameRoom(String roomId, String gameName) {
    }

}
