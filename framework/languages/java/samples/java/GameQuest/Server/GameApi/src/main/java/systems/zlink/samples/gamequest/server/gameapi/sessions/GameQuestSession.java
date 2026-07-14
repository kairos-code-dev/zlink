package systems.zlink.samples.gamequest.server.gameapi.sessions;

import java.time.Instant;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkStreamError;
import systems.zlink.samples.gamequest.server.configuration.SampleNames;
import systems.zlink.samples.gamequest.server.configuration.SampleTopology;
import systems.zlink.samples.gamequest.server.gameapi.store.GameQuestStore;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

public final class GameQuestSession implements ZLinkSession {
    private final ZLinkSessionContext context;
    private final ZLinkClient channels;
    private final GameQuestStore store;
    private final SampleTopology topology;
    private final GameQuestSessionRegistry sessions;
    private String playerId;

    public GameQuestSession(
        ZLinkSessionContext context,
        ZLinkClient channels,
        GameQuestStore store,
        SampleTopology topology,
        GameQuestSessionRegistry sessions) {
        this.context = context;
        this.channels = channels;
        this.store = store;
        this.topology = topology;
        this.sessions = sessions;
    }

    @Override
    public ZLinkSessionContext context() {
        return context;
    }

    @Override
    public java.util.concurrent.CompletionStage<Void> onConnected() {
        return java.util.concurrent.CompletableFuture.completedFuture(null);
    }

    @Override
    public java.util.concurrent.CompletionStage<Void> onDisconnected() {
        if (playerId != null) {
            sessions.unbind(playerId, this);
            store.unbind(playerId);
        }
        return java.util.concurrent.CompletableFuture.completedFuture(null);
    }

    @Override
    public java.util.concurrent.CompletionStage<Void> onError(ZLinkStreamError error) {
        return java.util.concurrent.CompletableFuture.completedFuture(null);
    }

    @Override
    public java.util.concurrent.CompletionStage<Void> onDispatch(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload) {
        return switch (dispatch.packetName()) {
            case "JoinSessionReq" -> handleJoin(payload.decode(Messages.JoinSessionReq.class));
            case "GetQuestProgressReq" -> handleGetProgress(payload.decode(Messages.GetQuestProgressReq.class));
            case "SyncQuestProgressReq" -> handleSync(payload.decode(Messages.SyncQuestProgressReq.class));
            case "KillMonsterReq" -> handleKill(payload.decode(Messages.KillMonsterReq.class));
            case "CollectItemReq" -> handleCollect(payload.decode(Messages.CollectItemReq.class));
            case "CompleteMissionReq" -> handleMission(payload.decode(Messages.CompleteMissionReq.class));
            case "EnterAreaReq" -> handleArea(payload.decode(Messages.EnterAreaReq.class));
            case "UnlockFeatureReq" -> handleFeature(payload.decode(Messages.UnlockFeatureReq.class));
            default -> throw new IllegalStateException("Unknown GameQuest packet: " + dispatch.packetName());
        };
    }

    private java.util.concurrent.CompletionStage<Void> handleJoin(Messages.JoinSessionReq request) {
        playerId = request.playerId();
        sessions.bind(request.playerId(), this);
        store.bind(request.playerId(), topology.gameApi().instanceName());
        return channels
            .requestToChannel(
                ownerChannel(request.playerId()),
                new Messages.GetQuestProgressReq(request.playerId()))
            .submit(Messages.GetQuestProgressRes.class)
            .thenAccept(ownerProjection -> {
                store.mergeProjection(request.playerId(), ownerProjection.activeQuests());
                context.client().reply(new Messages.JoinSessionRes(ownerProjection.activeQuests())).submit();
            });
    }

    private java.util.concurrent.CompletionStage<Void> handleGetProgress(Messages.GetQuestProgressReq request) {
        return channels
            .requestToChannel(
                ownerChannel(request.playerId()),
                request)
            .submit(Messages.GetQuestProgressRes.class)
            .thenAccept(ownerProjection -> {
                store.mergeProjection(request.playerId(), ownerProjection.activeQuests());
                context.client().reply(ownerProjection).submit();
            });
    }

