package systems.zlink.samples.tictactoe.server.play.gamespots;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

public final class TicTacToeGameDirectory {
    private static final Map<String, TicTacToeGame> Games = new ConcurrentHashMap<>();

    private TicTacToeGameDirectory() {
    }

    static void register(TicTacToeGame game) {
        Games.put(game.gameId(), game);
    }

    public static TicTacToeGame get(String gameId) {
        TicTacToeGame game = Games.get(gameId);
        if (game == null) {
            throw new IllegalArgumentException("unknown game " + gameId);
        }
        return game;
    }

    public static TicTacToeGame findByActor(String actorId) {
        return Games.values().stream()
            .filter(game -> game.hasPlayer(actorId))
            .findFirst()
            .orElseThrow(() -> new IllegalArgumentException(
                "actor has not joined a game: " + actorId));
    }
}
