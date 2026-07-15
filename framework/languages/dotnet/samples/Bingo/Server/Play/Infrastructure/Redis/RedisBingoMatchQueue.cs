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
                                  local ownerRid = ARGV[2]
                                  local newRoomId = ARGV[3]
                                  local requiredPlayers = tonumber(ARGV[4])
                                  local nowMs = ARGV[5]

                                  local roomId = redis.call('HGET', key, 'RoomId')
                                  if not roomId then
                                    redis.call('HMSET', key,
                                      'RoomId', newRoomId,
                                      'OwnerPlayNodeRid', ownerRid,
                                      'ReservedActorIds', actorId,
                                      'RequiredPlayers', requiredPlayers,
                                      'CreatedAtUnixMs', nowMs)
                                    return { newRoomId, ownerRid }
                                  end

                                  local existingOwnerRid = redis.call('HGET', key, 'OwnerPlayNodeRid')
                                  local actors = redis.call('HGET', key, 'ReservedActorIds') or ''
                                  local needle = '|' .. actorId .. '|'
                                  if string.find('|' .. actors .. '|', needle, 1, true) then
                                    return { roomId, existingOwnerRid }
                                  end

                                  local count = 0
                                  for _ in string.gmatch(actors, '[^|]+') do
                                    count = count + 1
                                  end

                                  if count >= requiredPlayers then
                                    redis.call('HMSET', key,
                                      'RoomId', newRoomId,
                                      'OwnerPlayNodeRid', ownerRid,
                                      'ReservedActorIds', actorId,
                                      'RequiredPlayers', requiredPlayers,
                                      'CreatedAtUnixMs', nowMs)
                                    return { newRoomId, ownerRid }
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
                                  return { roomId, existingOwnerRid }
                                  """;

    public async ValueTask<BingoMatchReservation> ReserveAsync(
        string mode,
        string actorId,
        string preferredOwnerNodeRid,
        string newRoomId,
        int requiredPlayers,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var values = (RedisResult[]?)await redis.GetDatabase().ScriptEvaluateAsync(
            Script,
            [MatchKey(mode)],
            [
                actorId,
                preferredOwnerNodeRid,
                newRoomId,
                requiredPlayers,
                DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
            ]);
        if (values is not { Length: 2 })
            throw new InvalidOperationException("Redis match queue returned an invalid reservation.");

        return new BingoMatchReservation(
            (string)values[0]!,
            (string)values[1]!);
    }

    private RedisKey MatchKey(string mode)
    {
        return $"{configuration.RedisKeyPrefix}match:{mode}";
    }
}
