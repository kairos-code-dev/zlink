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
    if schemaFormat ~= 'location-authority-hybrid-v1'
        or schemaEpoch ~= '1' then
        error('incompatible Redis location authority schema')
    end
else
    redis.call('HSET', KEYS[19],
        'format', 'location-authority-hybrid-v1',
        'epoch', '1')
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
        capacityDelta = tonumber(row.capacityDelta)
    }
end

local function snapshot(row)
    if not row or row.deleted == '1' then return missing() end
    return {
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
end

local historyFields = {
    'authorityKey', 'payload', 'storeVersion', 'objectGeneration',
    'authorityOwnerGeneration', 'ownerId', 'ownerLeaseGeneration',
    'allocationState', 'objectKind', 'stableType', 'descriptorKey',
    'descriptorLifecycleGeneration', 'capacityDelta'
}

local function archiveCurrent(currentKey, historyKey, revisionIndexKey)
    local row = rowAt(currentKey)
    if not row or not row.storeVersion then return end
    local revision = decimalToHex(row.storeVersion)
    redis.call('HSET', historyKey, revision .. ':deleted', '0')
    for _, field in ipairs(historyFields) do
        redis.call('HSET', historyKey, revision .. ':' .. field, row[field])
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

local function capacityNodeBucket(descriptorKey, lifecycle)
    local lifecycleValue = tostring(lifecycle)
    return string.len(descriptorKey) .. ':' .. descriptorKey
        .. string.len(lifecycleValue) .. ':' .. lifecycleValue
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
        'runtimeState', 'capabilities', 'nodeActiveLimit',
        'nodePendingLimit')
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
    if request.placementProfile and request.placementProfile ~= '' then
        local found = false
        for _, profile in ipairs(
            matched.placementProfiles or matched.PlacementProfiles or {}) do
            if profile == request.placementProfile then found = true break end
        end
        if not found then return nil, 'targetUnavailable' end
    end
    local nodeBucket = capacityNodeBucket(
        target.descriptorKey, target.lifecycleGeneration)
    local typeBucket = capacityTypeBucket(
        nodeBucket, request.objectKind, request.stableType)
    local delta = tonumber(request.capacityDelta)
    local nodeActive = tonumber(redis.call('HGET', KEYS[7], nodeBucket) or '0')
    local nodePending = tonumber(redis.call('HGET', KEYS[8], nodeBucket) or '0')
    local typeActive = tonumber(redis.call('HGET', KEYS[9], typeBucket) or '0')
    local typePending = tonumber(redis.call('HGET', KEYS[10], typeBucket) or '0')
    local matchedActiveLimit = tonumber(
        matched.activeLimit or matched.ActiveLimit)
    local matchedPendingLimit = tonumber(
        matched.pendingLimit or matched.PendingLimit)
    if nodeActive + delta > tonumber(metadata[7] or '2147483647')
        or nodePending + delta > tonumber(metadata[8] or '2147483647')
        or (matchedActiveLimit
            and typeActive + delta > matchedActiveLimit)
        or (matchedPendingLimit
            and typePending + delta > matchedPendingLimit) then
        return nil, 'placementCapacityExhausted'
    end
    return {node = nodeBucket, type = typeBucket}, nil
end

local function capacityAdd(key, bucket, delta)
    local current = tonumber(redis.call('HGET', key, bucket) or '0')
    local nextValue = current + delta
    if nextValue < 0 or nextValue > 2147483647 then
        error('placement capacity counter out of range')
    end
    if nextValue == 0 then redis.call('HDEL', key, bucket)
    else redis.call('HSET', key, bucket, nextValue) end
end

local function rowBuckets(row)
    local node = capacityNodeBucket(
        row.descriptorKey, row.descriptorLifecycleGeneration)
    return node, capacityTypeBucket(node, row.objectKind, row.stableType)
end

