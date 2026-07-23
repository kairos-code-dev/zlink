package systems.zlink.framework.locations.redis;

final class ZLinkRedisLocationScripts {
    private ZLinkRedisLocationScripts() {
    }

    private static final String PROLOGUE = """
        if redis.replicate_commands then redis.replicate_commands() end
        local time = redis.call('TIME')
        local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
        """;

    static final String WRITE = PROLOGUE + """

        local intent = ARGV[1]
        local owner = ARGV[2]
        local currentOwner = redis.call('HGET', KEYS[1], 'owner')

        local function bumpStamps()
            redis.call('INCR', ARGV[8])
            if ARGV[9] ~= '' then redis.call('INCR', ARGV[9]) end
        end

        local function storeRow(gen)
            redis.call('HSET', KEYS[1],
                'owner', owner, 'gen', gen, 'json', ARGV[4], 'updatedAtMs', nowMs)
            if ARGV[10] == '1' then
                redis.call('HSET', KEYS[1], 'mesh', ARGV[11])
            end
            redis.call('SADD', KEYS[3], ARGV[5])
            redis.call('SADD', ARGV[7] .. owner, ARGV[5])
            if currentOwner and currentOwner ~= owner then
                redis.call('SREM', ARGV[7] .. currentOwner, ARGV[5])
            end
            bumpStamps()
        end

        if intent == 'new' then
            if currentOwner
                and tonumber(
                    redis.call(
                        'HGET', KEYS[4], 'expiresAt') or '0')
                    > nowMs then
                return {'conflict', 0, nowMs}
            end
            local gen = redis.call('INCR', KEYS[2])
            storeRow(gen)
            return {'stored', gen, nowMs}
        end

        if intent == 'takeover' then
            local gen = redis.call('INCR', KEYS[2])
            storeRow(gen)
            return {'stored', gen, nowMs}
        end

        if currentOwner and currentOwner == owner
            and tonumber(redis.call('HGET', KEYS[1], 'gen')) == tonumber(ARGV[3]) then
            local gen = tonumber(ARGV[3])
            redis.call('HSET', KEYS[1], 'json', ARGV[4], 'updatedAtMs', nowMs)
            bumpStamps()
            return {'stored', gen, nowMs}
        end
        return {'stale', 0, nowMs}
        """;

    static final String WRITE_MESH_NODE = PROLOGUE + """

        local function leaseIsLive(leaseKey, ownerId, generation)
            return redis.call('HGET', leaseKey, 'ownerId')
                    == ownerId
                and redis.call('HGET', leaseKey, 'generation')
                    == generation
                and tonumber(
                    redis.call('HGET', leaseKey, 'expiresAt') or '0')
                    > nowMs
        end

        if not leaseIsLive(KEYS[3], ARGV[2], ARGV[3]) then
            return {'stale', 0, nowMs}
        end

        local currentOwner = redis.call('HGET', KEYS[1], 'owner')
        local currentLease = redis.call(
            'HGET', KEYS[4], 'ownerLeaseGeneration')
        local currentLifecycle =
            redis.call(
                'HGET', KEYS[4], 'lifecycleGeneration')
        local currentRevision = redis.call(
            'HGET', KEYS[4], 'descriptorRevision')
        local currentFingerprint =
            redis.call('HGET', KEYS[4], 'immutableDigest')

        if ARGV[1] == 'new' then
            if currentOwner
                and leaseIsLive(
                    KEYS[9], currentOwner, currentLease) then
                return {'conflict', 0, nowMs}
            end
        elseif ARGV[1] == 'takeover' then
            if currentOwner
                and leaseIsLive(
                    KEYS[9], currentOwner, currentLease) then
                return {'conflict', 0, nowMs}
            end
        else
            if not currentOwner
                or currentOwner ~= ARGV[2]
                or currentLease ~= ARGV[3]
                or currentLifecycle ~= ARGV[4]
                or not currentFingerprint
                or not currentRevision then
                return {'stale', 0, nowMs}
            end
            if tonumber(ARGV[5]) == tonumber(currentRevision) then
                if currentFingerprint == ARGV[6]
                    and redis.call('HGET', KEYS[1], 'json') == ARGV[7] then
                    return {'stored', tonumber(currentLifecycle), nowMs}
                end
                return {'protocol-error', 0, nowMs}
            end
            if tonumber(ARGV[5]) < tonumber(currentRevision) then
                return {'stale', 0, nowMs}
            end
            if currentFingerprint ~= ARGV[6] then
                return {'stale', 0, nowMs}
            end
        end

        local previousOwner = currentOwner
        redis.call('HSET', KEYS[1],
            'owner', ARGV[2],
            'gen', ARGV[4],
            'json', ARGV[7],
            'updatedAtMs', nowMs,
            'mesh', ARGV[9])
        redis.call('HSET', KEYS[4],
            'descriptorKey', ARGV[13],
            'descriptorRevision', ARGV[5],
            'lifecycleGeneration', ARGV[4],
            'ownerId', ARGV[2],
            'ownerLeaseGeneration', ARGV[3],
            'objectRole', ARGV[10],
            'runtimeState', ARGV[11],
            'applicationVersion', ARGV[12],
            'capabilities', ARGV[14],
            'nodeActiveLimit', ARGV[15],
            'nodePendingLimit', ARGV[16],
            'immutableDigest', ARGV[6])
        redis.call('SADD', KEYS[2], ARGV[8])
        redis.call('SADD', KEYS[5], ARGV[8])
        if previousOwner and previousOwner ~= ARGV[2] then
            redis.call('SREM', KEYS[6], ARGV[8])
        end
        redis.call('INCR', KEYS[7])
        redis.call('INCR', KEYS[8])
        return {'stored', tonumber(ARGV[4]), nowMs}
        """;

