package systems.zlink.samples.tictactoe.server.play.gamespots;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotCreateResponse;
import systems.zlink.framework.spots.ZLinkTimer;
import systems.zlink.framework.spots.ZLinkTimerOptions;
import systems.zlink.samples.tictactoe.server.play.actors.PlayActor;
import systems.zlink.samples.tictactoe.server.play.gamespots.handlers.TicTacToeGameCreatedHandler;
import systems.zlink.samples.tictactoe.server.play.gamespots.handlers.TicTacToeGameTimerHandler;
import systems.zlink.samples.tictactoe.shared.contracts.GameState;
import systems.zlink.samples.tictactoe.shared.contracts.GameStateNotify;
import systems.zlink.samples.tictactoe.shared.contracts.PlaceMarkRes;
import systems.zlink.samples.tictactoe.shared.contracts.PlayerJoinedNotify;
import systems.zlink.samples.tictactoe.shared.contracts.TicTacToeGameJoinReq;
import systems.zlink.samples.tictactoe.shared.contracts.TicTacToeGameJoinRes;

public final class TicTacToeGame implements ZLinkSpot {
    private static final Duration GAME_TICK_PERIOD = Duration.ofSeconds(1);
    private static final Duration TURN_TIMEOUT = Duration.ofSeconds(15);

    private final ZLinkSpotContext context;
    private final String gameId;
    private final char[] board = ".........".toCharArray();
    private final List<PlayerSlot> players = new ArrayList<>();
    private String status = "WaitingForPlayers";
    private String nextTurn = "X";
    private String winner = "";
    private String lastMoveActorId = "";
    private int lastMoveCell = -1;
    private Instant turnDeadline;
    private ZLinkTimer gameTick;
    private boolean created;
    private final TicTacToeGameCreatedHandler createdHandler;
    private final ObjectMapper json;

    public TicTacToeGame(
        ZLinkSpotContext context,
        TicTacToeGameCreatedHandler createdHandler,
        ObjectMapper json) {
        this.context = context;
        this.gameId = context.spotRid().toHex();
        this.createdHandler = createdHandler;
        this.json = json;
    }

    public String gameId() {
        return gameId;
    }

    @Override
    public ZLinkSpotContext context() {
        return context;
    }

    @Override
    public CompletionStage<ZLinkSpotCreateResponse> onCreateAsync(Message request) {
        return createdHandler.handleAsync(this, request);
    }

    @Override
    public CompletionStage<ZLinkSpotActorJoinResponse> onActorJoinAsync(
        ZLinkActor actor,
        Message request,
        CancellationToken cancellationToken) {
        if (!(actor instanceof PlayActor player)) {
            return CompletableFuture.failedFuture(
                new IllegalArgumentException("tic-tac-toe game only accepts PlayActor."));
        }
        TicTacToeGameJoinReq joinRequest = decode(request, TicTacToeGameJoinReq.class);
        return join(player, joinRequest.gameId())
            .thenApply(reply -> ZLinkSpotActorJoinResponse.accept(encode(reply)));
    }

