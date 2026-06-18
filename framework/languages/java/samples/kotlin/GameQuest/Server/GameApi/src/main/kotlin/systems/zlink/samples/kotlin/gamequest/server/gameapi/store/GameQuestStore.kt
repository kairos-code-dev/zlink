package systems.zlink.samples.kotlin.gamequest.server.gameapi.store

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
import java.util.TreeSet
import java.util.concurrent.TimeUnit
import java.util.concurrent.locks.LockSupport
import org.springframework.stereotype.Component
import systems.zlink.samples.kotlin.gamequest.server.configuration.QuestStatuses
import systems.zlink.samples.kotlin.gamequest.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.gamequest.shared.contracts.BindQuestSessionReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.BindQuestSessionRes
import systems.zlink.samples.kotlin.gamequest.shared.contracts.GameplayEventEnvelope
import systems.zlink.samples.kotlin.gamequest.shared.contracts.GetGameplaySnapshotRes
import systems.zlink.samples.kotlin.gamequest.shared.contracts.ItemCountSnapshot
import systems.zlink.samples.kotlin.gamequest.shared.contracts.KillCountSnapshot
import systems.zlink.samples.kotlin.gamequest.shared.contracts.QuestProgress
import systems.zlink.samples.kotlin.gamequest.shared.contracts.StoredQuestEvent
import systems.zlink.samples.kotlin.gamequest.shared.contracts.UnbindQuestSessionReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.UnbindQuestSessionRes

/**
 * File-backed GameApi store. Mirrors the .NET `GameQuestStore`: it owns the
 * gameplay event log (idempotent append), the unpublished-kill counter, the
 * quest session bindings, and read-only access to the shared quest projection /
 * event stream that `QuestMission` writes.
 *
 * Every mutation acquires an OS file lock over a per-file lock file, reads the
 * JSON document, applies the change, and writes it back. Both GameApi and
 * QuestMission instances share the same store directory.
 */
@Component
class GameQuestStore {
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

    // ----- gameplay events -----

    fun getOrAddGameplayEvent(candidate: GameplayEventEnvelope): GameplayEventEnvelope =
        update(
            "gameplay-events.json",
            object : TypeReference<MutableList<GameplayEventEnvelope>>() {},
        ) { events ->
            val existing = events.firstOrNull {
                it.playerId == candidate.playerId && it.idempotencyKey == candidate.idempotencyKey
            }
            if (existing != null) {
                existing
            } else {
                events.add(candidate)
                candidate
            }
        }

    fun addUnpublishedKill(playerId: String, count: Int) {
        update(
            "unpublished-kills.json",
            object : TypeReference<MutableMap<String, Int>>() {},
        ) { kills ->
            kills[playerId] = (kills[playerId] ?: 0) + count
            0
        }
    }

    fun readSnapshot(playerId: String): GetGameplaySnapshotRes {
        val events = read("gameplay-events.json", object : TypeReference<MutableList<GameplayEventEnvelope>>() {})
        val unpublishedKills = read("unpublished-kills.json", object : TypeReference<MutableMap<String, Int>>() {})

        val killByMonster = LinkedHashMap<String, Int>()
        for (event in events) {
            if (event.playerId == playerId && event.eventType == "MonsterKilled") {
                killByMonster[event.value] = (killByMonster[event.value] ?: 0) + event.count
            }
        }
        val unpublished = unpublishedKills[playerId] ?: 0
        if (unpublished > 0) {
            killByMonster["wolf"] = (killByMonster["wolf"] ?: 0) + unpublished
        }
        val killCounts = killByMonster.map { (monster, count) -> KillCountSnapshot(monster, null, count) }

        var maxCreatedAt = 0L
        for (event in events) {
            if (event.playerId == playerId) {
                maxCreatedAt = maxOf(maxCreatedAt, event.createdAtUnixMs)
            }
        }

        return GetGameplaySnapshotRes(
            playerId,
            killCounts,
            itemCounts(events, playerId),
            distinctSortedValues(events, playerId, "MissionCompleted"),
            distinctSortedValues(events, playerId, "FeatureUnlocked"),
            distinctSortedValues(events, playerId, "AreaEntered"),
            maxCreatedAt + unpublished,
        )
    }

    private fun itemCounts(events: List<GameplayEventEnvelope>, playerId: String): List<ItemCountSnapshot> {
        val byItem = LinkedHashMap<String, Int>()
        for (event in events) {
            if (event.playerId == playerId && event.eventType == "ItemCollected") {
                byItem[event.value] = (byItem[event.value] ?: 0) + event.count
            }
        }
        return byItem.map { (item, count) -> ItemCountSnapshot(item, count) }
    }

    private fun distinctSortedValues(
        events: List<GameplayEventEnvelope>,
        playerId: String,
        eventType: String,
    ): List<String> {
        val values = TreeSet<String>()
        for (event in events) {
            if (event.playerId == playerId && event.eventType == eventType) {
                values.add(event.value)
            }
        }
        return values.toList()
    }

    // ----- quest session bindings -----

    fun bindSession(request: BindQuestSessionReq): BindQuestSessionRes {
        update(
            "quest-subscriptions.json",
            object : TypeReference<MutableList<BindQuestSessionReq>>() {},
        ) { bindings ->
            bindings.removeIf { it.playerId == request.playerId }
            bindings.add(request)
            true
        }
        update(
            "quest-subscription-history.json",
            object : TypeReference<MutableList<BindQuestSessionReq>>() {},
        ) { bindings ->
            val present = bindings.any {
                it.playerId == request.playerId && it.connectionId == request.connectionId
            }
            if (!present) {
                bindings.add(request)
            }
            true
        }
        return BindQuestSessionRes(true)
    }

