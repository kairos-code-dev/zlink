package systems.zlink.samples.gamequest.server.questmission.application;

import static systems.zlink.framework.ZLinkAwait.await;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.nio.charset.StandardCharsets;
import java.util.List;
import org.springframework.stereotype.Component;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.spots.ZLinkSpotCreateResult;
import systems.zlink.framework.spots.ZLinkSpotCreateState;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.samples.gamequest.server.configuration.SampleNames;
import systems.zlink.samples.gamequest.server.questmission.QuestMissionOptions;
import systems.zlink.samples.gamequest.server.questmission.domain.QuestDomain;
import systems.zlink.samples.gamequest.server.questmission.domain.QuestDomain.QuestDefinition;
import systems.zlink.samples.gamequest.server.questmission.spots.PlayerQuestSpot;
import systems.zlink.samples.gamequest.server.questmission.store.QuestStore;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

/**
 * Owns the event-sourced quest workflow: it dedupes the source gameplay event,
 * applies the projection transition, appends quest events, and notifies the
 * bound GameApi over its channel. Mirrors the .NET {@code QuestEventProcessor}.
 */
@Component
public final class QuestEventProcessor {
    private final QuestStore store;
    private final ZLinkSpotManager spots;
    private final QuestOwnerRouter ownerRouter;
    private final ZLinkClient channels;
    private final ObjectMapper json;
    private final String missionName;

    public QuestEventProcessor(
        QuestStore store,
        ZLinkSpotManager spots,
        QuestOwnerRouter ownerRouter,
        ZLinkClient channels,
        ObjectMapper json,
        QuestMissionOptions options) {
        this.store = store;
        this.spots = spots;
        this.ownerRouter = ownerRouter;
        this.channels = channels;
        this.json = json;
        this.missionName = options.missionName();
    }

    public void process(Messages.GameplayEventEnvelope gameplayEvent) {
        if (!ownerRouter.isLocalOwner(gameplayEvent.playerId())) {
            return;
        }

        QuestDefinition definition = "SnapshotKillCount".equals(gameplayEvent.eventType())
            ? QuestDomain.firstHunt()
            : QuestDomain.match(gameplayEvent);
        if (definition == null) {
            return;
        }

        ensurePlayerQuestSpot(gameplayEvent.playerId());
        Messages.QuestProgress before = store
            .readProgress(gameplayEvent.playerId(), definition.questId())
            .orElse(null);
        if (store.hasSourceEvent(gameplayEvent.playerId(), definition.questId(), gameplayEvent.eventId())) {
            return;
        }

        Messages.QuestProgress after = QuestDomain.apply(definition, before, gameplayEvent);
        List<Messages.StoredQuestEvent> questEvents = QuestDomain.toEvents(definition, before, after, gameplayEvent);
        if (questEvents.isEmpty()) {
            return;
        }
        if (!store.appendAndProject(after, questEvents)) {
            return;
        }

        List<Messages.QuestProgress> projection = store.readProjection(gameplayEvent.playerId());
        boolean completed = questEvents.stream()
            .anyMatch(event -> "QuestCompletedEvent".equals(event.eventType()));
        boolean notified = notifyBoundGameApi(
            gameplayEvent.sourceApi(),
            new Messages.NotifyQuestProgressReq(
                gameplayEvent.playerId(),
                projection,
                completed ? definition.questId() : null));

        System.err.printf(
            "gamequest mission processed mission=%s player=%s quest=%s source=%s events=%d notified=%s%n",
            missionName, gameplayEvent.playerId(), definition.questId(), gameplayEvent.eventId(),
            questEvents.size(), notified);
    }

    public Messages.SyncQuestProgressRes sync(Messages.SyncQuestProgressReq request) {
        if (!ownerRouter.isLocalOwner(request.playerId())) {
            return new Messages.SyncQuestProgressRes(store.readProjection(request.playerId()));
        }

        Messages.GetGameplaySnapshotRes snapshot = channels
            .requestToChannel(
                SampleNames.gameApiActionChannel("api-a"),
                new Messages.GetGameplaySnapshotReq(request.playerId()))
            .timeout(SampleNames.RequestTimeout)
            .await(Messages.GetGameplaySnapshotRes.class);
        int killCount = snapshot.killCounts().stream()
            .mapToInt(Messages.KillCountSnapshot::count)
            .sum();
        if (killCount > 0) {
            process(new Messages.GameplayEventEnvelope(
                request.playerId() + "-snapshot-" + snapshot.snapshotVersion(),
                request.playerId(),
                "snapshot-" + snapshot.snapshotVersion(),
                "SnapshotKillCount",
                "kills",
                killCount,
                "api-a",
                System.currentTimeMillis()));
        }
        return new Messages.SyncQuestProgressRes(store.readProjection(request.playerId()));
    }

    private void ensurePlayerQuestSpot(String playerId) {
        Message payload = Message.from(encode(new PlayerQuestSpot.PlayerQuestSpotCreateReq(playerId)));
        try {
            ZLinkSpotCreateResult result = await(spots.getOrCreate(
                PlayerQuestSpot.class,
                RoutingId.from(("player:" + playerId).getBytes(StandardCharsets.UTF_8)),
                payload));
            if (result.state() == ZLinkSpotCreateState.REJECTED) {
                throw new IllegalStateException("Player quest spot for '" + playerId + "' was rejected.");
            }
        } finally {
            payload.close();
        }
    }

    private boolean notifyBoundGameApi(String sourceApi, Messages.NotifyQuestProgressReq request) {
        String apiName = "api-b".equals(sourceApi) ? "api-b" : "api-a";
        try {
            Messages.NotifyQuestProgressRes delivered = channels
                .requestToChannel(SampleNames.gameApiActionChannel(apiName), request)
                .timeout(SampleNames.RequestTimeout)
                .await(Messages.NotifyQuestProgressRes.class);
            return delivered != null && delivered.delivered();
        } catch (RuntimeException error) {
            System.err.printf(
                "gamequest mission projection kept while stream notify failed. player=%s%n",
                request.playerId());
            return false;
        }
    }

    private byte[] encode(Object value) {
        try {
            return json.writeValueAsBytes(value);
        } catch (JsonProcessingException ex) {
            throw new IllegalStateException("Failed to encode PlayerQuestSpotCreateReq.", ex);
        }
    }
}
