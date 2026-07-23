const PROLOGUE = `
if redis.replicate_commands then redis.replicate_commands() end
local time = redis.call('TIME')
local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
`;

export const WRITE_SCRIPT = PROLOGUE + `
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

if currentOwner and currentOwner == owner
    and (tonumber(ARGV[3]) == 0
        or tonumber(redis.call('HGET', KEYS[1], 'gen')) == tonumber(ARGV[3])) then
    local gen = tonumber(redis.call('HGET', KEYS[1], 'gen'))
    redis.call('HSET', KEYS[1], 'json', ARGV[4], 'updatedAtMs', nowMs)
    bumpStamps()
    return {'stored', gen, nowMs}
end
return {'stale', 0, nowMs}
`;

export const REMOVE_SCRIPT = PROLOGUE + `
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
`;

export const REMOVE_ALL_BY_OWNER_SCRIPT = PROLOGUE + `
local lease = redis.call(
    'HMGET', KEYS[11], 'ownerId', 'generation', 'expiresAt')
if lease[1] ~= ARGV[12] or lease[2] ~= ARGV[11]
    or tonumber(lease[3] or '0') <= nowMs then
    return -1
end
local removed = 0
for i = 1, 5 do
    local ownerIndex = KEYS[i]
    local kindIndex = KEYS[i + 5]
    local rowPrefix = ARGV[i]
    local stampBase = ARGV[i + 5]
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
`;

export const CLAIM_LEASE_SCRIPT = PROLOGUE + `
if redis.call('EXISTS', KEYS[1]) == 1 then return {'conflict', nowMs} end
local current = redis.call('HGET', KEYS[2], 'leaseGeneration')
if current == '9223372036854775807' then return {'exhausted', nowMs} end
local generation = redis.call('HINCRBY', KEYS[2], 'leaseGeneration', 1)
local expiresAtMs = nowMs + tonumber(ARGV[2])
redis.call('HSET', KEYS[1],
    'ownerId', ARGV[1],
    'generation', generation,
    'expiresAt', expiresAtMs)
redis.call('PEXPIRE', KEYS[1], ARGV[2])
return {'claimed', generation, expiresAtMs, nowMs}
`;

export const READ_LEASE_SCRIPT = PROLOGUE + `
local values = redis.call('HMGET', KEYS[1], 'ownerId', 'generation', 'expiresAt')
if values[1] ~= ARGV[1] or not values[2]
    or tonumber(values[3] or '0') <= nowMs then
    return {'missing', nowMs}
end
return {'found', values[2], values[3], nowMs}
`;

export const RENEW_LEASE_SCRIPT = PROLOGUE + `
local values = redis.call('HMGET', KEYS[1], 'ownerId', 'generation', 'expiresAt')
local currentGeneration = values[2]
if values[1] ~= ARGV[1] or not currentGeneration
    or currentGeneration ~= ARGV[2]
    or tonumber(values[3] or '0') <= nowMs then
    return {'stale', nowMs}
end
local expiresAtMs = nowMs + tonumber(ARGV[3])
redis.call('HSET', KEYS[1], 'expiresAt', expiresAtMs)
redis.call('PEXPIRE', KEYS[1], ARGV[3])
return {'renewed', expiresAtMs, nowMs}
`;

export const RELEASE_LEASE_SCRIPT = PROLOGUE + `
local values = redis.call('HMGET', KEYS[1], 'ownerId', 'generation')
if values[1] ~= ARGV[1] or values[2] ~= ARGV[2] then return {'stale'} end
redis.call('DEL', KEYS[1])
return {'released'}
`;

