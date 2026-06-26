package systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.bingoroomspot;

import static systems.zlink.framework.ZLinkAwait.await;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.time.Duration;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotCreateResponse;
import systems.zlink.framework.spots.ZLinkTimer;
import systems.zlink.framework.spots.ZLinkTimerOptions;
import systems.zlink.samples.bingo.server.configuration.SampleNames;
import systems.zlink.samples.bingo.server.configuration.SampleTimings;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.actors.PlayerActor;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.handlers.BingoRoomSpotCreatedHandler;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.handlers.BingoRoomTimerHandler;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.handlers.BingoWinnerEventHandler;
import systems.zlink.samples.bingo.server.play.domain.bingo.BingoGame;
import systems.zlink.samples.bingo.server.play.domain.bingo.BingoRoomGame;
import systems.zlink.samples.bingo.server.play.domain.bingo.BingoRoomModels;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class BingoRoomSpot implements ZLinkSpot<PlayerActor> {
    private final ZLinkSpotContext context;
    private final BingoRoomSpotCreatedHandler createdHandler;
    private final ObjectMapper json;
    private final Map<String, PlayerActor> actors = new HashMap<>();
    private final Map<String, PlayerActor> observers = new HashMap<>();
    private BingoRoomModels.BingoRoomSettings settings =
        BingoRoomModels.BingoRoomSettings.create("two-player", 0, SampleTimings.DrawPeriod.toMillis());
    private BingoRoomGame game;
    private ZLinkTimer timer;
    private boolean cleanupStarted;

    public BingoRoomSpot(
        ZLinkSpotContext context,
        BingoRoomSpotCreatedHandler createdHandler,
        ObjectMapper json) {
        this.context = context;
        this.createdHandler = createdHandler;
        this.json = json;
        this.game = BingoGame.room(context.spotRid().toString(), settings);
    }

    @Override
    public ZLinkSpotContext context() {
        return context;
    }

    @Override
    public void configure() {
        context.handlers().addSubscribe(
            SampleNames.WinnerTopic,
            BingoWinnerEventHandler.class);
    }

    @Override
    public ZLinkSpotCreateResponse onCreate(ZLinkMessage request) {
        return createdHandler.handle(this, request);
    }

    @Override
    public ZLinkSpotActorJoinResponse onActorJoin(
        PlayerActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken) {
        Messages.BingoRoomJoinReq joinRequest = request.decode(Messages.BingoRoomJoinReq.class);
        return ZLinkSpotActorJoinResponse.accept(join(actor, joinRequest));
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
        observers.remove(actor.actorId());
    }

    @Override
    public void onDisconnectActor(
        PlayerActor actor,
        CancellationToken cancellationToken) {
        actor.markDisconnected();
    }

    @Override
    public void onInitialize() {
        if (settings.observerMode()) {
            return;
        }
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
        if (!request.observeOnly() && !request.roomId().equals(context.spotRid().toString())) {
            throw new IllegalStateException("Join request room id does not match bingo room.");
        }
        actor.setDisplayName(request.displayName());
        actor.joinRoom(request.roomId());
        if (request.observeOnly()) {
            return joinObserver(actor, request);
        }
        if (settings.observerMode()) {
            throw new IllegalStateException("Player actor cannot join an observer BingoRoom.");
        }
        actors.put(actor.actorId(), actor);
        BingoRoomGame.Change change = game.join(actor.actorId(), request.displayName());
        publishEvents(
            change.events(),
            actorId -> actorId.equals(actor.actorId()) ? null : actors.get(actorId));
        return new Messages.BingoRoomJoinRes(change.state());
    }

    public Messages.SubmitBingoCardRes submitCard(
        PlayerActor actor,
        Messages.SubmitBingoCardReq request) {
        if (!request.roomId().equals(context.spotRid().toString())) {
            throw new IllegalStateException("Submit request room id does not match bingo room.");
        }
        if (game == null) {
            throw new IllegalStateException("Observer BingoRoom does not own game state.");
        }
        BingoRoomGame.Change change = game.submitCard(actor.actorId(), request.card());
        publishEvents(change.events(), actors::get);
        return new Messages.SubmitBingoCardRes(change.state());
    }

    public void tick() {
        if (game == null || cleanupStarted) {
            return;
        }
        BingoRoomGame.Change change = game.drawNext();
        publishEvents(change.events(), actors::get);
        publishWinner(change);
        leaveFinishedActors(change);
    }

    public void announceWinner(Messages.BingoWinnerEvent event) {
        if (!settings.observerMode()
            || observers.isEmpty()
            || !event.roomId().equals(settings.observedRoomId())) {
            return;
        }
        for (PlayerActor observer : List.copyOf(observers.values())) {
            observer.push(new Messages.BingoRewardAnnouncedNotify(
                    event.roomId(),
                    event.actorId(),
                    event.drawSeq(),
                    event.itemId(),
                    event.itemName(),
                    event.rarity(),
                    context.nodeRid().toString()));
        }
    }

    public Messages.StopObservingBingoEventsRes stopObserving(
        PlayerActor actor,
        Messages.StopObservingBingoEventsReq request) {
        if (!settings.observerMode()
            || !request.roomId().equals(settings.observedRoomId())
            || !observers.containsKey(actor.actorId())) {
            return new Messages.StopObservingBingoEventsRes(false, context.nodeRid().toString());
        }
        observers.remove(actor.actorId());
        await(context.leaveActor(actor));
        return new Messages.StopObservingBingoEventsRes(true, context.nodeRid().toString());
    }

    public void applySettings(BingoRoomModels.BingoRoomSettings settings) {
        if (!settings.observerMode() && settings.requiredPlayers() <= 0) {
            throw new IllegalStateException("Bingo room requires at least one player.");
        }
        if (settings.maxDrawNumber() <= 0) {
            throw new IllegalStateException("Bingo room requires at least one draw number.");
        }
        if (settings.drawPeriodMillis() <= 0) {
            throw new IllegalStateException("Bingo room draw period must be positive.");
        }
        this.settings = settings;
        this.game = settings.observerMode() ? null : BingoGame.room(context.spotRid().toString(), settings);
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

    private void publishWinner(BingoRoomGame.Change change) {
        if (!change.state().status().equals("Finished") || change.state().winners().isEmpty()) {
            return;
        }
        String winner = change.state().winners().getFirst();
        context.outbound()
            .publish(
                SampleNames.WinnerTopic,
                new Messages.BingoWinnerEvent(
                    change.state().roomId(),
                    winner,
                    change.state().drawSeq(),
                    "rare-golden-dauber",
                    "Golden Dauber",
                    "Legendary"))
            .await();
    }

    private void publishEvents(
        List<BingoRoomModels.RoomEvent> events,
        java.util.function.Function<String, PlayerActor> actorResolver) {
        for (BingoRoomModels.RoomEvent event : events) {
            publishEvent(event, actorResolver.apply(event.recipientActorId()));
        }
    }

    private void publishEvent(BingoRoomModels.RoomEvent event, PlayerActor recipient) {
        if (recipient == null) {
            return;
        }
        switch (event.kind()) {
            case PLAYER_JOINED -> recipient.push(new Messages.PlayerJoinedNotify(
                    event.state().roomId(),
                    event.joinedActorId(),
                    event.joinedDisplayName(),
                    event.seat(),
                    event.host(),
                    event.state()));
            case GAME_STARTED -> recipient.push(new Messages.BingoGameStartedNotify(event.state()));
            case NUMBER_DRAWN -> recipient.push(new Messages.BingoNumberDrawnNotify(
                    event.state().roomId(),
                    event.state().drawSeq(),
                    event.drawnNumber(),
                    event.state()));
            case STATE -> recipient.push(new Messages.BingoStateNotify(event.state()));
            case GAME_ENDED -> recipient.push(new Messages.BingoGameEndedNotify(event.state()));
        }
    }

    private Messages.BingoRoomJoinRes joinObserver(
        PlayerActor actor,
        Messages.BingoRoomJoinReq request) {
        if (!settings.observerMode() || !request.roomId().equals(settings.observedRoomId())) {
            throw new IllegalStateException("Observe-only actor can join only its observer BingoRoom.");
        }
        observers.put(actor.actorId(), actor);
        return new Messages.BingoRoomJoinRes(new Messages.BingoRoomState(
            request.roomId(),
            "Running",
            "",
            false,
            0,
            null,
            List.of(),
            List.of(),
            List.of()));
    }

}
