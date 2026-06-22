package systems.zlink.samples.tictactoe.server.play.application.gamecreation;

import java.util.concurrent.atomic.AtomicInteger;
import systems.zlink.samples.tictactoe.server.configuration.SampleSettings;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameRes;
import systems.zlink.samples.tictactoe.shared.contracts.PlayNodeInfo;

public final class TicTacToeGameCreator {
    private final AtomicInteger sequence = new AtomicInteger();
    private final SampleSettings settings;
    private final GameRoomRouteWriter routes;

    public TicTacToeGameCreator(
        SampleSettings settings,
        GameRoomRouteWriter routes) {
        this.settings = settings;
        this.routes = routes;
    }

    public GameRoom nextRoom(String gameName) {
        String normalized = gameName == null || gameName.isBlank() ? "tic-tac-toe" : gameName;
        return new GameRoom("ttt-room-%03d".formatted(sequence.incrementAndGet()), normalized);
    }

    public CreateGameRes created(GameRoom room) {
        routes.save(new GameRoomRoute(
            room.roomId(),
            settings.playEndpoint(),
            settings.spotEndpoint(),
            settings.playSpotNodeRid()));
        System.out.println("game-created roomId=" + room.roomId()
            + " ownerPlayEndpoint=" + settings.playEndpoint()
            + " nodeRid=" + settings.playSpotNodeRid());
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

    public record GameRoomRoute(
        String roomId,
        String ownerPlayEndpoint,
        String ownerSpotEndpoint,
        String ownerSpotNodeRid) {
    }

    public interface GameRoomRouteWriter {
        void save(GameRoomRoute route);
    }
}
