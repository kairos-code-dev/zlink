package systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots;

import java.util.ArrayList;
import java.util.List;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;

public final class TicTacToeGameSpot implements ZLinkSpot {
    private final String matchId;
    private final String ownerActorId;
    private final char[] board = ".........".toCharArray();
    private String xActorId;
    private String oActorId;
    private String turnActorId;
    private String winnerActorId;
    private String lastMoveActorId;
    private Integer lastMoveCell;
    private boolean draw;

    public TicTacToeGameSpot() {
        this("match-sample", "sample");
    }

    public TicTacToeGameSpot(String matchId, String ownerActorId) {
        this.matchId = matchId;
        this.ownerActorId = ownerActorId;
    }

    @Override
    public ZLinkSpotContext context() {
        return null;
    }

    public String matchId() {
        return matchId;
    }

    public String ownerActorId() {
        return ownerActorId;
    }

    public synchronized JoinResult join(String actorId) {
        String mark;
        if (actorId.equals(xActorId)) {
            mark = "X";
        } else if (actorId.equals(oActorId)) {
            mark = "O";
        } else if (xActorId == null) {
            xActorId = actorId;
            turnActorId = actorId;
            mark = "X";
        } else if (oActorId == null) {
            oActorId = actorId;
            mark = "O";
        } else {
            throw new IllegalStateException("Match is full: " + matchId);
        }
        return new JoinResult(matchId, actorId, mark, snapshot());
    }

    public synchronized State placeMark(String actorId, int cell) {
        if (!actorId.equals(turnActorId)) {
            throw new IllegalStateException("It is not " + actorId + "'s turn");
        }
        if (cell < 0 || cell >= board.length || board[cell] != '.') {
            throw new IllegalArgumentException("Invalid move: " + cell);
        }
        board[cell] = actorId.equals(xActorId) ? 'X' : 'O';
        lastMoveActorId = actorId;
        lastMoveCell = cell;
        winnerActorId = winner();
        draw = winnerActorId == null && new String(board).indexOf('.') < 0;
        if (winnerActorId == null && !draw) {
            turnActorId = actorId.equals(xActorId) ? oActorId : xActorId;
        }
        return snapshot();
    }

    private String winner() {
        int[][] lines = {
            {0, 1, 2}, {3, 4, 5}, {6, 7, 8},
            {0, 3, 6}, {1, 4, 7}, {2, 5, 8},
            {0, 4, 8}, {2, 4, 6}
        };
        for (int[] line : lines) {
            char mark = board[line[0]];
            if (mark != '.' && mark == board[line[1]] && mark == board[line[2]]) {
                return mark == 'X' ? xActorId : oActorId;
            }
        }
        return null;
    }

    private State snapshot() {
        String status = winnerActorId != null ? "Won" : draw ? "Draw" : "Playing";
        return new State(
            matchId,
            new String(board),
            status,
            turnActorId,
            winnerActorId,
            draw,
            xActorId,
            oActorId,
            lastMoveActorId,
            lastMoveCell);
    }

    public record JoinResult(String matchId, String actorId, String mark, State state) {
        public String encode() {
            return matchId + "|" + actorId + "|" + mark + "|" + state.encode();
        }
    }

    public record State(
        String matchId,
        String board,
        String status,
        String turnActorId,
        String winnerActorId,
        boolean draw,
        String xActorId,
        String oActorId,
        String lastMoveActorId,
        Integer lastMoveCell) {
        public String encode() {
            List<String> parts = new ArrayList<>();
            parts.add(nullToEmpty(matchId));
            parts.add(nullToEmpty(board));
            parts.add(nullToEmpty(status));
            parts.add(nullToEmpty(turnActorId));
            parts.add(nullToEmpty(winnerActorId));
            parts.add(Boolean.toString(draw));
            parts.add(nullToEmpty(xActorId));
            parts.add(nullToEmpty(oActorId));
            parts.add(nullToEmpty(lastMoveActorId));
            parts.add(lastMoveCell == null ? "" : Integer.toString(lastMoveCell));
            return String.join(",", parts);
        }
    }

    private static String nullToEmpty(String value) {
        return value == null ? "" : value;
    }
}
