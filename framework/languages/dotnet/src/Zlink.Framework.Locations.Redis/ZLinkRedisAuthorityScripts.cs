namespace Zlink.Framework.Locations.Redis;

internal static class ZLinkRedisAuthorityScripts
{
    private const string Prologue = """
        if redis.replicate_commands then redis.replicate_commands() end
        local time = redis.call('TIME')
        local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
        local maxCounter = '9223372036854775807'
        local function canIncrement(value)
            if not value then return true end
            if string.len(value) < string.len(maxCounter) then return true end
            if string.len(value) > string.len(maxCounter) then return false end
            return value < maxCounter
        end
        """;

    internal const string Read = Prologue + """

        local version = redis.call('HGET', KEYS[1], ARGV[1])
        if not version then return {nowMs, 0} end
        return {
            nowMs, 1, version,
            redis.call('HGET', KEYS[2], ARGV[1]),
            redis.call('HGET', KEYS[3], ARGV[1]),
            redis.call('HGET', KEYS[4], ARGV[1]),
            redis.call('HGET', KEYS[5], ARGV[1]),
            redis.call('HGET', KEYS[6], ARGV[1])
        }
        """;

    internal const string CompareExchange = Prologue + """

        local key = ARGV[1]
        local current = redis.call('HGET', KEYS[2], key)
        local matches = false
        if ARGV[2] == 'missing' then
            matches = not current
        else
            matches = current and current == ARGV[3]
        end
        if not matches then
            if not current then return {'conflict-missing', nowMs} end
            return {
                'conflict-found', nowMs, current,
                redis.call('HGET', KEYS[3], key),
                redis.call('HGET', KEYS[4], key),
                redis.call('HGET', KEYS[5], key),
                redis.call('HGET', KEYS[6], key),
                redis.call('HGET', KEYS[7], key)
            }
        end
        if not canIncrement(redis.call('HGET', KEYS[1], 'revision')) then
            return {'exhausted', nowMs}
        end
        local version = redis.call('HINCRBY', KEYS[1], 'revision', 1)
        if ARGV[4] == 'delete' then
            for index = 2, 8 do redis.call('HDEL', KEYS[index], key) end
            redis.call('SREM', KEYS[9], key)
            return {'deleted', nowMs, version}
        end

        redis.call('HSET', KEYS[2], key, version)
        redis.call('HSET', KEYS[3], key, ARGV[5])
        redis.call('SADD', KEYS[9], key)
        return {
            'stored', nowMs, version,
            redis.call('HGET', KEYS[3], key),
            redis.call('HGET', KEYS[4], key),
            redis.call('HGET', KEYS[5], key),
            redis.call('HGET', KEYS[6], key),
            redis.call('HGET', KEYS[7], key)
        }
        """;

    internal const string StartScan = Prologue + """

        redis.call('DEL', KEYS[9])
        local keys = redis.call('SMEMBERS', KEYS[8])
        table.sort(keys)
        for _, key in ipairs(keys) do
            if string.sub(key, 1, string.len(ARGV[1])) == ARGV[1] then
                local version = redis.call('HGET', KEYS[1], key)
                if version then
                    redis.call('RPUSH', KEYS[9],
                        key, version,
                        redis.call('HGET', KEYS[2], key),
                        redis.call('HGET', KEYS[3], key),
                        redis.call('HGET', KEYS[4], key),
                        redis.call('HGET', KEYS[5], key),
                        redis.call('HGET', KEYS[6], key))
                end
            end
        end
        redis.call('PEXPIRE', KEYS[9], ARGV[2])
        local total = math.floor(redis.call('LLEN', KEYS[9]) / 7)
        local count = math.min(tonumber(ARGV[3]), total)
        local values = {}
        if count > 0 then
            values = redis.call('LRANGE', KEYS[9], 0, count * 7 - 1)
        end
        return {nowMs, total, values}
        """;

    internal const string ContinueScan = Prologue + """

        if redis.call('EXISTS', KEYS[1]) == 0 then return {'expired', nowMs} end
        redis.call('PEXPIRE', KEYS[1], ARGV[3])
        local total = math.floor(redis.call('LLEN', KEYS[1]) / 7)
        local position = tonumber(ARGV[1])
        local count = math.min(tonumber(ARGV[2]), total - position)
        local values = {}
        if count > 0 then
            values = redis.call(
                'LRANGE', KEYS[1], position * 7, (position + count) * 7 - 1)
        end
        return {'page', nowMs, total, values}
        """;

