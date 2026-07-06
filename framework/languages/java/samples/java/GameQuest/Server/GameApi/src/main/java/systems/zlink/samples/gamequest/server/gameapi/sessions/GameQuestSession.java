package systems.zlink.samples.gamequest.server.gameapi.sessions;

import static systems.zlink.framework.ZLinkAwait.await;

import java.time.Instant;
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
    private String playerId;

    public GameQuestSession(
        ZLinkSessionContext context,
        ZLinkClient channels,
        GameQuestStore store) {
        this.context = context;
        this.channels = channels;
        this.store = store;
    }

    @Override
    public ZLinkSessionContext context() {
        return context;
    }

    @Override
    public void onConnected() {
    }

    @Override
    public void onDisconnected() {
        if (playerId != null) {
            store.unbind(playerId);
        }
    }

    @Override
    public void onError(ZLinkStreamError error) {
    }

    @Override
    public void onDispatch(ZLinkSessionDispatchContext dispatch, ZLinkMessage payload) {
        switch (dispatch.packetName()) {
            case "JoinSessionReq" -> handleJoin(payload.decode(Messages.JoinSessionReq.class));
            case "GetQuestProgressReq" -> handleGetProgress(payload.decode(Messages.GetQuestProgressReq.class));
            case "SyncQuestProgressReq" -> handleSync(payload.decode(Messages.SyncQuestProgressReq.class));
            case "KillMonsterReq" -> handleKill(payload.decode(Messages.KillMonsterReq.class));
            case "CollectItemReq" -> handleCollect(payload.decode(Messages.CollectItemReq.class));
            case "CompleteMissionReq" -> handleMission(payload.decode(Messages.CompleteMissionReq.class));
            case "EnterAreaReq" -> handleArea(payload.decode(Messages.EnterAreaReq.class));
            case "UnlockFeatureReq" -> handleFeature(payload.decode(Messages.UnlockFeatureReq.class));
            default -> throw new IllegalStateException("Unknown GameQuest packet: " + dispatch.packetName());
        }
    }

    private void handleJoin(Messages.JoinSessionReq request) {
        playerId = request.playerId();
        store.bind(request.playerId(), SampleTopology.apiName());
        Messages.GetQuestProgressRes ownerProjection = channels
            .requestToChannel(
                ownerChannel(request.playerId()),
                new Messages.GetQuestProgressReq(request.playerId()))
            .await(Messages.GetQuestProgressRes.class);
        store.mergeProjection(request.playerId(), ownerProjection.activeQuests());
        context.client()
            .reply(new Messages.JoinSessionRes(ownerProjection.activeQuests()))
            .submit();
    }

    private void handleGetProgress(Messages.GetQuestProgressReq request) {
        Messages.GetQuestProgressRes ownerProjection = channels
            .requestToChannel(
                ownerChannel(request.playerId()),
                request)
            .await(Messages.GetQuestProgressRes.class);
        store.mergeProjection(request.playerId(), ownerProjection.activeQuests());
        context.client()
            .reply(ownerProjection)
            .submit();
    }

    private void handleSync(Messages.SyncQuestProgressReq request) {
        Messages.SyncQuestProgressRes response = channels
            .requestToChannel(
                ownerChannel(request.playerId()),
                request)
            .await(Messages.SyncQuestProgressRes.class);
        store.mergeProjection(request.playerId(), response.updatedQuests());
        context.client().reply(response).submit();
    }

    private void handleKill(Messages.KillMonsterReq request) {
        Messages.GameplayEventEnvelope event = event(
            request.playerId(),
            request.idempotencyKey(),
            "kill",
            request.monsterId(),
            1,
            true);
        store.recordGameplay(event);
        Messages.QuestProcessingRes processed = process(event);
        context.client().reply(new Messages.KillMonsterRes(processed.eventId())).submit();
    }

    private void handleCollect(Messages.CollectItemReq request) {
        Messages.GameplayEventEnvelope event = event(
            request.playerId(),
            request.idempotencyKey(),
            "collect",
            request.itemId(),
            request.count(),
            true);
        store.recordGameplay(event);
        Messages.QuestProcessingRes processed = process(event);
        context.client().reply(new Messages.CollectItemRes(processed.eventId())).submit();
    }

    private void handleMission(Messages.CompleteMissionReq request) {
        Messages.GameplayEventEnvelope event = event(
            request.playerId(),
            request.idempotencyKey(),
            "mission",
            request.missionId(),
            1,
            true);
        store.recordGameplay(event);
        Messages.QuestProcessingRes processed = process(event);
        context.client().reply(new Messages.CompleteMissionRes(processed.eventId())).submit();
    }

    private void handleArea(Messages.EnterAreaReq request) {
        Messages.GameplayEventEnvelope event = event(
            request.playerId(),
            request.idempotencyKey(),
            "area",
            request.areaId(),
            1,
            true);
        store.recordGameplay(event);
        Messages.QuestProcessingRes processed = process(event);
        context.client().reply(new Messages.EnterAreaRes(processed.eventId())).submit();
    }

    private void handleFeature(Messages.UnlockFeatureReq request) {
        Messages.GameplayEventEnvelope event = event(
            request.playerId(),
            request.idempotencyKey(),
            "feature",
            request.featureId(),
            1,
            true);
        store.recordGameplay(event);
        Messages.QuestProcessingRes processed = process(event);
        context.client().reply(new Messages.UnlockFeatureRes(processed.eventId())).submit();
    }

    private Messages.QuestProcessingRes process(Messages.GameplayEventEnvelope event) {
        Messages.QuestProcessingRes processed = channels
            .requestToChannel(
                ownerChannel(event.playerId()),
                event)
            .await(Messages.QuestProcessingRes.class);
        store.mergeProjection(event.playerId(), processed.projection());
        processed.progressNotifications().forEach(notification -> context.client().send(notification).submit());
        processed.completedNotifications().forEach(notification -> context.client().send(notification).submit());
        return processed;
    }

    private static String ownerChannel(String playerId) {
        return SampleNames.questOwnerChannelFor(SampleTopology.ownerMissionName(playerId));
    }

    private static Messages.GameplayEventEnvelope event(
        String playerId,
        String idempotencyKey,
        String eventType,
        String value,
        int count,
        boolean publish) {
        return new Messages.GameplayEventEnvelope(
            playerId + "-" + idempotencyKey,
            playerId,
            idempotencyKey,
            eventType,
            value,
            count,
            SampleTopology.apiName(),
            Instant.now().toEpochMilli(),
            publish);
    }
}
