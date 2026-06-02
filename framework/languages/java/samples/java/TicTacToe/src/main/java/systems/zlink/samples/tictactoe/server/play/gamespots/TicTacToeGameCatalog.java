package systems.zlink.samples.tictactoe.server.play.gamespots;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

public final class TicTacToeGameCatalog {
    private static final Map<String, TicTacToeGame> Games = new ConcurrentHashMap<>();

    private TicTacToeGameCatalog() {
    }

    public static TicTacToeGame create(String gameName) {
        String gameId = "room-" + (Games.size() + 1);
        TicTacToeGame game = new TicTacToeGame(gameId, gameName);
        Games.put(gameId, game);
        return game;
    }

    public static TicTacToeGame get(String gameId) {
        TicTacToeGame game = Games.get(gameId);
        if (game == null) {
            throw new IllegalArgumentException("unknown game " + gameId);
        }
        return game;
    }
}