    internal const string Reserve = Prologue + """

        local key = ARGV[1]
        local existing = redis.call('HGET', KEYS[2], key)
        if existing then
            return {
                'exists', nowMs, existing,
                redis.call('HGET', KEYS[3], key),
                redis.call('HGET', KEYS[4], key),
                redis.call('HGET', KEYS[5], key),
                redis.call('HGET', KEYS[6], key),
                redis.call('HGET', KEYS[7], key)
            }
        end
        if redis.call('EXISTS', KEYS[9]) == 0 then
            return {'owner-stale', nowMs}
        end
        if not canIncrement(redis.call('HGET', KEYS[1], 'revision'))
            or not canIncrement(redis.call('HGET', KEYS[1], 'object'))
            or not canIncrement(redis.call('HGET', KEYS[1], 'owner')) then
            return {'exhausted', nowMs}
        end
        local version = redis.call('HINCRBY', KEYS[1], 'revision', 1)
        local objectGeneration = redis.call('HINCRBY', KEYS[1], 'object', 1)
        local ownerGeneration = redis.call('HINCRBY', KEYS[1], 'owner', 1)
        redis.call('HSET', KEYS[2], key, version)
        redis.call('HSET', KEYS[3], key, ARGV[2])
        redis.call('HSET', KEYS[4], key, objectGeneration)
        redis.call('HSET', KEYS[5], key, ownerGeneration)
        redis.call('HSET', KEYS[6], key, ARGV[3])
        redis.call('HSET', KEYS[7], key, ARGV[4])
        redis.call('SADD', KEYS[8], key)
        redis.call('HSET', KEYS[10],
            'status', 'reserved',
            'key', key,
            'version', version,
            'object', objectGeneration,
            'ownerGeneration', ownerGeneration,
            'ownerId', ARGV[3],
            'ownerLease', ARGV[4],
            'targetMesh', ARGV[5],
            'targetRid', ARGV[6])
        return {'reserved', nowMs, version, objectGeneration, ownerGeneration}
        """;

    internal const string CommitReservation = Prologue + """

        local status = redis.call('HGET', KEYS[9], 'status')
        if not status then return {'stale', nowMs} end
        if status == 'committed' then
            local key = redis.call('HGET', KEYS[9], 'key')
            return {
                'already', nowMs,
                redis.call('HGET', KEYS[2], key),
                redis.call('HGET', KEYS[3], key),
                redis.call('HGET', KEYS[4], key),
                redis.call('HGET', KEYS[5], key),
                redis.call('HGET', KEYS[6], key),
                redis.call('HGET', KEYS[7], key)
            }
        end
        if status ~= 'reserved' then return {'stale', nowMs} end
        local key = redis.call('HGET', KEYS[9], 'key')
        if key ~= ARGV[1]
            or redis.call('HGET', KEYS[2], key) ~= ARGV[2] then
            return {'stale', nowMs}
        end
        if not canIncrement(redis.call('HGET', KEYS[1], 'revision')) then
            return {'exhausted', nowMs}
        end
        local version = redis.call('HINCRBY', KEYS[1], 'revision', 1)
        redis.call('HSET', KEYS[2], key, version)
        redis.call('HSET', KEYS[3], key, ARGV[3])
        redis.call('HSET', KEYS[9], 'status', 'committed', 'finalVersion', version)
        return {
            'committed', nowMs, version,
            redis.call('HGET', KEYS[3], key),
            redis.call('HGET', KEYS[4], key),
            redis.call('HGET', KEYS[5], key),
            redis.call('HGET', KEYS[6], key),
            redis.call('HGET', KEYS[7], key)
        }
        """;

    internal const string AbortReservation = Prologue + """

        local status = redis.call('HGET', KEYS[9], 'status')
        if not status then return {'stale', nowMs} end
        if status == 'aborted' then return {'already', nowMs} end
        if status ~= 'reserved' then return {'stale', nowMs} end
        local key = redis.call('HGET', KEYS[9], 'key')
        if key ~= ARGV[1]
            or redis.call('HGET', KEYS[2], key) ~= ARGV[2] then
            return {'stale', nowMs}
        end
        if not canIncrement(redis.call('HGET', KEYS[1], 'revision')) then
            return {'exhausted', nowMs}
        end
        redis.call('HINCRBY', KEYS[1], 'revision', 1)
        for index = 2, 7 do redis.call('HDEL', KEYS[index], key) end
        redis.call('SREM', KEYS[8], key)
        redis.call('HSET', KEYS[9], 'status', 'aborted')
        return {'aborted', nowMs}
        """;

