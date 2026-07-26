using Bingo.Server.Configuration;
using Bingo.Server.Play.Application.RoomAllocation;
using StackExchange.Redis;

namespace Bingo.Server.Play.Infrastructure.Redis;

internal sealed class RedisBingoMatchQueue(
    IConnectionMultiplexer redis,
    SampleRuntimeConfiguration<SamplePlayNode> configuration) : IBingoMatchQueue
{
    private const string Script = """
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

    public async ValueTask<BingoMatchReservation> ReserveAsync(
        string mode,
        string actorId,
        string newRoomId,
        int requiredPlayers,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var value = await redis.GetDatabase().ScriptEvaluateAsync(
            Script,
            [MatchKey(mode)],
            [
                actorId,
                newRoomId,
                requiredPlayers,
                DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
            ]);
        var roomId = (string?)value;
        if (string.IsNullOrWhiteSpace(roomId))
            throw new InvalidOperationException("Redis match queue returned an invalid reservation.");

        return new BingoMatchReservation(roomId);
    }

    private RedisKey MatchKey(string mode)
    {
        return $"{configuration.RedisKeyPrefix}match:{mode}";
    }
}