    static final String REMOVE_MESH_NODE = PROLOGUE + """

        local currentOwner = redis.call('HGET', KEYS[1], 'owner')
        local currentLease =
            redis.call(
                'HGET', KEYS[3], 'ownerLeaseGeneration')
        if not currentOwner
            or currentOwner ~= ARGV[1]
            or currentLease ~= ARGV[2] then
            return {'stale', 0, nowMs}
        end
        redis.call('DEL', KEYS[1])
        redis.call('DEL', KEYS[3])
        redis.call('SREM', KEYS[2], ARGV[3])
        redis.call('SREM', KEYS[4], ARGV[3])
        redis.call('INCR', KEYS[5])
        redis.call('INCR', KEYS[6])
        return {'stored', tonumber(ARGV[2]), nowMs}
        """;

    static final String WRITE_CLIENT_SERVER = PROLOGUE + """

        local intent = ARGV[1]
        local owner = ARGV[2]
        local leaseGeneration = ARGV[3]
        local lifecycle = ARGV[4]
        local revision = tonumber(ARGV[5])
        local immutable = ARGV[6]
        local json = ARGV[7]
        local lease = redis.call(
            'HMGET', KEYS[4], 'ownerId', 'generation', 'expiresAt')
        if lease[1] ~= owner or lease[2] ~= leaseGeneration
            or tonumber(lease[3] or '0') <= nowMs then
            return {'conflict', 0, nowMs}
        end

        local function nextGeneration()
            local current =
                redis.call('HGET', KEYS[5], 'descriptorGeneration')
                    or '0'
            if current == '9223372036854775807' then
                return nil
            end
            redis.call(
                'HINCRBY', KEYS[5], 'descriptorGeneration', 1)
            return redis.call(
                'HGET', KEYS[5], 'descriptorGeneration')
        end

        local function store(gen)
            redis.call('HSET', KEYS[1],
                'owner', owner,
                'gen', gen,
                'json', json,
                'updatedAtMs', nowMs,
                'channel', ARGV[10])
            redis.call('HSET', KEYS[2],
                'descriptorKey', ARGV[11],
                'lifecycleGeneration', lifecycle,
                'descriptorRevision', revision,
                'immutableDigest', immutable,
                'ownerId', owner,
                'ownerLeaseGeneration', leaseGeneration,
                'runtimeState', ARGV[8],
                'weight', ARGV[9])
            redis.call('SADD', KEYS[3], ARGV[11])
            redis.call('SADD', KEYS[6], ARGV[11])
            redis.call('ZADD', KEYS[11], 0, ARGV[11])
            redis.call('INCR', KEYS[9])
            redis.call('INCR', KEYS[10])
        end

        if redis.call('EXISTS', KEYS[1]) == 0 then
            if intent ~= 'new' and intent ~= 'takeover' then
                return {'stale', 0, nowMs}
            end
            local gen = nextGeneration()
            if not gen then
                return {'exhausted', 0, nowMs}
            end
            store(gen)
            return {'stored', gen, nowMs}
        end

        local storedOwner =
            redis.call('HGET', KEYS[2], 'ownerId')
        local storedLeaseGeneration =
            redis.call(
                'HGET', KEYS[2], 'ownerLeaseGeneration')
        if storedOwner ~= owner
            or storedLeaseGeneration ~= leaseGeneration then
            if storedOwner ~= ARGV[12]
                or storedLeaseGeneration ~= ARGV[13] then
                return {'stale', 0, nowMs}
            end
            local oldLease = redis.call(
                'HMGET', KEYS[7],
                'ownerId', 'generation', 'expiresAt')
            if oldLease[1] == storedOwner
                and oldLease[2] == storedLeaseGeneration
                and tonumber(oldLease[3] or '0') > nowMs then
                return {'conflict', 0, nowMs}
            end
            if intent ~= 'new' and intent ~= 'takeover' then
                return {'stale', 0, nowMs}
            end
            local gen = nextGeneration()
            if not gen then
                return {'exhausted', 0, nowMs}
            end
            redis.call('SREM', KEYS[8], ARGV[11])
            store(gen)
            return {'stored', gen, nowMs}
        end

        local storedRevision =
            tonumber(redis.call(
                'HGET', KEYS[2], 'descriptorRevision'))
        if redis.call(
                'HGET', KEYS[2], 'lifecycleGeneration') == lifecycle
            and redis.call(
                'HGET', KEYS[2], 'immutableDigest') == immutable
            and revision == storedRevision then
            if redis.call('HGET', KEYS[1], 'json') == json then
                return {
                    'stored',
                    redis.call('HGET', KEYS[1], 'gen'),
                    redis.call('HGET', KEYS[1], 'updatedAtMs')
                }
            end
            return {'protocol-error', 0, nowMs}
        end
        if redis.call(
                'HGET', KEYS[2], 'lifecycleGeneration') ~= lifecycle
            or redis.call(
                'HGET', KEYS[2], 'immutableDigest') ~= immutable
            or revision <= storedRevision then
            return {
                'stale',
                redis.call('HGET', KEYS[1], 'gen'),
                nowMs
            }
        end
        local gen = redis.call('HGET', KEYS[1], 'gen')
        store(gen)
        return {'stored', gen, nowMs}
        """;

