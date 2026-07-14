package systems.zlink.samples.gamequest.server.questmission.store;

import java.time.Instant;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import systems.zlink.samples.gamequest.server.configuration.RedisSampleStore;
import systems.zlink.samples.gamequest.server.configuration.SampleTopology;
import systems.zlink.samples.gamequest.server.questmission.domain.QuestDomain;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

public final class QuestStore implements AutoCloseable {
    private final QuestDomain domain = new QuestDomain();
    private final RedisSampleStore shared;
    private final Map<String, List<Messages.QuestProgress>> projections = new HashMap<>();
    private final Map<String, String> eventIdsByIdempotency = new HashMap<>();
    private final List<Messages.StoredQuestEvent> events = new ArrayList<>();
    private final Map<String, Integer> rehydrates = new HashMap<>();

    public QuestStore(SampleTopology topology) {
        shared = new RedisSampleStore(topology);
    }

    public synchronized Messages.QuestProcessingRes apply(Messages.GameplayMsg event) {
        String key = event.playerId() + ":" + event.idempotencyKey();
        if (eventIdsByIdempotency.containsKey(key)) {
            return new Messages.QuestProcessingRes(
                eventIdsByIdempotency.get(key),
                copyProjection(event.playerId()),
                List.of(),
                List.of(),
                true);
        }
        eventIdsByIdempotency.put(key, event.eventId());
        List<Messages.QuestProgress> current = copyProjection(event.playerId());
        QuestDomain.QuestDecision decision = domain.apply(event, current, nextVersion(event.playerId()));
        projections.put(event.playerId(), new ArrayList<>(decision.projection()));
        events.addAll(decision.storedEvents());
        shared.writeProjection(event.playerId(), decision.projection());
        shared.appendQuestEvents(decision.storedEvents());
        return new Messages.QuestProcessingRes(
            event.eventId(),
            copyProjection(event.playerId()),
            decision.progressNotifications(),
            decision.completedNotifications(),
            false);
    }

    public synchronized Messages.SyncQuestProgressRes sync(String playerId, int firstHuntCount) {
        List<Messages.QuestProgress> projection = copyProjection(playerId);
        Messages.QuestProgress firstHunt = projection.stream()
            .filter(progress -> progress.questId().equals(Messages.QuestIds.FirstHunt))
            .findFirst()
            .orElse(null);
        if (firstHunt == null || firstHunt.currentCount() < firstHuntCount) {
            long now = Instant.now().toEpochMilli();
            Messages.QuestProgress reconciled = new Messages.QuestProgress(
                playerId,
                Messages.QuestIds.FirstHunt,
                firstHuntCount >= 3 ? Messages.QuestStatuses.RewardGranted : Messages.QuestStatuses.InProgress,
                firstHuntCount,
                3,
                "sync-" + now,
                now);
            projection.removeIf(progress -> progress.questId().equals(Messages.QuestIds.FirstHunt));
            projection.add(reconciled);
            projections.put(playerId, projection);
            Messages.StoredQuestEvent reconciledEvent = new Messages.StoredQuestEvent(
                "sync-" + now,
                null,
                playerId,
                Messages.QuestIds.FirstHunt,
                Messages.QuestProgressReconciledEvent.class.getSimpleName(),
                reconciled.currentCount(),
                reconciled.requiredCount(),
                reconciled.status(),
                nextVersion(playerId),
                now);
            events.add(reconciledEvent);
            shared.writeProjection(playerId, projection);
            shared.appendQuestEvents(List.of(reconciledEvent));
        }
        return new Messages.SyncQuestProgressRes(copyProjection(playerId));
    }

    public synchronized Messages.DeleteQuestProjectionRes deleteProjection(String playerId, String questId) {
        projections.computeIfAbsent(playerId, ignored -> new ArrayList<>())
            .removeIf(progress -> progress.questId().equals(questId));
        shared.writeProjection(playerId, projections.getOrDefault(playerId, List.of()));
        return new Messages.DeleteQuestProjectionRes(true);
    }

    public synchronized Messages.QuestProgress rebuildProjection(String playerId, String questId, int count) {
        List<Messages.StoredQuestEvent> stream = events.stream()
            .filter(event -> event.playerId().equals(playerId) && event.questId().equals(questId))
            .sorted(Comparator.comparingLong(Messages.StoredQuestEvent::version))
            .toList();
        if (stream.isEmpty()) {
            throw new IllegalStateException("Quest stream was not found for " + playerId + "/" + questId);
        }

        int current = 0;
        int required = 1;
        String status = Messages.QuestStatuses.InProgress;
        String lastEventId = null;
        long updatedAt = 0;
        for (Messages.StoredQuestEvent event : stream) {
            current = event.currentCount();
            required = event.requiredCount();
            status = event.status();
            lastEventId = event.sourceEventId();
            updatedAt = Math.max(updatedAt, event.createdAtUnixMs());
        }

        Messages.QuestProgress rebuilt = new Messages.QuestProgress(
            playerId,
            questId,
            status,
            current,
            required,
            lastEventId,
            updatedAt);
        projections.computeIfAbsent(playerId, ignored -> new ArrayList<>())
            .removeIf(progress -> progress.questId().equals(questId));
        projections.get(playerId).add(rebuilt);
        shared.writeProjection(playerId, projections.get(playerId));
        return rebuilt;
    }

    public synchronized void markRehydrated(String playerId) {
        rehydrates.merge(playerId, 1, Integer::sum);
        shared.recordRehydrated(playerId);
    }

    public synchronized List<Messages.QuestProgress> projection(String playerId) {
        return copyProjection(playerId);
    }

    public synchronized List<Messages.StoredQuestEvent> events() {
        return List.copyOf(events);
    }

    public synchronized Map<String, Integer> rehydrates() {
        return Map.copyOf(rehydrates);
    }

    public synchronized Set<String> players() {
        return new HashSet<>(projections.keySet());
    }

    @Override
    public void close() {
        shared.close();
    }

    private long nextVersion(String playerId) {
        return events.stream()
            .filter(event -> event.playerId().equals(playerId))
            .count() + 1;
    }

    private List<Messages.QuestProgress> copyProjection(String playerId) {
        return new ArrayList<>(projections.getOrDefault(playerId, List.of()));
    }
}
