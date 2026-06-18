package systems.zlink.samples.gamequest.server.gameapi.store;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.json.JsonMapper;
import com.fasterxml.jackson.databind.MapperFeature;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeSet;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.locks.LockSupport;
import java.util.function.Function;
import org.springframework.stereotype.Component;
import systems.zlink.samples.gamequest.server.configuration.QuestStatuses;
import systems.zlink.samples.gamequest.server.configuration.SampleTopology;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

/**
 * File-backed GameApi store. Mirrors the .NET {@code GameQuestStore}: it owns
 * the gameplay event log (idempotent append), the unpublished-kill counter, the
 * quest session bindings, and read-only access to the shared quest projection /
 * event stream that {@code QuestMission} writes.
 *
 * <p>Every mutation acquires an OS file lock over a per-file lock file, reads
 * the JSON document, applies the change, and writes it back. Both GameApi and
 * QuestMission instances share the same store directory.
 */
@Component
public final class GameQuestStore {
    private final ObjectMapper json = JsonMapper.builder()
        .configure(MapperFeature.ACCEPT_CASE_INSENSITIVE_PROPERTIES, true)
        .configure(MapperFeature.USE_STD_BEAN_NAMING, true)
        .findAndAddModules()
        .build();
    private final Path directory;

    public GameQuestStore() {
        this.directory = Paths.get(SampleTopology.StoreDirectory);
        try {
            Files.createDirectories(directory);
        } catch (IOException ex) {
            throw new UncheckedIOException("Failed to create GameQuest store directory.", ex);
        }
    }

    // ----- gameplay events -----

    public Messages.GameplayEventEnvelope getOrAddGameplayEvent(Messages.GameplayEventEnvelope candidate) {
        return update(
            "gameplay-events.json",
            new TypeReference<List<Messages.GameplayEventEnvelope>>() {
            },
            events -> {
                for (Messages.GameplayEventEnvelope existing : events) {
                    if (existing.playerId().equals(candidate.playerId())
                        && existing.idempotencyKey().equals(candidate.idempotencyKey())) {
                        return existing;
                    }
                }
                events.add(candidate);
                return candidate;
            });
    }

    public void addUnpublishedKill(String playerId, int count) {
        update(
            "unpublished-kills.json",
            new TypeReference<Map<String, Integer>>() {
            },
            kills -> {
                kills.merge(playerId, count, Integer::sum);
                return 0;
            });
    }

    public Messages.GetGameplaySnapshotRes readSnapshot(String playerId) {
        List<Messages.GameplayEventEnvelope> events = read(
            "gameplay-events.json",
            new TypeReference<List<Messages.GameplayEventEnvelope>>() {
            },
            new ArrayList<>());
        Map<String, Integer> unpublishedKills = read(
            "unpublished-kills.json",
            new TypeReference<Map<String, Integer>>() {
            },
            new LinkedHashMap<>());

        Map<String, Integer> killByMonster = new LinkedHashMap<>();
        for (Messages.GameplayEventEnvelope event : events) {
            if (event.playerId().equals(playerId) && "MonsterKilled".equals(event.eventType())) {
                killByMonster.merge(event.value(), event.count(), Integer::sum);
            }
        }
        int unpublished = unpublishedKills.getOrDefault(playerId, 0);
        if (unpublished > 0) {
            killByMonster.merge("wolf", unpublished, Integer::sum);
        }
        List<Messages.KillCountSnapshot> killCounts = new ArrayList<>();
        for (Map.Entry<String, Integer> entry : killByMonster.entrySet()) {
            killCounts.add(new Messages.KillCountSnapshot(entry.getKey(), null, entry.getValue()));
        }

        long maxCreatedAt = 0;
        for (Messages.GameplayEventEnvelope event : events) {
            if (event.playerId().equals(playerId)) {
                maxCreatedAt = Math.max(maxCreatedAt, event.createdAtUnixMs());
            }
        }

        return new Messages.GetGameplaySnapshotRes(
            playerId,
            killCounts,
            itemCounts(events, playerId),
            distinctSortedValues(events, playerId, "MissionCompleted"),
            distinctSortedValues(events, playerId, "FeatureUnlocked"),
            distinctSortedValues(events, playerId, "AreaEntered"),
            maxCreatedAt + unpublished);
    }

    private List<Messages.ItemCountSnapshot> itemCounts(
        List<Messages.GameplayEventEnvelope> events, String playerId) {
        Map<String, Integer> byItem = new LinkedHashMap<>();
        for (Messages.GameplayEventEnvelope event : events) {
            if (event.playerId().equals(playerId) && "ItemCollected".equals(event.eventType())) {
                byItem.merge(event.value(), event.count(), Integer::sum);
            }
        }
        List<Messages.ItemCountSnapshot> counts = new ArrayList<>();
        for (Map.Entry<String, Integer> entry : byItem.entrySet()) {
            counts.add(new Messages.ItemCountSnapshot(entry.getKey(), entry.getValue()));
        }
        return counts;
    }