    static final String REMOVE_CLIENT_SERVER = PROLOGUE + """

        local owner = redis.call('HGET', KEYS[2], 'ownerId')
        local leaseGeneration = redis.call(
            'HGET', KEYS[2], 'ownerLeaseGeneration')
        if not owner or owner ~= ARGV[1]
            or leaseGeneration ~= ARGV[2] then
            return {'stale', 0, nowMs}
        end
        local generation = redis.call('HGET', KEYS[1], 'gen')
        redis.call('DEL', KEYS[1], KEYS[2])
        redis.call('SREM', KEYS[3], ARGV[3])
        redis.call('SREM', KEYS[4], ARGV[3])
        redis.call('INCR', KEYS[5])
        redis.call('INCR', KEYS[6])
        redis.call('ZREM', KEYS[7], ARGV[3])
        return {'stored', generation, nowMs}
        """;

    // Both service descriptor families share the same atomic fence algorithm.
    // Their physical keys, indexes, immutable digest and row codec remain
    // dedicated to the descriptor family.
    static final String WRITE_FANOUT_PUBLISHER =
        WRITE_CLIENT_SERVER;
    static final String REMOVE_FANOUT_PUBLISHER =
        REMOVE_CLIENT_SERVER;

    static final String REMOVE = PROLOGUE + """

        local currentOwner = redis.call('HGET', KEYS[1], 'owner')
        if not currentOwner
            or currentOwner ~= ARGV[1]
            or tonumber(redis.call('HGET', KEYS[1], 'gen')) ~= tonumber(ARGV[2]) then
            return {'stale', 0, nowMs}
        end

        redis.call('DEL', KEYS[1])
        redis.call('SREM', KEYS[2], ARGV[3])
        redis.call('SREM', ARGV[4] .. currentOwner, ARGV[3])
        redis.call('INCR', ARGV[5])
        if ARGV[6] ~= '' then redis.call('INCR', ARGV[6]) end
        return {'stored', tonumber(ARGV[2]), nowMs}
        """;