export const MESH_DESCRIPTOR_WRITE_SCRIPT = PROLOGUE + `
local intent = ARGV[1]
local owner = ARGV[2]
local leaseGeneration = ARGV[3]
local lifecycle = ARGV[4]
local revision = tonumber(ARGV[5])
local immutable = ARGV[6]
local json = ARGV[7]
local currentOwner = redis.call('HGET', KEYS[1], 'owner')
local lease = redis.call('HMGET', KEYS[4], 'ownerId', 'generation', 'expiresAt')
local liveGeneration = lease[2]
if not liveGeneration or liveGeneration ~= leaseGeneration then
    return {'conflict', 0, nowMs}
end
if lease[1] ~= owner or tonumber(lease[3] or '0') <= nowMs then
    return {'conflict', 0, nowMs}
end
local function store(gen)
    redis.call('HSET', KEYS[1],
        'owner', owner, 'gen', gen, 'json', json,
        'updatedAtMs', nowMs, 'mesh', ARGV[13])
    redis.call('HSET', KEYS[2],
        'descriptorKey', ARGV[14],
        'lifecycleGeneration', lifecycle,
        'descriptorRevision', revision,
        'immutableDigest', immutable,
        'ownerId', owner,
        'ownerLeaseGeneration', leaseGeneration,
        'objectRole', ARGV[8],
        'runtimeState', ARGV[9],
        'applicationVersion', ARGV[15],
        'capabilities', ARGV[10],
        'nodeActiveLimit', ARGV[11],
        'nodePendingLimit', ARGV[12])
    redis.call('SADD', KEYS[3], ARGV[14])
    redis.call('SADD', KEYS[6], ARGV[14])
end
if not currentOwner then
    if intent ~= 'new' and intent ~= 'takeover' then
        return {'stale', 0, nowMs}
    end
    local current = redis.call('HGET', KEYS[5], 'descriptorGeneration') or '0'
    if current == '9223372036854775807' then return {'exhausted', 0, nowMs} end
    redis.call('HINCRBY', KEYS[5], 'descriptorGeneration', 1)
    local gen = redis.call('HGET', KEYS[5], 'descriptorGeneration')
    store(gen)
    return {'stored', gen, nowMs}
end
local storedLeaseGeneration = redis.call('HGET', KEYS[2], 'ownerLeaseGeneration')
local storedOwner = redis.call('HGET', KEYS[2], 'ownerId')
if (storedOwner ~= owner or storedLeaseGeneration ~= leaseGeneration)
    and (intent == 'new' or intent == 'takeover') then
    local current = redis.call('HGET', KEYS[5], 'descriptorGeneration') or '0'
    if current == '9223372036854775807' then return {'exhausted', 0, nowMs} end
    redis.call('HINCRBY', KEYS[5], 'descriptorGeneration', 1)
    local gen = redis.call('HGET', KEYS[5], 'descriptorGeneration')
    store(gen)
    return {'stored', gen, nowMs}
end
local storedRevision = tonumber(redis.call('HGET', KEYS[2], 'descriptorRevision'))
if redis.call('HGET', KEYS[2], 'lifecycleGeneration') == lifecycle
    and redis.call('HGET', KEYS[2], 'immutableDigest') == immutable
    and currentOwner == owner
    and storedLeaseGeneration == leaseGeneration
    and revision == storedRevision
    and redis.call('HGET', KEYS[1], 'json') == json then
    return {'stored', tonumber(redis.call('HGET', KEYS[1], 'gen')),
        tonumber(redis.call('HGET', KEYS[1], 'updatedAtMs'))}
end
if redis.call('HGET', KEYS[2], 'lifecycleGeneration') ~= lifecycle
    or redis.call('HGET', KEYS[2], 'immutableDigest') ~= immutable
    or currentOwner ~= owner
    or storedLeaseGeneration ~= leaseGeneration
    or revision <= storedRevision then
    return {'stale', tonumber(redis.call('HGET', KEYS[1], 'gen')), nowMs}
end
local gen = tonumber(redis.call('HGET', KEYS[1], 'gen'))
store(gen)
return {'stored', gen, nowMs}
`;