    fun unbindSession(request: UnbindQuestSessionReq): UnbindQuestSessionRes {
        val removed = update(
            "quest-subscriptions.json",
            object : TypeReference<MutableList<BindQuestSessionReq>>() {},
        ) { bindings ->
            val before = bindings.size
            bindings.removeIf {
                it.playerId == request.playerId && it.connectionId == request.connectionId
            }
            bindings.size != before
        }
        return UnbindQuestSessionRes(removed)
    }

    fun readBindings(): List<BindQuestSessionReq> {
        val bindings = read("quest-subscriptions.json", object : TypeReference<MutableList<BindQuestSessionReq>>() {})
        return bindings.sortedBy { it.playerId }
    }

    fun readBindingHistory(): List<BindQuestSessionReq> {
        val bindings = read(
            "quest-subscription-history.json",
            object : TypeReference<MutableList<BindQuestSessionReq>>() {},
        )
        return bindings.sortedBy { it.playerId }
    }

    // ----- quest projection (shared with QuestMission) -----

    fun readProjection(playerId: String): List<QuestProgress> {
        val all = read("quest-projection.json", object : TypeReference<MutableList<QuestProgress>>() {})
        return all.filter { it.playerId == playerId }.sortedBy { it.questId }
    }

    fun deleteProjection(playerId: String, questId: String) {
        update(
            "quest-projection.json",
            object : TypeReference<MutableList<QuestProgress>>() {},
        ) { projection ->
            projection.removeIf { it.playerId == playerId && it.questId == questId }
            true
        }
    }

    fun rebuildProjection(playerId: String, questId: String): QuestProgress {
        val stream = readQuestEvents()
            .filter { it.playerId == playerId && it.questId == questId }
            .sortedBy { it.version }
        check(stream.isNotEmpty()) { "Quest stream was not found for $playerId/$questId." }

        var currentCount = 0
        var requiredCount = 1
        var status = QuestStatuses.Active
        var lastEventId: String? = null
        var updatedAtUnixMs = 0L
        for (event in stream) {
            val root = decodePayload(event.payload)
            if (event.eventType == "QuestProgressedEvent" || event.eventType == "QuestProgressReconciledEvent") {
                currentCount = intValue(root, "currentCount", currentCount)
                requiredCount = intValue(root, "requiredCount", requiredCount)
            }
            if (event.eventType == "QuestCompletedEvent") {
                status = QuestStatuses.Completed
            } else if (event.eventType == "QuestRewardGrantedEvent") {
                status = QuestStatuses.RewardGranted
            }
            lastEventId = event.sourceEventId
            updatedAtUnixMs = maxOf(updatedAtUnixMs, event.createdAtUnixMs)
        }

        val rebuilt = QuestProgress(playerId, questId, status, currentCount, requiredCount, lastEventId, updatedAtUnixMs)
        update(
            "quest-projection.json",
            object : TypeReference<MutableList<QuestProgress>>() {},
        ) { projection ->
            projection.removeIf { it.playerId == playerId && it.questId == questId }
            projection.add(rebuilt)
            true
        }
        return rebuilt
    }

    fun readQuestEvents(): List<StoredQuestEvent> {
        val events = read("quest-events.json", object : TypeReference<MutableList<StoredQuestEvent>>() {})
        return events.sortedWith(
            compareBy<StoredQuestEvent> { it.playerId }.thenBy { it.questId }.thenBy { it.version },
        )
    }

    @Suppress("UNCHECKED_CAST")
    private fun decodePayload(payload: ByteArray?): Map<String, Any?> {
        if (payload == null || payload.isEmpty()) {
            return emptyMap()
        }
        return json.readValue(payload, Map::class.java) as Map<String, Any?>
    }

    private fun intValue(root: Map<String, Any?>, field: String, fallback: Int): Int {
        val value = root[field]
        return if (value is Number) value.toInt() else fallback
    }

    // ----- locked read / update -----

    private fun <T : Any> read(fileName: String, type: TypeReference<T>): T =
        withFile(fileName, type, write = false) { it }.result

    private fun <T : Any, R> update(fileName: String, type: TypeReference<T>, mutator: (T) -> R): R =
        withFile(fileName, type, write = true, action = mutator).extra

    private data class Outcome<T : Any, R>(val result: T, val extra: R)

    private fun <T : Any, R> withFile(
        fileName: String,
        type: TypeReference<T>,
        write: Boolean,
        action: (T) -> R,
    ): Outcome<T, R> {
        val file = directory.resolve(fileName)
        val lockFile = directory.resolve("$fileName.lock")
        FileChannel.open(lockFile, StandardOpenOption.CREATE, StandardOpenOption.WRITE).use { channel ->
            val lock = acquire(channel)
            try {
                val state = load(file, type)
                val extra = action(state)
                if (write) {
                    save(file, state)
                }
                return Outcome(state, extra)
            } finally {
                lock.release()
            }
        }
    }

    @Suppress("UNCHECKED_CAST")
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
        return lock ?: throw IllegalStateException("Timed out acquiring GameQuest store lock.")
    }
}