    static final String REMOVE_ALL_BY_OWNER = PROLOGUE + """
        local lease = redis.call(
            'HMGET', KEYS[9], 'ownerId', 'generation', 'expiresAt')
        if lease[1] ~= ARGV[9]
            or lease[2] ~= ARGV[10]
            or tonumber(lease[3] or '0') <= nowMs then
            return -1
        end
        local removed = 0
        for i = 1, 4 do
            local ownerIndex = KEYS[i]
            local kindIndex = KEYS[i + 4]
            local rowPrefix = ARGV[i]
            local stampBase = ARGV[i + 5]
            local rowKeys = redis.call('SMEMBERS', ownerIndex)
            for _, indexedKey in ipairs(rowKeys) do
                local rowKey = indexedKey
                local rowHash = rowPrefix .. indexedKey
                local mesh = redis.call('HGET', rowHash, 'mesh')
                if redis.call('DEL', rowHash) == 1 then
                    removed = removed + 1
                    redis.call('SREM', kindIndex, rowKey)
                    if mesh then
                        redis.call('INCR', stampBase .. ':' .. mesh)
                    end
                    redis.call('INCR', stampBase)
                end
            end
            redis.call('DEL', ownerIndex)
        end
        return removed
        """;

    static final String REMOVE_ALL_MESH_NODES = PROLOGUE + """

        local lease = redis.call(
            'HMGET', KEYS[4], 'ownerId', 'generation', 'expiresAt')
        if lease[1] ~= ARGV[1]
            or lease[2] ~= ARGV[2]
            or tonumber(lease[3] or '0') <= nowMs then
            return -1
        end
        local removed = 0
        for index = 3, #ARGV do
            local keyOffset = 5 + (index - 3) * 3
            local rowKey = ARGV[index]
            if redis.call('HGET', KEYS[keyOffset], 'owner')
                    == ARGV[1] then
                redis.call('DEL', KEYS[keyOffset])
                redis.call('DEL', KEYS[keyOffset + 1])
                redis.call('SREM', KEYS[2], rowKey)
                redis.call('INCR', KEYS[3])
                redis.call('INCR', KEYS[keyOffset + 2])
                removed = removed + 1
            end
            redis.call('SREM', KEYS[1], rowKey)
        end
        if redis.call('SCARD', KEYS[1]) == 0 then
            redis.call('DEL', KEYS[1])
        end
        return removed
        """;

    static final String RENEW_LEASE = PROLOGUE + """

        redis.call('SET', KEYS[1], ARGV[2] .. '|' .. nowMs, 'PX', ARGV[3])
        redis.call('SADD', KEYS[2], ARGV[1])
        return nowMs
        """;

    static final String CLAIM_OWNER_LEASE = PROLOGUE + """

        local currentExpiry = tonumber(
            redis.call('HGET', KEYS[1], 'expiresAt') or '0')
        if currentExpiry > nowMs then
                return {'conflict', nowMs}
        end
        if redis.call(
                'HGET', KEYS[2], 'leaseGeneration')
                == '9223372036854775807' then
            return {'generation-exhausted', nowMs}
        end
        local generation = redis.call(
            'HINCRBY', KEYS[2], 'leaseGeneration', 1)
        local expiresAt = nowMs + tonumber(ARGV[2])
        redis.call('HSET', KEYS[1],
            'ownerId', ARGV[1],
            'generation', generation,
            'expiresAt', expiresAt)
        redis.call('PEXPIRE', KEYS[1], ARGV[2])
        return {'claimed', generation, expiresAt, nowMs}
        """;