    private List<String> distinctSortedValues(
        List<Messages.GameplayEventEnvelope> events, String playerId, String eventType) {
        TreeSet<String> values = new TreeSet<>();
        for (Messages.GameplayEventEnvelope event : events) {
            if (event.playerId().equals(playerId) && eventType.equals(event.eventType())) {
                values.add(event.value());
            }
        }
        return new ArrayList<>(values);
    }

    // ----- quest session bindings -----

    public Messages.BindQuestSessionRes bindSession(Messages.BindQuestSessionReq request) {
        update(
            "quest-subscriptions.json",
            new TypeReference<List<Messages.BindQuestSessionReq>>() {
            },
            bindings -> {
                bindings.removeIf(binding -> binding.playerId().equals(request.playerId()));
                bindings.add(request);
                return true;
            });
        update(
            "quest-subscription-history.json",
            new TypeReference<List<Messages.BindQuestSessionReq>>() {
            },
            bindings -> {
                boolean present = bindings.stream().anyMatch(binding ->
                    binding.playerId().equals(request.playerId())
                        && binding.connectionId().equals(request.connectionId()));
                if (!present) {
                    bindings.add(request);
                }
                return true;
            });
        return new Messages.BindQuestSessionRes(true);
    }

    public Messages.UnbindQuestSessionRes unbindSession(Messages.UnbindQuestSessionReq request) {
        boolean removed = update(
            "quest-subscriptions.json",
            new TypeReference<List<Messages.BindQuestSessionReq>>() {
            },
            bindings -> {
                int before = bindings.size();
                bindings.removeIf(binding ->
                    binding.playerId().equals(request.playerId())
                        && binding.connectionId().equals(request.connectionId()));
                return bindings.size() != before;
            });
        return new Messages.UnbindQuestSessionRes(removed);
    }

    public List<Messages.BindQuestSessionReq> readBindings() {
        List<Messages.BindQuestSessionReq> bindings = read(
            "quest-subscriptions.json",
            new TypeReference<List<Messages.BindQuestSessionReq>>() {
            },
            new ArrayList<>());
        bindings.sort(Comparator.comparing(Messages.BindQuestSessionReq::playerId));
        return bindings;
    }

    public List<Messages.BindQuestSessionReq> readBindingHistory() {
        List<Messages.BindQuestSessionReq> bindings = read(
            "quest-subscription-history.json",
            new TypeReference<List<Messages.BindQuestSessionReq>>() {
            },
            new ArrayList<>());
        bindings.sort(Comparator.comparing(Messages.BindQuestSessionReq::playerId));
        return bindings;
    }

    // ----- quest projection (shared with QuestMission) -----

    public List<Messages.QuestProgress> readProjection(String playerId) {
        List<Messages.QuestProgress> all = read(
            "quest-projection.json",
            new TypeReference<List<Messages.QuestProgress>>() {
            },
            new ArrayList<>());
        List<Messages.QuestProgress> result = new ArrayList<>();
        for (Messages.QuestProgress progress : all) {
            if (progress.playerId().equals(playerId)) {
                result.add(progress);
            }
        }
        result.sort(Comparator.comparing(Messages.QuestProgress::questId));
        return result;
    }

    public void deleteProjection(String playerId, String questId) {
        update(
            "quest-projection.json",
            new TypeReference<List<Messages.QuestProgress>>() {
            },
            projection -> {
                projection.removeIf(progress ->
                    progress.playerId().equals(playerId) && progress.questId().equals(questId));
                return true;
            });
    }

    public Messages.QuestProgress rebuildProjection(String playerId, String questId) {
        List<Messages.StoredQuestEvent> events = readQuestEvents();
        List<Messages.StoredQuestEvent> stream = new ArrayList<>();
        for (Messages.StoredQuestEvent event : events) {
            if (event.playerId().equals(playerId) && event.questId().equals(questId)) {
                stream.add(event);
            }
        }
        stream.sort(Comparator.comparingLong(Messages.StoredQuestEvent::version));
        if (stream.isEmpty()) {
            throw new IllegalStateException(
                "Quest stream was not found for " + playerId + "/" + questId + ".");
        }

        int currentCount = 0;
        int requiredCount = 1;
        String status = QuestStatuses.Active;
        String lastEventId = null;
        long updatedAtUnixMs = 0;
        for (Messages.StoredQuestEvent event : stream) {
            Map<String, Object> root = decodePayload(event.payload());
            if ("QuestProgressedEvent".equals(event.eventType())
                || "QuestProgressReconciledEvent".equals(event.eventType())) {
                currentCount = intValue(root, "currentCount", currentCount);
                requiredCount = intValue(root, "requiredCount", requiredCount);
            }
            if ("QuestCompletedEvent".equals(event.eventType())) {
                status = QuestStatuses.Completed;
            } else if ("QuestRewardGrantedEvent".equals(event.eventType())) {
                status = QuestStatuses.RewardGranted;
            }
            lastEventId = event.sourceEventId();
            updatedAtUnixMs = Math.max(updatedAtUnixMs, event.createdAtUnixMs());
        }

        Messages.QuestProgress rebuilt = new Messages.QuestProgress(
            playerId, questId, status, currentCount, requiredCount, lastEventId, updatedAtUnixMs);
        update(
            "quest-projection.json",
            new TypeReference<List<Messages.QuestProgress>>() {
            },
            projection -> {
                projection.removeIf(progress ->
                    progress.playerId().equals(playerId) && progress.questId().equals(questId));
                projection.add(rebuilt);
                return true;
            });
        return rebuilt;
    }

