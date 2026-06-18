package systems.zlink.samples.kotlin.gamequest.server.questmission.store

import com.fasterxml.jackson.core.type.TypeReference
import com.fasterxml.jackson.databind.MapperFeature
import com.fasterxml.jackson.databind.ObjectMapper
import com.fasterxml.jackson.databind.json.JsonMapper
import com.fasterxml.jackson.module.kotlin.registerKotlinModule
import java.nio.channels.FileChannel
import java.nio.channels.FileLock
import java.nio.charset.StandardCharsets
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.Paths
import java.nio.file.StandardOpenOption
import java.util.concurrent.TimeUnit
import java.util.concurrent.locks.LockSupport
import org.springframework.stereotype.Component
import systems.zlink.samples.kotlin.gamequest.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.gamequest.shared.contracts.QuestProgress
import systems.zlink.samples.kotlin.gamequest.shared.contracts.StoredQuestEvent

/**
 * File-backed event store and read-model projection for QuestMission. Mirrors
 * the .NET `QuestStore`: append-only event log with optimistic versioning and
 * source-event dedupe, plus the projection that the GameApi read path and client
 * notifications consume.
 */
@Component
class QuestStore {
    private val json: ObjectMapper = JsonMapper.builder()
        .configure(MapperFeature.ACCEPT_CASE_INSENSITIVE_PROPERTIES, true)
        .configure(MapperFeature.USE_STD_BEAN_NAMING, true)
        .build()
        .registerKotlinModule()
    private val directory: Path

    init {
        directory = Paths.get(SampleTopology.StoreDirectory)
        Files.createDirectories(directory)
    }

    fun readProgress(playerId: String, questId: String): QuestProgress? =
        readProjection(playerId).firstOrNull { it.questId == questId }

    fun readProjection(playerId: String): List<QuestProgress> {
        val all = read("quest-projection.json", object : TypeReference<MutableList<QuestProgress>>() {})
        return all.filter { it.playerId == playerId }.sortedBy { it.questId }
    }

    fun hasSourceEvent(playerId: String, questId: String, sourceEventId: String): Boolean =
        readEvents().any {
            it.playerId == playerId && it.questId == questId && sourceEventId == it.sourceEventId
        }

    fun appendAndProject(progress: QuestProgress, events: List<StoredQuestEvent>): Boolean {
        val appended = update(
            "quest-events.json",
            object : TypeReference<MutableList<StoredQuestEvent>>() {},
        ) { stored ->
            val alreadyForSource = stored.any {
                it.playerId == progress.playerId &&
                    it.questId == progress.questId &&
                    it.sourceEventId != null &&
                    it.sourceEventId == progress.lastEventId
            }
            if (alreadyForSource) {
                false
            } else {
                var nextVersion = (
                    stored.filter { it.playerId == progress.playerId && it.questId == progress.questId }
                        .maxOfOrNull { it.version } ?: 0
                    ) + 1
                var any = false
                for (event in events) {
                    val exists = stored.any { it.eventId == event.eventId }
                    if (!exists) {
                        stored.add(
                            StoredQuestEvent(
                                event.eventId,
                                event.sourceEventId,
                                event.playerId,
                                event.questId,
                                event.eventType,
                                event.payload,
                                nextVersion++,
                                event.createdAtUnixMs,
                            ),
                        )
                        any = true
                    }
                }
                any
            }
        }

        if (!appended) {
            return false
        }

        update(
            "quest-projection.json",
            object : TypeReference<MutableList<QuestProgress>>() {},
        ) { projection ->
            projection.removeIf { it.playerId == progress.playerId && it.questId == progress.questId }
            projection.add(progress)
            true
        }
        return true
    }

    fun readEvents(): List<StoredQuestEvent> {
        val events = read("quest-events.json", object : TypeReference<MutableList<StoredQuestEvent>>() {})
        return events.sortedWith(
            compareBy<StoredQuestEvent> { it.playerId }.thenBy { it.questId }.thenBy { it.version },
        )
    }

    // ----- locked read / update -----

    private fun <T : Any> read(fileName: String, type: TypeReference<T>): T {
        val file = directory.resolve(fileName)
        val lockFile = directory.resolve("$fileName.lock")
        FileChannel.open(lockFile, StandardOpenOption.CREATE, StandardOpenOption.WRITE).use { channel ->
            val lock = acquire(channel)
            try {
                return load(file, type)
            } finally {
                lock.release()
            }
        }
    }

    private fun <T : Any, R> update(fileName: String, type: TypeReference<T>, mutator: (T) -> R): R {
        val file = directory.resolve(fileName)
        val lockFile = directory.resolve("$fileName.lock")
        FileChannel.open(lockFile, StandardOpenOption.CREATE, StandardOpenOption.WRITE).use { channel ->
            val lock = acquire(channel)
            try {
                val state = load(file, type)
                val result = mutator(state)
                save(file, state)
                return result
            } finally {
                lock.release()
            }
        }
    }

    private fun <T : Any> load(file: Path, type: TypeReference<T>): T {
        if (!Files.exists(file)) {
            return empty(type)
        }
        val bytes = Files.readAllBytes(file)
        if (bytes.isEmpty()) {
            return empty(type)
        }
        val value: T? = json.readValue(bytes, type)
        return value ?: empty(type)
    }

    @Suppress("UNCHECKED_CAST")
    private fun <T : Any> empty(type: TypeReference<T>): T =
        if (type.type.typeName.startsWith("java.util.Map")) {
            LinkedHashMap<Any?, Any?>() as T
        } else {
            ArrayList<Any?>() as T
        }

    private fun save(file: Path, state: Any) {
        Files.write(
            file,
            json.writeValueAsString(state).toByteArray(StandardCharsets.UTF_8),
            StandardOpenOption.CREATE,
            StandardOpenOption.TRUNCATE_EXISTING,
            StandardOpenOption.WRITE,
        )
    }

    private fun acquire(channel: FileChannel): FileLock {
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(30)
        var lock = channel.tryLock()
        while (lock == null && System.nanoTime() < deadline) {
            LockSupport.parkNanos(TimeUnit.MILLISECONDS.toNanos(10))
            lock = channel.tryLock()
        }
        return lock ?: throw IllegalStateException("Timed out acquiring QuestMission store lock.")
    }
}