    static final String READ_OWNER_LEASE = PROLOGUE + """

        if redis.call('HGET', KEYS[1], 'ownerId') ~= ARGV[1] then
            return {'missing', nowMs}
        end
        local generation = redis.call(
            'HGET', KEYS[1], 'generation')
        local expiresAt = redis.call(
            'HGET', KEYS[1], 'expiresAt')
        if tonumber(expiresAt) <= nowMs then
            redis.call('DEL', KEYS[1])
            return {'missing', nowMs}
        end
        return {'found', generation, expiresAt, nowMs}
        """;

    static final String RENEW_OWNER_LEASE = PROLOGUE + """

        if redis.call('HGET', KEYS[1], 'ownerId') ~= ARGV[1] then
            return {'stale', nowMs}
        end
        local generation = redis.call(
            'HGET', KEYS[1], 'generation')
        local expiresAt = redis.call(
            'HGET', KEYS[1], 'expiresAt')
        if generation ~= ARGV[2] or tonumber(expiresAt) <= nowMs then
            if tonumber(expiresAt) <= nowMs then
                redis.call('DEL', KEYS[1])
            end
            return {'stale', nowMs}
        end
        local renewedExpiry = nowMs + tonumber(ARGV[3])
        redis.call('HSET', KEYS[1], 'expiresAt', renewedExpiry)
        redis.call('PEXPIRE', KEYS[1], ARGV[3])
        return {'renewed', renewedExpiry, nowMs}
        """;

    static final String RELEASE_OWNER_LEASE = PROLOGUE + """

        if redis.call('HGET', KEYS[1], 'ownerId') ~= ARGV[1] then
            return {'stale', nowMs}
        end
        local generation = redis.call(
            'HGET', KEYS[1], 'generation')
        local expiresAt = redis.call(
            'HGET', KEYS[1], 'expiresAt')
        if generation ~= ARGV[2] or tonumber(expiresAt) <= nowMs then
            if tonumber(expiresAt) <= nowMs then
                redis.call('DEL', KEYS[1])
            end
            return {'stale', nowMs}
        end
        redis.call('DEL', KEYS[1])
        return {'released', nowMs}
        """;

    static final String REMOVE_LEASE = PROLOGUE + """

        local removed = redis.call('DEL', KEYS[1])
        redis.call('SREM', KEYS[2], ARGV[1])
        return removed
        """;

    static final String LIST_LEASES = PROLOGUE + """

        local owners = redis.call('SMEMBERS', KEYS[1])
        local out = {}
        for _, ownerId in ipairs(owners) do
            local leaseKey = ARGV[1] .. ownerId
            local pttl = redis.call('PTTL', leaseKey)
            if pttl < 0 then
                redis.call('SREM', KEYS[1], ownerId)
            else
                out[#out + 1] = ownerId
                out[#out + 1] = redis.call('GET', leaseKey)
                out[#out + 1] = pttl
            end
        end
        return {nowMs, out}
        """;

