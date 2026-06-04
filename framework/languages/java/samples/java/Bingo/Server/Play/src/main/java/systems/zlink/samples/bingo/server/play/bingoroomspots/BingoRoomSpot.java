package systems.zlink.samples.bingo.server.play.bingoroomspots;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import java.util.Queue;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkTimer;
import systems.zlink.framework.spots.ZLinkTimerOptions;
import systems.zlink.samples.bingo.server.play.actors.PlayerActor;
import systems.zlink.samples.bingo.server.play.bingoroomspots.handlers.BingoRoomTimerHandler;
import systems.zlink.samples.bingo.shared.configuration.SampleTimings;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class BingoRoomSpot implements ZLinkSpot {
    private static final String WaitingForPlayers = "WaitingForPlayers";
    private static final String Running = "Running";
    private static final String Finished = "Finished";
    private static final int RequiredPlayers = 4;
    private static final int MaxDrawNumber = 75;
    private static final List<String> SameSequenceWinnerProbe =
        List.of("player-2", "player-3");

    private final ZLinkSpotContext context;
    private final BingoNotificationPublisher notifications;
    private final List<BingoRoomModels.RoomPlayer> players = new ArrayList<>();
    private final Queue<Integer> drawDeck = new ArrayDeque<>();
    private final List<Integer> drawnNumbers = new ArrayList<>();
    private final List<String> winners = new ArrayList<>();
    private ZLinkTimer timer;
    private String status = WaitingForPlayers;

    public BingoRoomSpot(
        ZLinkSpotContext context,
        BingoNotificationPublisher notifications) {
        this.context = context;
        this.notifications = notifications;
        resetDrawDeck();
    }

    @Override
    public ZLinkSpotContext context() {
        return context;
    }

    @Override
    public CompletionStage<Void> onInitializeAsync() {
        return context.addTimer(
                "bingo-draw",
                SampleTimings.DrawPeriod,
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
        BingoRoomModels.RoomPlayer existing = players.stream()
            .filter(player -> player.actor().actorId().equals(actor.actorId()))
            .findFirst()
            .orElse(null);
        if (existing != null) {
            return CompletableFuture.completedFuture(
                new Messages.BingoRoomJoinRes(snapshot()));
        }

        if (!status.equals(WaitingForPlayers) || players.size() >= RequiredPlayers) {
            return CompletableFuture.failedFuture(new IllegalStateException(
                "Room " + request.roomId() + " cannot accept more players."));
        }

        actor.setDisplayName(request.displayName());
        actor.joinRoom(request.roomId());
        BingoRoomModels.RoomPlayer player =
            new BingoRoomModels.RoomPlayer(actor, players.size(), BingoCard.create());
        players.add(player);

        Messages.BingoRoomState state = snapshot();
        return notifications.publishAsync(playerJoinedEvents(player, state))
            .thenApply(ignored -> new Messages.BingoRoomJoinRes(state));
    }

    public CompletionStage<Messages.StartBingoGameRes> startAsync(
        PlayerActor actor,
        Messages.StartBingoGameReq request) {
        if (!request.roomId().equals(context.spotRid().toHex())) {
            return CompletableFuture.failedFuture(new IllegalStateException(
                "Start request room id does not match actor room."));
        }
        if (players.isEmpty() || !players.getFirst().actor().actorId().equals(actor.actorId())) {
            return CompletableFuture.failedFuture(new IllegalStateException(
                "Only the host actor can start the bingo room."));
        }
        if (players.size() != RequiredPlayers) {
            return CompletableFuture.failedFuture(new IllegalStateException(
                "Bingo room requires exactly " + RequiredPlayers + " players before start."));
        }
        if (status.equals(WaitingForPlayers)) {
            status = Running;
            Messages.BingoRoomState state = snapshot();
            return notifications.publishAsync(eventsForAll(BingoRoomModels.EventKind.GAME_STARTED, state))
                .thenApply(ignored -> new Messages.StartBingoGameRes(snapshot()));
        }
        return CompletableFuture.completedFuture(new Messages.StartBingoGameRes(snapshot()));
    }

    public CompletionStage<Void> tickAsync() {
        if (!status.equals(Running) || drawDeck.isEmpty()) {
            return CompletableFuture.completedFuture(null);
        }

        int number = drawDeck.remove();
        drawnNumbers.add(number);
        List<String> newlyCompleted = new ArrayList<>();
        for (BingoRoomModels.RoomPlayer player : players) {
            int before = player.card().completedLines();
            player.card().markDrawnNumber(number);
            if (player.card().completedLines() > before) {
                newlyCompleted.add(player.actor().actorId());
            }
        }

        if (!newlyCompleted.isEmpty()) {
            status = Finished;
            winners.addAll(newlyCompleted);
        }

        Messages.BingoRoomState state = snapshot();
        BingoRoomModels.EventKind kind = status.equals(Finished)
            ? BingoRoomModels.EventKind.GAME_ENDED
            : BingoRoomModels.EventKind.STATE;
        return notifications.publishAsync(numberDrawnEvents(state, number))
            .thenCompose(ignored -> notifications.publishAsync(eventsForAll(kind, state)));
    }

    private Messages.BingoRoomState snapshot() {
        String hostActorId = players.isEmpty() ? "" : players.getFirst().actor().actorId();
        Integer lastDrawn = drawnNumbers.isEmpty() ? null : drawnNumbers.getLast();
        return new Messages.BingoRoomState(
            context.spotRid().toHex(),
            status,
            hostActorId,
            status.equals(WaitingForPlayers) && players.size() == RequiredPlayers,
            drawnNumbers.size(),
            lastDrawn,
            List.copyOf(drawnNumbers),
            players.stream().map(player -> player.toState(hostActorId)).toList(),
            List.copyOf(winners));
    }

    private List<BingoRoomModels.RoomEvent> playerJoinedEvents(
        BingoRoomModels.RoomPlayer joined,
        Messages.BingoRoomState state) {
        return players.stream()
            .map(player -> new BingoRoomModels.RoomEvent(
                BingoRoomModels.EventKind.PLAYER_JOINED,
                player.actor(),
                state,
                joined.actor().actorId(),
                joined.actor().displayName(),
                joined.seat(),
                joined.seat() == 0,
                0))
            .toList();
    }

    private List<BingoRoomModels.RoomEvent> numberDrawnEvents(
        Messages.BingoRoomState state,
        int number) {
        return players.stream()
            .map(player -> new BingoRoomModels.RoomEvent(
                BingoRoomModels.EventKind.NUMBER_DRAWN,
                player.actor(),
                state,
                null,
                null,
                -1,
                false,
                number))
            .toList();
    }

    private List<BingoRoomModels.RoomEvent> eventsForAll(
        BingoRoomModels.EventKind kind,
        Messages.BingoRoomState state) {
        return players.stream()
            .map(player -> new BingoRoomModels.RoomEvent(
                kind,
                player.actor(),
                state,
                null,
                null,
                -1,
                false,
                0))
            .toList();
    }

    private void resetDrawDeck() {
        drawDeck.clear();
        for (int number = 1; number <= MaxDrawNumber; number++) {
            drawDeck.add(number);
        }
    }
}
