package systems.zlink.samples.tictactoe.server.play.application.gamecreation;

import java.util.concurrent.atomic.AtomicInteger;

public final class TicTacToeGameCreator {
    private final AtomicInteger sequence = new AtomicInteger();

    public GameRoom nextRoom(String gameName) {
        String normalized = gameName == null || gameName.isBlank() ? "tic-tac-toe" : gameName;
        return new GameRoom("ttt-room-%03d".formatted(sequence.incrementAndGet()), normalized);
    }

    public record GameRoom(String roomId, String gameName) {
    }
}
