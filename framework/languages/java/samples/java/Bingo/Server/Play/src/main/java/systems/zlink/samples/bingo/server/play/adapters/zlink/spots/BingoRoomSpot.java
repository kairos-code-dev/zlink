package systems.zlink.samples.bingo.server.play.adapters.zlink.spots;

import static systems.zlink.framework.ZLinkAwait.await;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.time.Duration;
import java.util.HashMap;
import java.util.Map;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.CancellationToken;
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

public final class BingoRoomSpot implements ZLinkSpot<PlayerActor> {
    private final ZLinkSpotContext context;
    private final BingoNotificationPublisher notifications;
    private final BingoRoomSpotCreatedHandler createdHandler;
    private final ObjectMapper json;
    private final Map<String, PlayerActor> actors = new HashMap<>();
    private BingoRoomModels.BingoRoomSettings settings =
        BingoRoomModels.BingoRoomSettings.create("two-player", 0);
    private BingoRoomGame game;
    private ZLinkTimer timer;
    private boolean cleanupStarted;

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
    public ZLinkSpotCreateResponse onCreate(Message request) {
        return createdHandler.handle(this, request);
    }

    @Override
    public ZLinkSpotActorJoinResponse onActorJoin(
        PlayerActor actor,
        Message request,
        CancellationToken cancellationToken) {
        Messages.BingoRoomJoinReq joinRequest = decode(request, Messages.BingoRoomJoinReq.class);
        Message reply = encode(join(actor, joinRequest));
        return ZLinkSpotActorJoinResponse.accept(reply);
    }

    @Override
    public void onJoinedActor(
        PlayerActor actor,
        CancellationToken cancellationToken) {
    }

    @Override
    public void onLeaveActor(
        PlayerActor actor,
        CancellationToken cancellationToken) {
        actors.remove(actor.actorId());
    }

    @Override
    public void onDisconnectActor(
        PlayerActor actor,
        CancellationToken cancellationToken) {
        actor.markDisconnected();
    }

    @Override
    public void onInitialize() {
        timer = await(context.addTimer(
                "bingo-draw",
                Duration.ofMillis(settings.drawPeriodMillis()),
                BingoRoomTimerHandler.class,
                new ZLinkTimerOptions()));
    }

    @Override
    public void onClosing() {
        if (timer != null) {
            await(timer.cancelAsync());
        }
    }

    public Messages.BingoRoomJoinRes join(
        PlayerActor actor,
        Messages.BingoRoomJoinReq request) {
        if (!actor.actorId().equals(request.actorId())) {
            throw new IllegalStateException("Join request actor id does not match bound actor.");
        }
        if (!request.roomId().equals(context.spotRid().toString())) {
            throw new IllegalStateException("Join request room id does not match bingo room.");
        }
        actor.setDisplayName(request.displayName());
        actor.joinRoom(request.roomId());
        actors.put(actor.actorId(), actor);
        BingoRoomGame.Change change = game.join(actor.actorId(), request.displayName());
        notifications.publish(change.events(), actors::get);
        return new Messages.BingoRoomJoinRes(change.state());
    }

    public Messages.SubmitBingoCardRes submitCard(
        PlayerActor actor,
        Messages.SubmitBingoCardReq request) {
        if (!request.roomId().equals(context.spotRid().toString())) {
            throw new IllegalStateException("Submit request room id does not match bingo room.");
        }
        BingoRoomGame.Change change = game.submitCard(actor.actorId(), request.card());
        notifications.publish(change.events(), actors::get);
        return new Messages.SubmitBingoCardRes(change.state());
    }

    public void tick() {
        BingoRoomGame.Change change = game.drawNext();
        notifications.publish(change.events(), actors::get);
        leaveFinishedActors(change);
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
        this.cleanupStarted = false;
    }

    private void leaveFinishedActors(BingoRoomGame.Change change) {
        if (cleanupStarted || !change.state().status().equals("Finished")) {
            return;
        }

        cleanupStarted = true;
        for (PlayerActor actor : actors.values().toArray(PlayerActor[]::new)) {
            actor.markForDestroyAfterRoomLeave();
            await(context.leaveActor(actor));
        }
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
