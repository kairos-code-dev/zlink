package systems.zlink.samples.tictactoe.server.play.adapters.zlink.spots;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.time.Duration;
import java.time.Instant;
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
import systems.zlink.samples.tictactoe.server.play.adapters.zlink.actors.PlayActor;
import systems.zlink.samples.tictactoe.server.play.domain.tictactoe.TicTacToeMatch;
import systems.zlink.samples.tictactoe.server.play.adapters.zlink.spots.handlers.TicTacToeGameCreatedHandler;
import systems.zlink.samples.tictactoe.server.play.adapters.zlink.spots.handlers.TicTacToeGameTimerHandler;
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
    private final String roomId;
    private final TicTacToeMatch match;
    private ZLinkTimer gameTick;
    private boolean created;
    private final TicTacToeGameCreatedHandler createdHandler;
    private final ObjectMapper json;

    public TicTacToeGame(
        ZLinkSpotContext context,
        TicTacToeGameCreatedHandler createdHandler,
        ObjectMapper json) {
        this.context = context;
        this.roomId = context.spotRid().toString();
        this.match = new TicTacToeMatch(roomId);
        this.createdHandler = createdHandler;
        this.json = json;
    }

    public String roomId() {
        return roomId;
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
        if (!player.actorId().equals(joinRequest.actorId())) {
            return CompletableFuture.failedFuture(
                new IllegalStateException("join request actor id does not match bound actor"));
        }
        return join(player, joinRequest.roomId())
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

    public CompletionStage<TicTacToeGameJoinRes> join(PlayActor actor, String roomId) {
        ensureCreated();
        if (!this.roomId.equals(roomId)) {
            throw new IllegalStateException("join request room id does not match game room");
        }
        TicTacToeMatch.JoinResult joined = match.join(
            actor.actorId(),
            Instant.now(),
            TURN_TIMEOUT);
        actor.joinGame(roomId);
        rememberActor(actor);
        CompletionStage<Void> notify = joined.newlyJoined()
            ? notifyPlayerJoined(actor, joined.mark(), joined.state())
            : CompletableFuture.completedFuture(null);
        return notify
            .thenCompose(ignored -> broadcast(joined.state(), actor.actorId()))
            .thenApply(ignored -> new TicTacToeGameJoinRes(joined.state()));
    }

    public CompletionStage<PlaceMarkRes> placeMark(PlayActor actor, int cell) {
        ensureCreated();
        GameState state = match.placeMark(
            actor.actorId(),
            cell,
            Instant.now(),
            TURN_TIMEOUT);
        return broadcast(state, actor.actorId())
            .thenApply(ignored -> new PlaceMarkRes(state));
    }

    public String winner() {
        return snapshot().winner();
    }

    public boolean hasPlayer(String actorId) {
        return actors.stream().anyMatch(actor -> actor.actorId().equals(actorId));
    }

    public GameState snapshot() {
        ensureCreated();
        return match.snapshot();
    }

    public CompletionStage<Void> tickAsync() {
        ensureCreated();
        GameState timedOut = match.timeOutCurrentTurn(Instant.now());
        if (timedOut == null) {
            return CompletableFuture.completedFuture(null);
        }
        return broadcast(timedOut, null);
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

    private final List<PlayActor> actors = new java.util.ArrayList<>();

    private CompletionStage<Void> broadcast(GameState state, String excludedActorId) {
        List<CompletionStage<Void>> sends = actors.stream()
            .filter(actor -> excludedActorId == null || !actor.actorId().equals(excludedActorId))
            .map(actor -> actor.context().boundSession()
                .send(new GameStateNotify(state))
                .submit())
            .toList();
        return allOf(sends);
    }

    private CompletionStage<Void> notifyPlayerJoined(
        PlayActor joinedActor,
        String mark,
        GameState state) {
        PlayerJoinedNotify message = new PlayerJoinedNotify(
            state.roomId(),
            joinedActor.actorId(),
            mark,
            state);
        List<CompletionStage<Void>> sends = actors.stream()
            .filter(actor -> !actor.actorId().equals(joinedActor.actorId()))
            .map(actor -> actor.context().boundSession()
                .send(message)
                .submit())
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

    private void rememberActor(PlayActor actor) {
        for (int i = 0; i < actors.size(); i++) {
            if (actors.get(i).actorId().equals(actor.actorId())) {
                actors.set(i, actor);
                return;
            }
        }
        actors.add(actor);
    }
}
