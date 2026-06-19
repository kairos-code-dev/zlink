package systems.zlink.samples.kotlin.tictactoe.server.configuration

import io.lettuce.core.RedisClient
import io.lettuce.core.api.StatefulRedisConnection

class RedisRoomRouteStore(settings: SampleSettings) : AutoCloseable {
    private val client: RedisClient = RedisClient.create(redisUri(settings.redisEndpoint))
    private val connection: StatefulRedisConnection<String, String> = client.connect()
    private val keyPrefix: String = settings.redisKeyPrefix

    fun save(route: RoomRoute) {
        connection.sync().hset(
            key(route.roomId),
            mapOf(
                "ownerPlayEndpoint" to route.ownerPlayEndpoint,
                "ownerSpotEndpoint" to route.ownerSpotEndpoint,
                "ownerSpotNodeRid" to route.ownerSpotNodeRid,
            ),
        )
    }

    fun require(roomId: String): RoomRoute {
        val values = connection.sync().hgetall(key(roomId))
        check(values.isNotEmpty()) { "room route was not found: $roomId" }
        return RoomRoute(
            roomId = roomId,
            ownerPlayEndpoint = values.getValue("ownerPlayEndpoint"),
            ownerSpotEndpoint = values.getValue("ownerSpotEndpoint"),
            ownerSpotNodeRid = values.getValue("ownerSpotNodeRid"),
        )
    }

    override fun close() {
        connection.close()
        client.shutdown()
    }

    private fun key(roomId: String): String = keyPrefix + roomId

    private fun redisUri(endpoint: String): String =
        if (endpoint.startsWith("redis://")) endpoint else "redis://$endpoint"

    data class RoomRoute(
        val roomId: String,
        val ownerPlayEndpoint: String,
        val ownerSpotEndpoint: String,
        val ownerSpotNodeRid: String,
    )
}