export const MESH_DESCRIPTOR_REMOVE_SCRIPT = PROLOGUE + `
local owner = redis.call('HGET', KEYS[2], 'ownerId')
local leaseGeneration = redis.call('HGET', KEYS[2], 'ownerLeaseGeneration')
if not owner or owner ~= ARGV[1] or leaseGeneration ~= ARGV[2] then
    return {'stale', 0, nowMs}
end
local generation = tonumber(redis.call('HGET', KEYS[1], 'gen'))
redis.call('DEL', KEYS[1], KEYS[2])
redis.call('SREM', KEYS[3], ARGV[3])
redis.call('SREM', KEYS[4], ARGV[3])
return {'stored', generation, nowMs}
`;

const TRANSFER_PROLOGUE = PROLOGUE + `
local function readRecord()
    return {
        redis.call('HGET', KEYS[1], 'state'),
        redis.call('HGET', KEYS[1], 'source'),
        redis.call('HGET', KEYS[1], 'target'),
        redis.call('HGET', KEYS[1], 'expectedActorGeneration'),
        redis.call('HGET', KEYS[1], 'expectedMembershipEpoch'),
        redis.call('HGET', KEYS[1], 'participants'),
        redis.call('HGET', KEYS[1], 'recoveryOwnerId'),
        redis.call('HGET', KEYS[1], 'recoveryLeaseExpiresAtMs'),
        redis.call('HGET', KEYS[1], 'updatedAtMs')
    }
end
`;

export const PREPARE_ACTOR_TRANSFER_SCRIPT = TRANSFER_PROLOGUE + `
local active = redis.call('GET', KEYS[2])
if active then
    if active == ARGV[1] and redis.call('HGET', KEYS[1], 'state') == 'Prepared' then
        return {'stored', readRecord()}
    end
    return {'conflict'}
end
local expiresAt = nowMs + tonumber(ARGV[8])
redis.call('HSET', KEYS[1],
    'state', 'Prepared',
    'source', ARGV[2],
    'target', ARGV[3],
    'expectedActorGeneration', ARGV[4],
    'expectedMembershipEpoch', ARGV[5],
    'participants', ARGV[6],
    'recoveryOwnerId', ARGV[7],
    'recoveryLeaseExpiresAtMs', expiresAt,
    'updatedAtMs', nowMs)
redis.call('SET', KEYS[2], ARGV[1])
return {'stored', readRecord()}
`;

export const COMMIT_ACTOR_TRANSFER_SCRIPT = TRANSFER_PROLOGUE + `
if redis.call('EXISTS', KEYS[1]) == 0 or redis.call('GET', KEYS[2]) ~= ARGV[1] then
    return {'notfound'}
end
if redis.call('HGET', KEYS[1], 'state') ~= 'Prepared' then return {'invalid'} end
if redis.call('HGET', KEYS[1], 'recoveryOwnerId') ~= ARGV[2] then return {'conflict'} end
redis.call('HSET', KEYS[1], 'state', 'Committed', 'updatedAtMs', nowMs)
return {'stored', readRecord()}
`;

export const ACTIVATE_ACTOR_TRANSFER_SCRIPT = TRANSFER_PROLOGUE + `
if redis.call('EXISTS', KEYS[1]) == 0 then return {'notfound'} end
if redis.call('HGET', KEYS[1], 'state') ~= 'Committed' then return {'invalid'} end
if redis.call('HGET', KEYS[1], 'recoveryOwnerId') ~= ARGV[2] then return {'conflict'} end
redis.call('HSET', KEYS[1], 'state', 'Activated', 'updatedAtMs', nowMs)
if redis.call('GET', KEYS[2]) == ARGV[1] then redis.call('DEL', KEYS[2]) end
return {'stored', readRecord()}
`;