    static final String ACQUIRE_ROUTING_ID_SLOT = PROLOGUE + """

        local config = redis.call('HGET', KEYS[1], 'config')
        local slotCount = tonumber(ARGV[2])
        if not config then
            redis.call('HSET', KEYS[1],
                'config', ARGV[1], 'slotCount', slotCount, 'identityMode', 'allocated')
        elseif redis.call('HGET', KEYS[1], 'identityMode') ~= 'allocated' then
            return {'identity-conflict', nowMs}
        elseif config ~= ARGV[1]
            or tonumber(redis.call('HGET', KEYS[1], 'slotCount')) ~= slotCount then
            return {'mismatch', config, redis.call('HGET', KEYS[1], 'slotCount'), nowMs}
        end

        local ownerField = 'owner:' .. ARGV[3]
        local existingSlot = tonumber(redis.call('HGET', KEYS[1], ownerField))
        if existingSlot then
            local value = redis.call('HGET', KEYS[1], 'slot:' .. existingSlot)
            if value then
                local currentOwner, generation = string.match(value, '([^|]*)|([^|]*)|')
                if currentOwner == ARGV[3] and redis.call('EXISTS', KEYS[2]) == 1 then
                    local renewedExpiry = nowMs + tonumber(ARGV[4])
                    redis.call('HSET', KEYS[1], 'slot:' .. existingSlot,
                        currentOwner .. '|' .. generation .. '|' .. renewedExpiry)
                    local leaseValue = redis.call('GET', KEYS[2])
                    local nodeRid = leaseValue and string.match(leaseValue, '([^|]*)|') or ''
                    redis.call('SET', KEYS[2], nodeRid .. '|' .. nowMs, 'PX', ARGV[4])
                    redis.call('SADD', KEYS[3], ARGV[3])
                    return {'acquired', existingSlot, tonumber(generation), renewedExpiry, nowMs}
                end
            end
        end

        local selected = 0
        for slot = 1, slotCount do
            local value = redis.call('HGET', KEYS[1], 'slot:' .. slot)
            if not value then
                selected = slot
                break
            end
            local currentOwner = string.match(value, '([^|]*)|')
            if redis.call('EXISTS', ARGV[5] .. currentOwner) == 0 then
                redis.call('HDEL', KEYS[1], 'owner:' .. currentOwner)
                selected = slot
                break
            end
        end

        if selected == 0 then return {'exhausted', nowMs} end

        local generationField = 'generation:' .. selected
        local generation = redis.call('HINCRBY', KEYS[1], generationField, 1)
        local expiresAt = nowMs + tonumber(ARGV[4])
        redis.call('HSET', KEYS[1],
            'slot:' .. selected, ARGV[3] .. '|' .. generation .. '|' .. expiresAt,
            ownerField, selected)
        local leaseValue = redis.call('GET', KEYS[2])
        local nodeRid = leaseValue and string.match(leaseValue, '([^|]*)|') or ''
        redis.call('SET', KEYS[2], nodeRid .. '|' .. nowMs, 'PX', ARGV[4])
        redis.call('SADD', KEYS[3], ARGV[3])
        return {'acquired', selected, generation, expiresAt, nowMs}
        """;

    static final String RELEASE_ROUTING_ID_SLOT = PROLOGUE + """

        local slotField = 'slot:' .. ARGV[1]
        local value = redis.call('HGET', KEYS[1], slotField)
        if not value then return {'stale', nowMs} end
        local currentOwner, generation = string.match(value, '([^|]*)|([^|]*)|')
        if currentOwner ~= ARGV[2] or tonumber(generation) ~= tonumber(ARGV[3]) then
            return {'stale', nowMs}
        end
        redis.call('HDEL', KEYS[1], slotField, 'owner:' .. currentOwner)
        return {'released', nowMs}
        """;

    static final String LIST_ROUTING_ID_SLOTS = PROLOGUE + """

        local config = redis.call('HGET', KEYS[1], 'config')
        if not config then return {'', 0, nowMs, {}} end
        local slotCount = tonumber(redis.call('HGET', KEYS[1], 'slotCount'))
        local allocations = {}
        for slot = 1, slotCount do
            local value = redis.call('HGET', KEYS[1], 'slot:' .. slot)
            if value then
                local owner, generation = string.match(value, '([^|]*)|([^|]*)|')
                local remaining = redis.call('PTTL', ARGV[1] .. owner)
                if remaining >= 0 then
                    allocations[#allocations + 1] = slot
                    allocations[#allocations + 1] = owner
                    allocations[#allocations + 1] = tonumber(generation)
                    allocations[#allocations + 1] = nowMs + remaining
                end
            end
        end
        return {config, slotCount, nowMs, allocations}
        """;
}
