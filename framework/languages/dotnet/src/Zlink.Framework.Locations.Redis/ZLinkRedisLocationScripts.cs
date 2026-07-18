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
///   P:lease:{ownerId}       STRING "nodeRidHex|updatedAtMs" with PX TTL
///   P:leases                SET   owner ids that ever held a lease
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
    /// KEYS[1] row hash, KEYS[2] generation counter, KEYS[3] kind index set.
    /// ARGV[1] intent 'new'|'renew'|'takeover', ARGV[2] caller owner id,
    /// ARGV[3] caller generation (renew guard), ARGV[4] row json,
    /// ARGV[5] row key, ARGV[6] lease key prefix, ARGV[7] owner index key
    /// prefix, ARGV[8] stamp key (kind, mesh scope), ARGV[9] stamp key
    /// (kind, null scope) or '' when the kind has no mesh, ARGV[10] '1' when
    /// the row carries a mesh name, ARGV[11] mesh name.
    /// </summary>
    internal const string Write = Prologue + """

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
            -- A row is alive only while its owner's lease key still exists;
            -- lease expiry is Redis PX TTL, so this check is store-clock based.
            if currentOwner and redis.call('EXISTS', ARGV[6] .. currentOwner) == 1 then
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

    /// <summary>
    /// Bulk remove of one owner's rows across all location kinds in one
    /// atomic script. Generation counters survive.
    ///
    /// KEYS[1..3] owner index sets for mesh, spot, actor.
    /// KEYS[4..6] kind index sets for mesh, spot, actor.
    /// ARGV[1..3] row hash key prefixes for mesh, spot, actor.
    /// ARGV[4..6] stamp key bases for mesh, spot, actor.
    /// </summary>
    internal const string RemoveAllByOwner = """
        if redis.replicate_commands then redis.replicate_commands() end
        local removed = 0
        for i = 1, 3 do
            local ownerIndex = KEYS[i]
            local kindIndex = KEYS[i + 3]
            local rowPrefix = ARGV[i]
            local stampBase = ARGV[i + 3]
            local rowKeys = redis.call('SMEMBERS', ownerIndex)
            for _, rowKey in ipairs(rowKeys) do
                local rowHash = rowPrefix .. rowKey
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

    /// <summary>
    /// Owner lease upsert: SET with PX TTL so expiry is judged by the Redis
    /// clock, plus index bookkeeping for the snapshot list. Returns nowMs.
    ///
    /// KEYS[1] lease key, KEYS[2] lease index set.
    /// ARGV[1] owner id, ARGV[2] node rid hex, ARGV[3] TTL in ms.
    /// </summary>
    internal const string RenewLease = Prologue + """

        redis.call('SET', KEYS[1], ARGV[2] .. '|' .. nowMs, 'PX', ARGV[3])
        redis.call('SADD', KEYS[2], ARGV[1])
        return nowMs
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
        return removed
        """;

    /// <summary>
    /// Single snapshot of every live lease plus the store clock, so callers
    /// can compute LeaseExpiresAt = StoreNow + PTTL without any application
    /// wall clock. Index entries whose lease key already expired are cleaned
    /// lazily. Returns {nowMs, {ownerId, value, pttlMs, ...}}.
    ///
    /// KEYS[1] lease index set. ARGV[1] lease key prefix.
    /// </summary>
    internal const string ListLeases = Prologue + """

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

    /// <summary>
    /// Atomically fixes group metadata, renews an idempotent owner claim, or assigns the lowest
    /// logically expired/free slot. The owner lease is extended in the same Redis operation.
    ///
    /// KEYS[1] group hash, KEYS[2] owner lease key, KEYS[3] lease index.
    /// ARGV[1] canonical member JSON, ARGV[2] slot count, ARGV[3] owner id,
    /// ARGV[4] lease TTL milliseconds, ARGV[5] owner lease prefix.
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

        local ownerField = 'owner:' .. ARGV[3]
        local existingSlot = tonumber(redis.call('HGET', KEYS[1], ownerField))
        if existingSlot then
            local value = redis.call('HGET', KEYS[1], 'slot:' .. existingSlot)
            if value then
                local currentOwner, generation, expiresAt = string.match(value, '([^|]*)|([^|]*)|([^|]*)')
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
            local currentOwner, generation, expiresAt = string.match(value, '([^|]*)|([^|]*)|([^|]*)')
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
    /// ARGV[1] is the owner lease key prefix.
    /// </summary>
    internal const string ListRoutingIdSlots = Prologue + """

        local config = redis.call('HGET', KEYS[1], 'config')
        if not config then return {'', 0, nowMs, {}} end
        local slotCount = tonumber(redis.call('HGET', KEYS[1], 'slotCount'))
        local allocations = {}
        for slot = 1, slotCount do
            local value = redis.call('HGET', KEYS[1], 'slot:' .. slot)
            if value then
                local owner, generation, expiresAt = string.match(value, '([^|]*)|([^|]*)|([^|]*)')
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
