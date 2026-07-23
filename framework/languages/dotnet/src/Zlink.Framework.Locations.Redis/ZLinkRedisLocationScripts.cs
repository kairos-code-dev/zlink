namespace Zlink.Framework.Locations.Redis;

/// <summary>
/// Lua sources for every write decision. The draft contract (9절, 11절)
/// requires generation claims, owner-guarded removes, and owner lease
/// refreshes to be atomic per row key, and requires the store clock — never
/// the caller's wall clock — to produce UpdatedAt and lease expiry. Each
/// script therefore reads Redis TIME itself and returns the timestamp it
/// recorded.
///
/// Key layout under the configured prefix P (kind is mesh/spot/actor,
/// rowKey is the canonical length-prefixed key string):
///   P:row:{kind}:{rowKey}   HASH  owner, gen, json, updatedAtMs[, mesh]
///   P:gen:{kind}:{rowKey}   STRING generation counter, never deleted
///   P:keys:{kind}           SET   all row keys of the kind (list index)
///   P:own:{kind}:{ownerId}  SET   row keys owned by one owner (bulk remove)
///   P:{zlink-location-v1}:owner-lease:{digest}
///                            HASH ownerId, generation, expiresAt with PX TTL
///   P:stamp:{kind}[:{mesh}] STRING change stamp counter per scope
///
/// Owner-lease and old-owner index keys are computed inside the scripts from
/// prefixes passed as ARGV because they depend on the row's current owner,
/// which is only known inside the atomic step. This is valid on a standalone
/// Redis (the official extension's supported topology); a cluster deployment
/// would need a hash-tagged KeyPrefix so all keys share one slot.
/// </summary>
internal static class ZLinkRedisLocationScripts
{
    /// <summary>Shared prologue: effect replication for Redis &lt; 7 and the
    /// store-clock timestamp in milliseconds.</summary>
    private const string Prologue = """
        if redis.replicate_commands then redis.replicate_commands() end
        local time = redis.call('TIME')
        local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
        """;

    /// <summary>
    /// One atomic write honoring the write intent, mirroring
    /// ZLinkInMemoryLocationStore.Write. Returns {status, generation, nowMs}
    /// with status 'stored' | 'stale' | 'conflict'.
    ///
    /// KEYS[1] row hash, KEYS[2] generation counter, KEYS[3] kind index set,
    /// KEYS[4] exact current-owner lease HASH.
    /// ARGV[1] intent 'new'|'renew'|'takeover', ARGV[2] caller owner id,
    /// ARGV[3] caller generation (renew guard), ARGV[4] row json,
    /// ARGV[5] row key, ARGV[6] owner index key prefix, ARGV[7] stamp key
    /// (kind, mesh scope), ARGV[8] stamp key (kind, null scope) or '' when
    /// the kind has no mesh, ARGV[9] '1' when the row carries a mesh name,
    /// ARGV[10] mesh name, ARGV[11] expected current owner.
    /// </summary>
    internal const string Write = Prologue + """

        local intent = ARGV[1]
        local owner = ARGV[2]
        local currentOwner = redis.call('HGET', KEYS[1], 'owner')
        local expectedOwner = ARGV[11]
        if (currentOwner or '') ~= expectedOwner then
            return {'retry', 0, nowMs}
        end

        local function bumpStamps()
            redis.call('INCR', ARGV[7])
            if ARGV[8] ~= '' then redis.call('INCR', ARGV[8]) end
        end

        local function storeRow(gen)
            redis.call('HSET', KEYS[1],
                'owner', owner, 'gen', gen, 'json', ARGV[4], 'updatedAtMs', nowMs)
            if ARGV[9] == '1' then
                redis.call('HSET', KEYS[1], 'mesh', ARGV[10])
            end
            redis.call('SADD', KEYS[3], ARGV[5])
            redis.call('SADD', ARGV[6] .. owner, ARGV[5])
            if currentOwner and currentOwner ~= owner then
                redis.call('SREM', ARGV[6] .. currentOwner, ARGV[5])
            end
            bumpStamps()
        end

        if intent == 'new' then
            -- A row is alive only while its owner's lease key still exists;
            -- lease expiry is Redis PX TTL, so this check is store-clock based.
            if currentOwner and redis.call('EXISTS', KEYS[4]) == 1 then
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

        -- renew: only the exact current owner token may update row fields,
        -- and the generation never changes. A caller generation of 0 means
        -- the row kind does not carry the store token (actor rows), so the
        -- guard is the owner id alone.
        if currentOwner and currentOwner == owner
            and (tonumber(ARGV[3]) == 0
                or tonumber(redis.call('HGET', KEYS[1], 'gen')) == tonumber(ARGV[3])) then
            local gen = tonumber(redis.call('HGET', KEYS[1], 'gen'))
            redis.call('HSET', KEYS[1], 'json', ARGV[4], 'updatedAtMs', nowMs)
            bumpStamps()
            return {'stored', gen, nowMs}
        end
        return {'stale', 0, nowMs}
        """;