local function sourceMatches(row, value)
    return row and row.allocationState == 'active'
       and row.storeVersion == tostring(value.expectedStoreVersion)
       and row.objectKind == value.objectKind
       and row.stableType == value.stableType
       and sameDescriptor(row, value.sourceDescriptorKey,
            value.sourceNodeLifecycleGeneration)
       and tonumber(row.capacityDelta) == tonumber(value.capacityDelta)
       and sameOwner(row, value.sourceOwner)
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
        capacityDelta = request.capacityDelta
    }
    writeRow(KEYS[1], row)
    redis.call('HSET', KEYS[14],
        'status', 'reserved', 'authorityKey', request.key,
        'storeVersion', revision, 'requestJson', ARGV[2],
        'nodeBucket', buckets.node, 'typeBucket', buckets.type)
    capacityAdd(KEYS[8], buckets.node, tonumber(request.capacityDelta))
    capacityAdd(KEYS[10], buckets.type, tonumber(request.capacityDelta))
    indexCurrent(request.key, revision)
    local result = snapshot(row)
    return cjson.encode({kind = 'reserved',
        reservationId = request.reservationId, creating = result})
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
    request.capacityDelta = 0
    local _, failure = descriptorAdmission(request.target)
    if failure then return cjson.encode({kind = 'stale'}) end
    request.capacityDelta = tonumber(row.capacityDelta)
    if not counterAvailable('storeRevision', 1) then
        return cjson.encode({kind = 'generationExhausted'})
    end
    archiveCurrent(KEYS[1], KEYS[2], KEYS[3])
    row.storeVersion = nextCounter('storeRevision')
    row.payload = request.payload
    row.allocationState = 'active'
    writeRow(KEYS[1], row)
    capacityAdd(KEYS[8], reservation.nodeBucket, -tonumber(row.capacityDelta))
    capacityAdd(KEYS[10], reservation.typeBucket, -tonumber(row.capacityDelta))
    capacityAdd(KEYS[7], reservation.nodeBucket, tonumber(row.capacityDelta))
    capacityAdd(KEYS[9], reservation.typeBucket, tonumber(row.capacityDelta))
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
    capacityAdd(KEYS[8], reservation.nodeBucket, -tonumber(row.capacityDelta))
    capacityAdd(KEYS[10], reservation.typeBucket, -tonumber(row.capacityDelta))
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
    if not sourceMatches(row, request) then
        return cjson.encode({kind = 'conflict', current = snapshot(row)})
    end
    local buckets, failure = descriptorAdmission(request.target)
    if failure then return cjson.encode({kind = failure}) end
    local sourceNode, sourceType = rowBuckets(row)
    redis.call('HSET', KEYS[14],
        'status', 'reserved', 'authorityKey', request.key,
        'requestJson', ARGV[2], 'sourceNodeBucket', sourceNode,
        'sourceTypeBucket', sourceType, 'targetNodeBucket', buckets.node,
        'targetTypeBucket', buckets.type, 'capacityDelta', request.capacityDelta)
    capacityAdd(KEYS[8], buckets.node, tonumber(request.capacityDelta))
    capacityAdd(KEYS[10], buckets.type, tonumber(request.capacityDelta))
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
        -tonumber(reservation.capacityDelta))
    capacityAdd(KEYS[10], reservation.targetTypeBucket,
        -tonumber(reservation.capacityDelta))
    redis.call('HSET', KEYS[14], 'status', 'aborted')
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
        request.capacityDelta = tonumber(row.capacityDelta)
        request.target = reservation.request.target
        local _, failure = descriptorAdmission(request.target)
        if failure then return cjson.encode({kind = 'conflict', current = snapshot(row)}) end
    end
    archiveCurrent(KEYS[1], KEYS[2], KEYS[3])
    row.storeVersion = nextCounter('storeRevision')
    if request.mutationKind == 'delete' then
        local nodeBucket, typeBucket = rowBuckets(row)
        capacityAdd(KEYS[7], nodeBucket, -tonumber(row.capacityDelta))
        capacityAdd(KEYS[9], typeBucket, -tonumber(row.capacityDelta))
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
            -tonumber(reservation.capacityDelta))
        capacityAdd(KEYS[9], reservation.sourceTypeBucket,
            -tonumber(reservation.capacityDelta))
        capacityAdd(KEYS[8], reservation.targetNodeBucket,
            -tonumber(reservation.capacityDelta))
        capacityAdd(KEYS[10], reservation.targetTypeBucket,
            -tonumber(reservation.capacityDelta))
        capacityAdd(KEYS[7], reservation.targetNodeBucket,
            tonumber(reservation.capacityDelta))
        capacityAdd(KEYS[9], reservation.targetTypeBucket,
            tonumber(reservation.capacityDelta))
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
    local reservationCount = #request.targetReservations
    local seen = {}
    local newOwners = 0
    for index, participant in ipairs(request.participants) do
        local row = rowAt(KEYS[19 + index])
        if not row or row.allocationState ~= 'active'
            or row.storeVersion ~= tostring(participant.expectedStoreVersion) then
            return cjson.encode({kind = 'conflict'})
        end
        if participant.ownerTransition == 'preserve' then
            local leaseKey = KEYS[19 + participantCount + reservationCount + index]
            if not leaseLive({ownerId = row.ownerId,
                leaseGeneration = row.ownerLeaseGeneration}, leaseKey) then
                return cjson.encode({kind = 'conflict'})
            end
        else newOwners = newOwners + 1 end
    end
    for index = 1, reservationCount do
        local reservationKey = KEYS[19 + participantCount + index]
        local reservation = readReservation(reservationKey)
        if not reservation or reservation.status ~= 'reserved'
            or seen[reservation.authorityKey] then
            return cjson.encode({kind = 'conflict'})
        end
        seen[reservation.authorityKey] = true
        local found = false
        local participantRow = nil
        for participantIndex, participant in ipairs(request.participants) do
            if participant.key == reservation.authorityKey
                and participant.ownerTransition == 'newOwner'
                and tostring(participant.expectedStoreVersion)
                    == tostring(reservation.request.expectedStoreVersion) then
                found = true
                participantRow = rowAt(KEYS[19 + participantIndex])
                break
            end
        end
        if not found or not sourceMatches(participantRow, reservation.request) then
            return cjson.encode({kind = 'conflict'})
        end
        request.objectKind = reservation.request.objectKind
        request.stableType = reservation.request.stableType
        request.capacityDelta = reservation.request.capacityDelta
        request.placementProfile = reservation.request.placementProfile
        local descriptorOffset =
            19 + participantCount * 2 + reservationCount + index
        local _, failure = descriptorAdmission(
            reservation.request.target,
            KEYS[descriptorOffset],
            KEYS[descriptorOffset + reservationCount],
            KEYS[descriptorOffset + reservationCount * 2])
        if failure then return cjson.encode({kind = 'conflict'}) end
    end
    if newOwners ~= reservationCount then return cjson.encode({kind = 'conflict'}) end
    for index = 1, reservationCount do
        redis.call('HSET', KEYS[19 + participantCount + index],
            'status', 'prepared', 'aggregateKey', KEYS[14])
    end
    redis.call('HSET', KEYS[14],
        'status', 'prepared', 'requestJson', ARGV[2],
        'participantCount', participantCount,
        'reservationCount', reservationCount)
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
    local reservationCount = #prepared.targetReservations
    local ownerChanges = 0
    for index, participant in ipairs(prepared.participants) do
        local row = rowAt(KEYS[19 + index])
        if not row
            or row.storeVersion ~= tostring(participant.expectedStoreVersion) then
            return cjson.encode({kind = 'stale'})
        end
        if participant.ownerTransition == 'newOwner' then
            ownerChanges = ownerChanges + 1
            local reservation = nil
            local reservationIndex = nil
            for candidateIndex = 1, reservationCount do
                local candidate = readReservation(
                    KEYS[19 + participantCount * 5 + candidateIndex])
                if candidate and candidate.authorityKey == participant.key then
                    reservation = candidate
                    reservationIndex = candidateIndex
                    break
                end
            end
            if not reservation or reservation.status ~= 'prepared'
                or reservation.aggregateKey ~= KEYS[14]
                or not sourceMatches(row, reservation.request) then
                return cjson.encode({kind = 'stale'})
            end
            request.objectKind = reservation.request.objectKind
            request.stableType = reservation.request.stableType
            request.capacityDelta = reservation.request.capacityDelta
            request.placementProfile = reservation.request.placementProfile
            local descriptorOffset =
                19 + participantCount * 6 + reservationCount
                    + reservationIndex
            local _, failure = descriptorAdmission(
                reservation.request.target,
                KEYS[descriptorOffset],
                KEYS[descriptorOffset + reservationCount],
                KEYS[descriptorOffset + reservationCount * 2])
            if failure then return cjson.encode({kind = 'stale'}) end
        else
            local preserveLeaseOffset =
                19 + participantCount * 5 + reservationCount + index
            if not leaseLive({ownerId = row.ownerId,
                leaseGeneration = row.ownerLeaseGeneration},
                KEYS[preserveLeaseOffset]) then
                return cjson.encode({kind = 'stale'})
            end
        end
    end
    local sourceNodeRequired = {}
    local sourceTypeRequired = {}
    local targetNodeRequired = {}
    local targetTypeRequired = {}
    for reservationIndex = 1, reservationCount do
        local reservation = readReservation(
            KEYS[19 + participantCount * 5 + reservationIndex])
        local delta = reservation and tonumber(reservation.capacityDelta)
        if not reservation or not delta then
            return cjson.encode({kind = 'stale'})
        end
        sourceNodeRequired[reservation.sourceNodeBucket] =
            (sourceNodeRequired[reservation.sourceNodeBucket] or 0) + delta
        sourceTypeRequired[reservation.sourceTypeBucket] =
            (sourceTypeRequired[reservation.sourceTypeBucket] or 0) + delta
        targetNodeRequired[reservation.targetNodeBucket] =
            (targetNodeRequired[reservation.targetNodeBucket] or 0) + delta
        targetTypeRequired[reservation.targetTypeBucket] =
            (targetTypeRequired[reservation.targetTypeBucket] or 0) + delta
    end
    local function capacitySatisfies(key, required)
        for bucket, delta in pairs(required) do
            if tonumber(redis.call('HGET', key, bucket) or '0') < delta then
                return false
            end
        end
        return true
    end
    if not capacitySatisfies(KEYS[7], sourceNodeRequired)
        or not capacitySatisfies(KEYS[9], sourceTypeRequired)
        or not capacitySatisfies(KEYS[8], targetNodeRequired)
        or not capacitySatisfies(KEYS[10], targetTypeRequired) then
        return cjson.encode({kind = 'stale'})
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
            local reservation = nil
            local reservationKey = nil
            for reservationIndex = 1, reservationCount do
                local candidateKey =
                    KEYS[19 + participantCount * 5 + reservationIndex]
                local candidate = readReservation(candidateKey)
                if candidate and candidate.authorityKey == participant.key then
                    reservation = candidate
                    reservationKey = candidateKey
                    break
                end
            end
            if not reservation or reservation.status ~= 'prepared'
                or reservation.aggregateKey ~= KEYS[14] then
                return cjson.encode({kind = 'stale'})
            end
            capacityAdd(KEYS[7], reservation.sourceNodeBucket,
                -tonumber(reservation.capacityDelta))
            capacityAdd(KEYS[9], reservation.sourceTypeBucket,
                -tonumber(reservation.capacityDelta))
            capacityAdd(KEYS[8], reservation.targetNodeBucket,
                -tonumber(reservation.capacityDelta))
            capacityAdd(KEYS[10], reservation.targetTypeBucket,
                -tonumber(reservation.capacityDelta))
            capacityAdd(KEYS[7], reservation.targetNodeBucket,
                tonumber(reservation.capacityDelta))
            capacityAdd(KEYS[9], reservation.targetTypeBucket,
                tonumber(reservation.capacityDelta))
            row.authorityOwnerGeneration = nextCounter('authorityOwnerGeneration')
            row.ownerId = reservation.request.targetOwner.ownerId
            row.ownerLeaseGeneration = reservation.request.targetOwner.leaseGeneration
            row.descriptorKey = reservation.request.targetDescriptorKey
            row.descriptorLifecycleGeneration =
                reservation.request.targetNodeLifecycleGeneration
            redis.call('HSET', reservationKey, 'status', 'committed')
        end
        writeRow(currentKey, row)
        redis.call('ZADD', KEYS[5], 0, participant.keyHex)
        redis.call('HSET', KEYS[16], participant.keyHex,
            decimalToHex(row.storeVersion))
        redis.call('HSET', KEYS[6], participant.key,
            participant.membershipMutation)
        pruneHistory(currentKey, historyKey, revisionsKey,
            participant.key, participant.keyHex)
        pruneValueHistory(membershipHistoryKey, membershipRevisionsKey)
    end
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
    local prepared = aggregate.request
    local participantCount = #prepared.participants
    local pendingNodeRequired = {}
    local pendingTypeRequired = {}
    for index = 1, #prepared.targetReservations do
        local reservationKey = KEYS[19 + participantCount + index]
        local reservation = readReservation(reservationKey)
        local delta = reservation and tonumber(reservation.capacityDelta)
        if not reservation or not delta then
            return cjson.encode({kind = 'stale'})
        end
        pendingNodeRequired[reservation.targetNodeBucket] =
            (pendingNodeRequired[reservation.targetNodeBucket] or 0) + delta
        pendingTypeRequired[reservation.targetTypeBucket] =
            (pendingTypeRequired[reservation.targetTypeBucket] or 0) + delta
    end
    for bucket, delta in pairs(pendingNodeRequired) do
        if tonumber(redis.call('HGET', KEYS[8], bucket) or '0') < delta then
            return cjson.encode({kind = 'stale'})
        end
    end
    for bucket, delta in pairs(pendingTypeRequired) do
        if tonumber(redis.call('HGET', KEYS[10], bucket) or '0') < delta then
            return cjson.encode({kind = 'stale'})
        end
    end
    for index = 1, #prepared.targetReservations do
        local reservationKey = KEYS[19 + participantCount + index]
        local reservation = readReservation(reservationKey)
        if not reservation or reservation.status ~= 'prepared'
            or reservation.aggregateKey ~= KEYS[14] then
            return cjson.encode({kind = 'stale'})
        end
        capacityAdd(KEYS[8], reservation.targetNodeBucket,
            -tonumber(reservation.capacityDelta))
        capacityAdd(KEYS[10], reservation.targetTypeBucket,
            -tonumber(reservation.capacityDelta))
        redis.call('HSET', reservationKey, 'status', 'aborted')
    end
    redis.call('HSET', KEYS[14], 'status', 'aborted')
    return cjson.encode({kind = 'aborted'})
end

if op == 'capacityProjection' then
    return cjson.encode({
        kind = 'capacityProjection',
        nodeActive = mapOf(redis.call('HGETALL', KEYS[7])),
        nodePending = mapOf(redis.call('HGETALL', KEYS[8]))
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
