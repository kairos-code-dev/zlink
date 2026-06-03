package systems.zlink.samples.tictactoe.server.play.gamespots;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.samples.tictactoe.server.play.actors.PlayActor;
import systems.zlink.samples.tictactoe.shared.contracts.GameState;
import systems.zlink.samples.tictactoe.shared.contracts.GameStateNotify;
import systems.zlink.samples.tictactoe.shared.contracts.PlaceMarkRes;
import systems.zlink.samples.tictactoe.shared.contracts.PlayerJoinedNotify;
import systems.zlink.samples.tictactoe.shared.contracts.TicTacToeGameJoinRes;

public final class TicTacToeGame implements ZLinkSpot {
    private final ZLinkSpotContext context;
    private final String gameId;
    private final char[] board = ".........".toCharArray();
    private final List<PlayerSlot> players = new ArrayList<>();
    private String status = "WaitingForPlayers";
    private String nextTurn = "X";
    private String winner = "";
    private String lastMoveActorId = "";
    private int lastMoveCell = -1;

    public TicTacToeGame(ZLinkSpotContext context) {
        this.context = context;
        this.gameId = context.spotRid().toHex();
    }

    public String gameId() {
        return gameId;
    }

    @Override
    public ZLinkSpotContext context() {
        return context;
    }

    @Override
    public CompletionStage<Void> onCreateAsync(List<Message> createParts) {
        return CompletableFuture.completedFuture(null);
    }

    public CompletionStage<TicTacToeGameJoinRes> join(PlayActor actor, String gameId) {
        PlayerSlot slot = players.stream()
            .filter(player -> player.actor().actorId().equals(actor.actorId()))
            .findFirst()
            .orElse(null);
        boolean isNewPlayer = slot == null;
        if (slot == null) {
            String mark = players.isEmpty() ? "X" : "O";
            slot = new PlayerSlot(actor, mark);
            players.add(slot);
        }
        if (players.size() == 2 && status.equals("WaitingForPlayers")) {
            status = "InProgress";
        }
        actor.joinGame(gameId);
        GameState state = snapshot();
        CompletionStage<Void> notify = isNewPlayer
            ? notifyPlayerJoined(actor, slot, state)
            : CompletableFuture.completedFuture(null);
        return notify
            .thenCompose(ignored -> broadcast(state, actor.actorId()))
            .thenApply(ignored -> new TicTacToeGameJoinRes(state));
    }

    public CompletionStage<PlaceMarkRes> placeMark(PlayActor actor, int cell) {
        PlayerSlot slot = players.stream()
            .filter(player -> player.actor().actorId().equals(actor.actorId()))
            .findFirst()
            .orElseThrow(() -> new IllegalStateException("player has not joined"));
        if (!status.equals("InProgress")) {
            throw new IllegalStateException("game is not in progress");
        }
        if (!slot.mark().equals(nextTurn)) {
            throw new IllegalStateException("unexpected turn");
        }
        if ((uint(cell)) >= board.length || board[cell] != '.') {
            throw new IllegalArgumentException("invalid cell");
        }

        board[cell] = slot.mark().charAt(0);
        lastMoveActorId = actor.actorId();
        lastMoveCell = cell;
        advance(slot);
        GameState state = snapshot();
        return broadcast(state, actor.actorId())
            .thenApply(ignored -> new PlaceMarkRes(state));
    }

    public String winner() {
        return winner;
    }

    public boolean hasPlayer(String actorId) {
        return players.stream().anyMatch(player -> player.actor().actorId().equals(actorId));
    }

    public GameState snapshot() {
        return new GameState(
            gameId,
            new String(board),
            status,
            winner.isEmpty() ? null : winner,
            nextTurn,
            actorIdForMark("X"),
            actorIdForMark("O"),
            lastMoveActorId.isEmpty() ? null : lastMoveActorId,
            lastMoveCell < 0 ? null : lastMoveCell);
    }

    private void advance(PlayerSlot slot) {
        if (hasWon(slot.mark().charAt(0))) {
            status = "Won";
            winner = slot.actor().actorId();
            nextTurn = "";
        } else if (new String(board).indexOf('.') < 0) {
            status = "Draw";
            nextTurn = "";
        } else {
            nextTurn = slot.mark().equals("X") ? "O" : "X";
        }
    }

    private String actorIdForMark(String mark) {
        return players.stream()
            .filter(player -> player.mark().equals(mark))
            .map(player -> player.actor().actorId())
            .findFirst()
            .orElse(null);
    }

    private CompletionStage<Void> broadcast(GameState state, String excludedActorId) {
        List<CompletionStage<Void>> sends = players.stream()
            .map(PlayerSlot::actor)
            .filter(actor -> !actor.actorId().equals(excludedActorId))
            .map(actor -> actor.context().boundSession().send(new GameStateNotify(state)).submitAsync())
            .toList();
        return allOf(sends);
    }

    private CompletionStage<Void> notifyPlayerJoined(
        PlayActor joinedActor,
        PlayerSlot joinedSlot,
        GameState state) {
        PlayerJoinedNotify message = new PlayerJoinedNotify(
            state.gameId(),
            joinedActor.actorId(),
            joinedSlot.mark(),
            state);
        List<CompletionStage<Void>> sends = players.stream()
            .map(PlayerSlot::actor)
            .filter(actor -> !actor.actorId().equals(joinedActor.actorId()))
            .map(actor -> actor.context().boundSession().send(message).submitAsync())
            .toList();
        return allOf(sends);
    }

    private static CompletionStage<Void> allOf(List<CompletionStage<Void>> stages) {
        return CompletableFuture.allOf(stages.stream()
            .map(CompletionStage::toCompletableFuture)
            .toArray(CompletableFuture[]::new));
    }

    private boolean hasWon(char mark) {
        int[][] lines = {
            {0, 1, 2}, {3, 4, 5}, {6, 7, 8},
            {0, 3, 6}, {1, 4, 7}, {2, 5, 8},
            {0, 4, 8}, {2, 4, 6}
        };
        for (int[] line : lines) {
            if (board[line[0]] == mark && board[line[1]] == mark && board[line[2]] == mark) {
                return true;
            }
        }
        return false;
    }

    private static int uint(int value) {
        return value < 0 ? Integer.MAX_VALUE : value;
    }

    private record PlayerSlot(PlayActor actor, String mark) {
    }
}