    internal const string WriteMeshNode = Prologue + """

        local intent = ARGV[1]
        local owner = ARGV[2]
        local leaseGeneration = ARGV[3]
        local incomingJson = ARGV[4]
        local incoming = cjson.decode(incomingJson)
        local currentOwner = redis.call('HGET', KEYS[1], 'owner')
        local currentJson = redis.call('HGET', KEYS[1], 'json')
        if (currentOwner or '') ~= ARGV[20] then
            return {'retry', 0, nowMs}
        end
        local incomingLeaseGeneration =
            redis.call('HGET', KEYS[4], 'generation')
        if not incomingLeaseGeneration
            or incomingLeaseGeneration ~= leaseGeneration then
            return {'stale', 0, nowMs}
        end

        local function value(row, upper, lower)
            return row[upper] ~= nil and row[upper] or row[lower]
        end
        local function deepEqual(left, right)
            if type(left) ~= type(right) then return false end
            if type(left) ~= 'table' then return left == right end
            for key, item in pairs(left) do
                if not deepEqual(item, right[key]) then return false end
            end
            for key, _ in pairs(right) do
                if left[key] == nil then return false end
            end
            return true
        end
        local function sameKeySet(left, right)
            if type(left) ~= 'table' or type(right) ~= 'table' then
                return false
            end
            for key, _ in pairs(left) do
                if right[key] == nil then return false end
            end
            for key, _ in pairs(right) do
                if left[key] == nil then return false end
            end
            return true
        end
        local function sameImmutable(left, right)
            local leftCapacity = value(left, 'Capacity', 'capacity')
            local rightCapacity = value(right, 'Capacity', 'capacity')
            return tostring(value(left, 'MeshName', 'meshName'))
                    == tostring(value(right, 'MeshName', 'meshName'))
                and tostring(value(left, 'Rid', 'rid'))
                    == tostring(value(right, 'Rid', 'rid'))
                and tostring(value(left, 'LifecycleGeneration',
                    'lifecycleGeneration'))
                    == tostring(value(right, 'LifecycleGeneration',
                        'lifecycleGeneration'))
                and tostring(value(left, 'Endpoint', 'endpoint'))
                    == tostring(value(right, 'Endpoint', 'endpoint'))
                and tostring(value(left, 'SecurityIdentity',
                    'securityIdentity'))
                    == tostring(value(right, 'SecurityIdentity',
                        'securityIdentity'))
                and tostring(value(left, 'OwnerId', 'ownerId'))
                    == tostring(value(right, 'OwnerId', 'ownerId'))
                and tostring(value(left, 'LeaseGeneration',
                    'leaseGeneration'))
                    == tostring(value(right, 'LeaseGeneration',
                        'leaseGeneration'))
                and tostring(value(left, 'ApplicationVersion',
                    'applicationVersion'))
                    == tostring(value(right, 'ApplicationVersion',
                        'applicationVersion'))
                and tostring(value(left, 'ObjectRole', 'objectRole'))
                    == tostring(value(right, 'ObjectRole', 'objectRole'))
                and sameKeySet(
                    value(left, 'ChannelWeights', 'channelWeights'),
                    value(right, 'ChannelWeights', 'channelWeights'))
                and deepEqual(
                    value(left, 'ObjectCapabilities', 'objectCapabilities'),
                    value(right, 'ObjectCapabilities', 'objectCapabilities'))
                and tostring(value(leftCapacity, 'ActiveLimit', 'activeLimit'))
                    == tostring(value(
                        rightCapacity, 'ActiveLimit', 'activeLimit'))
                and tostring(value(leftCapacity, 'PendingLimit', 'pendingLimit'))
                    == tostring(value(
                        rightCapacity, 'PendingLimit', 'pendingLimit'))
        end
        local function bumpStamps()
            redis.call('INCR', ARGV[7])
            if ARGV[8] ~= '' then redis.call('INCR', ARGV[8]) end
        end
        local function storeRow(generation)
            redis.call('HSET', KEYS[1],
                'owner', owner,
                'gen', generation,
                'json', incomingJson,
                'updatedAtMs', nowMs,
                'mesh', ARGV[10])
            redis.call('HSET', KEYS[5],
                'owner', owner,
                'gen', generation,
                'json', incomingJson,
                'updatedAtMs', nowMs,
                'mesh', ARGV[10])
            redis.call('HSET', KEYS[6],
                'descriptorKey', ARGV[5],
                'descriptorRevision', ARGV[11],
                'lifecycleGeneration', ARGV[12],
                'ownerId', owner,
                'ownerLeaseGeneration', leaseGeneration,
                'objectRole', ARGV[13],
                'runtimeState', ARGV[14],
                'applicationVersion', ARGV[15],
                'capabilities', ARGV[16],
                'nodeActiveLimit', ARGV[17],
                'nodePendingLimit', ARGV[18],
                'immutableDigest', ARGV[19])
            redis.call('SADD', KEYS[3], ARGV[5])
            redis.call('SADD', KEYS[7], ARGV[5])
            redis.call('SADD', KEYS[8], ARGV[5])
            if KEYS[10] ~= KEYS[8] then
                redis.call('SREM', KEYS[10], ARGV[5])
            end
            redis.call('SADD', ARGV[6] .. owner, ARGV[5])
            if currentOwner and currentOwner ~= owner then
                redis.call('SREM', ARGV[6] .. currentOwner, ARGV[5])
            end
            bumpStamps()
        end

        local currentOwnerLive = currentOwner
            and redis.call('EXISTS', KEYS[9]) == 1
        if intent == 'new' and currentOwnerLive then
            return {'conflict', 0, nowMs}
        end
        if intent == 'takeover' and currentOwnerLive then
            return {'stale', 0, nowMs}
        end
        if intent == 'renew' then
            if not currentJson or currentOwner ~= owner then
                return {'stale', 0, nowMs}
            end
            local current = cjson.decode(currentJson)
            if tostring(value(current, 'LeaseGeneration', 'leaseGeneration'))
                    ~= leaseGeneration
                or tostring(value(current, 'LifecycleGeneration',
                    'lifecycleGeneration'))
                    ~= tostring(value(incoming, 'LifecycleGeneration',
                        'lifecycleGeneration'))
                or not sameImmutable(current, incoming) then
                return {'stale', 0, nowMs}
            end
            local currentRevision = tonumber(
                value(current, 'DescriptorRevision', 'descriptorRevision'))
            local incomingRevision = tonumber(
                value(incoming, 'DescriptorRevision', 'descriptorRevision'))
            if not currentRevision or not incomingRevision then
                return {'stale', 0, nowMs}
            end
            if incomingRevision == currentRevision
                and incomingJson == currentJson then
                return {
                    'stored',
                    tonumber(redis.call('HGET', KEYS[1], 'gen')),
                    nowMs
                }
            end
            if incomingRevision <= currentRevision then
                return {'stale', 0, nowMs}
            end
            local generation =
                tonumber(redis.call('HGET', KEYS[1], 'gen'))
            storeRow(generation)
            return {'stored', generation, nowMs}
        end

        local generation = redis.call('INCR', KEYS[2])
        storeRow(generation)
        return {'stored', generation, nowMs}
        """;