export const ABORT_ACTOR_TRANSFER_SCRIPT = TRANSFER_PROLOGUE + `
if redis.call('EXISTS', KEYS[1]) == 0 then return {'notfound'} end
if redis.call('HGET', KEYS[1], 'state') ~= 'Prepared' then return {'invalid'} end
if redis.call('HGET', KEYS[1], 'recoveryOwnerId') ~= ARGV[2] then return {'conflict'} end
redis.call('HSET', KEYS[1], 'state', 'Aborted', 'updatedAtMs', nowMs)
if redis.call('GET', KEYS[2]) == ARGV[1] then redis.call('DEL', KEYS[2]) end
return {'stored', readRecord()}
`;

export const TAKE_OVER_ACTOR_TRANSFER_SCRIPT = TRANSFER_PROLOGUE + `
if redis.call('EXISTS', KEYS[1]) == 0 then return {'notfound'} end
local state = redis.call('HGET', KEYS[1], 'state')
if state ~= 'Prepared' and state ~= 'Committed' then return {'invalid'} end
if tonumber(redis.call('HGET', KEYS[1], 'recoveryLeaseExpiresAtMs')) > nowMs then
    return {'conflict'}
end
local expiresAt = nowMs + tonumber(ARGV[3])
redis.call('HSET', KEYS[1],
    'recoveryOwnerId', ARGV[2],
    'recoveryLeaseExpiresAtMs', expiresAt,
    'updatedAtMs', nowMs)
return {'stored', readRecord()}
`;

export const ACQUIRE_ROUTING_ID_SLOT_SCRIPT = PROLOGUE + `
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
        local currentOwner, generation = string.match(value, '([^|]*)|([^|]*)|')
        local lease = redis.call(
            'HMGET', KEYS[2], 'ownerId', 'generation', 'expiresAt')
        if currentOwner == ARGV[3]
            and lease[1] == currentOwner
            and lease[2] == generation
            and tonumber(lease[3] or '0') > nowMs then
            local renewedExpiry = nowMs + tonumber(ARGV[4])
            redis.call('HSET', KEYS[1], 'slot:' .. existingSlot,
                currentOwner .. '|' .. generation .. '|' .. renewedExpiry)
            redis.call('HSET', KEYS[2], 'expiresAt', renewedExpiry)
            redis.call('PEXPIRE', KEYS[2], ARGV[4])
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
    if redis.call('EXISTS', KEYS[2 + slot]) == 0 then
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
redis.call('HSET', KEYS[2],
    'ownerId', ARGV[3],
    'generation', generation,
    'expiresAt', expiresAt)
redis.call('PEXPIRE', KEYS[2], ARGV[4])
return {'acquired', selected, generation, expiresAt, nowMs}
`;

export const RELEASE_ROUTING_ID_SLOT_SCRIPT = PROLOGUE + `
local slotField = 'slot:' .. ARGV[1]
local value = redis.call('HGET', KEYS[1], slotField)
if not value then return {'stale', nowMs} end
local currentOwner, generation = string.match(value, '([^|]*)|([^|]*)|')
if currentOwner ~= ARGV[2] or tonumber(generation) ~= tonumber(ARGV[3]) then
    return {'stale', nowMs}
end
redis.call('HDEL', KEYS[1], slotField, 'owner:' .. currentOwner)
return {'released', nowMs}
`;

export const LIST_ROUTING_ID_SLOTS_SCRIPT = PROLOGUE + `
local config = redis.call('HGET', KEYS[1], 'config')
if not config then return {'', 0, nowMs, {}} end
local slotCount = tonumber(redis.call('HGET', KEYS[1], 'slotCount'))
local allocations = {}
for slot = 1, slotCount do
    local value = redis.call('HGET', KEYS[1], 'slot:' .. slot)
    if value then
        local owner, generation = string.match(value, '([^|]*)|([^|]*)|')
        local remaining = redis.call('PTTL', KEYS[1 + slot])
        if remaining >= 0 then
            allocations[#allocations + 1] = slot
            allocations[#allocations + 1] = owner
            allocations[#allocations + 1] = tonumber(generation)
            allocations[#allocations + 1] = nowMs + remaining
        end
    end
end
return {config, slotCount, nowMs, allocations}
`;
