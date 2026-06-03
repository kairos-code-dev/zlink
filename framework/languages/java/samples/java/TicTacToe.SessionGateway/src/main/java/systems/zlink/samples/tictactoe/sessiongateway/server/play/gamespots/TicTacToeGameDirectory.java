package systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

public final class TicTacToeGameDirectory {
    private static final Map<String, TicTacToeGameSpot> ROOMS = new ConcurrentHashMap<>();

    private TicTacToeGameDirectory() {
    }

    public static TicTacToeGameSpot create(String ownerActorId) {
        String matchId = "match-" + ownerActorId;
        TicTacToeGameSpot room = new TicTacToeGameSpot(matchId, ownerActorId);
        ROOMS.put(matchId, room);
        return room;
    }

    public static TicTacToeGameSpot get(String matchId) {
        TicTacToeGameSpot room = ROOMS.get(matchId);
        if (room == null) {
            throw new IllegalArgumentException("Unknown match: " + matchId);
        }
        return room;
    }
}