    internal const string WriteClientServer = Prologue + """

        local intent = ARGV[1]
        local owner = ARGV[2]
        local leaseGeneration = ARGV[3]
        local incomingJson = ARGV[4]
        local currentOwner = redis.call('HGET', KEYS[1], 'owner')
        local currentJson = redis.call('HGET', KEYS[1], 'json')
        if (currentOwner or '') ~= ARGV[8] then
            return {'retry', 0, nowMs}
        end
        local incomingLeaseGeneration =
            redis.call('HGET', KEYS[4], 'generation')
        if not incomingLeaseGeneration
            or incomingLeaseGeneration ~= leaseGeneration then
            return {'stale', 0, nowMs}
        end

        local function value(row, upper, lower)
            return row[upper] ~= nil and row[upper] or row[lower]
        end
        local function storeRow(generation)
            redis.call('HSET', KEYS[1],
                'owner', owner,
                'gen', generation,
                'json', incomingJson,
                'updatedAtMs', nowMs,
                'channelIndex', KEYS[6])
            redis.call('SADD', KEYS[3], ARGV[5])
            redis.call('ZADD', KEYS[6], 0, ARGV[5])
            redis.call('SADD', ARGV[6] .. owner, ARGV[5])
            if currentOwner and currentOwner ~= owner then
                redis.call('SREM', ARGV[6] .. currentOwner, ARGV[5])
            end
        end

        local currentOwnerLive = currentOwner
            and redis.call('EXISTS', KEYS[5]) == 1
        if intent == 'new' and currentOwnerLive then
            return {'conflict', 0, nowMs}
        end
        if intent == 'takeover' and currentOwnerLive then
            return {'stale', 0, nowMs}
        end
        if intent == 'renew' then
            if not currentJson or currentOwner ~= owner then
                return {'stale', 0, nowMs}
            end
            local current = cjson.decode(currentJson)
            local incoming = cjson.decode(incomingJson)
            if tostring(value(current, 'LeaseGeneration', 'leaseGeneration'))
                    ~= leaseGeneration
                or tostring(value(current, 'LifecycleGeneration',
                    'lifecycleGeneration'))
                    ~= tostring(value(incoming, 'LifecycleGeneration',
                        'lifecycleGeneration'))
                or tostring(value(current, 'Endpoint', 'endpoint'))
                    ~= tostring(value(incoming, 'Endpoint', 'endpoint'))
                or tostring(value(current, 'SecurityIdentity',
                    'securityIdentity'))
                    ~= tostring(value(incoming, 'SecurityIdentity',
                        'securityIdentity')) then
                return {'stale', 0, nowMs}
            end
            local currentRevision = tonumber(value(
                current, 'DescriptorRevision', 'descriptorRevision'))
            local incomingRevision = tonumber(value(
                incoming, 'DescriptorRevision', 'descriptorRevision'))
            if incomingRevision <= currentRevision then
                return {'stale', 0, nowMs}
            end
            local generation = tonumber(redis.call('HGET', KEYS[1], 'gen'))
            storeRow(generation)
            return {'stored', generation, nowMs}
        end

        local generation = redis.call('INCR', KEYS[2])
        storeRow(generation)
        return {'stored', generation, nowMs}
        """;