    internal const string PrepareAggregate = Prologue + """

        local status = redis.call('HGET', KEYS[9], 'status')
        if status == 'prepared' then return {'already', nowMs} end
        if status then return {'stale', nowMs} end
        if redis.call('EXISTS', KEYS[10]) == 0 then
            return {'conflict', nowMs}
        end
        local count = tonumber(ARGV[3])
        local offset = 4
        for index = 0, count - 1 do
            local key = ARGV[offset + index * 5]
            local expected = ARGV[offset + index * 5 + 1]
            if redis.call('HGET', KEYS[1], key) ~= expected then
                return {'conflict', nowMs}
            end
        end
        redis.call('HSET', KEYS[9],
            'status', 'prepared',
            'targetOwner', ARGV[1],
            'targetLease', ARGV[2],
            'count', count)
        for index = 0, count - 1 do
            local base = offset + index * 5
            redis.call('HSET', KEYS[9],
                'key:' .. index, ARGV[base],
                'expected:' .. index, ARGV[base + 1],
                'transition:' .. index, ARGV[base + 2],
                'payload:' .. index, ARGV[base + 3],
                'membership:' .. index, ARGV[base + 4])
        end
        return {'prepared', nowMs}
        """;

    internal const string CommitAggregate = Prologue + """

        local status = redis.call('HGET', KEYS[9], 'status')
        if status == 'committed' then return {'already', nowMs} end
        if status ~= 'prepared' then return {'stale', nowMs} end
        local count = tonumber(redis.call('HGET', KEYS[9], 'count'))
        local ownerChanges = 0
        for index = 0, count - 1 do
            local key = redis.call('HGET', KEYS[9], 'key:' .. index)
            local expected = redis.call('HGET', KEYS[9], 'expected:' .. index)
            if redis.call('HGET', KEYS[1], key) ~= expected then
                return {'stale', nowMs}
            end
            local transition = redis.call('HGET', KEYS[9], 'transition:' .. index)
            if transition == '2' or transition == '3' then
                ownerChanges = ownerChanges + 1
            end
        end
        local revision = redis.call('HGET', KEYS[8], 'revision')
        local ownerCounter = redis.call('HGET', KEYS[8], 'owner')
        for index = 1, count do
            if not canIncrement(revision) then return {'exhausted', nowMs} end
            revision = tostring(tonumber(revision or '0') + 1)
        end
        for index = 1, ownerChanges do
            if not canIncrement(ownerCounter) then return {'exhausted', nowMs} end
            ownerCounter = tostring(tonumber(ownerCounter or '0') + 1)
        end
        local targetOwner = redis.call('HGET', KEYS[9], 'targetOwner')
        local targetLease = redis.call('HGET', KEYS[9], 'targetLease')
        for index = 0, count - 1 do
            local key = redis.call('HGET', KEYS[9], 'key:' .. index)
            local transition = redis.call('HGET', KEYS[9], 'transition:' .. index)
            local version = redis.call('HINCRBY', KEYS[8], 'revision', 1)
            redis.call('HSET', KEYS[1], key, version)
            redis.call('HSET', KEYS[2], key,
                redis.call('HGET', KEYS[9], 'payload:' .. index))
            redis.call('HSET', KEYS[7], key,
                redis.call('HGET', KEYS[9], 'membership:' .. index))
            if transition == '2' or transition == '3' then
                local ownerGeneration = redis.call('HINCRBY', KEYS[8], 'owner', 1)
                redis.call('HSET', KEYS[4], key, ownerGeneration)
                redis.call('HSET', KEYS[5], key, targetOwner)
                redis.call('HSET', KEYS[6], key, targetLease)
            end
        end
        redis.call('HSET', KEYS[9], 'status', 'committed')
        return {'committed', nowMs}
        """;

    internal const string AbortAggregate = Prologue + """

        local status = redis.call('HGET', KEYS[1], 'status')
        if status == 'aborted' then return {'already', nowMs} end
        if status ~= 'prepared' then return {'stale', nowMs} end
        redis.call('HSET', KEYS[1], 'status', 'aborted')
        return {'aborted', nowMs}
        """;
}
