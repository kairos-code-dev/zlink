package systems.zlink.samples.gamequest.server.questmission.domain;

import java.util.ArrayList;
import java.util.List;
import systems.zlink.samples.gamequest.server.configuration.QuestIds;
import systems.zlink.samples.gamequest.server.configuration.QuestStatuses;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

/** Quest catalog and projection builder. Mirrors the .NET {@code QuestDomain}. */
public final class QuestDomain {
    private QuestDomain() {
    }

    public record QuestDefinition(String questId, String eventType, String value, int required) {
    }

    public record PendingQuestEvent(
        String eventId,
        String sourceEventId,
        String playerId,
        String questId,
        String eventType,
        Object payload,
        long createdAtUnixMs) {
    }

    public static final List<QuestDefinition> CATALOG = List.of(
        new QuestDefinition(QuestIds.FirstHunt, "MonsterKilled", "*", 3),
        new QuestDefinition(QuestIds.OpenAuction, "FeatureUnlocked", "auction", 1),
        new QuestDefinition(QuestIds.HerbGathering, "ItemCollected", "healing-herb", 5),
        new QuestDefinition(QuestIds.ClearTutorial, "MissionCompleted", "tutorial", 1),
        new QuestDefinition(QuestIds.VisitRuins, "AreaEntered", "ruins", 1));

    public static QuestDefinition match(Messages.GameplayEventEnvelope event) {
        for (QuestDefinition definition : CATALOG) {
            if (definition.eventType().equals(event.eventType())
                && ("*".equals(definition.value()) || definition.value().equals(event.value()))) {
                return definition;
            }
        }
        return null;
    }

    public static QuestDefinition firstHunt() {
        return CATALOG.stream()
            .filter(definition -> definition.questId().equals(QuestIds.FirstHunt))
            .findFirst()
            .orElseThrow();
    }

    public static Messages.QuestProgress apply(
        QuestDefinition definition, Messages.QuestProgress current, Messages.GameplayEventEnvelope event) {
        int currentCount = current == null ? 0 : current.currentCount();
        int nextCount = "SnapshotKillCount".equals(event.eventType())
            ? Math.max(currentCount, event.count())
            : Math.min(definition.required(), currentCount + event.count());
        boolean complete = nextCount >= definition.required();
        return new Messages.QuestProgress(
            event.playerId(),
            definition.questId(),
            complete ? QuestStatuses.RewardGranted : QuestStatuses.Active,
            nextCount,
            definition.required(),
            event.eventId(),
            System.currentTimeMillis());
    }

    public static List<PendingQuestEvent> toEvents(
        QuestDefinition definition,
        Messages.QuestProgress before,
        Messages.QuestProgress after,
        Messages.GameplayEventEnvelope source) {
        if (before != null && source.eventId().equals(before.lastEventId())) {
            return List.of();
        }
        if (before != null
            && before.currentCount() == after.currentCount()
            && before.status().equals(after.status())) {
            return List.of();
        }

        List<PendingQuestEvent> events = new ArrayList<>();
        String kind = "SnapshotKillCount".equals(source.eventType())
            ? "QuestProgressReconciledEvent"
            : "QuestProgressedEvent";
        events.add(create(kind, definition, before, after, source));

        String beforeStatus = before == null ? null : before.status();
        if (!QuestStatuses.RewardGranted.equals(beforeStatus)
            && QuestStatuses.RewardGranted.equals(after.status())) {
            events.add(create("QuestCompletedEvent", definition, before, after, source));
            events.add(create("QuestRewardGrantedEvent", definition, before, after, source));
        }
        return events;
    }

    private static PendingQuestEvent create(
        String eventType,
        QuestDefinition definition,
        Messages.QuestProgress before,
        Messages.QuestProgress after,
        Messages.GameplayEventEnvelope source) {
        long now = System.currentTimeMillis();
        String eventId = after.playerId() + "-" + definition.questId() + "-" + source.eventId() + "-" + eventType;
        Object payload = switch (eventType) {
            case "QuestProgressedEvent" -> new Messages.QuestProgressedEvent(
                eventId,
                after.playerId(),
                definition.questId(),
                Math.max(0, after.currentCount() - (before == null ? 0 : before.currentCount())),
                after.currentCount(),
                after.requiredCount(),
                source.eventId());
            case "QuestCompletedEvent" -> new Messages.QuestCompletedEvent(
                eventId, after.playerId(), definition.questId(), source.eventId(), now);
            case "QuestRewardGrantedEvent" -> new Messages.QuestRewardGrantedEvent(
                eventId, after.playerId(), definition.questId(), source.eventId(),
                "reward-" + definition.questId(), now);
            case "QuestProgressReconciledEvent" -> new Messages.QuestProgressReconciledEvent(
                eventId, after.playerId(), definition.questId(), after.currentCount(), "GameplaySnapshot", now);
            default -> throw new IllegalArgumentException("Unknown quest event type: " + eventType);
        };
        return new PendingQuestEvent(
            eventId,
            source.eventId(),
            after.playerId(),
            definition.questId(),
            eventType,
            payload,
            now);
    }
}