    /// <summary>
    /// Owner-guarded remove mirroring ZLinkInMemoryLocationStore.Remove: the
    /// row is deleted only on an exact owner id + generation match, and the
    /// generation counter key is left untouched. Returns
    /// {status, generation, nowMs}.
    ///
    /// KEYS[1] row hash, KEYS[2] kind index set.
    /// ARGV[1] owner id, ARGV[2] generation, ARGV[3] row key,
    /// ARGV[4] owner index key prefix, ARGV[5] stamp key (kind, mesh scope),
    /// ARGV[6] stamp key (kind, null scope) or ''.
    /// </summary>
    internal const string Remove = Prologue + """

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

    internal const string RemoveMeshNode = Prologue + """

        local currentOwner = redis.call('HGET', KEYS[1], 'owner')
        if not currentOwner
            or currentOwner ~= ARGV[1]
            or tonumber(redis.call('HGET', KEYS[1], 'gen'))
                ~= tonumber(ARGV[2]) then
            return {'stale', 0, nowMs}
        end

        redis.call('DEL', KEYS[1], KEYS[3], KEYS[4])
        redis.call('SREM', KEYS[2], ARGV[3])
        redis.call('SREM', KEYS[5], ARGV[3])
        redis.call('SREM', KEYS[6], ARGV[3])
        redis.call('SREM', ARGV[4] .. currentOwner, ARGV[3])
        redis.call('INCR', ARGV[5])
        if ARGV[6] ~= '' then redis.call('INCR', ARGV[6]) end
        return {'stored', tonumber(ARGV[2]), nowMs}
        """;

    internal const string RemoveClientServer = Prologue + """

