package systems.zlink.samples.bingo.server.play.infrastructure.zlink.matchmaking;

import io.lettuce.core.RedisClient;
import io.lettuce.core.ScriptOutputType;
import io.lettuce.core.api.StatefulRedisConnection;
import java.time.Instant;
import systems.zlink.samples.bingo.server.configuration.SampleTopology;
import systems.zlink.samples.bingo.server.play.application.roomallocation.BingoMatchQueue;
import systems.zlink.samples.bingo.server.play.application.roomallocation.BingoMatchReservation;

public final class RedisBingoMatchQueue implements BingoMatchQueue, AutoCloseable {
    private static final String SCRIPT = """
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
""";

    private final RedisClient client;
    private final StatefulRedisConnection<String, String> connection;
    private final String keyPrefix;

    public RedisBingoMatchQueue(SampleTopology topology) {
        client = RedisClient.create(redisUri(topology.redisEndpoint()));
        connection = client.connect();
        keyPrefix = topology.redisKeyPrefix();
    }

    @Override
    public BingoMatchReservation reserve(
        String mode,
        String actorId,
        String newRoomId,
        int requiredPlayers) {
        String roomId = connection.sync().eval(
            SCRIPT,
            ScriptOutputType.VALUE,
            new String[] { matchKey(mode) },
            actorId,
            newRoomId,
            Integer.toString(requiredPlayers),
            Long.toString(Instant.now().toEpochMilli()));
        if (roomId == null || roomId.isBlank()) {
            throw new IllegalStateException("Redis match queue returned an invalid reservation.");
        }
        return new BingoMatchReservation(roomId);
    }

    @Override
    public void close() {
        connection.close();
        client.shutdown();
    }

    private String matchKey(String mode) {
        return keyPrefix + "match:" + mode;
    }

    private static String redisUri(String endpoint) {
        return endpoint.startsWith("redis://") ? endpoint : "redis://" + endpoint;
    }
}
