package systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.matchmaking

import io.lettuce.core.RedisClient
import io.lettuce.core.ScriptOutputType
import io.lettuce.core.api.StatefulRedisConnection
import java.time.Instant
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.bingo.server.play.application.roomallocation.BingoMatchQueue
import systems.zlink.samples.kotlin.bingo.server.play.application.roomallocation.BingoMatchReservation

class RedisBingoMatchQueue(private val topology: SampleTopology) : BingoMatchQueue, AutoCloseable {
    private val client: RedisClient = RedisClient.create(redisUri(topology.redisEndpoint))
    private val connection: StatefulRedisConnection<String, String> = client.connect()

    override fun reserve(
        mode: String,
        actorId: String,
        newRoomId: String,
        requiredPlayers: Int,
    ): BingoMatchReservation {
        val roomId = connection.sync().eval<String>(
            Script,
            ScriptOutputType.VALUE,
            arrayOf(matchKey(mode)),
            actorId,
            newRoomId,
            requiredPlayers.toString(),
            Instant.now().toEpochMilli().toString(),
        )
        check(!roomId.isNullOrBlank()) {
            "Redis match queue returned an invalid reservation."
        }
        return BingoMatchReservation(roomId)
    }

    override fun close() {
        connection.close()
        client.shutdown()
    }

    private fun matchKey(mode: String): String =
        topology.redisKeyPrefix + "match:" + mode

    private fun redisUri(endpoint: String): String =
        if (endpoint.startsWith("redis://")) endpoint else "redis://$endpoint"

    companion object {
        private const val Script: String = """
local key = KEYS[1]
local actorId = ARGV[1]
local newRoomId = ARGV[2]
local requiredPlayers = tonumber(ARGV[3])
local nowMs = ARGV[4]

local roomId = redis.call('HGET', key, 'RoomId')
if not roomId then
  redis.call('HMSET', key,
    'RoomId', newRoomId,
    'ReservedActorIds', actorId,
    'RequiredPlayers', requiredPlayers,
    'CreatedAtUnixMs', nowMs)
  return newRoomId
end

local actors = redis.call('HGET', key, 'ReservedActorIds') or ''
local needle = '|' .. actorId .. '|'
if string.find('|' .. actors .. '|', needle, 1, true) then
  return roomId
end

local count = 0
for _ in string.gmatch(actors, '[^|]+') do
  count = count + 1
end

if count >= requiredPlayers then
  redis.call('HMSET', key,
    'RoomId', newRoomId,
    'ReservedActorIds', actorId,
    'RequiredPlayers', requiredPlayers,
    'CreatedAtUnixMs', nowMs)
  return newRoomId
end

if actors == '' then
  actors = actorId
else
  actors = actors .. '|' .. actorId
end
redis.call('HSET', key, 'ReservedActorIds', actors)
if count + 1 >= requiredPlayers then
  redis.call('DEL', key)
end
return roomId
"""
    }
}
