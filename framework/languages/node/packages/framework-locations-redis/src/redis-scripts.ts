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

export const REMOVE_ALL_BY_OWNER_SCRIPT = `
if redis.replicate_commands then redis.replicate_commands() end
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

export const RENEW_LEASE_SCRIPT = PROLOGUE + `
redis.call('SET', KEYS[1], ARGV[2] .. '|' .. nowMs, 'PX', ARGV[3])
redis.call('SADD', KEYS[2], ARGV[1])
return nowMs
`;

export const REMOVE_LEASE_SCRIPT = PROLOGUE + `
local removed = redis.call('DEL', KEYS[1])
redis.call('SREM', KEYS[2], ARGV[1])
return removed
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

export const LIST_LEASES_SCRIPT = PROLOGUE + `
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
`;
