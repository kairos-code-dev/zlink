namespace Zlink.Framework.Locations.Redis;

internal static partial class ZLinkRedisAuthorityScripts
{
    internal const string Unified = """
if redis.replicate_commands then redis.replicate_commands() end
local time = redis.call('TIME')
local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
local op = ARGV[1]
local request = cjson.decode(ARGV[2])
local MAX = '9223372036854775807'

local schemaFormat = redis.call('HGET', KEYS[19], 'format')
local schemaEpoch = redis.call('HGET', KEYS[19], 'epoch')
if schemaFormat then
    if schemaFormat ~= 'location-authority-hybrid-v3'
        or schemaEpoch ~= '3' then
        error('incompatible Redis location authority schema')
    end
else
    redis.call('HSET', KEYS[19],
        'format', 'location-authority-hybrid-v3',
        'epoch', '3')
end

local function mapOf(values)
    local result = {}
    for index = 1, #values, 2 do result[values[index]] = values[index + 1] end
    return result
end

local function copyMap(value)
    local result = {}
    for key, item in pairs(value) do result[key] = item end
    return result
end

local function decCompare(left, right)
    left = tostring(left or '0')
    right = tostring(right or '0')
    if #left ~= #right then return #left < #right and -1 or 1 end
    if left == right then return 0 end
    return left < right and -1 or 1
end

local function decimalToHex(value)
    local decimal = tostring(value)
    local digits = decimal
    local output = ''
    repeat
        local quotient = ''
        local remainder = 0
        for index = 1, #digits do
            local current = remainder * 10 + tonumber(string.sub(digits, index, index))
            local digit = math.floor(current / 16)
            remainder = current % 16
            if #quotient > 0 or digit > 0 then quotient = quotient .. tostring(digit) end
        end
        output = string.sub('0123456789abcdef', remainder + 1, remainder + 1) .. output
        digits = quotient
    until digits == ''
    return string.rep('0', 16 - #output) .. output
end

local function hexDecode(value)
    return (string.gsub(value, '..', function(pair)
        return string.char(tonumber(pair, 16))
    end))
end

local function counterAvailable(field, count)
    local current = redis.call('HGET', KEYS[4], field) or '0'
    for _ = 1, count or 1 do
        if current == MAX then return false end
        local carry = 1
        local nextValue = {}
        for index = #current, 1, -1 do
            local digit = tonumber(string.sub(current, index, index)) + carry
            if digit >= 10 then digit = digit - 10 else carry = 0 end
            table.insert(nextValue, 1, tostring(digit))
        end
        if carry == 1 then table.insert(nextValue, 1, '1') end
        current = table.concat(nextValue)
    end
    return true
end

local function nextCounter(field)
    redis.call('HINCRBY', KEYS[4], field, 1)
    return redis.call('HGET', KEYS[4], field)
end

local function rowAt(key)
    if redis.call('EXISTS', key) == 0 then return nil end
    return mapOf(redis.call('HGETALL', key))
end

local function missing()
    return {kind = 'missing', storeNowMs = nowMs}
end

local function descriptorFromKey(value)
    if not value then return {meshName = '', rid = ''} end
    local firstColon = string.find(value, ':', 1, true)
    if not firstColon then return {meshName = '', rid = ''} end
    local meshLength = tonumber(string.sub(value, 1, firstColon - 1))
    if not meshLength then return {meshName = '', rid = ''} end
    local meshStart = firstColon + 1
    local meshName = string.sub(value, meshStart, meshStart + meshLength - 1)
    local ridLengthStart = meshStart + meshLength
    local secondColon = string.find(value, ':', ridLengthStart, true)
    if not secondColon then return {meshName = '', rid = ''} end
    local ridLength = tonumber(string.sub(value, ridLengthStart, secondColon - 1))
    if not ridLength then return {meshName = '', rid = ''} end
    return {
        meshName = meshName,
        rid = string.sub(value, secondColon + 1, secondColon + ridLength)
    }
end

local function allocation(row)
    if not row or not row.allocationState then return nil end
    return {
        state = row.allocationState,
        objectKind = row.objectKind,
        stableType = row.stableType,
        descriptor = descriptorFromKey(row.descriptorKey),
        descriptorLifecycleGeneration = row.descriptorLifecycleGeneration,
        capacityBundle = row.capacityBundle
    }
end

local function snapshot(row)
    if not row or row.deleted == '1' then return missing() end
    local pendingCreation = nil
    if row.allocationState == 'pending' and row.reservationId then
        pendingCreation = {
            reservationId = row.reservationId,
            requestContentReference = row.requestContentReference,
            requestSha256 = row.requestSha256,
            requestEncodedSize = tonumber(row.requestEncodedSize)
        }
    end
    return {
        kind = 'snapshot',
        storeVersion = row.storeVersion,
        payload = row.payload,
        objectGeneration = row.objectGeneration,
        authorityOwnerGeneration = row.authorityOwnerGeneration,
        ownerId = row.ownerId,
        ownerLeaseGeneration = row.ownerLeaseGeneration,
        allocation = allocation(row),
        pendingCreation = pendingCreation,
        storeNowMs = nowMs
    }
end

local historyFields = {
    'authorityKey', 'payload', 'storeVersion', 'objectGeneration',
    'authorityOwnerGeneration', 'ownerId', 'ownerLeaseGeneration',
    'allocationState', 'objectKind', 'stableType', 'descriptorKey',
    'descriptorLifecycleGeneration', 'capacityBundle', 'reservationId',
    'requestContentReference', 'requestSha256', 'requestEncodedSize'
}

local function archiveCurrent(currentKey, historyKey, revisionIndexKey)
    local row = rowAt(currentKey)
    if not row or not row.storeVersion then return end
    local revision = decimalToHex(row.storeVersion)
    redis.call('HSET', historyKey, revision .. ':deleted', '0')
    for _, field in ipairs(historyFields) do
        if row[field] ~= nil then
            redis.call('HSET', historyKey, revision .. ':' .. field, row[field])
        end
    end
    redis.call('ZADD', revisionIndexKey, 0, revision)
end

local function writeTombstone(
    historyKey, revisionIndexKey, revision, authorityKey)
    redis.call('HSET', historyKey,
        revision .. ':deleted', '1',
        revision .. ':authorityKey', authorityKey)
    redis.call('ZADD', revisionIndexKey, 0, revision)
end

local function historyRow(historyKey, revision)
    local deleted = redis.call('HGET', historyKey, revision .. ':deleted')
    if not deleted then return nil end
    if deleted == '1' then
        return {deleted = '1', authorityKey = redis.call(
            'HGET', historyKey, revision .. ':authorityKey')}
    end
    local row = {}
    for _, field in ipairs(historyFields) do
        row[field] = redis.call('HGET', historyKey, revision .. ':' .. field)
    end
    return row
end

local function deleteHistoryRevision(historyKey, revision)
    redis.call('HDEL', historyKey, revision .. ':deleted')
    for _, field in ipairs(historyFields) do
        redis.call('HDEL', historyKey, revision .. ':' .. field)
    end
end

local function writeRow(key, row)
    redis.call('DEL', key)
    local fields = {}
    for name, value in pairs(row) do
        if value ~= nil then
            table.insert(fields, name)
            table.insert(fields, tostring(value))
        end
    end
    if #fields > 0 then redis.call('HSET', key, unpack(fields)) end
end

local function keyHex(value)
    if request.keyHex then return request.keyHex end
    return (string.gsub(value, '.', function(character)
        return string.format('%02x', string.byte(character))
    end))
end

local function pruneHistory(
    currentKey, historyKey, revisionsKey, authorityKey, authorityKeyHex)
    authorityKeyHex = authorityKeyHex or keyHex(authorityKey)
    local expired = redis.call('ZRANGEBYSCORE', KEYS[17], '-inf', nowMs,
        'LIMIT', 0, 64)
    for _, member in ipairs(expired) do
        redis.call('ZREM', KEYS[17], member)
        redis.call('ZREM', KEYS[18], member)
    end
    local oldest = redis.call('ZRANGE', KEYS[18], 0, 0)[1]
    local current = rowAt(currentKey)
    if not oldest then
        redis.call('DEL', historyKey, revisionsKey)
        if not current then
            redis.call('ZREM', KEYS[5], authorityKeyHex)
            redis.call('HDEL', KEYS[16], authorityKeyHex)
        end
        return
    end
    local watermarkHex = string.sub(oldest, 1, 16)
    local anchor = redis.call('ZREVRANGEBYLEX', revisionsKey,
        '[' .. watermarkHex, '-', 'LIMIT', 0, 1)[1]
    if anchor then
        local obsolete = redis.call('ZRANGEBYLEX', revisionsKey,
            '-', '(' .. anchor, 'LIMIT', 0, 128)
        for _, revision in ipairs(obsolete) do
            redis.call('ZREM', revisionsKey, revision)
            deleteHistoryRevision(historyKey, revision)
        end
    end
end

local function indexCurrent(authorityKey, revision)
    redis.call('ZADD', KEYS[5], 0, keyHex(authorityKey))
    redis.call('HSET', KEYS[16], keyHex(authorityKey), decimalToHex(revision))
    pruneHistory(KEYS[1], KEYS[2], KEYS[3], authorityKey,
        keyHex(authorityKey))
end

local function pruneValueHistory(historyKey, revisionsKey)
    local oldest = redis.call('ZRANGE', KEYS[18], 0, 0)[1]
    if not oldest then
        redis.call('DEL', historyKey, revisionsKey)
        return
    end
    local watermarkHex = string.sub(oldest, 1, 16)
    local anchor = redis.call('ZREVRANGEBYLEX', revisionsKey,
        '[' .. watermarkHex, '-', 'LIMIT', 0, 1)[1]
    if not anchor then return end
    local obsolete = redis.call('ZRANGEBYLEX', revisionsKey,
        '-', '(' .. anchor, 'LIMIT', 0, 128)
    for _, revision in ipairs(obsolete) do
        redis.call('ZREM', revisionsKey, revision)
        redis.call('HDEL', historyKey, revision)
    end
end

local function sameOwner(row, owner)
    return row.ownerId == owner.ownerId
       and row.ownerLeaseGeneration == tostring(owner.leaseGeneration)
end

local function sameDescriptor(row, descriptorKey, lifecycle)
    return row.descriptorKey == descriptorKey
       and row.descriptorLifecycleGeneration == tostring(lifecycle)
end

local function capacityNodeBucket(descriptorKey, lifecycle, objectKind)
    local lifecycleValue = tostring(lifecycle)
    local population = objectKind == 'actor' and 'actor' or 'spot'
    return string.len(descriptorKey) .. ':' .. descriptorKey
        .. string.len(lifecycleValue) .. ':' .. lifecycleValue
        .. string.len(population) .. ':' .. population
end

local function capacityTypeBucket(nodeBucket, objectKind, stableType)
    return nodeBucket
        .. string.len(objectKind) .. ':' .. objectKind
        .. string.len(stableType) .. ':' .. stableType
end

local function decodeCapacityBundle(encoded)
    local offset = 1
    local function segment()
        local colon = string.find(encoded, ':', offset, true)
        if not colon then error('invalid capacity bundle') end
        local length = tonumber(string.sub(encoded, offset, colon - 1))
        if not length or length < 0 then error('invalid capacity bundle') end
        local start = colon + 1
        local finish = start + length - 1
        if finish > #encoded then error('invalid capacity bundle') end
        offset = finish + 1
        return string.sub(encoded, start, finish)
    end
    if segment() ~= 'zlink-capacity-bundle-v2' then
        error('invalid capacity bundle')
    end
    local capacity = {
        actors = tonumber(segment()),
        spots = tonumber(segment()),
        spotType = nil
    }
    local presence = segment()
    if presence == '1' then
        capacity.spotType = {
            objectKind = segment(),
            stableType = segment(),
            count = tonumber(segment())
        }
    elseif presence ~= '0' then error('invalid capacity bundle') end
    if offset ~= #encoded + 1 then error('invalid capacity bundle') end
    return capacity
end

local function capacityDelta(capacity, objectKind)
    return objectKind == 'actor'
        and tonumber(capacity.actors)
        or tonumber(capacity.spots)
end

local function leaseLive(owner, leaseKey)
    local value = redis.call(
        'HMGET', leaseKey, 'ownerId', 'generation', 'expiresAt')
    return value[1] == owner.ownerId
       and value[2] == tostring(owner.leaseGeneration)
       and tonumber(value[3] or '0') > nowMs
end

local function descriptorAdmission(target, descriptorKey, admissionKey, leaseKey)
    descriptorKey = descriptorKey or KEYS[11]
    admissionKey = admissionKey or KEYS[12]
    leaseKey = leaseKey or KEYS[13]
    local public = redis.call('HMGET', descriptorKey, 'owner', 'mesh')
    local metadata = redis.call('HMGET', admissionKey,
        'lifecycleGeneration', 'ownerId', 'ownerLeaseGeneration', 'objectRole',
        'runtimeState', 'capabilities', 'actorLimit', 'spotLimit')
    if not public[1] or public[1] ~= target.owner.ownerId
        or public[2] ~= target.descriptor.meshName
        or not metadata[1]
        or metadata[1] ~= tostring(target.lifecycleGeneration)
        or metadata[2] ~= target.owner.ownerId
        or metadata[3] ~= tostring(target.owner.leaseGeneration)
        or (metadata[4] ~= 'server' and metadata[4] ~= '2')
        or metadata[5] ~= '1'
        or not leaseLive(target.owner, leaseKey) then
        return nil, 'targetUnavailable'
    end
    local capabilities = cjson.decode(metadata[6] or '[]')
    local matched = nil
    for _, capability in ipairs(capabilities) do
        local capabilityKind = capability.objectKind or capability.ObjectKind
        local capabilityKindToken = ({
            ['1'] = 'actor',
            ['2'] = 'user_spot',
            ['3'] = 'instance_spot'
        })[tostring(capabilityKind)] or tostring(capabilityKind)
        local capabilityType = capability.stableType or capability.StableType
        if capabilityKindToken == request.objectKind
            and capabilityType == request.stableType then
            matched = capability
            break
        end
    end
    if not matched then return nil, 'targetUnavailable' end
    local nodeBucket = capacityNodeBucket(
        target.descriptorKey, target.lifecycleGeneration, request.objectKind)
    local typeBucket = nil
    if request.objectKind ~= 'actor' then
        typeBucket = capacityTypeBucket(
            nodeBucket, request.objectKind, request.stableType)
    end
    local delta = capacityDelta(request.capacity, request.objectKind)
    local nodeActive = tonumber(redis.call('HGET', KEYS[7], nodeBucket) or '0')
    local nodePending = tonumber(redis.call('HGET', KEYS[8], nodeBucket) or '0')
    local typeActive = typeBucket
        and tonumber(redis.call('HGET', KEYS[9], typeBucket) or '0') or 0
    local typePending = typeBucket
        and tonumber(redis.call('HGET', KEYS[10], typeBucket) or '0') or 0
    local populationLimit = request.objectKind == 'actor'
        and tonumber(metadata[7] or '0')
        or tonumber(metadata[8] or '0')
    local matchedLimit = request.objectKind == 'actor'
        and 0
        or tonumber(matched.limit or matched.Limit or '0')
    if (populationLimit > 0
            and nodeActive + nodePending + delta > populationLimit)
        or (matchedLimit > 0
            and typeActive + typePending + delta > matchedLimit) then
        return nil, 'placementCapacityExhausted'
    end
    return {node = nodeBucket, type = typeBucket}, nil
end

local function capacityAdd(key, bucket, delta)
    if not bucket or bucket == '' or delta == 0 then return end
    local current = tonumber(redis.call('HGET', key, bucket) or '0')
    local nextValue = current + delta
    if nextValue < 0 or nextValue > 2147483647 then
        error('placement capacity counter out of range')
    end
    if nextValue == 0 then redis.call('HDEL', key, bucket)
    else redis.call('HSET', key, bucket, nextValue) end
end

local function capacitySatisfies(key, bucket, required)
    return not bucket or bucket == '' or required == 0
        or tonumber(redis.call('HGET', key, bucket) or '0') >= required
end

local function rowBuckets(row)
    local node = capacityNodeBucket(
        row.descriptorKey, row.descriptorLifecycleGeneration, row.objectKind)
    local typeBucket = nil
    if row.objectKind ~= 'actor' then
        typeBucket = capacityTypeBucket(node, row.objectKind, row.stableType)
    end
    return node, typeBucket
end

local function sourceMatches(row, value)
    return row and row.allocationState == 'active'
       and row.storeVersion == tostring(value.expectedStoreVersion)
       and row.objectKind == value.objectKind
       and row.stableType == value.stableType
       and sameDescriptor(row, value.sourceDescriptorKey,
            value.sourceNodeLifecycleGeneration)
       and row.capacityBundle == value.capacityBundle
       and sameOwner(row, value.sourceOwner)
end

local function aggregateLockKey(authorityKey)
    return '\0aggregate:' .. authorityKey
end

local function readReservation(key)
    if redis.call('EXISTS', key) == 0 then return nil end
    local value = mapOf(redis.call('HGETALL', key))
    value.request = value.requestJson and cjson.decode(value.requestJson) or nil
    return value
end

if op == 'read' then
    return cjson.encode(snapshot(rowAt(KEYS[1])))
end

if op == 'reserve' then
    if request.checkEntrySpotClaim
        and redis.call('EXISTS', KEYS[20]) == 1 then
        return cjson.encode({kind = 'conflict', current = missing()})
    end
    local current = rowAt(KEYS[1])
    if current then
        local result = snapshot(current)
        if current.objectKind ~= request.objectKind
            or current.stableType ~= request.stableType then
            return cjson.encode({kind = 'typeMismatch', current = result})
        end
        return cjson.encode({kind = current.allocationState == 'active'
            and 'alreadyExists' or 'conflict', current = result})
    end
    local buckets, failure = descriptorAdmission(request.target)
    if failure then return cjson.encode({
        kind = failure,
        current = missing()
    }) end
    if not counterAvailable('storeRevision', 1)
        or not counterAvailable('objectGeneration', 1)
        or not counterAvailable('authorityOwnerGeneration', 1) then
        return cjson.encode({kind = 'generationExhausted'})
    end
    if current then archiveCurrent(KEYS[1], KEYS[2], KEYS[3]) end
    local revision = nextCounter('storeRevision')
    local objectGeneration = nextCounter('objectGeneration')
    local ownerGeneration = nextCounter('authorityOwnerGeneration')
    local row = {
        authorityKey = request.key, storeVersion = revision,
        payload = request.payload, objectGeneration = objectGeneration,
        authorityOwnerGeneration = ownerGeneration,
        ownerId = request.target.owner.ownerId,
        ownerLeaseGeneration = request.target.owner.leaseGeneration,
        allocationState = 'pending', objectKind = request.objectKind,
        stableType = request.stableType,
        descriptorKey = request.target.descriptorKey,
        descriptorLifecycleGeneration = request.target.lifecycleGeneration,
        capacityBundle = request.capacityBundle,
        reservationId = request.reservationId,
        requestContentReference = request.intent.requestContentReference,
        requestSha256 = request.intent.requestSha256,
        requestEncodedSize = request.intent.requestEncodedSize
    }
    writeRow(KEYS[1], row)
    redis.call('HSET', KEYS[14],
        'status', 'reserved', 'authorityKey', request.key,
        'storeVersion', revision, 'requestJson', ARGV[2],
        'nodeBucket', buckets.node, 'typeBucket', buckets.type or '')
    local delta = capacityDelta(request.capacity, request.objectKind)
    capacityAdd(KEYS[8], buckets.node, delta)
    capacityAdd(KEYS[10], buckets.type, delta)
    indexCurrent(request.key, revision)
    local result = snapshot(row)
    return cjson.encode({kind = 'reserved',
        reservationId = request.reservationId, creating = result})
end

if op == 'readCreationTerminal' then
    local terminal = rowAt(KEYS[14])
    if not terminal then
        return cjson.encode({kind = 'missing', storeNowMs = nowMs})
    end
    if tonumber(terminal.expiresAtUnixMs) <= nowMs then
        redis.call('DEL', KEYS[14])
        return cjson.encode({kind = 'missing', storeNowMs = nowMs})
    end
    return cjson.encode({
        kind = 'found',
        storeNowMs = nowMs,
        terminal = terminal
    })
end

if op == 'completeCreation' then
    local terminalKey = KEYS[20]
    local existingTerminal = rowAt(terminalKey)
    if existingTerminal then
        if existingTerminal.reservationId == request.reservationId
            and existingTerminal.state == request.terminal.state
            and existingTerminal.terminalEnvelopeSha256
                == request.terminal.terminalEnvelopeSha256
            and existingTerminal.expiresAtUnixMs
                == request.terminal.expiresAtUnixMs then
            return cjson.encode({
                kind = 'alreadyCompleted',
                storeNowMs = nowMs,
                terminal = existingTerminal
            })
        end
        return cjson.encode({kind = 'stale'})
    end
    if tonumber(request.terminal.expiresAtUnixMs) <= nowMs then
        return cjson.encode({kind = 'invalidExpiry', storeNowMs = nowMs})
    end
    local row = rowAt(KEYS[1])
    local reservation = readReservation(KEYS[14])
    if not row or not reservation or reservation.status ~= 'reserved'
        or row.allocationState ~= 'pending'
        or reservation.request.reservationId ~= request.reservationId
        or row.storeVersion ~= tostring(request.expectedStoreVersion) then
        return cjson.encode({kind = 'stale'})
    end
    if not counterAvailable('storeRevision', 1) then
        return cjson.encode({kind = 'generationExhausted'})
    end

    local completedKind = nil
    if request.terminal.state == 'Created' then
        request.objectKind = row.objectKind
        request.stableType = row.stableType
        local rowCapacity = decodeCapacityBundle(row.capacityBundle)
        request.capacity = {actors = 0, spots = 0}
        local _, failure = descriptorAdmission(request.target)
        if failure then return cjson.encode({kind = 'stale'}) end
        archiveCurrent(KEYS[1], KEYS[2], KEYS[3])
        row.storeVersion = nextCounter('storeRevision')
        row.payload = request.payload
        row.allocationState = 'active'
        row.reservationId = nil
        row.requestContentReference = nil
        row.requestSha256 = nil
        row.requestEncodedSize = nil
        writeRow(KEYS[1], row)
        local delta = capacityDelta(rowCapacity, row.objectKind)
        capacityAdd(KEYS[8], reservation.nodeBucket, -delta)
        capacityAdd(KEYS[10], reservation.typeBucket, -delta)
        capacityAdd(KEYS[7], reservation.nodeBucket, delta)
        capacityAdd(KEYS[9], reservation.typeBucket, delta)
        redis.call('HSET', KEYS[14], 'status', 'committed')
        indexCurrent(request.key, row.storeVersion)
        completedKind = 'created'
    elseif request.terminal.state == 'Rejected'
        or request.terminal.state == 'Failed' then
        archiveCurrent(KEYS[1], KEYS[2], KEYS[3])
        row.storeVersion = nextCounter('storeRevision')
        local revision = decimalToHex(row.storeVersion)
        writeTombstone(KEYS[2], KEYS[3], revision, request.key)
        redis.call('DEL', KEYS[1])
        local delta = capacityDelta(
            decodeCapacityBundle(row.capacityBundle), row.objectKind)
        capacityAdd(KEYS[8], reservation.nodeBucket, -delta)
        capacityAdd(KEYS[10], reservation.typeBucket, -delta)
        redis.call(
            'HSET', KEYS[14], 'status',
            request.terminal.state == 'Rejected' and 'rejected' or 'failed')
        indexCurrent(request.key, row.storeVersion)
        completedKind = request.terminal.state == 'Rejected'
            and 'rejected' or 'failed'
    else
        return cjson.encode({kind = 'stale'})
    end

    redis.call('HSET', terminalKey,
        'state', request.terminal.state,
        'sourceNodeRid', request.terminal.sourceNodeRid,
        'sourceNodeGeneration', request.terminal.sourceNodeGeneration,
        'operationIdHigh', request.terminal.operationIdHigh,
        'operationIdLow', request.terminal.operationIdLow,
        'reservationId', request.reservationId,
        'objectKind', row.objectKind,
        'terminalEnvelope', request.terminal.terminalEnvelope,
        'terminalEnvelopeSha256', request.terminal.terminalEnvelopeSha256,
        'expiresAtUnixMs', request.terminal.expiresAtUnixMs)
    redis.call('PEXPIREAT', terminalKey, request.terminal.expiresAtUnixMs)
    return cjson.encode({
        kind = completedKind,
        storeNowMs = nowMs,
        terminal = rowAt(terminalKey),
        ready = request.terminal.state == 'Created' and snapshot(row) or nil
    })
end

if op == 'commit' then
    local row = rowAt(KEYS[1])
    local reservation = readReservation(KEYS[14])
    if reservation and reservation.status == 'committed'
        and row and row.allocationState == 'active' then
        return cjson.encode({kind = 'alreadyCommitted', ready = snapshot(row)})
    end
    if not row or not reservation or reservation.status ~= 'reserved'
        or row.allocationState ~= 'pending'
        or row.storeVersion ~= tostring(request.expectedStoreVersion) then
        return cjson.encode({kind = 'stale'})
    end
    request.objectKind = row.objectKind
    request.stableType = row.stableType
    local rowCapacity = decodeCapacityBundle(row.capacityBundle)
    request.capacity = {actors = 0, spots = 0}
    local _, failure = descriptorAdmission(request.target)
    if failure then return cjson.encode({kind = 'stale'}) end
    if not counterAvailable('storeRevision', 1) then
        return cjson.encode({kind = 'generationExhausted'})
    end
    archiveCurrent(KEYS[1], KEYS[2], KEYS[3])
    row.storeVersion = nextCounter('storeRevision')
    row.payload = request.payload
    row.allocationState = 'active'
    row.reservationId = nil
    row.requestContentReference = nil
    row.requestSha256 = nil
    row.requestEncodedSize = nil
    writeRow(KEYS[1], row)
    local delta = capacityDelta(rowCapacity, row.objectKind)
    capacityAdd(KEYS[8], reservation.nodeBucket, -delta)
    capacityAdd(KEYS[10], reservation.typeBucket, -delta)
    capacityAdd(KEYS[7], reservation.nodeBucket, delta)
    capacityAdd(KEYS[9], reservation.typeBucket, delta)
    redis.call('HSET', KEYS[14], 'status', 'committed')
    indexCurrent(request.key, row.storeVersion)
    return cjson.encode({kind = 'committed', ready = snapshot(row)})
end

if op == 'abort' then
    local row = rowAt(KEYS[1])
    local reservation = readReservation(KEYS[14])
    if reservation and reservation.status == 'aborted' then
        return cjson.encode({kind = 'alreadyAborted'})
    end
    if not row or not reservation or reservation.status ~= 'reserved'
        or row.allocationState ~= 'pending'
        or reservation.request.reservationId ~= request.reservationId
        or row.storeVersion ~= tostring(request.expectedStoreVersion) then
        return cjson.encode({kind = 'stale'})
    end
    if not counterAvailable('storeRevision', 1) then
        return cjson.encode({kind = 'generationExhausted'})
    end
    archiveCurrent(KEYS[1], KEYS[2], KEYS[3])
    row.storeVersion = nextCounter('storeRevision')
    local revision = decimalToHex(row.storeVersion)
    writeTombstone(KEYS[2], KEYS[3], revision, request.key)
    redis.call('DEL', KEYS[1])
    local delta = capacityDelta(
        decodeCapacityBundle(row.capacityBundle), row.objectKind)
    capacityAdd(KEYS[8], reservation.nodeBucket, -delta)
    capacityAdd(KEYS[10], reservation.typeBucket, -delta)
    redis.call('HSET', KEYS[14], 'status', 'aborted')
    indexCurrent(request.key, row.storeVersion)
    return cjson.encode({kind = 'aborted'})
end

if op == 'reserveRelocation' then
    local existing = readReservation(KEYS[14])
    if existing then
        if existing.requestJson == ARGV[2] then
            return cjson.encode({kind = 'alreadyReserved', fence = request.reservationId})
        end
        return cjson.encode({kind = 'conflict', current = snapshot(rowAt(KEYS[1]))})
    end
    local row = rowAt(KEYS[1])
    if not sourceMatches(row, request)
        or redis.call('HEXISTS', KEYS[6], aggregateLockKey(request.key)) == 1 then
        return cjson.encode({kind = 'conflict', current = snapshot(row)})
    end
    local buckets, failure = descriptorAdmission(request.target)
    if failure then return cjson.encode({kind = failure}) end
    local sourceNode, sourceType = rowBuckets(row)
    redis.call('HSET', KEYS[14],
        'status', 'reserved', 'authorityKey', request.key,
        'requestJson', ARGV[2], 'sourceNodeBucket', sourceNode,
        'sourceTypeBucket', sourceType or '', 'targetNodeBucket', buckets.node,
        'targetTypeBucket', buckets.type or '',
        'capacityBundle', request.capacityBundle)
    local delta = capacityDelta(request.capacity, request.objectKind)
    capacityAdd(KEYS[8], buckets.node, delta)
    capacityAdd(KEYS[10], buckets.type, delta)
    return cjson.encode({kind = 'reserved', fence = request.reservationId})
end

if op == 'abortRelocation' then
    local reservation = readReservation(KEYS[14])
    if not reservation then return cjson.encode({kind = 'stale'}) end
    if reservation.status == 'aborted' then
        return cjson.encode({kind = 'alreadyAborted'})
    end
    if reservation.status == 'committed' then
        return cjson.encode({kind = 'alreadyCommitted'})
    end
    if reservation.status ~= 'reserved' then return cjson.encode({kind = 'stale'}) end
    capacityAdd(KEYS[8], reservation.targetNodeBucket,
        -capacityDelta(reservation.request.capacity,
            reservation.request.objectKind))
    capacityAdd(KEYS[10], reservation.targetTypeBucket,
        -capacityDelta(reservation.request.capacity,
            reservation.request.objectKind))
    redis.call('HSET', KEYS[14], 'status', 'aborted')
    return cjson.encode({kind = 'aborted'})
end

if op == 'cas' then
    local row = rowAt(KEYS[1])
    if not row or row.allocationState ~= 'active'
        or row.storeVersion ~= tostring(request.expectedStoreVersion) then
        return cjson.encode({kind = 'conflict', current = snapshot(row)})
    end
    if redis.call('HEXISTS', KEYS[6], aggregateLockKey(request.key)) == 1 then
        return cjson.encode({kind = 'conflict', current = snapshot(row)})
    end
    if request.mutationKind == 'delete' or request.transition == 'preserve' then
        if not leaseLive({ownerId = row.ownerId,
            leaseGeneration = row.ownerLeaseGeneration}, KEYS[15]) then
            return cjson.encode({kind = 'conflict', current = snapshot(row)})
        end
    end
    local ownerIncrement = request.transition == 'newOwner' and 1 or 0
    if not counterAvailable('storeRevision', 1)
        or (ownerIncrement == 1
            and not counterAvailable('authorityOwnerGeneration', 1)) then
        return cjson.encode({kind = 'generationExhausted'})
    end
    local reservation = nil
    if request.transition == 'newOwner' then
        reservation = readReservation(KEYS[14])
        if not reservation or reservation.status ~= 'reserved'
            or reservation.authorityKey ~= request.key
            or not sameOwner(row, reservation.request.sourceOwner)
            or not sameOwner({ownerId = request.targetOwner.ownerId,
                ownerLeaseGeneration = tostring(request.targetOwner.leaseGeneration)},
                reservation.request.targetOwner) then
            return cjson.encode({kind = 'conflict', current = snapshot(row)})
        end
        request.objectKind = row.objectKind
        request.stableType = row.stableType
        request.capacity = {actors = 0, spots = 0}
        request.target = reservation.request.target
        local _, failure = descriptorAdmission(request.target)
        if failure then return cjson.encode({kind = 'conflict', current = snapshot(row)}) end
    end
    archiveCurrent(KEYS[1], KEYS[2], KEYS[3])
    row.storeVersion = nextCounter('storeRevision')
    if request.mutationKind == 'delete' then
        local nodeBucket, typeBucket = rowBuckets(row)
        local delta = capacityDelta(
            decodeCapacityBundle(row.capacityBundle), row.objectKind)
        capacityAdd(KEYS[7], nodeBucket, -delta)
        capacityAdd(KEYS[9], typeBucket, -delta)
        local revision = decimalToHex(row.storeVersion)
        writeTombstone(KEYS[2], KEYS[3], revision, request.key)
        redis.call('DEL', KEYS[1])
        indexCurrent(request.key, row.storeVersion)
        return cjson.encode({kind = 'deleted',
            storeVersion = row.storeVersion, storeNowMs = nowMs})
    end
    row.payload = request.payload
    if request.transition == 'newOwner' then
        capacityAdd(KEYS[7], reservation.sourceNodeBucket,
            -capacityDelta(reservation.request.capacity,
                reservation.request.objectKind))
        capacityAdd(KEYS[9], reservation.sourceTypeBucket,
            -capacityDelta(reservation.request.capacity,
                reservation.request.objectKind))
        capacityAdd(KEYS[8], reservation.targetNodeBucket,
            -capacityDelta(reservation.request.capacity,
                reservation.request.objectKind))
        capacityAdd(KEYS[10], reservation.targetTypeBucket,
            -capacityDelta(reservation.request.capacity,
                reservation.request.objectKind))
        capacityAdd(KEYS[7], reservation.targetNodeBucket,
            capacityDelta(reservation.request.capacity,
                reservation.request.objectKind))
        capacityAdd(KEYS[9], reservation.targetTypeBucket,
            capacityDelta(reservation.request.capacity,
                reservation.request.objectKind))
        row.authorityOwnerGeneration = nextCounter('authorityOwnerGeneration')
        row.ownerId = request.targetOwner.ownerId
        row.ownerLeaseGeneration = request.targetOwner.leaseGeneration
        row.descriptorKey = reservation.request.targetDescriptorKey
        row.descriptorLifecycleGeneration =
            reservation.request.targetNodeLifecycleGeneration
        redis.call('HSET', KEYS[14], 'status', 'committed')
    end
    writeRow(KEYS[1], row)
    indexCurrent(request.key, row.storeVersion)
    local result = snapshot(row)
    result.kind = 'stored'
    return cjson.encode(result)
end

if op == 'prepareAggregate' then
    local aggregate = readReservation(KEYS[14])
    if aggregate then
        if aggregate.status == 'prepared' and aggregate.requestJson == ARGV[2] then
            return cjson.encode({kind = 'alreadyPrepared', fence = request.fence})
        end
        return cjson.encode({kind = aggregate.status == 'committed'
            and 'stale' or 'conflict'})
    end
    local participantCount = #request.participants
    local aggregateCapacity = request.capacity
    local aggregateSpotType = aggregateCapacity.spotType
    if aggregateSpotType == cjson.null then aggregateSpotType = nil end
    local newOwners = 0
    local actors = 0
    local spots = 0
    local spotKind = nil
    local spotType = nil
    for index, participant in ipairs(request.participants) do
        local row = rowAt(KEYS[19 + index])
        if not row or row.allocationState ~= 'active'
            or row.storeVersion ~= tostring(participant.expectedStoreVersion) then
            return cjson.encode({kind = 'conflict'})
        end
        if redis.call(
                'HEXISTS', KEYS[6], aggregateLockKey(participant.key)) == 1 then
            return cjson.encode({kind = 'conflict'})
        end
        if participant.ownerTransition == 'preserve' then
            local leaseKey = KEYS[19 + participantCount + reservationCount + index]
            if not leaseLive({ownerId = row.ownerId,
                leaseGeneration = row.ownerLeaseGeneration}, leaseKey) then
                return cjson.encode({kind = 'conflict'})
            end
        else
            newOwners = newOwners + 1
            local capacity = decodeCapacityBundle(row.capacityBundle)
            actors = actors + tonumber(capacity.actors)
            spots = spots + tonumber(capacity.spots)
            if capacity.spotType then
                if spotKind and (spotKind ~= capacity.spotType.objectKind
                    or spotType ~= capacity.spotType.stableType) then
                    return cjson.encode({kind = 'conflict'})
                end
                spotKind = capacity.spotType.objectKind
                spotType = capacity.spotType.stableType
            end
            request.objectKind = row.objectKind
            request.stableType = row.stableType
            request.capacity = {actors = 0, spots = 0}
            local targetOffset = 20 + participantCount * 2
            local _, failure = descriptorAdmission(
                request.target,
                KEYS[targetOffset],
                KEYS[targetOffset + 1],
                KEYS[targetOffset + 2])
            if failure and failure ~= 'placementCapacityExhausted' then
                return cjson.encode({kind = 'conflict'})
            end
        end
    end
    if actors ~= tonumber(aggregateCapacity.actors)
        or spots ~= tonumber(aggregateCapacity.spots)
        or ((aggregateSpotType == nil) ~= (spotKind == nil))
        or (aggregateSpotType
            and (aggregateSpotType.objectKind ~= spotKind
                or aggregateSpotType.stableType ~= spotType
                or tonumber(aggregateSpotType.count) ~= spots)) then
        return cjson.encode({kind = 'conflict'})
    end
    local targetOffset = 20 + participantCount * 2
    local actorBucket = nil
    local spotBucket = nil
    local typeBucket = nil
    if actors > 0 then
        request.objectKind = 'actor'
        request.stableType = ''
        request.capacity = {actors = actors, spots = 0}
        local buckets, failure = descriptorAdmission(
            request.target, KEYS[targetOffset],
            KEYS[targetOffset + 1], KEYS[targetOffset + 2])
        if failure == 'targetUnavailable' then
            -- Every Actor stable type was checked above; only population applies here.
            actorBucket = capacityNodeBucket(
                request.target.descriptorKey,
                request.target.lifecycleGeneration, 'actor')
            local active = tonumber(redis.call('HGET', KEYS[7], actorBucket) or '0')
            local pending = tonumber(redis.call('HGET', KEYS[8], actorBucket) or '0')
            local limit = tonumber(redis.call(
                'HGET', KEYS[targetOffset + 1], 'actorLimit') or '0')
            if limit > 0 and active + pending + actors > limit then
                return cjson.encode({kind = 'conflict'})
            end
        elseif failure then return cjson.encode({kind = 'conflict'})
        else actorBucket = buckets.node end
    end
    if spots > 0 then
        request.objectKind = spotKind
        request.stableType = spotType
        request.capacity = {actors = 0, spots = spots}
        local buckets, failure = descriptorAdmission(
            request.target, KEYS[targetOffset],
            KEYS[targetOffset + 1], KEYS[targetOffset + 2])
        if failure then return cjson.encode({kind = 'conflict'}) end
        spotBucket = buckets.node
        typeBucket = buckets.type
    end
    capacityAdd(KEYS[8], actorBucket, actors)
    capacityAdd(KEYS[8], spotBucket, spots)
    capacityAdd(KEYS[10], typeBucket, spots)
    for _, participant in ipairs(request.participants) do
        redis.call('HSET', KEYS[6],
            aggregateLockKey(participant.key), KEYS[14])
    end
    redis.call('HSET', KEYS[14],
        'status', 'prepared', 'requestJson', ARGV[2],
        'participantCount', participantCount,
        'targetActorBucket', actorBucket or '',
        'targetSpotBucket', spotBucket or '',
        'targetTypeBucket', typeBucket or '',
        'capacityBundle', request.capacityBundle)
    return cjson.encode({kind = 'prepared', fence = request.fence})
end

if op == 'commitAggregate' then
    local aggregate = readReservation(KEYS[14])
    if not aggregate then return cjson.encode({kind = 'stale'}) end
    if aggregate.status == 'committed' then
        return cjson.encode({kind = 'alreadyCommitted'})
    end
    if aggregate.status ~= 'prepared' then return cjson.encode({kind = 'stale'}) end
    local prepared = aggregate.request
    local participantCount = #prepared.participants
    local ownerChanges = 0
    for index, participant in ipairs(prepared.participants) do
        local row = rowAt(KEYS[19 + index])
        if not row
            or row.storeVersion ~= tostring(participant.expectedStoreVersion) then
            return cjson.encode({kind = 'stale'})
        end
        if redis.call(
                'HGET', KEYS[6], aggregateLockKey(participant.key)) ~= KEYS[14] then
            return cjson.encode({kind = 'stale'})
        end
        if participant.ownerTransition == 'newOwner' then
            ownerChanges = ownerChanges + 1
            request.objectKind = row.objectKind
            request.stableType = row.stableType
            request.capacity = {actors = 0, spots = 0}
            local descriptorOffset = 20 + participantCount * 6
            local _, failure = descriptorAdmission(
                prepared.target,
                KEYS[descriptorOffset],
                KEYS[descriptorOffset + 1],
                KEYS[descriptorOffset + 2])
            if failure then return cjson.encode({kind = 'stale'}) end
        else
            local preserveLeaseOffset =
                19 + participantCount * 5 + index
            if not leaseLive({ownerId = row.ownerId,
                leaseGeneration = row.ownerLeaseGeneration},
                KEYS[preserveLeaseOffset]) then
                return cjson.encode({kind = 'stale'})
            end
        end
    end
    local preparedCapacity = decodeCapacityBundle(aggregate.capacityBundle)
    if not capacitySatisfies(
            KEYS[8], aggregate.targetActorBucket,
            tonumber(preparedCapacity.actors))
        or not capacitySatisfies(
            KEYS[8], aggregate.targetSpotBucket,
            tonumber(preparedCapacity.spots))
        or not capacitySatisfies(
            KEYS[10], aggregate.targetTypeBucket,
            tonumber(preparedCapacity.spots)) then
        return cjson.encode({kind = 'stale'})
    end
    local sourceNodeRequired = {}
    local sourceTypeRequired = {}
    for index, participant in ipairs(prepared.participants) do
        if participant.ownerTransition == 'newOwner' then
            local row = rowAt(KEYS[19 + index])
            local capacity = decodeCapacityBundle(row.capacityBundle)
            local delta = capacityDelta(capacity, row.objectKind)
            local nodeBucket, typeBucket = rowBuckets(row)
            sourceNodeRequired[nodeBucket] =
                (sourceNodeRequired[nodeBucket] or 0) + delta
            if typeBucket then
                sourceTypeRequired[typeBucket] =
                    (sourceTypeRequired[typeBucket] or 0) + delta
            end
        end
    end
    for bucket, required in pairs(sourceNodeRequired) do
        if not capacitySatisfies(KEYS[7], bucket, required) then
            return cjson.encode({kind = 'stale'})
        end
    end
    for bucket, required in pairs(sourceTypeRequired) do
        if not capacitySatisfies(KEYS[9], bucket, required) then
            return cjson.encode({kind = 'stale'})
        end
    end
    if not counterAvailable('storeRevision', participantCount)
        or not counterAvailable('authorityOwnerGeneration', ownerChanges) then
        return cjson.encode({kind = 'generationExhausted'})
    end
    for index, participant in ipairs(prepared.participants) do
        local currentKey = KEYS[19 + index]
        local historyKey = KEYS[19 + participantCount + index]
        local revisionsKey = KEYS[19 + participantCount * 2 + index]
        local membershipHistoryKey =
            KEYS[19 + participantCount * 3 + index]
        local membershipRevisionsKey =
            KEYS[19 + participantCount * 4 + index]
        local row = rowAt(currentKey)
        archiveCurrent(currentKey, historyKey, revisionsKey)
        local previousMembership =
            redis.call('HGET', KEYS[6], participant.key)
        if previousMembership then
            local previousRevision = decimalToHex(row.storeVersion)
            redis.call('HSET', membershipHistoryKey,
                previousRevision, previousMembership)
            redis.call('ZADD', membershipRevisionsKey,
                0, previousRevision)
        end
        row.storeVersion = nextCounter('storeRevision')
        row.payload = participant.authorityPayload
        if participant.ownerTransition == 'newOwner' then
            local capacity = decodeCapacityBundle(row.capacityBundle)
            local delta = capacityDelta(capacity, row.objectKind)
            local sourceNode, sourceType = rowBuckets(row)
            local targetNode = row.objectKind == 'actor'
                and aggregate.targetActorBucket or aggregate.targetSpotBucket
            local targetType = nil
            if row.objectKind ~= 'actor' then
                targetType = aggregate.targetTypeBucket
            end
            capacityAdd(KEYS[7], sourceNode, -delta)
            capacityAdd(KEYS[9], sourceType, -delta)
            capacityAdd(KEYS[7], targetNode, delta)
            capacityAdd(KEYS[9], targetType, delta)
            row.authorityOwnerGeneration = nextCounter('authorityOwnerGeneration')
            row.ownerId = prepared.targetOwner.ownerId
            row.ownerLeaseGeneration = prepared.targetOwner.leaseGeneration
            row.descriptorKey = prepared.target.descriptorKey
            row.descriptorLifecycleGeneration =
                prepared.target.lifecycleGeneration
        end
        writeRow(currentKey, row)
        redis.call('ZADD', KEYS[5], 0, participant.keyHex)
        redis.call('HSET', KEYS[16], participant.keyHex,
            decimalToHex(row.storeVersion))
        redis.call('HSET', KEYS[6], participant.key,
            participant.membershipMutation)
        redis.call('HDEL', KEYS[6], aggregateLockKey(participant.key))
        pruneHistory(currentKey, historyKey, revisionsKey,
            participant.key, participant.keyHex)
        pruneValueHistory(membershipHistoryKey, membershipRevisionsKey)
    end
    local capacity = decodeCapacityBundle(aggregate.capacityBundle)
    capacityAdd(KEYS[8], aggregate.targetActorBucket,
        -tonumber(capacity.actors))
    capacityAdd(KEYS[8], aggregate.targetSpotBucket,
        -tonumber(capacity.spots))
    capacityAdd(KEYS[10], aggregate.targetTypeBucket,
        -tonumber(capacity.spots))
    redis.call('HSET', KEYS[14], 'status', 'committed')
    return cjson.encode({kind = 'committed'})
end

if op == 'abortAggregate' then
    local aggregate = readReservation(KEYS[14])
    if not aggregate then return cjson.encode({kind = 'stale'}) end
    if aggregate.status == 'aborted' then
        return cjson.encode({kind = 'alreadyAborted'})
    end
    if aggregate.status ~= 'prepared' then return cjson.encode({kind = 'stale'}) end
    local capacity = decodeCapacityBundle(aggregate.capacityBundle)
    for _, participant in ipairs(aggregate.request.participants) do
        if redis.call(
                'HGET', KEYS[6], aggregateLockKey(participant.key)) ~= KEYS[14] then
            return cjson.encode({kind = 'stale'})
        end
    end
    if not capacitySatisfies(
            KEYS[8], aggregate.targetActorBucket, tonumber(capacity.actors))
        or not capacitySatisfies(
            KEYS[8], aggregate.targetSpotBucket, tonumber(capacity.spots))
        or not capacitySatisfies(
            KEYS[10], aggregate.targetTypeBucket, tonumber(capacity.spots)) then
        return cjson.encode({kind = 'stale'})
    end
    capacityAdd(KEYS[8], aggregate.targetActorBucket,
        -tonumber(capacity.actors))
    capacityAdd(KEYS[8], aggregate.targetSpotBucket,
        -tonumber(capacity.spots))
    capacityAdd(KEYS[10], aggregate.targetTypeBucket,
        -tonumber(capacity.spots))
    for _, participant in ipairs(aggregate.request.participants) do
        redis.call('HDEL', KEYS[6], aggregateLockKey(participant.key))
    end
    redis.call('HSET', KEYS[14], 'status', 'aborted')
    return cjson.encode({kind = 'aborted'})
end

if op == 'capacityProjection' then
    return cjson.encode({
        kind = 'capacityProjection',
        nodeActive = mapOf(redis.call('HGETALL', KEYS[7])),
        nodePending = mapOf(redis.call('HGETALL', KEYS[8])),
        typeActive = mapOf(redis.call('HGETALL', KEYS[9])),
        typePending = mapOf(redis.call('HGETALL', KEYS[10]))
    })
end

if op == 'scan' then
    local scan = rowAt(KEYS[20])
    if request.start then
        local watermark = redis.call('HGET', KEYS[4], 'storeRevision') or '0'
        local watermarkMember =
            decimalToHex(watermark) .. ':' .. request.scanId
        redis.call('HSET', KEYS[20],
            'watermark', watermark, 'prefix', request.prefix,
            'watermarkMember', watermarkMember,
            'lastHex', '', 'expiresAtMs', nowMs + request.retentionMs)
        redis.call('PEXPIRE', KEYS[20], request.retentionMs)
        redis.call('ZADD', KEYS[17], nowMs + request.retentionMs, watermarkMember)
        redis.call('ZADD', KEYS[18], 0, watermarkMember)
        return cjson.encode({kind = 'started', watermark = watermark,
            storeNowMs = nowMs})
    elseif not scan or tonumber(scan.expiresAtMs) <= nowMs
        or scan.prefix ~= request.prefix
        or scan.lastHex ~= request.expectedLastHex then
        return cjson.encode({kind = 'scanExpired'})
    end
    local candidates = request.candidates
    local rows = {}
    local lastHex = scan.lastHex
    local pageBytes = 128
    for candidateIndex, candidate in ipairs(candidates) do
        local authorityKey = hexDecode(candidate)
        if string.sub(authorityKey, 1, #request.prefix) == request.prefix then
            local currentKeyIndex =
                request.dynamicStart + ((candidateIndex - 1) * 3)
            local current = rowAt(KEYS[currentKeyIndex])
            local selected = current
            if not current
                or decCompare(current.storeVersion, scan.watermark) > 0 then
                local watermarkHex = decimalToHex(scan.watermark)
                local revisions = redis.call('ZREVRANGEBYLEX',
                    KEYS[currentKeyIndex + 2], '[' .. watermarkHex, '-', 'LIMIT', 0, 1)
                if #revisions == 0 then
                    selected = nil
                else
                    selected = historyRow(
                        KEYS[currentKeyIndex + 1], revisions[1])
                end
            end
            if selected and selected.deleted ~= '1' then
                local entry = {key = authorityKey, row = snapshot(selected)}
                local entryBytes = #cjson.encode(entry) + 1
                if pageBytes + entryBytes > 4194304 then break end
                table.insert(rows, entry)
                pageBytes = pageBytes + entryBytes
                lastHex = candidate
                if #rows >= request.limit then break end
            else
                lastHex = candidate
            end
        else
            lastHex = candidate
        end
    end
    redis.call('HSET', KEYS[20], 'lastHex', lastHex)
    redis.call('PEXPIRE', KEYS[20], request.retentionMs)
    local more = false
    if #candidates > 0 then
        more = redis.call('ZRANGEBYLEX', KEYS[5], '(' .. lastHex, '+',
            'LIMIT', 0, 1)[1] ~= nil
    end
    if not more then
        redis.call('DEL', KEYS[20])
        redis.call('ZREM', KEYS[17], scan.watermarkMember)
        redis.call('ZREM', KEYS[18], scan.watermarkMember)
    end
    return cjson.encode({kind = 'page', rows = rows,
        scanId = request.scanId, lastHex = lastHex,
        hasMore = more, storeNowMs = nowMs})
end

error('unknown authority operation')
""";
}
