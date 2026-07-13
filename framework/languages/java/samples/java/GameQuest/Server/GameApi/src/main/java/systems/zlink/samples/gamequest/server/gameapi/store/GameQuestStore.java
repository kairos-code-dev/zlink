package systems.zlink.samples.gamequest.server.gameapi.store;

import java.time.Instant;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.function.BooleanSupplier;
import systems.zlink.samples.gamequest.server.configuration.RedisSampleStore;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

public final class GameQuestStore implements AutoCloseable {
    private final RedisSampleStore shared = new RedisSampleStore();
    private final Map<String, List<Messages.QuestProgress>> projections = new HashMap<>();
    private final Map<String, Set<String>> completedMissions = new HashMap<>();
    private final Map<String, Set<String>> unlockedFeatures = new HashMap<>();
    private final Map<String, Set<String>> enteredAreas = new HashMap<>();
    private final Map<String, Map<String, Integer>> kills = new HashMap<>();
    private final Map<String, Map<String, Integer>> items = new HashMap<>();

    public synchronized void bind(String playerId, String apiName) {
        shared.bind(playerId, apiName);
    }

    public synchronized void unbind(String playerId) {
        shared.unbind(playerId);
    }

    public synchronized void recordGameplay(Messages.GameplayMsg event) {
        switch (event.eventType()) {
            case "kill" -> kills.computeIfAbsent(event.playerId(), ignored -> new HashMap<>())
                .merge(event.value(), event.count(), Integer::sum);
            case "collect" -> items.computeIfAbsent(event.playerId(), ignored -> new HashMap<>())
                .merge(event.value(), event.count(), Integer::sum);
            case "mission" -> completedMissions.computeIfAbsent(event.playerId(), ignored -> new HashSet<>())
                .add(event.value());
            case "feature" -> unlockedFeatures.computeIfAbsent(event.playerId(), ignored -> new HashSet<>())
                .add(event.value());
            case "area" -> enteredAreas.computeIfAbsent(event.playerId(), ignored -> new HashSet<>())
                .add(event.value());
            default -> {
            }
        }
    }

    public synchronized void mergeProjection(String playerId, List<Messages.QuestProgress> projection) {
        projections.put(playerId, new ArrayList<>(projection));
        shared.writeProjection(playerId, projection);
    }

    public synchronized List<Messages.QuestProgress> projection(String playerId) {
        List<Messages.QuestProgress> sharedProjection = shared.readProjection(playerId);
        if (!sharedProjection.isEmpty()) {
            projections.put(playerId, new ArrayList<>(sharedProjection));
            return new ArrayList<>(sharedProjection);
        }
        return new ArrayList<>(projections.getOrDefault(playerId, List.of()));
    }

    public synchronized void deleteProjection(String playerId, String questId) {
        projections.computeIfAbsent(playerId, ignored -> new ArrayList<>())
            .removeIf(progress -> progress.questId().equals(questId));
        shared.writeProjection(playerId, projections.getOrDefault(playerId, List.of()));
    }

    public synchronized Messages.QuestProgress rebuildProjection(String playerId, String questId) {
        int current = items.getOrDefault(playerId, Map.of()).getOrDefault("healing-herb", 0);
        int required = Messages.QuestIds.HerbGathering.equals(questId) ? 5 : 3;
        Messages.QuestProgress rebuilt = new Messages.QuestProgress(
            playerId,
            questId,
            current >= required ? Messages.QuestStatuses.RewardGranted : Messages.QuestStatuses.InProgress,
            current,
            required,
            "rebuilt-" + Instant.now().toEpochMilli(),
            Instant.now().toEpochMilli());
        projections.computeIfAbsent(playerId, ignored -> new ArrayList<>()).removeIf(p -> p.questId().equals(questId));
        projections.get(playerId).add(rebuilt);
        shared.writeProjection(playerId, projections.get(playerId));
        return rebuilt;
    }

    public synchronized void addUnpublishedKill(String playerId) {
        kills.computeIfAbsent(playerId, ignored -> new HashMap<>())
            .merge("wolf", 1, Integer::sum);
    }

    public synchronized int wolfKills(String playerId) {
        return kills.getOrDefault(playerId, Map.of()).getOrDefault("wolf", 0);
    }

    public synchronized Messages.GetGameplaySnapshotRes snapshot(String playerId) {
        List<Messages.KillCountSnapshot> killCounts = kills.getOrDefault(playerId, Map.of())
            .entrySet()
            .stream()
            .map(entry -> new Messages.KillCountSnapshot(entry.getKey(), null, entry.getValue()))
            .toList();
        List<Messages.ItemCountSnapshot> itemCounts = items.getOrDefault(playerId, Map.of())
            .entrySet()
            .stream()
            .map(entry -> new Messages.ItemCountSnapshot(entry.getKey(), entry.getValue()))
            .toList();
        return new Messages.GetGameplaySnapshotRes(
            playerId,
            killCounts,
            itemCounts,
            new ArrayList<>(completedMissions.getOrDefault(playerId, Set.of())),
            new ArrayList<>(unlockedFeatures.getOrDefault(playerId, Set.of())),
            new ArrayList<>(enteredAreas.getOrDefault(playerId, Set.of())),
            killCounts.size() + itemCounts.size());
    }

