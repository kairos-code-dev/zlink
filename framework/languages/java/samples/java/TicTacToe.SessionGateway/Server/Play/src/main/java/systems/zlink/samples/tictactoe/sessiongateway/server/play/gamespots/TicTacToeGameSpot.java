package systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots;

import java.util.ArrayList;
import java.util.List;

public final class TicTacToeGameSpot {
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

    public TicTacToeGameSpot(String matchId, String ownerActorId) {
        this.matchId = matchId;
        this.ownerActorId = ownerActorId;
    }

    public String matchId() {
        return matchId;
    }

    public String ownerActorId() {
        return ownerActorId;
    }

    public synchronized JoinResult join(String actorId) {
        String mark;
        boolean isNewActor = false;
        if (actorId.equals(xActorId)) {
            mark = "X";
        } else if (actorId.equals(oActorId)) {
            mark = "O";
        } else if (xActorId == null) {
            xActorId = actorId;
            turnActorId = actorId;
            mark = "X";
            isNewActor = true;
        } else if (oActorId == null) {
            oActorId = actorId;
            mark = "O";
            isNewActor = true;
        } else {
            throw new IllegalStateException("Match is full: " + matchId);
        }
        State state = snapshot();
        return new JoinResult(matchId, actorId, mark, state, joinEvents(actorId, mark, state, isNewActor));
    }

    public synchronized MoveResult placeMark(String actorId, int cell) {
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
        State state = snapshot();
        List<GameEvent> events = winnerActorId == null && !draw
            ? turnChangedEvents(state)
            : gameEndedEvents(state);
        return new MoveResult(state, events);
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

    private List<GameEvent> joinEvents(
        String joinedActorId,
        String joinedMark,
        State state,
        boolean isNewActor) {
        List<GameEvent> events = new ArrayList<>();
        if (isNewActor) {
            for (String actorId : actorIds()) {
                if (!actorId.equals(joinedActorId)) {
                    events.add(GameEvent.opponentJoined(actorId, joinedActorId, joinedMark, state));
                }
            }
        }
        events.addAll(turnChangedEvents(state));
        return events;
    }

    private List<GameEvent> turnChangedEvents(State state) {
        if (!"Playing".equals(state.status())) {
            return List.of();
        }
        return actorIds().stream()
            .map(actorId -> GameEvent.turnChanged(actorId, state))
            .toList();
    }

    private List<GameEvent> gameEndedEvents(State state) {
        return actorIds().stream()
            .map(actorId -> GameEvent.gameEnded(actorId, state))
            .toList();
    }

    private List<String> actorIds() {
        List<String> actorIds = new ArrayList<>();
        if (xActorId != null) {
            actorIds.add(xActorId);
        }
        if (oActorId != null) {
            actorIds.add(oActorId);
        }
        return actorIds;
    }

    public record JoinResult(
        String matchId,
        String actorId,
        String mark,
        State state,
        List<GameEvent> events) {
        public String encode() {
            return matchId + "|" + actorId + "|" + mark + "|" + state.encode();
        }
    }

    public record MoveResult(State state, List<GameEvent> events) {
        public String encode() {
            return state.encode();
        }
    }

    public record GameEvent(
        String kind,
        String recipientActorId,
        String joinedActorId,
        String joinedMark,
        State state) {
        static GameEvent opponentJoined(
            String recipientActorId,
            String joinedActorId,
            String joinedMark,
            State state) {
            return new GameEvent("OpponentJoined", recipientActorId, joinedActorId, joinedMark, state);
        }

        static GameEvent turnChanged(String recipientActorId, State state) {
            return new GameEvent("TurnChanged", recipientActorId, null, null, state);
        }

        static GameEvent gameEnded(String recipientActorId, State state) {
            return new GameEvent("GameEnded", recipientActorId, null, null, state);
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