        local currentOwner = redis.call('HGET', KEYS[1], 'owner')
        local currentJson = redis.call('HGET', KEYS[1], 'json')
        if not currentOwner or not currentJson or currentOwner ~= ARGV[1] then
            return {'stale', 0, nowMs}
        end
        local current = cjson.decode(currentJson)
        local leaseGeneration =
            current.LeaseGeneration or current.leaseGeneration
        if tonumber(leaseGeneration) ~= tonumber(ARGV[2]) then
            return {'stale', 0, nowMs}
        end

        local generation = tonumber(redis.call('HGET', KEYS[1], 'gen'))
        redis.call('DEL', KEYS[1])
        redis.call('SREM', KEYS[2], ARGV[3])
        redis.call('ZREM', KEYS[3], ARGV[3])
        redis.call('SREM', ARGV[4] .. currentOwner, ARGV[3])
        return {'stored', generation, nowMs}
        """;

    /// <summary>
    /// Bulk remove of one owner's rows across all location kinds in one
    /// atomic script. Generation counters survive.
    ///
    /// KEYS[1..4] owner index sets for mesh, spot, actor, ClientServer.
    /// KEYS[5..8] kind index sets in the same order.
    /// KEYS[9] exact owner lease hash.
    /// ARGV[1..4] row hash key prefixes in the same order.
    /// ARGV[5..8] stamp key bases. ClientServer does not use a stamp.
    /// ARGV[9] exact owner lease generation.
    /// </summary>
    internal const string RemoveAllByOwner = """
        if redis.replicate_commands then redis.replicate_commands() end
        local leaseGeneration = redis.call('HGET', KEYS[9], 'generation')
        if not leaseGeneration
            or leaseGeneration ~= ARGV[9] then
            return 0
        end
        local removed = 0
        for i = 1, 4 do
            local ownerIndex = KEYS[i]
            local kindIndex = KEYS[i + 4]
            local rowPrefix = ARGV[i]
            local stampBase = ARGV[i + 4]
            local rowKeys = redis.call('SMEMBERS', ownerIndex)
            for _, rowKey in ipairs(rowKeys) do
                local rowHash = rowPrefix .. rowKey
                local mesh = redis.call('HGET', rowHash, 'mesh')
                local channelIndex =
                    redis.call('HGET', rowHash, 'channelIndex')
                if redis.call('DEL', rowHash) == 1 then
                    removed = removed + 1
                    redis.call('SREM', kindIndex, rowKey)
                    if channelIndex then
                        redis.call('ZREM', channelIndex, rowKey)
                    end
                    if stampBase ~= '' and mesh then
                        redis.call('INCR', stampBase .. ':' .. mesh)
                    end
                    if stampBase ~= '' then redis.call('INCR', stampBase) end
                end
            end
            redis.call('DEL', ownerIndex)
        end
        return removed
        """;

    /// <summary>
    /// Owner lease upsert: SET with PX TTL so expiry is judged by the Redis
    /// clock, plus index bookkeeping for the snapshot list. Returns nowMs.
    ///
    /// KEYS[1] lease key, KEYS[2] lease index set.
    /// ARGV[1] owner id, ARGV[2] node rid hex, ARGV[3] TTL in ms.
    /// </summary>
    internal const string RenewLease = Prologue + """

        local generation = redis.call('HGET', KEYS[1], 'generation')
        if not generation then
            generation = redis.call('HINCRBY', KEYS[3], 'leaseGeneration', 1)
        end
        redis.call('HSET', KEYS[1],
            'ownerId', ARGV[1],
            'generation', generation,
            'expiresAt', nowMs + tonumber(ARGV[3]))
        redis.call('PEXPIRE', KEYS[1], ARGV[3])
        redis.call('SADD', KEYS[2], ARGV[1])
        redis.call('HSET', KEYS[4], ARGV[1], ARGV[2] .. '|' .. nowMs)
        return {nowMs, generation}
        """;

    internal const string ClaimLease = Prologue + """

