package systems.zlink.samples.bingo.server.play.adapters.zlink.spots;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.time.Duration;
import java.util.HashMap;
import java.util.Map;
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
import systems.zlink.samples.bingo.server.play.adapters.zlink.actors.PlayerActor;
import systems.zlink.samples.bingo.server.play.adapters.zlink.notifications.BingoNotificationPublisher;
import systems.zlink.samples.bingo.server.play.adapters.zlink.spots.handlers.BingoRoomSpotCreatedHandler;
import systems.zlink.samples.bingo.server.play.adapters.zlink.spots.handlers.BingoRoomTimerHandler;
import systems.zlink.samples.bingo.server.play.domain.bingo.BingoGame;
import systems.zlink.samples.bingo.server.play.domain.bingo.BingoRoomGame;
import systems.zlink.samples.bingo.server.play.domain.bingo.BingoRoomModels;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class BingoRoomSpot implements ZLinkSpot {
    private final ZLinkSpotContext context;
    private final BingoNotificationPublisher notifications;
    private final BingoRoomSpotCreatedHandler createdHandler;
    private final ObjectMapper json;
    private final Map<String, PlayerActor> actors = new HashMap<>();
    private BingoRoomModels.BingoRoomSettings settings =
        BingoRoomModels.BingoRoomSettings.create("two-player", 0);
    private BingoRoomGame game;
    private ZLinkTimer timer;

    public BingoRoomSpot(
        ZLinkSpotContext context,
        BingoNotificationPublisher notifications,
        BingoRoomSpotCreatedHandler createdHandler,
        ObjectMapper json) {
        this.context = context;
        this.notifications = notifications;
        this.createdHandler = createdHandler;
        this.json = json;
        this.game = BingoGame.room(context.spotRid().toString(), settings);
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
        if (!(actor instanceof PlayerActor player)) {
            return CompletableFuture.failedFuture(
                new IllegalArgumentException("Bingo room only accepts PlayerActor."));
        }
        Messages.BingoRoomJoinReq joinRequest = decode(request, Messages.BingoRoomJoinReq.class);
        return joinAsync(player, joinRequest)
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
                "bingo-draw",
                Duration.ofMillis(settings.drawPeriodMillis()),
                BingoRoomTimerHandler.class,
                new ZLinkTimerOptions())
            .thenAccept(created -> timer = created);
    }

    @Override
    public CompletionStage<Void> onClosingAsync() {
        return timer == null ? CompletableFuture.completedFuture(null) : timer.cancelAsync();
    }

    public CompletionStage<Messages.BingoRoomJoinRes> joinAsync(
        PlayerActor actor,
        Messages.BingoRoomJoinReq request) {
        if (!actor.actorId().equals(request.actorId())) {
            return CompletableFuture.failedFuture(new IllegalStateException(
                "Join request actor id does not match bound actor."));
        }
        if (!request.roomId().equals(context.spotRid().toString())) {
            return CompletableFuture.failedFuture(new IllegalStateException(
                "Join request room id does not match bingo room."));
        }
        actor.setDisplayName(request.displayName());
        actor.joinRoom(request.roomId());
        actors.put(actor.actorId(), actor);
        BingoRoomGame.Change change = game.join(actor.actorId(), request.displayName());
        return notifications.publishAsync(change.events(), actors::get)
            .thenApply(ignored -> new Messages.BingoRoomJoinRes(change.state()));
    }

    public CompletionStage<Messages.SubmitBingoCardRes> submitCardAsync(
        PlayerActor actor,
        Messages.SubmitBingoCardReq request) {
        if (!request.roomId().equals(context.spotRid().toString())) {
            return CompletableFuture.failedFuture(new IllegalStateException(
                "Submit request room id does not match bingo room."));
        }
        BingoRoomGame.Change change = game.submitCard(actor.actorId(), request.card());
        return notifications.publishAsync(change.events(), actors::get)
            .thenApply(ignored -> new Messages.SubmitBingoCardRes(change.state()));
    }

    public CompletionStage<Void> tickAsync() {
        BingoRoomGame.Change change = game.drawNext();
        return notifications.publishAsync(change.events(), actors::get);
    }

    public void applySettings(BingoRoomModels.BingoRoomSettings settings) {
        if (settings.requiredPlayers() <= 0) {
            throw new IllegalStateException("Bingo room requires at least one player.");
        }
        if (settings.maxDrawNumber() <= 0) {
            throw new IllegalStateException("Bingo room requires at least one draw number.");
        }
        if (settings.drawPeriodMillis() <= 0) {
            throw new IllegalStateException("Bingo room draw period must be positive.");
        }
        this.settings = settings;
        this.game = BingoGame.room(context.spotRid().toString(), settings);
    }

    private <T> T decode(Message request, Class<T> type) {
        try {
            return json.readValue(request.toByteArray(), type);
        } catch (java.io.IOException ex) {
            throw new IllegalArgumentException("Failed to decode " + type.getSimpleName() + ".", ex);
        }
    }

    private Message encode(Object reply) {
        try {
            return Message.from(json.writeValueAsBytes(reply));
        } catch (JsonProcessingException ex) {
            throw new IllegalArgumentException("Failed to encode bingo room join reply.", ex);
        }
    }
}
