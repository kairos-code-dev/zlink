package systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

public final class TicTacToeGameDirectory {
    private final Map<String, TicTacToeGameSpot> rooms = new ConcurrentHashMap<>();

    public TicTacToeGameSpot create(String ownerActorId) {
        String matchId = "match-" + ownerActorId;
        TicTacToeGameSpot room = new TicTacToeGameSpot(matchId, ownerActorId);
        rooms.put(matchId, room);
        return room;
    }

    public TicTacToeGameSpot get(String matchId) {
        TicTacToeGameSpot room = rooms.get(matchId);
        if (room == null) {
            throw new IllegalArgumentException("Unknown match: " + matchId);
        }
        return room;
    }
}