        if redis.call('EXISTS', KEYS[1]) == 1 then
            return {'conflict', nowMs}
        end
        local current = redis.call('HGET', KEYS[3], 'leaseGeneration')
        if current and current == '9223372036854775807' then
            return {'exhausted', nowMs}
        end
        local generation = redis.call('HINCRBY', KEYS[3], 'leaseGeneration', 1)
        redis.call('HSET', KEYS[1],
            'ownerId', ARGV[1],
            'generation', generation,
            'expiresAt', nowMs + tonumber(ARGV[2]))
        redis.call('PEXPIRE', KEYS[1], ARGV[2])
        redis.call('SADD', KEYS[2], ARGV[1])
        redis.call('HSET', KEYS[4], ARGV[1], '|' .. nowMs)
        return {'claimed', nowMs, generation}
        """;

    internal const string ReadLease = Prologue + """

        local generation = redis.call('HGET', KEYS[1], 'generation')
        local pttl = redis.call('PTTL', KEYS[1])
        if not generation or pttl < 0 then return {'missing', nowMs} end
        return {'found', nowMs, generation, pttl}
        """;

    internal const string RenewExactLease = Prologue + """

        local generation = redis.call('HGET', KEYS[1], 'generation')
        if not generation or generation ~= ARGV[1] then
            return {'stale', nowMs}
        end
        redis.call('HSET', KEYS[1],
            'generation', generation,
            'expiresAt', nowMs + tonumber(ARGV[2]))
        redis.call('PEXPIRE', KEYS[1], ARGV[2])
        return {'renewed', nowMs}
        """;

    internal const string ReleaseExactLease = Prologue + """

        local generation = redis.call('HGET', KEYS[1], 'generation')
        if not generation or generation ~= ARGV[2] then
            return {'stale', nowMs}
        end
        redis.call('DEL', KEYS[1])
        redis.call('SREM', KEYS[2], ARGV[1])
        redis.call('HDEL', KEYS[3], ARGV[1])
        return {'released', nowMs}
        """;

    /// <summary>
    /// Lease removal for the owner's own shutdown path. Returns removed
    /// count, 0 or 1.
    ///
    /// KEYS[1] lease key, KEYS[2] lease index set. ARGV[1] owner id.
    /// </summary>
    internal const string RemoveLease = Prologue + """

        local removed = redis.call('DEL', KEYS[1])
        redis.call('SREM', KEYS[2], ARGV[1])
        redis.call('HDEL', KEYS[3], ARGV[1])
        return removed
        """;

    /// <summary>
    /// Single snapshot of every live lease plus the store clock, so callers
    /// can compute LeaseExpiresAt = StoreNow + PTTL without any application
    /// wall clock. Index entries whose lease key already expired are cleaned
    /// lazily. Returns {nowMs, {ownerId, value, pttlMs, ...}}.
    ///
    /// KEYS[1] lease index set, KEYS[2] compatibility metadata, followed by
    /// one exact canonical lease key for every owner in ARGV.
    /// </summary>
    internal const string ListLeases = Prologue + """

