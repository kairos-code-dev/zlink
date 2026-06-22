package systems.zlink.samples.gamequest.server.questmission.application;

import java.util.List;
import org.springframework.stereotype.Component;
import systems.zlink.samples.gamequest.server.questmission.QuestMissionOptions;
import systems.zlink.samples.gamequest.server.questmission.domain.QuestDomain;
import systems.zlink.samples.gamequest.server.questmission.domain.QuestDomain.QuestDefinition;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

/**
 * Owns the event-sourced quest workflow: it dedupes the source gameplay event,
 * applies the projection transition, appends quest events, and notifies the
 * bound GameApi over its channel. Mirrors the .NET {@code QuestEventProcessor}.
 */
@Component
public final class QuestEventProcessor {
    private final QuestProgressStore store;
    private final PlayerQuestOwnerProvisioner playerQuestOwners;
    private final QuestOwnerRouter ownerRouter;
    private final GameApiSnapshotClient snapshots;
    private final QuestProgressNotifier notifications;
    private final String missionName;

    public QuestEventProcessor(
        QuestProgressStore store,
        PlayerQuestOwnerProvisioner playerQuestOwners,
        QuestOwnerRouter ownerRouter,
        GameApiSnapshotClient snapshots,
        QuestProgressNotifier notifications,
        QuestMissionOptions options) {
        this.store = store;
        this.playerQuestOwners = playerQuestOwners;
        this.ownerRouter = ownerRouter;
        this.snapshots = snapshots;
        this.notifications = notifications;
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

        playerQuestOwners.ensure(gameplayEvent.playerId());
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
        boolean notified = notifications.notify(
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

        Messages.GetGameplaySnapshotRes snapshot = snapshots.read("api-a", request.playerId());
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

    public interface QuestProgressStore {
        java.util.Optional<Messages.QuestProgress> readProgress(String playerId, String questId);

        List<Messages.QuestProgress> readProjection(String playerId);

        boolean hasSourceEvent(String playerId, String questId, String sourceEventId);

        boolean appendAndProject(Messages.QuestProgress progress, List<Messages.StoredQuestEvent> events);
    }

    public interface PlayerQuestOwnerProvisioner {
        void ensure(String playerId);
    }

    public interface GameApiSnapshotClient {
        Messages.GetGameplaySnapshotRes read(String apiName, String playerId);
    }

    public interface QuestProgressNotifier {
        boolean notify(String sourceApi, Messages.NotifyQuestProgressReq request);
    }
}