    public List<Messages.StoredQuestEvent> readQuestEvents() {
        List<Messages.StoredQuestEvent> events = read(
            "quest-events.json",
            new TypeReference<List<Messages.StoredQuestEvent>>() {
            },
            new ArrayList<>());
        events.sort(Comparator
            .comparing(Messages.StoredQuestEvent::playerId)
            .thenComparing(Messages.StoredQuestEvent::questId)
            .thenComparingLong(Messages.StoredQuestEvent::version));
        return events;
    }

    @SuppressWarnings("unchecked")
    private Map<String, Object> decodePayload(byte[] payload) {
        if (payload == null || payload.length == 0) {
            return Map.of();
        }
        try {
            return json.readValue(payload, Map.class);
        } catch (IOException ex) {
            throw new UncheckedIOException("Failed to decode stored quest event payload.", ex);
        }
    }

    private static int intValue(Map<String, Object> root, String field, int fallback) {
        Object value = root.get(field);
        return value instanceof Number number ? number.intValue() : fallback;
    }

    // ----- locked read / update -----

    private <T> T read(String fileName, TypeReference<T> type, T fallback) {
        return withFile(fileName, type, fallback, state -> state, false).result();
    }

    private <T, R> R update(String fileName, TypeReference<T> type, Function<T, R> mutator) {
        return withFile(fileName, type, null, mutator, true).extra();
    }

    private record Outcome<T, R>(T result, R extra) {
    }

    private <T, R> Outcome<T, R> withFile(
        String fileName,
        TypeReference<T> type,
        T fallback,
        Function<T, R> action,
        boolean write) {
        Path file = directory.resolve(fileName);
        Path lockFile = directory.resolve(fileName + ".lock");
        try (FileChannel channel = FileChannel.open(
            lockFile, StandardOpenOption.CREATE, StandardOpenOption.WRITE)) {
            FileLock lock = acquire(channel);
            try {
                T state = load(file, type, fallback);
                R extra = action.apply(state);
                if (write) {
                    save(file, state);
                }
                return new Outcome<>(state, extra);
            } finally {
                lock.release();
            }
        } catch (IOException ex) {
            throw new UncheckedIOException("Failed to access GameQuest store file " + fileName + ".", ex);
        }
    }

    private <T> T load(Path file, TypeReference<T> type, T fallback) throws IOException {
        if (!Files.exists(file)) {
            return fallback == null ? empty(type) : cloneFallback(fallback);
        }
        byte[] bytes = Files.readAllBytes(file);
        if (bytes.length == 0) {
            return fallback == null ? empty(type) : cloneFallback(fallback);
        }
        T value = json.readValue(bytes, type);
        return value == null
            ? (fallback == null ? empty(type) : cloneFallback(fallback))
            : value;
    }

    @SuppressWarnings("unchecked")
    private <T> T empty(TypeReference<T> type) {
        return type.getType().getTypeName().startsWith("java.util.Map")
            ? (T) new LinkedHashMap<>()
            : (T) new ArrayList<>();
    }

    @SuppressWarnings("unchecked")
    private <T> T cloneFallback(T fallback) {
        if (fallback == null) {
            return null;
        }
        if (fallback instanceof List<?>) {
            return (T) new ArrayList<>((List<?>) fallback);
        }
        if (fallback instanceof Map<?, ?>) {
            return (T) new LinkedHashMap<>((Map<?, ?>) fallback);
        }
        return fallback;
    }

    private void save(Path file, Object state) throws IOException {
        Files.write(
            file,
            json.writeValueAsString(state).getBytes(StandardCharsets.UTF_8),
            StandardOpenOption.CREATE,
            StandardOpenOption.TRUNCATE_EXISTING,
            StandardOpenOption.WRITE);
    }

    private FileLock acquire(FileChannel channel) throws IOException {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(30);
        FileLock lock = channel.tryLock();
        while (lock == null && System.nanoTime() < deadline) {
            LockSupport.parkNanos(TimeUnit.MILLISECONDS.toNanos(10));
            lock = channel.tryLock();
        }
        if (lock == null) {
            throw new IllegalStateException("Timed out acquiring GameQuest store lock.");
        }
        return lock;
    }
}