        local out = {}
        for index, ownerId in ipairs(ARGV) do
            local leaseKey = KEYS[index + 2]
            local pttl = redis.call('PTTL', leaseKey)
            if pttl < 0 then
                redis.call('SREM', KEYS[1], ownerId)
                redis.call('HDEL', KEYS[2], ownerId)
            else
                local generation = redis.call('HGET', leaseKey, 'generation')
                local metadata = redis.call('HGET', KEYS[2], ownerId) or '|0'
                out[#out + 1] = ownerId
                out[#out + 1] = generation .. '|' .. metadata
                out[#out + 1] = pttl
            end
        end
        return {nowMs, out}
        """;

    /// <summary>
    /// Atomically fixes group metadata, renews an idempotent owner claim, or assigns the lowest
    /// logically expired/free slot. The owner lease is extended in the same Redis operation.
    ///
    /// KEYS[1] group hash, KEYS[2] owner lease key, KEYS[3] lease index,
    /// KEYS[4] provider counters.
    /// ARGV[1] canonical member JSON, ARGV[2] slot count, ARGV[3] owner id,
    /// ARGV[4] lease TTL milliseconds. Remaining arguments pair with the
    /// explicit canonical lease keys after KEYS[4].
    /// </summary>
    internal const string AcquireRoutingIdSlot = Prologue + """

        local config = redis.call('HGET', KEYS[1], 'config')
        local slotCount = tonumber(ARGV[2])
        if not config then
            redis.call('HSET', KEYS[1],
                'config', ARGV[1], 'slotCount', slotCount, 'identityMode', 'allocated')
        elseif config ~= ARGV[1] or tonumber(redis.call('HGET', KEYS[1], 'slotCount')) ~= slotCount then
            return {'mismatch', config, redis.call('HGET', KEYS[1], 'slotCount'), nowMs}
        end
        local function leaseIsLive(owner, fallbackExpiry)
            for index = 6, #ARGV do
                if ARGV[index] == owner then
                    return redis.call('EXISTS', KEYS[index - 1]) == 1
                end
            end
            return tonumber(fallbackExpiry) > nowMs
        end

        local ownerField = 'owner:' .. ARGV[3]
        if redis.call('EXISTS', KEYS[2]) == 0 then
            local currentLeaseGeneration = redis.call('HGET', KEYS[4], 'leaseGeneration')
            if currentLeaseGeneration == '9223372036854775807' then
                return {'lease-exhausted', nowMs}
            end
            local leaseGeneration = redis.call('HINCRBY', KEYS[4], 'leaseGeneration', 1)
            redis.call('HSET', KEYS[2],
                'ownerId', ARGV[3],
                'generation', leaseGeneration,
                'expiresAt', nowMs + tonumber(ARGV[4]))
            redis.call('PEXPIRE', KEYS[2], ARGV[4])
            redis.call('SADD', KEYS[3], ARGV[3])
        end
        local existingSlot = tonumber(redis.call('HGET', KEYS[1], ownerField))
        if existingSlot then
            local value = redis.call('HGET', KEYS[1], 'slot:' .. existingSlot)
            if value then
                local currentOwner, generation, expiresAt = string.match(value, '([^|]*)|([^|]*)|([^|]*)')
                if currentOwner == ARGV[3] and redis.call('EXISTS', KEYS[2]) == 1 then
                    local renewedExpiry = nowMs + tonumber(ARGV[4])
                    redis.call('HSET', KEYS[1], 'slot:' .. existingSlot,
                        currentOwner .. '|' .. generation .. '|' .. renewedExpiry)
                    redis.call('PEXPIRE', KEYS[2], ARGV[4])
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
            local currentOwner, generation, expiresAt = string.match(value, '([^|]*)|([^|]*)|([^|]*)')
            if not leaseIsLive(currentOwner, expiresAt) then
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
        redis.call('HSET', KEYS[2], 'expiresAt', expiresAt)
        redis.call('PEXPIRE', KEYS[2], ARGV[4])
        redis.call('SADD', KEYS[3], ARGV[3])
        return {'acquired', selected, generation, expiresAt, nowMs}
        """;

    /// <summary>Releases only an exact owner id and slot generation.</summary>
    internal const string ReleaseRoutingIdSlot = Prologue + """

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

    /// <summary>
    /// Returns group metadata and all logically live slot values at one store time.
    /// ARGV contains the owner id paired with each exact lease key after
    /// KEYS[1].
    /// </summary>
    internal const string ListRoutingIdSlots = Prologue + """

        local config = redis.call('HGET', KEYS[1], 'config')
        if not config then return {'', 0, nowMs, {}} end
        local slotCount = tonumber(redis.call('HGET', KEYS[1], 'slotCount'))
        local allocations = {}
        local function leaseExpiry(owner)
            for index, indexedOwner in ipairs(ARGV) do
                if indexedOwner == owner then
                    local remaining = redis.call('PTTL', KEYS[index + 1])
                    if remaining >= 0 then return nowMs + remaining end
                    return nil
                end
            end
            return nil
        end
        for slot = 1, slotCount do
            local value = redis.call('HGET', KEYS[1], 'slot:' .. slot)
            if value then
                local owner, generation, expiresAt = string.match(value, '([^|]*)|([^|]*)|([^|]*)')
                local liveExpiry = leaseExpiry(owner)
                if liveExpiry then
                    allocations[#allocations + 1] = slot
                    allocations[#allocations + 1] = owner
                    allocations[#allocations + 1] = tonumber(generation)
                    allocations[#allocations + 1] = liveExpiry
                end
            end
        end
        return {config, slotCount, nowMs, allocations}
        """;
}
