package systems.zlink.samples.kotlin.gamequest.server.configuration

import com.fasterxml.jackson.databind.JavaType
import com.fasterxml.jackson.module.kotlin.jacksonObjectMapper
import com.fasterxml.jackson.module.kotlin.readValue
import io.lettuce.core.RedisClient
import io.lettuce.core.api.StatefulRedisConnection
import io.lettuce.core.api.sync.RedisCommands
import java.nio.charset.StandardCharsets
import java.time.Duration
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore
import systems.zlink.samples.kotlin.gamequest.shared.contracts.QuestProgress
import systems.zlink.samples.kotlin.gamequest.shared.contracts.StoredQuestEvent

object SampleNames {
    const val StreamNode = "gamequest-stream"
    const val QuestOwnerRouteChannel = "gamequest.quest-owner.route"
    const val CompletedMarker = "gamequest=completed"
    const val ServerEvidenceMarker = "gamequest-server-evidence=completed"
}

object SampleTimings {
    val RequestTimeout: Duration = Duration.ofSeconds(20)
    val ConnectTimeout: Duration = Duration.ofSeconds(5)
}

object SampleTopology {
    val ApiAStreamEndpoint: String = property("apiAStreamEndpoint", "tcp://127.0.0.1:28301")
    val ApiBStreamEndpoint: String = property("apiBStreamEndpoint", "tcp://127.0.0.1:28302")
    val ApiAHttpEndpoint: String = property("apiAHttpEndpoint", "http://127.0.0.1:28311")
    val ApiBHttpEndpoint: String = property("apiBHttpEndpoint", "http://127.0.0.1:28312")
    val MissionARouteEndpoint: String = property("missionARouteEndpoint", "tcp://127.0.0.1:28321")
    val MissionBRouteEndpoint: String = property("missionBRouteEndpoint", "tcp://127.0.0.1:28322")
    val MissionAHttpEndpoint: String = property("missionAHttpEndpoint", "http://127.0.0.1:28331")
    val MissionBHttpEndpoint: String = property("missionBHttpEndpoint", "http://127.0.0.1:28332")
    val RedisEndpoint: String = property("redisEndpoint", "127.0.0.1:6379")
    val RedisKeyPrefix: String = property("redisKeyPrefix", "gamequest:kotlin:")

    fun apiName(): String = System.getProperty("zlink.samples.gamequest.apiName", "api-a")
    fun missionName(): String = System.getProperty("zlink.samples.gamequest.missionName", "mission-a")
    fun selectedApiStreamEndpoint(): String = if (apiName() == "api-b") ApiBStreamEndpoint else ApiAStreamEndpoint
    fun selectedApiHttpEndpoint(): String = if (apiName() == "api-b") ApiBHttpEndpoint else ApiAHttpEndpoint
    fun selectedMissionRouteEndpoint(): String =
        if (missionName() == "mission-b") MissionBRouteEndpoint else MissionARouteEndpoint
    fun selectedMissionHttpEndpoint(): String =
        if (missionName() == "mission-b") MissionBHttpEndpoint else MissionAHttpEndpoint
    fun selectedMissionRid(): String = if (missionName() == "mission-b") "gamequest-mission-b" else "gamequest-mission-a"
    fun ownerRouteRid(playerId: String): RoutingId =
        RoutingId.from(if (ownerIndex(playerId) == 1) "gamequest-mission-b" else "gamequest-mission-a")

    private fun ownerIndex(playerId: String): Int =
        playerId.toByteArray(StandardCharsets.UTF_8).sumOf { it.toInt() and 0xff } % 2

    private fun property(name: String, fallback: String): String =
        System.getProperty("zlink.samples.gamequest.$name", fallback)
}

object SampleLocationStore {
    fun create(): ZLinkRedisLocationStore =
        ZLinkRedisLocationStore(
            ZLinkRedisLocationOptions()
                .setConnectionString(SampleTopology.RedisEndpoint)
                .setKeyPrefix("${SampleTopology.RedisKeyPrefix}locations:")
                .setCommandTimeout(Duration.ofMillis(500)),
        )
}

class RedisSampleStore : AutoCloseable {
    private val json = jacksonObjectMapper()
    private val client: RedisClient = RedisClient.create(redisUri(SampleTopology.RedisEndpoint))
    private val connection: StatefulRedisConnection<String, String> = client.connect()
    private val redis: RedisCommands<String, String> = connection.sync()

    fun bind(playerId: String, apiName: String) {
        redis.sadd(key("binding-history"), "$playerId:$apiName")
        redis.sadd(key("active-bindings"), playerId)
    }

    fun unbind(playerId: String) {
        redis.srem(key("active-bindings"), playerId)
    }

    fun bindingHistory(): List<String> = redis.smembers(key("binding-history")).sorted()
    fun activeBindings(): List<String> = redis.smembers(key("active-bindings")).sorted()

    fun writeProjection(playerId: String, projection: List<QuestProgress>) {
        redis.set(key("projection:$playerId"), json.writeValueAsString(projection))
    }

    fun readProjection(playerId: String): List<QuestProgress> {
        val value = redis.get(key("projection:$playerId"))
        if (value.isNullOrBlank()) {
            return emptyList()
        }
        val type: JavaType = json.typeFactory.constructCollectionType(List::class.java, QuestProgress::class.java)
        return json.readValue(value, type)
    }

    fun appendQuestEvents(events: List<StoredQuestEvent>) {
        if (events.isEmpty()) {
            return
        }
        redis.rpush(key("quest-events"), *events.map(json::writeValueAsString).toTypedArray())
    }

    fun readQuestEvents(): List<StoredQuestEvent> =
        redis.lrange(key("quest-events"), 0, -1).map { json.readValue<StoredQuestEvent>(it) }

    fun recordRehydrated(playerId: String) {
        redis.hincrby(key("owner-rehydrates"), playerId, 1)
    }

    fun rehydrates(): Map<String, String> = redis.hgetall(key("owner-rehydrates"))

    override fun close() {
        connection.close()
        client.shutdown()
    }

    private fun key(name: String): String = "${SampleTopology.RedisKeyPrefix}gamequest:$name"

    private fun redisUri(endpoint: String): String =
        if (endpoint.startsWith("redis://")) endpoint else "redis://$endpoint"
}