    public synchronized Messages.GameQuestServerAssertRes assertState() {
        List<Messages.QuestProgress> alice = projection("player-alice");
        List<Messages.QuestProgress> bob = projection("player-bob");
        List<String> evidence = new ArrayList<>();
        alice.forEach(p -> evidence.add(p.playerId() + ":" + p.questId() + ":" + p.status() + ":"
            + p.currentCount() + "/" + p.requiredCount()));
        bob.forEach(p -> evidence.add(p.playerId() + ":" + p.questId() + ":" + p.status() + ":"
            + p.currentCount() + "/" + p.requiredCount()));
        List<String> bindingHistory = shared.bindingHistory();
        List<String> activeBindings = shared.activeBindings();
        List<Messages.StoredQuestEvent> events = shared.readQuestEvents();
        Map<String, String> rehydrates = shared.rehydrates();
        bindingHistory.forEach(binding -> evidence.add("binding:" + binding));
        events.forEach(event -> evidence.add("event:" + event.playerId() + ":" + event.questId()
            + ":" + event.eventType() + ":v" + event.version() + ":source=" + event.sourceEventId()));
        rehydrates.forEach((playerId, count) -> evidence.add("rehydrated:" + playerId + ":" + count));

        List<BooleanSupplier> conditions = List.of(
            check(evidence, "missing:player-alice:first-hunt:RewardGranted", () -> alice.stream().anyMatch(p ->
                p.questId().equals(Messages.QuestIds.FirstHunt)
                    && p.status().equals(Messages.QuestStatuses.RewardGranted))),
            check(evidence, "missing:player-alice:open-auction:RewardGranted", () -> alice.stream().anyMatch(p ->
                p.questId().equals(Messages.QuestIds.OpenAuction)
                    && p.status().equals(Messages.QuestStatuses.RewardGranted))),
            check(evidence, "missing:player-bob:herb-gathering:RewardGranted", () -> bob.stream().anyMatch(p ->
                p.questId().equals(Messages.QuestIds.HerbGathering)
                    && p.status().equals(Messages.QuestStatuses.RewardGranted))),
            check(evidence, "missing:binding:player-bob:api-b", () -> bindingHistory.contains("player-bob:api-b")),
            check(evidence, "unexpected:active-binding:player-alice", () -> !activeBindings.contains("player-alice")),
            check(evidence, "missing:event:player-alice:first-hunt:QuestProgressedEvent:3",
                () -> count(events, "player-alice", Messages.QuestIds.FirstHunt,
                    Messages.QuestProgressedEvent.class.getSimpleName()) == 3),
            check(evidence, "missing:event:player-alice:first-hunt:QuestCompletedEvent:1",
                () -> count(events, "player-alice", Messages.QuestIds.FirstHunt,
                    Messages.QuestCompletedEvent.class.getSimpleName()) == 1),
            check(evidence, "missing:event:player-alice:first-hunt:QuestRewardGrantedEvent:1",
                () -> count(events, "player-alice", Messages.QuestIds.FirstHunt,
                    Messages.QuestRewardGrantedEvent.class.getSimpleName()) == 1),
            check(evidence, "missing:event:player-alice:first-hunt:QuestProgressReconciledEvent:1",
                () -> count(events, "player-alice", Messages.QuestIds.FirstHunt,
                    Messages.QuestProgressReconciledEvent.class.getSimpleName()) == 1),
            check(evidence, "missing:event:player-alice:open-auction:QuestCompletedEvent:1",
                () -> count(events, "player-alice", Messages.QuestIds.OpenAuction,
                    Messages.QuestCompletedEvent.class.getSimpleName()) == 1),
            check(evidence, "missing:event:player-alice:open-auction:QuestRewardGrantedEvent:1",
                () -> count(events, "player-alice", Messages.QuestIds.OpenAuction,
                    Messages.QuestRewardGrantedEvent.class.getSimpleName()) == 1),
            check(evidence, "missing:event:player-bob:herb-gathering:QuestCompletedEvent:1",
                () -> count(events, "player-bob", Messages.QuestIds.HerbGathering,
                    Messages.QuestCompletedEvent.class.getSimpleName()) == 1),
            check(evidence, "missing:event:player-bob:herb-gathering:QuestRewardGrantedEvent:1",
                () -> count(events, "player-bob", Messages.QuestIds.HerbGathering,
                    Messages.QuestRewardGrantedEvent.class.getSimpleName()) == 1),
            check(evidence, "missing:rehydrated:player-alice:2",
                () -> Integer.parseInt(rehydrates.getOrDefault("player-alice", "0")) >= 2),
            check(evidence, "missing:rehydrated:player-bob:1",
                () -> Integer.parseInt(rehydrates.getOrDefault("player-bob", "0")) >= 1),
            check(evidence, "duplicate:event-version", () -> uniqueEventVersions(events)));
        boolean passed = true;
        for (BooleanSupplier condition : conditions) {
            passed &= condition.getAsBoolean();
        }
        return new Messages.GameQuestServerAssertRes(passed, evidence.stream().sorted().toList());
    }

    @Override
    public void close() {
        shared.close();
    }

    private static BooleanSupplier check(List<String> evidence, String failure, BooleanSupplier condition) {
        return () -> {
            boolean passed = condition.getAsBoolean();
            if (!passed) {
                evidence.add("failure:" + failure);
            }
            return passed;
        };
    }

    private static long count(
        List<Messages.StoredQuestEvent> events,
        String playerId,
        String questId,
        String eventType) {
        return events.stream()
            .filter(event -> event.playerId().equals(playerId)
                && event.questId().equals(questId)
                && event.eventType().equals(eventType))
            .count();
    }

    private static boolean uniqueEventVersions(List<Messages.StoredQuestEvent> events) {
        return events.stream()
            .map(event -> event.playerId() + ":" + event.questId() + ":" + event.version())
            .distinct()
            .count() == events.size();
    }
}
