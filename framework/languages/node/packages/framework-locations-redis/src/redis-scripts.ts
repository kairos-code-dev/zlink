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
    and tonumber(redis.call('HGET', KEYS[1], 'gen')) == tonumber(ARGV[3]) then
    local gen = tonumber(ARGV[3])
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
for i = 1, 4 do
    local ownerIndex = KEYS[i]
    local kindIndex = KEYS[i + 4]
    local rowPrefix = ARGV[i]
    local stampBase = ARGV[i + 4]
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