    @Override
    public CompletionStage<Void> onPostActorJoinedAsync(
        ZLinkActor actor,
        CancellationToken cancellationToken) {
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> onActorLeftAsync(
        ZLinkActor actor,
        CancellationToken cancellationToken) {
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> onInitializeAsync() {
        return context.addTimer(
                "game-tick",
                GAME_TICK_PERIOD,
                TicTacToeGameTimerHandler.class,
                new ZLinkTimerOptions())
            .thenAccept(timer -> gameTick = timer);
    }

    @Override
    public CompletionStage<Void> onClosingAsync() {
        return gameTick == null
            ? CompletableFuture.completedFuture(null)
            : gameTick.cancelAsync();
    }

    public void markCreated(Message request) {
        if (!request.isEmpty()) {
            throw new IllegalArgumentException("tic-tac-toe game creation does not accept payload");
        }
        created = true;
    }

    public CompletionStage<TicTacToeGameJoinRes> join(PlayActor actor, String gameId) {
        ensureCreated();
        PlayerSlot slot = players.stream()
            .filter(player -> player.actor().actorId().equals(actor.actorId()))
            .findFirst()
            .orElse(null);
        boolean isNewPlayer = slot == null;
        if (slot == null) {
            if (players.size() >= 2) {
                throw new IllegalStateException("tic-tac-toe game already has two players");
            }
            String mark = players.size() == 0 ? "X" : "O";
            slot = new PlayerSlot(actor, mark);
            players.add(slot);
        } else {
            slot.actor(actor);
        }
        if (players.size() == 2 && status.equals("WaitingForPlayers")) {
            status = "InProgress";
            resetTurnDeadline();
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
        ensureCreated();
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
        ensureCreated();
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
            turnDeadline = null;
        } else if (new String(board).indexOf('.') < 0) {
            status = "Draw";
            winner = "";
            nextTurn = "";
            turnDeadline = null;
        } else {
            nextTurn = slot.mark().equals("X") ? "O" : "X";
            resetTurnDeadline();
        }
    }

    public CompletionStage<Void> tickAsync() {
        ensureCreated();
        if (!status.equals("InProgress")
            || turnDeadline == null
            || Instant.now().isBefore(turnDeadline)) {
            return CompletableFuture.completedFuture(null);
        }

        PlayerSlot timedOut = players.stream()
            .filter(player -> player.mark().equals(nextTurn))
            .findFirst()
            .orElse(null);
        PlayerSlot winningSlot = players.stream()
            .filter(player -> !player.mark().equals(nextTurn))
            .findFirst()
            .orElse(null);

        status = "TurnTimedOut";
        winner = winningSlot == null ? "" : winningSlot.actor().actorId();
        nextTurn = "";
        lastMoveActorId = timedOut == null ? "" : timedOut.actor().actorId();
        lastMoveCell = -1;
        turnDeadline = null;

        return broadcast(snapshot(), null);
    }

    private void resetTurnDeadline() {
        turnDeadline = Instant.now().plus(TURN_TIMEOUT);
    }

    private void ensureCreated() {
        if (!created) {
            throw new IllegalStateException("tic-tac-toe game has not completed creation");
        }
    }

    private <T> T decode(Message request, Class<T> type) {
        try {
            return json.readValue(request.toByteArray(), type);
        } catch (java.io.IOException ex) {
            throw new IllegalArgumentException("failed to decode " + type.getSimpleName(), ex);
        }
    }

    private Message encode(Object reply) {
        try {
            return Message.from(json.writeValueAsBytes(reply));
        } catch (JsonProcessingException ex) {
            throw new IllegalArgumentException("failed to encode tic-tac-toe join reply", ex);
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
            .filter(actor -> excludedActorId == null || !actor.actorId().equals(excludedActorId))
            .map(actor -> actor.context().boundSession()
                .send(new GameStateNotify(state))
                .submitAsync())
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
            .map(actor -> actor.context().boundSession()
                .send(message)
                .submitAsync())
            .toList();
        return allOf(sends);
    }

    private static CompletionStage<Void> allOf(List<CompletionStage<Void>> stages) {
        CompletionStage<Void> combined = CompletableFuture.completedFuture(null);
        for (CompletionStage<Void> stage : stages) {
            combined = combined.thenCompose(ignored -> stage);
        }
        return combined;
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

    private static final class PlayerSlot {
        private PlayActor actor;
        private final String mark;

        private PlayerSlot(PlayActor actor, String mark) {
            this.actor = actor;
            this.mark = mark;
        }

        private PlayActor actor() {
            return actor;
        }

        private void actor(PlayActor actor) {
            this.actor = actor;
        }

        private String mark() {
            return mark;
        }
    }
}
