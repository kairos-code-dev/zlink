package systems.zlink.samples.gamequest.server.questmission.infrastructure.store;

import systems.zlink.samples.gamequest.server.questmission.application.QuestEventProcessor;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.MapperFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.json.JsonMapper;
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
import java.util.Optional;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.locks.LockSupport;
import java.util.function.Function;
import org.springframework.stereotype.Component;
import systems.zlink.samples.gamequest.server.questmission.domain.QuestDomain.PendingQuestEvent;
import systems.zlink.samples.gamequest.server.configuration.SampleTopology;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

/**
 * File-backed event store and read-model projection for QuestMission. Mirrors
 * the .NET {@code QuestStore}: append-only event log with optimistic versioning
 * and source-event dedupe, plus the projection that the GameApi read path and
 * client notifications consume.
 */
@Component
public final class QuestStore implements QuestEventProcessor.QuestProgressStore {
    private final ObjectMapper json = JsonMapper.builder()
        .configure(MapperFeature.ACCEPT_CASE_INSENSITIVE_PROPERTIES, true)
        .configure(MapperFeature.USE_STD_BEAN_NAMING, true)
        .findAndAddModules()
        .build();
    private final Path directory;

    public QuestStore() {
        this.directory = Paths.get(SampleTopology.StoreDirectory);
        try {
            Files.createDirectories(directory);
        } catch (IOException ex) {
            throw new UncheckedIOException("Failed to create QuestMission store directory.", ex);
        }
    }

    @Override
    public Optional<Messages.QuestProgress> readProgress(String playerId, String questId) {
        return readProjection(playerId).stream()
            .filter(progress -> progress.questId().equals(questId))
            .findFirst();
    }

    @Override
    public List<Messages.QuestProgress> readProjection(String playerId) {
        List<Messages.QuestProgress> all = read(
            "quest-projection.json",
            new TypeReference<List<Messages.QuestProgress>>() {
            });
        List<Messages.QuestProgress> result = new ArrayList<>();
        for (Messages.QuestProgress progress : all) {
            if (progress.playerId().equals(playerId)) {
                result.add(progress);
            }
        }
        result.sort(Comparator.comparing(Messages.QuestProgress::questId));
        return result;
    }

    @Override
    public boolean hasSourceEvent(String playerId, String questId, String sourceEventId) {
        return readEvents().stream().anyMatch(event ->
            event.playerId().equals(playerId)
                && event.questId().equals(questId)
                && sourceEventId.equals(event.sourceEventId()));
    }

    @Override
    public boolean appendAndProject(Messages.QuestProgress progress, List<PendingQuestEvent> events) {
        boolean appended = update(
            "quest-events.json",
            new TypeReference<List<Messages.StoredQuestEvent>>() {
            },
            stored -> {
                boolean alreadyForSource = stored.stream().anyMatch(existing ->
                    existing.playerId().equals(progress.playerId())
                        && existing.questId().equals(progress.questId())
                        && existing.sourceEventId() != null
                        && existing.sourceEventId().equals(progress.lastEventId()));
                if (alreadyForSource) {
                    return false;
                }
                long nextVersion = stored.stream()
                    .filter(existing -> existing.playerId().equals(progress.playerId())
                        && existing.questId().equals(progress.questId()))
                    .mapToLong(Messages.StoredQuestEvent::version)
                    .max()
                    .orElse(0) + 1;
                boolean any = false;
                for (PendingQuestEvent event : events) {
                    boolean exists = stored.stream()
                        .anyMatch(existing -> existing.eventId().equals(event.eventId()));
                    if (!exists) {
                        stored.add(new Messages.StoredQuestEvent(
                            event.eventId(),
                            event.sourceEventId(),
                            event.playerId(),
                            event.questId(),
                            event.eventType(),
                            serializeQuestPayload(event.payload()),
                            nextVersion++,
                            event.createdAtUnixMs()));
                        any = true;
                    }
                }
                return any;
            });

        if (!appended) {
            return false;
        }

        update(
            "quest-projection.json",
            new TypeReference<List<Messages.QuestProgress>>() {
            },
            projection -> {
                projection.removeIf(p -> p.playerId().equals(progress.playerId())
                    && p.questId().equals(progress.questId()));
                projection.add(progress);
                return true;
            });
        return true;
    }

    public List<Messages.StoredQuestEvent> readEvents() {
        List<Messages.StoredQuestEvent> events = read(
            "quest-events.json",
            new TypeReference<List<Messages.StoredQuestEvent>>() {
            });
        events.sort(Comparator
            .comparing(Messages.StoredQuestEvent::playerId)
            .thenComparing(Messages.StoredQuestEvent::questId)
            .thenComparingLong(Messages.StoredQuestEvent::version));
        return events;
    }

    private byte[] serializeQuestPayload(Object payload) {
        try {
            return json.writeValueAsBytes(payload);
        } catch (JsonProcessingException ex) {
            throw new IllegalStateException("Failed to serialize quest event payload.", ex);
        }
    }

    // ----- locked read / update -----

    private <T> T read(String fileName, TypeReference<T> type) {
        Path file = directory.resolve(fileName);
        Path lockFile = directory.resolve(fileName + ".lock");
        try (FileChannel channel = FileChannel.open(
            lockFile, StandardOpenOption.CREATE, StandardOpenOption.WRITE)) {
            FileLock lock = acquire(channel);
            try {
                return load(file, type);
            } finally {
                lock.release();
            }
        } catch (IOException ex) {
            throw new UncheckedIOException("Failed to read QuestMission store file " + fileName + ".", ex);
        }
    }

    private <T, R> R update(String fileName, TypeReference<T> type, Function<T, R> mutator) {
        Path file = directory.resolve(fileName);
        Path lockFile = directory.resolve(fileName + ".lock");
        try (FileChannel channel = FileChannel.open(
            lockFile, StandardOpenOption.CREATE, StandardOpenOption.WRITE)) {
            FileLock lock = acquire(channel);
            try {
                T state = load(file, type);
                R result = mutator.apply(state);
                save(file, state);
                return result;
            } finally {
                lock.release();
            }
        } catch (IOException ex) {
            throw new UncheckedIOException("Failed to update QuestMission store file " + fileName + ".", ex);
        }
    }

    private <T> T load(Path file, TypeReference<T> type) throws IOException {
        if (!Files.exists(file)) {
            return empty(type);
        }
        byte[] bytes = Files.readAllBytes(file);
        if (bytes.length == 0) {
            return empty(type);
        }
        T value = json.readValue(bytes, type);
        return value == null ? empty(type) : value;
    }

    @SuppressWarnings("unchecked")
    private <T> T empty(TypeReference<T> type) {
        return type.getType().getTypeName().startsWith("java.util.Map")
            ? (T) new LinkedHashMap<>()
            : (T) new ArrayList<>();
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
            throw new IllegalStateException("Timed out acquiring QuestMission store lock.");
        }
        return lock;
    }
}