    private java.util.concurrent.CompletionStage<Void> handleSync(Messages.SyncQuestProgressReq request) {
        return channels
            .requestToChannel(
                ownerChannel(request.playerId()),
                request)
            .submit(Messages.SyncQuestProgressRes.class)
            .thenAccept(response -> {
                store.mergeProjection(request.playerId(), response.updatedQuests());
                context.client().reply(response).submit();
            });
    }

    private java.util.concurrent.CompletionStage<Void> handleKill(Messages.KillMonsterReq request) {
        Messages.GameplayMsg event = event(
            request.playerId(),
            request.idempotencyKey(),
            "kill",
            request.monsterId(),
            1,
            true);
        store.recordGameplay(event);
        return process(event).thenAccept(ignored ->
            context.client().reply(new Messages.KillMonsterRes(event.eventId())).submit());
    }

    private java.util.concurrent.CompletionStage<Void> handleCollect(Messages.CollectItemReq request) {
        Messages.GameplayMsg event = event(
            request.playerId(),
            request.idempotencyKey(),
            "collect",
            request.itemId(),
            request.count(),
            true);
        store.recordGameplay(event);
        return process(event).thenAccept(ignored ->
            context.client().reply(new Messages.CollectItemRes(event.eventId())).submit());
    }

    private java.util.concurrent.CompletionStage<Void> handleMission(Messages.CompleteMissionReq request) {
        Messages.GameplayMsg event = event(
            request.playerId(),
            request.idempotencyKey(),
            "mission",
            request.missionId(),
            1,
            true);
        store.recordGameplay(event);
        return process(event).thenAccept(ignored ->
            context.client().reply(new Messages.CompleteMissionRes(event.eventId())).submit());
    }

    private java.util.concurrent.CompletionStage<Void> handleArea(Messages.EnterAreaReq request) {
        Messages.GameplayMsg event = event(
            request.playerId(),
            request.idempotencyKey(),
            "area",
            request.areaId(),
            1,
            true);
        store.recordGameplay(event);
        return process(event).thenAccept(ignored ->
            context.client().reply(new Messages.EnterAreaRes(event.eventId())).submit());
    }

    private java.util.concurrent.CompletionStage<Void> handleFeature(Messages.UnlockFeatureReq request) {
        Messages.GameplayMsg event = event(
            request.playerId(),
            request.idempotencyKey(),
            "feature",
            request.featureId(),
            1,
            true);
        store.recordGameplay(event);
        return process(event).thenAccept(ignored ->
            context.client().reply(new Messages.UnlockFeatureRes(event.eventId())).submit());
    }

    private java.util.concurrent.CompletionStage<Void> process(
        Messages.GameplayMsg event) {
        channels.sendToChannel(ownerChannel(event.playerId()), event).submit();
        return java.util.concurrent.CompletableFuture.completedFuture(null);
    }

    CompletionStage<Void> deliver(Messages.QuestProcessingMsg message) {
        if (!message.playerId().equals(playerId)) {
            return java.util.concurrent.CompletableFuture.completedFuture(null);
        }
        store.mergeProjection(message.playerId(), message.projection());
        message.progressNotifications().forEach(notification ->
            context.client().send(notification).submit());
        message.completedNotifications().forEach(notification ->
            context.client().send(notification).submit());
        return java.util.concurrent.CompletableFuture.completedFuture(null);
    }

    private String ownerChannel(String playerId) {
        return SampleNames.questOwnerChannelFor(topology.ownerMissionName(playerId));
    }

    private Messages.GameplayMsg event(
        String playerId,
        String idempotencyKey,
        String eventType,
        String value,
        int count,
        boolean publish) {
        return Messages.GameplayMsg.create(
            playerId + "-" + idempotencyKey,
            playerId,
            eventType,
            idempotencyKey,
            value,
            count,
            topology.gameApi().instanceName(),
            Instant.now().toEpochMilli(),
            publish);
    }
}
