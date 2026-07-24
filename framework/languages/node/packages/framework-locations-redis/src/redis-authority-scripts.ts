/*
 * Every key touched by this script is supplied through KEYS. The public row is
 * stored as scalar HASH fields; JSON is limited to bounded reservation and
 * aggregate records and immutable history snapshots.
 */
export const AUTHORITY_HYBRID_SCRIPT = `
if redis.replicate_commands then redis.replicate_commands() end
local time = redis.call('TIME')
local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
local op = ARGV[1]
local request = cjson.decode(ARGV[2])
local MAX = '9223372036854775807'

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

local function capacityBundle(value)
    local offset = 1
    local function segment()
        local colon = string.find(value, ':', offset, true)
        if not colon then error('invalid capacity bundle') end
        local size = tonumber(string.sub(value, offset, colon - 1))
        if not size then error('invalid capacity bundle') end
        local start = colon + 1
        local result = string.sub(value, start, start + size - 1)
        if #result ~= size then error('invalid capacity bundle') end
        offset = start + size
        return result
    end
    if segment() ~= 'zlink-capacity-bundle-v2' then
        error('invalid capacity bundle domain')
    end
    local result = {
        actors = tonumber(segment()),
        spots = tonumber(segment())
    }
    local presence = segment()
    if presence == '1' then
        result.spotType = {
            objectKind = segment(),
            stableType = segment(),
            count = tonumber(segment())
        }
    elseif presence ~= '0' then error('invalid capacity bundle presence') end
    if offset ~= #value + 1 then error('invalid capacity bundle tail') end
    return result
end

local function allocation(row)
    if not row or not row.allocationState then return nil end
    return {
        state = row.allocationState,
        objectKind = row.objectKind,
        stableType = row.stableType,
        descriptor = descriptorFromKey(row.descriptorKey),
        descriptorLifecycleGeneration = row.descriptorLifecycleGeneration,
        capacity = capacityBundle(row.capacityBundle)
    }
end

local function snapshot(row)
    if not row or row.deleted == '1' then return missing() end
    local result = {
        kind = 'snapshot',
        storeVersion = row.storeVersion,
        payload = row.payload,
        objectGeneration = row.objectGeneration,
        authorityOwnerGeneration = row.authorityOwnerGeneration,
        ownerId = row.ownerId,
        ownerLeaseGeneration = row.ownerLeaseGeneration,
        allocation = allocation(row),
        storeNowMs = nowMs
    }
    if row.allocationState == 'reserved' then
        result.pendingCreation = {
            reservationId = row.pendingCreationReservationId,
            requestContentReference = row.pendingCreationReference,
            requestSha256 = row.pendingCreationSha256,
            requestEncodedSize = row.pendingCreationEncodedSize
        }
    end
    return result
end

local historyFields = {
    'authorityKey', 'payload', 'storeVersion', 'objectGeneration',
    'authorityOwnerGeneration', 'ownerId', 'ownerLeaseGeneration',
    'allocationState', 'objectKind', 'stableType', 'descriptorKey',
    'descriptorLifecycleGeneration', 'capacityBundle',
    'pendingCreationReservationId', 'pendingCreationReference',
    'pendingCreationSha256', 'pendingCreationEncodedSize'
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
    return request.keyHex or value
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

local function capacityNodeBucket(descriptorKey, lifecycle, kind)
    local lifecycleValue = tostring(lifecycle)
    return string.len(descriptorKey) .. ':' .. descriptorKey
        .. string.len(lifecycleValue) .. ':' .. lifecycleValue
        .. string.len(kind) .. ':' .. kind
end

local function capacityTypeBucket(nodeBucket, objectKind, stableType)
    return nodeBucket
        .. string.len(objectKind) .. ':' .. objectKind
        .. string.len(stableType) .. ':' .. stableType
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
        'runtimeState', 'capabilities', 'actorLimit',
        'spotLimit')
    if not public[1] or public[1] ~= target.owner.ownerId
        or public[2] ~= target.descriptor.meshName
        or not metadata[1]
        or metadata[1] ~= tostring(target.lifecycleGeneration)
        or metadata[2] ~= target.owner.ownerId
        or metadata[3] ~= tostring(target.owner.leaseGeneration)
        or metadata[4] ~= 'server'
        or metadata[5] ~= '1'
        or not leaseLive(target.owner, leaseKey) then
        return nil, 'targetUnavailable'
    end
    local capabilities = cjson.decode(metadata[6] or '[]')
    local matched = nil
    for _, capability in ipairs(capabilities) do
        if capability.objectKind == request.objectKind
            and capability.stableType == request.stableType then
            matched = capability
            break
        end
    end
    if not matched then return nil, 'targetUnavailable' end
    local actorBucket = capacityNodeBucket(
        target.descriptorKey, target.lifecycleGeneration, 'actor')
    local spotBucket = capacityNodeBucket(
        target.descriptorKey, target.lifecycleGeneration, 'spot')
    local vector = request.capacity
    local typeBucket = vector.spotType and capacityTypeBucket(
        spotBucket, vector.spotType.objectKind, vector.spotType.stableType) or ''
    local actorActive = tonumber(redis.call('HGET', KEYS[7], actorBucket) or '0')
    local actorReserved = tonumber(redis.call('HGET', KEYS[8], actorBucket) or '0')
    local spotActive = tonumber(redis.call('HGET', KEYS[20], spotBucket) or '0')
    local spotReserved = tonumber(redis.call('HGET', KEYS[21], spotBucket) or '0')
    local typeActive = tonumber(redis.call('HGET', KEYS[9], typeBucket) or '0')
    local typeReserved = tonumber(redis.call('HGET', KEYS[10], typeBucket) or '0')
    local typeLimit = vector.spotType and tonumber(matched.limit or '0') or 0
    if actorActive + actorReserved + tonumber(vector.actors) > tonumber(metadata[7])
        or spotActive + spotReserved + tonumber(vector.spots) > tonumber(metadata[8])
        or (vector.spotType and typeLimit > 0
            and typeActive + typeReserved + tonumber(vector.spotType.count) > typeLimit) then
        return nil, 'placementCapacityExhausted'
    end
    return {
        actor = actorBucket,
        spot = spotBucket,
        type = typeBucket
    }, nil
end

local function capacityAdd(key, bucket, delta)
    if delta == 0 then return end
    local current = tonumber(redis.call('HGET', key, bucket) or '0')
    local nextValue = current + delta
    if nextValue < 0 or nextValue > 2147483647 then
        error('placement capacity counter out of range')
    end
    if nextValue == 0 then redis.call('HDEL', key, bucket)
    else redis.call('HSET', key, bucket, nextValue) end
end

local function capacityVectorAdd(
    actorKey, spotKey, typeKey, buckets, vector, multiplier)
    capacityAdd(actorKey, buckets.actor, tonumber(vector.actors) * multiplier)
    capacityAdd(spotKey, buckets.spot, tonumber(vector.spots) * multiplier)
    if vector.spotType then
        capacityAdd(typeKey, buckets.type,
            tonumber(vector.spotType.count) * multiplier)
    end
end

local function addVector(total, value)
    total.actors = tonumber(total.actors) + tonumber(value.actors)
    total.spots = tonumber(total.spots) + tonumber(value.spots)
    if value.spotType then
        if total.spotType
            and (total.spotType.objectKind ~= value.spotType.objectKind
                or total.spotType.stableType ~= value.spotType.stableType) then
            return false
        end
        if not total.spotType then
            total.spotType = {
                objectKind = value.spotType.objectKind,
                stableType = value.spotType.stableType,
                count = 0
            }
        end
        total.spotType.count =
            tonumber(total.spotType.count) + tonumber(value.spotType.count)
    end
    return true
end

local function sameVector(left, right)
    return tonumber(left.actors) == tonumber(right.actors)
       and tonumber(left.spots) == tonumber(right.spots)
       and ((not left.spotType and not right.spotType)
        or (left.spotType and right.spotType
            and left.spotType.objectKind == right.spotType.objectKind
            and left.spotType.stableType == right.spotType.stableType
            and tonumber(left.spotType.count) == tonumber(right.spotType.count)))
end

local function rowBuckets(row)
    local actor = capacityNodeBucket(
        row.descriptorKey, row.descriptorLifecycleGeneration, 'actor')
    local spot = capacityNodeBucket(
        row.descriptorKey, row.descriptorLifecycleGeneration, 'spot')
    local vector = capacityBundle(row.capacityBundle)
    local typeBucket = vector.spotType and capacityTypeBucket(
        spot, vector.spotType.objectKind, vector.spotType.stableType) or ''
    return actor, spot, typeBucket
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

local function readReservation(key)
    if redis.call('EXISTS', key) == 0 then return nil end
    return mapOf(redis.call('HGETALL', key))
end

local function terminalValid(reservation, terminal)
    if redis.call('EXISTS', KEYS[19]) ~= 0
        or not reservation
        or reservation.objectKind ~= 'actor'
        or not terminal or not terminal.operation
        or type(terminal.operation.sourceNodeRid) ~= 'string'
        or #terminal.operation.sourceNodeRid < 1
        or #terminal.operation.sourceNodeRid > 255
        or type(terminal.operation.sourceNodeGeneration) ~= 'string'
        or type(terminal.operation.operationIdHigh) ~= 'string'
        or type(terminal.operation.operationIdLow) ~= 'string'
        or type(terminal.terminalEnvelope) ~= 'string'
        or #terminal.terminalEnvelope > 2097152
        or (#terminal.terminalEnvelope % 2) ~= 0
        or not string.match(terminal.terminalEnvelope, '^[0-9a-f]*$')
        or type(terminal.terminalEnvelopeSha256) ~= 'string'
        or #terminal.terminalEnvelopeSha256 ~= 64
        or not string.match(terminal.terminalEnvelopeSha256, '^[0-9a-f]+$')
        or not tonumber(terminal.expiresAtUnixMs)
        or tonumber(terminal.expiresAtUnixMs) <= nowMs then
        return false
    end
    return true
end

local function publishCreationTerminal(state, reservation, terminal)
    local operation = terminal.operation
    redis.call('HSET', KEYS[19],
        'state', state,
        'sourceNodeRid', operation.sourceNodeRid,
        'sourceNodeGeneration', operation.sourceNodeGeneration,
        'operationIdHigh', operation.operationIdHigh,
        'operationIdLow', operation.operationIdLow,
        'reservationId', reservation.reservationId,
        'objectKind', reservation.objectKind,
        'terminalEnvelope', terminal.terminalEnvelope,
        'terminalEnvelopeSha256', terminal.terminalEnvelopeSha256,
        'expiresAtUnixMs', terminal.expiresAtUnixMs)
    redis.call('PEXPIREAT', KEYS[19], terminal.expiresAtUnixMs)
end

local function creationTerminalResult(state, reservation, terminal)
    return {
        state = state,
        sourceNodeRid = terminal.operation.sourceNodeRid,
        sourceNodeGeneration = terminal.operation.sourceNodeGeneration,
        operationIdHigh = terminal.operation.operationIdHigh,
        operationIdLow = terminal.operation.operationIdLow,
        reservationId = reservation.reservationId,
        objectKind = reservation.objectKind,
        terminalEnvelope = terminal.terminalEnvelope,
        terminalEnvelopeSha256 = terminal.terminalEnvelopeSha256,
        expiresAtUnixMs = terminal.expiresAtUnixMs,
        storeNowMs = nowMs
    }
end

if op == 'read' then
    return cjson.encode(snapshot(rowAt(KEYS[1])))
end

if op == 'readCreationTerminal' then
    local terminal = rowAt(KEYS[19])
    if not terminal then
        return cjson.encode({kind = 'missing', storeNowMs = nowMs})
    end
    if not tonumber(terminal.expiresAtUnixMs)
        or tonumber(terminal.expiresAtUnixMs) <= nowMs then
        redis.call('DEL', KEYS[19])
        return cjson.encode({kind = 'missing', storeNowMs = nowMs})
    end
    terminal.kind = 'terminal'
    terminal.storeNowMs = nowMs
    return cjson.encode(terminal)
end

if op == 'reserve' then
    if redis.call('EXISTS', KEYS[19]) == 1 then
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
    if failure then return cjson.encode({kind = failure == 'placementCapacityExhausted'
        and failure or 'conflict', current = missing()}) end
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
        allocationState = 'reserved', objectKind = request.objectKind,
        stableType = request.stableType,
        descriptorKey = request.target.descriptorKey,
        descriptorLifecycleGeneration = request.target.lifecycleGeneration,
        capacityBundle = request.capacityBundle,
        pendingCreationReservationId = request.reservationId,
        pendingCreationReference = request.intent.requestContentReference,
        pendingCreationSha256 = request.intent.requestSha256,
        pendingCreationEncodedSize = request.intent.requestEncodedSize
    }
    writeRow(KEYS[1], row)
    redis.call('HSET', KEYS[14],
        'state', 'Reserved',
        'reservationId', request.reservationId,
        'authorityKey', request.key,
        'storeVersion', revision,
        'objectGeneration', objectGeneration,
        'authorityOwnerGeneration', ownerGeneration,
        'reservationVersion', revision,
        'objectKind', request.objectKind,
        'stableType', request.stableType,
        'targetDescriptorKey', request.target.descriptorKey,
        'targetDescriptorLifecycleGeneration', request.target.lifecycleGeneration,
        'targetOwnerId', request.target.owner.ownerId,
        'targetOwnerLeaseGeneration', request.target.owner.leaseGeneration,
        'creationReference', request.intent.requestContentReference,
        'creationSha256', request.intent.requestSha256,
        'creationEncodedSize', request.intent.requestEncodedSize,
        'capacityBundle', request.capacityBundle)
    capacityVectorAdd(
        KEYS[8], KEYS[21], KEYS[10], buckets, request.capacity, 1)
    indexCurrent(request.key, revision)
    local result = snapshot(row)
    return cjson.encode({kind = 'reserved',
        reservationId = request.reservationId, creating = result})
end

if op == 'commit' then
    local row = rowAt(KEYS[1])
    local reservation = readReservation(KEYS[14])
    if reservation and reservation.state == 'Committed'
        and row and row.allocationState == 'active' then
        return cjson.encode({kind = 'alreadyCommitted', ready = snapshot(row)})
    end
    if not row or not reservation or reservation.state ~= 'Reserved'
        or row.allocationState ~= 'reserved'
        or reservation.reservationId ~= request.reservationId
        or row.storeVersion ~= tostring(request.expectedStoreVersion) then
        return cjson.encode({kind = 'stale'})
    end
    if reservation.objectKind == 'actor' then
        return cjson.encode({kind = 'stale'})
    end
    request.objectKind = row.objectKind
    request.stableType = row.stableType
    request.capacity = capacityBundle(row.capacityBundle)
    request.capacityBundle = row.capacityBundle
    local _, failure = descriptorAdmission(request.target)
    if failure then return cjson.encode({kind = 'stale'}) end
    if not counterAvailable('storeRevision', 1) then
        return cjson.encode({kind = 'generationExhausted'})
    end
    archiveCurrent(KEYS[1], KEYS[2], KEYS[3])
    row.storeVersion = nextCounter('storeRevision')
    row.payload = request.payload
    row.allocationState = 'active'
    row.pendingCreationReservationId = nil
    row.pendingCreationReference = nil
    row.pendingCreationSha256 = nil
    row.pendingCreationEncodedSize = nil
    writeRow(KEYS[1], row)
    local actorBucket, spotBucket, typeBucket = rowBuckets(row)
    local storedBuckets = {
        actor = actorBucket, spot = spotBucket, type = typeBucket}
    capacityVectorAdd(
        KEYS[8], KEYS[21], KEYS[10], storedBuckets, request.capacity, -1)
    capacityVectorAdd(
        KEYS[7], KEYS[20], KEYS[9], storedBuckets, request.capacity, 1)
    redis.call('HSET', KEYS[14], 'state', 'Committed')
    indexCurrent(request.key, row.storeVersion)
    return cjson.encode({kind = 'committed', ready = snapshot(row)})
end

if op == 'completeCreation' then
    local row = rowAt(KEYS[1])
    local reservation = readReservation(KEYS[14])
    local completion = request.completion
    local terminal = completion and completion.terminal
    local existing = rowAt(KEYS[19])
    if existing then
        existing.storeNowMs = nowMs
        return cjson.encode({kind = 'alreadyCompleted', terminal = existing})
    end
    if not row or not reservation or reservation.state ~= 'Reserved'
        or row.allocationState ~= 'reserved'
        or reservation.reservationId ~= request.reservationId
        or row.storeVersion ~= tostring(request.expectedStoreVersion)
        or not completion
        or (completion.kind ~= 'created'
            and completion.kind ~= 'rejected'
            and completion.kind ~= 'failed')
        or not terminalValid(reservation, terminal) then
        return cjson.encode({kind = 'stale'})
    end
    request.objectKind = row.objectKind
    request.stableType = row.stableType
    request.capacity = capacityBundle(row.capacityBundle)
    request.capacityBundle = row.capacityBundle
    local _, failure = descriptorAdmission(request.target)
    if failure then return cjson.encode({kind = 'stale'}) end
    if not counterAvailable('storeRevision', 1) then
        return cjson.encode({kind = 'generationExhausted'})
    end
    archiveCurrent(KEYS[1], KEYS[2], KEYS[3])
    row.storeVersion = nextCounter('storeRevision')
    local actorBucket, spotBucket, typeBucket = rowBuckets(row)
    local storedBuckets = {
        actor = actorBucket, spot = spotBucket, type = typeBucket}
    capacityVectorAdd(
        KEYS[8], KEYS[21], KEYS[10], storedBuckets, request.capacity, -1)
    local state = completion.kind == 'created' and 'Created'
        or completion.kind == 'rejected' and 'Rejected' or 'Failed'
    local result = creationTerminalResult(state, reservation, terminal)
    if completion.kind == 'created' then
        row.payload = completion.readyPayload
        row.allocationState = 'active'
        row.pendingCreationReservationId = nil
        row.pendingCreationReference = nil
        row.pendingCreationSha256 = nil
        row.pendingCreationEncodedSize = nil
        writeRow(KEYS[1], row)
        capacityVectorAdd(
            KEYS[7], KEYS[20], KEYS[9], storedBuckets, request.capacity, 1)
        redis.call('HSET', KEYS[14], 'state', 'Committed')
        publishCreationTerminal(state, reservation, terminal)
        indexCurrent(request.key, row.storeVersion)
        return cjson.encode({
            kind = 'created', ready = snapshot(row), terminal = result})
    end
    local revision = decimalToHex(row.storeVersion)
    writeTombstone(KEYS[2], KEYS[3], revision, request.key)
    redis.call('DEL', KEYS[1])
    redis.call('HSET', KEYS[14], 'state',
        completion.kind == 'rejected' and 'Rejected' or 'Aborted')
    publishCreationTerminal(state, reservation, terminal)
    indexCurrent(request.key, row.storeVersion)
    return cjson.encode({kind = completion.kind, terminal = result})
end

if op == 'abort' then
    local row = rowAt(KEYS[1])
    local reservation = readReservation(KEYS[14])
    if reservation and reservation.state == 'Aborted' then
        return cjson.encode({kind = 'alreadyAborted'})
    end
    if not row or not reservation or reservation.state ~= 'Reserved'
        or row.allocationState ~= 'reserved'
        or reservation.reservationId ~= request.reservationId
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
    local actorBucket, spotBucket, typeBucket = rowBuckets(row)
    capacityVectorAdd(
        KEYS[8], KEYS[21], KEYS[10],
        {actor = actorBucket, spot = spotBucket, type = typeBucket},
        capacityBundle(row.capacityBundle), -1)
    redis.call('HSET', KEYS[14], 'state', 'Aborted')
    indexCurrent(request.key, row.storeVersion)
    return cjson.encode({kind = 'aborted'})
end

if op == 'reserveRelocation' then
    local existing = readReservation(KEYS[14])
    if existing then
        if existing.state == 'Reserved'
            and existing.reservationId == request.reservationId
            and existing.authorityKey == request.key
            and existing.expectedStoreVersion == tostring(request.expectedStoreVersion)
            and existing.objectKind == request.objectKind
            and existing.stableType == request.stableType
            and existing.sourceDescriptorKey == request.sourceDescriptorKey
            and existing.sourceDescriptorLifecycleGeneration
                == tostring(request.sourceNodeLifecycleGeneration)
            and existing.sourceOwnerId == request.sourceOwner.ownerId
            and existing.sourceOwnerLeaseGeneration
                == tostring(request.sourceOwner.leaseGeneration)
            and existing.targetDescriptorKey == request.targetDescriptorKey
            and existing.targetDescriptorLifecycleGeneration
                == tostring(request.targetNodeLifecycleGeneration)
            and existing.targetOwnerId == request.targetOwner.ownerId
            and existing.targetOwnerLeaseGeneration
                == tostring(request.targetOwner.leaseGeneration)
            and existing.capacityBundle == request.capacityBundle then
            return cjson.encode({kind = 'alreadyReserved', fence = request.reservationId})
        end
        return cjson.encode({kind = 'conflict', current = snapshot(rowAt(KEYS[1]))})
    end
    local row = rowAt(KEYS[1])
    if not sourceMatches(row, request) then
        return cjson.encode({kind = 'conflict', current = snapshot(row)})
    end
    local buckets, failure = descriptorAdmission(request.target)
    if failure then return cjson.encode({kind = failure}) end
    local sourceActor, sourceSpot, sourceType = rowBuckets(row)
    redis.call('HSET', KEYS[14],
        'state', 'Reserved',
        'reservationId', request.reservationId,
        'authorityKey', request.key,
        'expectedStoreVersion', request.expectedStoreVersion,
        'objectKind', request.objectKind,
        'stableType', request.stableType,
        'sourceDescriptorKey', request.sourceDescriptorKey,
        'sourceDescriptorLifecycleGeneration',
            request.sourceNodeLifecycleGeneration,
        'sourceOwnerId', request.sourceOwner.ownerId,
        'sourceOwnerLeaseGeneration', request.sourceOwner.leaseGeneration,
        'targetDescriptorKey', request.targetDescriptorKey,
        'targetDescriptorLifecycleGeneration',
            request.targetNodeLifecycleGeneration,
        'targetOwnerId', request.targetOwner.ownerId,
        'targetOwnerLeaseGeneration', request.targetOwner.leaseGeneration,
        'capacityBundle', request.capacityBundle)
    capacityVectorAdd(
        KEYS[8], KEYS[21], KEYS[10], buckets, request.capacity, 1)
    return cjson.encode({kind = 'reserved', fence = request.reservationId})
end

if op == 'abortRelocation' then
    local reservation = readReservation(KEYS[14])
    if not reservation then return cjson.encode({kind = 'stale'}) end
    if reservation.state == 'Aborted' then
        return cjson.encode({kind = 'alreadyAborted'})
    end
    if reservation.state == 'Committed' then
        return cjson.encode({kind = 'alreadyCommitted'})
    end
    if reservation.state ~= 'Reserved' then return cjson.encode({kind = 'stale'}) end
    local vector = capacityBundle(reservation.capacityBundle)
    local actorBucket = capacityNodeBucket(
        reservation.targetDescriptorKey,
        reservation.targetDescriptorLifecycleGeneration, 'actor')
    local spotBucket = capacityNodeBucket(
        reservation.targetDescriptorKey,
        reservation.targetDescriptorLifecycleGeneration, 'spot')
    local typeBucket = vector.spotType and capacityTypeBucket(
        spotBucket, vector.spotType.objectKind, vector.spotType.stableType) or ''
    capacityVectorAdd(
        KEYS[8], KEYS[21], KEYS[10],
        {actor = actorBucket, spot = spotBucket, type = typeBucket},
        vector, -1)
    redis.call('HSET', KEYS[14], 'state', 'Aborted')
    return cjson.encode({kind = 'aborted'})
end

if op == 'cas' then
    local row = rowAt(KEYS[1])
    if not row or row.allocationState ~= 'active'
        or row.storeVersion ~= tostring(request.expectedStoreVersion) then
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
        if not reservation or reservation.state ~= 'Reserved'
            or reservation.authorityKey ~= request.key
            or row.ownerId ~= reservation.sourceOwnerId
            or row.ownerLeaseGeneration ~= reservation.sourceOwnerLeaseGeneration
            or not sameOwner({ownerId = request.targetOwner.ownerId,
                ownerLeaseGeneration = tostring(request.targetOwner.leaseGeneration)},
                {ownerId = reservation.targetOwnerId,
                 leaseGeneration = reservation.targetOwnerLeaseGeneration}) then
            return cjson.encode({kind = 'conflict', current = snapshot(row)})
        end
        request.objectKind = row.objectKind
        request.stableType = row.stableType
        request.capacity = capacityBundle(row.capacityBundle)
        request.capacityBundle = row.capacityBundle
        request.target = {
            descriptor = descriptorFromKey(reservation.targetDescriptorKey),
            descriptorKey = reservation.targetDescriptorKey,
            lifecycleGeneration =
                reservation.targetDescriptorLifecycleGeneration,
            owner = {
                ownerId = reservation.targetOwnerId,
                leaseGeneration = reservation.targetOwnerLeaseGeneration
            }
        }
        local _, failure = descriptorAdmission(request.target)
        if failure then return cjson.encode({kind = 'conflict', current = snapshot(row)}) end
    end
    archiveCurrent(KEYS[1], KEYS[2], KEYS[3])
    row.storeVersion = nextCounter('storeRevision')
    if request.mutationKind == 'delete' then
        local actorBucket, spotBucket, typeBucket = rowBuckets(row)
        capacityVectorAdd(
            KEYS[7], KEYS[20], KEYS[9],
            {actor = actorBucket, spot = spotBucket, type = typeBucket},
            capacityBundle(row.capacityBundle), -1)
        local revision = decimalToHex(row.storeVersion)
        writeTombstone(KEYS[2], KEYS[3], revision, request.key)
        redis.call('DEL', KEYS[1])
        indexCurrent(request.key, row.storeVersion)
        return cjson.encode({kind = 'deleted',
            storeVersion = row.storeVersion, storeNowMs = nowMs})
    end
    row.payload = request.payload
    if request.transition == 'newOwner' then
        local vector = capacityBundle(reservation.capacityBundle)
        local sourceActor, sourceSpot, sourceType = rowBuckets(row)
        local targetActor = capacityNodeBucket(
            reservation.targetDescriptorKey,
            reservation.targetDescriptorLifecycleGeneration, 'actor')
        local targetSpot = capacityNodeBucket(
            reservation.targetDescriptorKey,
            reservation.targetDescriptorLifecycleGeneration, 'spot')
        local targetType = vector.spotType and capacityTypeBucket(
            targetSpot, vector.spotType.objectKind,
            vector.spotType.stableType) or ''
        local sourceBuckets = {
            actor = sourceActor, spot = sourceSpot, type = sourceType}
        local targetBuckets = {
            actor = targetActor, spot = targetSpot, type = targetType}
        capacityVectorAdd(
            KEYS[7], KEYS[20], KEYS[9], sourceBuckets, vector, -1)
        capacityVectorAdd(
            KEYS[8], KEYS[21], KEYS[10], targetBuckets, vector, -1)
        capacityVectorAdd(
            KEYS[7], KEYS[20], KEYS[9], targetBuckets, vector, 1)
        row.authorityOwnerGeneration = nextCounter('authorityOwnerGeneration')
        row.ownerId = request.targetOwner.ownerId
        row.ownerLeaseGeneration = request.targetOwner.leaseGeneration
        row.descriptorKey = reservation.targetDescriptorKey
        row.descriptorLifecycleGeneration =
            reservation.targetDescriptorLifecycleGeneration
        redis.call('HSET', KEYS[14], 'state', 'Committed')
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
        if aggregate.state == 'Reserved'
            and aggregate.aggregateId == request.aggregateId
            and aggregate.aggregateGeneration == tostring(request.aggregateGeneration)
            and aggregate.participants == request.participantsEncoded
            and aggregate.inventoryDigest == request.inventoryDigest
            and aggregate.targetDescriptorKey == request.targetDescriptorKey
            and aggregate.targetDescriptorLifecycleGeneration
                == tostring(request.targetDescriptorLifecycleGeneration)
            and aggregate.targetOwnerId == request.targetOwner.ownerId
            and aggregate.targetOwnerLeaseGeneration
                == tostring(request.targetOwner.leaseGeneration)
            and aggregate.capacityBundle == request.capacityBundle then
            return cjson.encode({kind = 'alreadyPrepared', fence = request.fence})
        end
        return cjson.encode({kind = aggregate.state == 'Committed'
            and 'stale' or 'conflict'})
    end
    local participantCount = #request.participants
    local sourceTotal = {actors = 0, spots = 0}
    local firstNewOwner = nil
    for index, participant in ipairs(request.participants) do
        local row = rowAt(KEYS[21 + index])
        if not row or row.allocationState ~= 'active'
            or row.storeVersion ~= tostring(participant.expectedStoreVersion) then
            return cjson.encode({kind = 'conflict'})
        end
        if participant.ownerTransition == 'preserve' then
            local leaseKey = KEYS[21 + participantCount + index]
            if not leaseLive({ownerId = row.ownerId,
                leaseGeneration = row.ownerLeaseGeneration}, leaseKey) then
                return cjson.encode({kind = 'conflict'})
            end
        elseif participant.ownerTransition == 'newOwner' then
            local vector = capacityBundle(row.capacityBundle)
            if not addVector(sourceTotal, vector) then
                return cjson.encode({kind = 'conflict'})
            end
            if not firstNewOwner then firstNewOwner = row end
            local originalCapacity = request.capacity
            request.objectKind = row.objectKind
            request.stableType = row.stableType
            request.capacity = {actors = 0, spots = 0}
            local _, failure = descriptorAdmission(request.target)
            request.capacity = originalCapacity
            if failure then return cjson.encode({kind = 'conflict'}) end
        else
            return cjson.encode({kind = 'conflict'})
        end
    end
    if not firstNewOwner or not sameVector(sourceTotal, request.capacity) then
        return cjson.encode({kind = 'conflict'})
    end
    request.objectKind = request.capacity.spotType
        and request.capacity.spotType.objectKind or firstNewOwner.objectKind
    request.stableType = request.capacity.spotType
        and request.capacity.spotType.stableType or firstNewOwner.stableType
    local targetBuckets, failure = descriptorAdmission(request.target)
    if failure then
        return cjson.encode({kind = failure == 'placementCapacityExhausted'
            and failure or 'conflict'})
    end
    capacityVectorAdd(
        KEYS[8], KEYS[21], KEYS[10], targetBuckets, request.capacity, 1)
    redis.call('HSET', KEYS[14],
        'state', 'Reserved',
        'aggregateId', request.aggregateId,
        'aggregateGeneration', request.aggregateGeneration,
        'participants', request.participantsEncoded,
        'inventoryDigest', request.inventoryDigest,
        'targetDescriptorKey', request.targetDescriptorKey,
        'targetDescriptorLifecycleGeneration',
            request.targetDescriptorLifecycleGeneration,
        'targetOwnerId', request.targetOwner.ownerId,
        'targetOwnerLeaseGeneration', request.targetOwner.leaseGeneration,
        'capacityBundle', request.capacityBundle)
    return cjson.encode({kind = 'prepared', fence = request.fence})
end

if op == 'commitAggregate' then
    local aggregate = readReservation(KEYS[14])
    if not aggregate then return cjson.encode({kind = 'stale'}) end
    if aggregate.state == 'Committed' then
        return cjson.encode({kind = 'alreadyCommitted'})
    end
    if aggregate.state ~= 'Reserved' then return cjson.encode({kind = 'stale'}) end
    local participants = cjson.decode(hexDecode(aggregate.participants))
    local participantCount = #participants
    local aggregateCapacity = capacityBundle(aggregate.capacityBundle)
    local sourceTotal = {actors = 0, spots = 0}
    local ownerChanges = 0
    local firstNewOwner = nil
    for index, participant in ipairs(participants) do
        local row = rowAt(KEYS[21 + index])
        if not row
            or row.allocationState ~= 'active'
            or row.storeVersion ~= tostring(participant.expectedStoreVersion) then
            return cjson.encode({kind = 'stale'})
        end
        if participant.ownerTransition == 'newOwner' then
            ownerChanges = ownerChanges + 1
            local vector = capacityBundle(row.capacityBundle)
            if not addVector(sourceTotal, vector) then
                return cjson.encode({kind = 'stale'})
            end
            if not firstNewOwner then firstNewOwner = row end
            local originalCapacity = request.capacity
            request.objectKind = row.objectKind
            request.stableType = row.stableType
            request.capacity = {actors = 0, spots = 0}
            local _, failure = descriptorAdmission(request.target)
            request.capacity = originalCapacity
            if failure then return cjson.encode({kind = 'stale'}) end
        elseif participant.ownerTransition == 'preserve' then
            local preserveLeaseOffset = 21 + participantCount * 5 + index
            if not leaseLive({ownerId = row.ownerId,
                leaseGeneration = row.ownerLeaseGeneration},
                KEYS[preserveLeaseOffset]) then
                return cjson.encode({kind = 'stale'})
            end
        else
            return cjson.encode({kind = 'stale'})
        end
    end
    if not firstNewOwner or not sameVector(sourceTotal, aggregateCapacity) then
        return cjson.encode({kind = 'stale'})
    end
    request.capacity = aggregateCapacity
    request.objectKind = aggregateCapacity.spotType
        and aggregateCapacity.spotType.objectKind or firstNewOwner.objectKind
    request.stableType = aggregateCapacity.spotType
        and aggregateCapacity.spotType.stableType or firstNewOwner.stableType
    local targetBuckets, targetFailure = descriptorAdmission(request.target)
    if targetFailure then return cjson.encode({kind = 'stale'}) end
    if not counterAvailable('storeRevision', participantCount)
        or not counterAvailable('authorityOwnerGeneration', ownerChanges) then
        return cjson.encode({kind = 'generationExhausted'})
    end
    for index, participant in ipairs(participants) do
        local currentKey = KEYS[21 + index]
        local historyKey = KEYS[21 + participantCount + index]
        local revisionsKey = KEYS[21 + participantCount * 2 + index]
        local membershipHistoryKey =
            KEYS[21 + participantCount * 3 + index]
        local membershipRevisionsKey =
            KEYS[21 + participantCount * 4 + index]
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
            local vector = capacityBundle(row.capacityBundle)
            local sourceActor, sourceSpot, sourceType = rowBuckets(row)
            capacityVectorAdd(
                KEYS[7], KEYS[20], KEYS[9],
                {actor = sourceActor, spot = sourceSpot, type = sourceType},
                vector, -1)
            row.authorityOwnerGeneration = nextCounter('authorityOwnerGeneration')
            row.ownerId = aggregate.targetOwnerId
            row.ownerLeaseGeneration = aggregate.targetOwnerLeaseGeneration
            row.descriptorKey = aggregate.targetDescriptorKey
            row.descriptorLifecycleGeneration =
                aggregate.targetDescriptorLifecycleGeneration
        end
        writeRow(currentKey, row)
        local participantKeyHex = keyHex(participant.key)
        redis.call('ZADD', KEYS[5], 0, participantKeyHex)
        redis.call('HSET', KEYS[16], participantKeyHex,
            decimalToHex(row.storeVersion))
        redis.call('HSET', KEYS[6], participant.key,
            participant.membershipMutation)
        pruneHistory(currentKey, historyKey, revisionsKey,
            participant.key, participantKeyHex)
        pruneValueHistory(membershipHistoryKey, membershipRevisionsKey)
    end
    capacityVectorAdd(
        KEYS[8], KEYS[21], KEYS[10], targetBuckets, aggregateCapacity, -1)
    capacityVectorAdd(
        KEYS[7], KEYS[20], KEYS[9], targetBuckets, aggregateCapacity, 1)
    redis.call('HSET', KEYS[14], 'state', 'Committed')
    return cjson.encode({kind = 'committed'})
end

if op == 'abortAggregate' then
    local aggregate = readReservation(KEYS[14])
    if not aggregate then return cjson.encode({kind = 'stale'}) end
    if aggregate.state == 'Aborted' then
        return cjson.encode({kind = 'alreadyAborted'})
    end
    if aggregate.state ~= 'Reserved' then return cjson.encode({kind = 'stale'}) end
    local vector = capacityBundle(aggregate.capacityBundle)
    local actorBucket = capacityNodeBucket(
        aggregate.targetDescriptorKey,
        aggregate.targetDescriptorLifecycleGeneration, 'actor')
    local spotBucket = capacityNodeBucket(
        aggregate.targetDescriptorKey,
        aggregate.targetDescriptorLifecycleGeneration, 'spot')
    local typeBucket = vector.spotType and capacityTypeBucket(
        spotBucket, vector.spotType.objectKind,
        vector.spotType.stableType) or ''
    capacityVectorAdd(
        KEYS[8], KEYS[21], KEYS[10],
        {actor = actorBucket, spot = spotBucket, type = typeBucket},
        vector, -1)
    redis.call('HSET', KEYS[14], 'state', 'Aborted')
    return cjson.encode({kind = 'aborted'})
end

if op == 'capacityProjection' then
    return cjson.encode({
        kind = 'capacityProjection',
        actorActive = mapOf(redis.call('HGETALL', KEYS[7])),
        actorReserved = mapOf(redis.call('HGETALL', KEYS[8])),
        spotActive = mapOf(redis.call('HGETALL', KEYS[20])),
        spotReserved = mapOf(redis.call('HGETALL', KEYS[21])),
        typeActive = mapOf(redis.call('HGETALL', KEYS[9])),
        typeReserved = mapOf(redis.call('HGETALL', KEYS[10]))
    })
end

if op == 'scan' then
    local scan = rowAt(KEYS[19])
    if request.start then
        local watermark = redis.call('HGET', KEYS[4], 'storeRevision') or '0'
        local watermarkMember =
            decimalToHex(watermark) .. ':' .. request.scanId
        redis.call('HSET', KEYS[19],
            'watermark', watermark, 'prefix', request.prefix,
            'watermarkMember', watermarkMember,
            'lastHex', '', 'expiresAtMs', nowMs + request.retentionMs)
        redis.call('PEXPIRE', KEYS[19], request.retentionMs)
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
    redis.call('HSET', KEYS[19], 'lastHex', lastHex)
    redis.call('PEXPIRE', KEYS[19], request.retentionMs)
    local more = false
    if #candidates > 0 then
        more = redis.call('ZRANGEBYLEX', KEYS[5], '(' .. lastHex, '+',
            'LIMIT', 0, 1)[1] ~= nil
    end
    if not more then
        redis.call('DEL', KEYS[19])
        redis.call('ZREM', KEYS[17], scan.watermarkMember)
        redis.call('ZREM', KEYS[18], scan.watermarkMember)
    end
    return cjson.encode({kind = 'page', rows = rows,
        scanId = request.scanId, lastHex = lastHex,
        hasMore = more, storeNowMs = nowMs})
end

error('unknown authority operation')
`;
