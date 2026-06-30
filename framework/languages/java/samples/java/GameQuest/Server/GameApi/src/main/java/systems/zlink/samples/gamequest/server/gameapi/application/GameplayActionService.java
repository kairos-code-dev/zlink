package systems.zlink.samples.gamequest.server.gameapi.application;

import org.springframework.stereotype.Component;
import systems.zlink.samples.gamequest.server.configuration.GameQuestRouting;
import systems.zlink.samples.gamequest.server.gameapi.GameApiInstanceOptions;
import systems.zlink.samples.gamequest.server.gameapi.domain.GameplayDomain;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

/**
 * Stateless gameplay action service. Each action stores an idempotent gameplay
 * event and publishes it through the gameplay event publisher; the QuestMission server
 * subscribes and advances quest progress. Mirrors the .NET
 * {@code GameplayActionService}.
 */
@Component
public final class GameplayActionService {
    private final GameplayEventStore store;
    private final GameplayEventPublisher events;
    private final QuestProgressSynchronizer quests;
    private final String apiName;

    public GameplayActionService(
        GameplayEventStore store,
        GameplayEventPublisher events,
        QuestProgressSynchronizer quests,
        GameApiInstanceOptions options) {
        this.store = store;
        this.events = events;
        this.quests = quests;
        this.apiName = options.apiName();
    }

    public Messages.KillMonsterRes killMonster(Messages.KillMonsterReq request) {
        Messages.GameplayEventMsg published = storeAndPublish(
            GameplayDomain.createMonsterKilled(
                request.playerId(), request.monsterId(), request.areaId(), request.idempotencyKey(), apiName));
        return new Messages.KillMonsterRes(published.eventId());
    }

    public Messages.CollectItemRes collectItem(Messages.CollectItemReq request) {
        Messages.GameplayEventMsg published = storeAndPublish(
            GameplayDomain.createItemCollected(
                request.playerId(), request.itemId(), request.count(), request.idempotencyKey(), apiName));
        return new Messages.CollectItemRes(published.eventId());
    }

    public Messages.CompleteMissionRes completeMission(Messages.CompleteMissionReq request) {
        Messages.GameplayEventMsg published = storeAndPublish(
            GameplayDomain.createMissionCompleted(
                request.playerId(), request.missionId(), request.idempotencyKey(), apiName));
        return new Messages.CompleteMissionRes(published.eventId());
    }

    public Messages.EnterAreaRes enterArea(Messages.EnterAreaReq request) {
        Messages.GameplayEventMsg published = storeAndPublish(
            GameplayDomain.createAreaEntered(
                request.playerId(), request.areaId(), request.idempotencyKey(), apiName));
        return new Messages.EnterAreaRes(published.eventId());
    }

    public Messages.UnlockFeatureRes unlockFeature(Messages.UnlockFeatureReq request) {
        Messages.GameplayEventMsg published = storeAndPublish(
            GameplayDomain.createFeatureUnlocked(
                request.playerId(), request.featureId(), request.idempotencyKey(), apiName));
        return new Messages.UnlockFeatureRes(published.eventId());
    }

    public Messages.SyncQuestProgressRes sync(String playerId) {
        String missionName = GameQuestRouting.ownerIndex(playerId) == 1 ? "mission-b" : "mission-a";
        return quests.sync(missionName, playerId);
    }

    private Messages.GameplayEventMsg storeAndPublish(Messages.GameplayEventMsg candidate) {
        Messages.GameplayEventMsg stored = store.getOrAddGameplayEvent(candidate);
        events.publish(stored);
        System.err.printf(
            "gamequest api event published api=%s player=%s event=%s type=%s%n",
            apiName, stored.playerId(), stored.eventId(), stored.eventType());
        return stored;
    }

    public interface GameplayEventStore {
        Messages.GameplayEventMsg getOrAddGameplayEvent(Messages.GameplayEventMsg candidate);
    }

    public interface GameplayEventPublisher {
        void publish(Messages.GameplayEventMsg event);
    }

    public interface QuestProgressSynchronizer {
        Messages.SyncQuestProgressRes sync(String missionName, String playerId);
    }
}
