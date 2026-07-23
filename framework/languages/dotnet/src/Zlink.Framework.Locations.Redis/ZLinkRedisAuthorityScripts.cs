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
        local function incrementDecimal(value)
            value = value or '0'
            local digits = {}
            local carry = 1
            for index = string.len(value), 1, -1 do
                local digit = string.byte(value, index) - 48 + carry
                if digit >= 10 then
                    digit = digit - 10
                    carry = 1
                else
                    carry = 0
                end
                table.insert(digits, 1, string.char(digit + 48))
            end
            if carry == 1 then table.insert(digits, 1, '1') end
            return table.concat(digits)
        end
        local function capacityBucket(mesh, rid, lifecycle, kind, stableType)
            return string.len(mesh) .. ':' .. mesh
                .. string.len(rid) .. ':' .. rid
                .. string.len(lifecycle) .. ':' .. lifecycle
                .. string.len(kind) .. ':' .. kind
                .. string.len(stableType) .. ':' .. stableType
        end
        local function normalizeDecimal(value)
            value = string.gsub(value or '0', '^0+', '')
            return value == '' and '0' or value
        end
        local function compareDecimal(left, right)
            left = normalizeDecimal(left)
            right = normalizeDecimal(right)
            if string.len(left) < string.len(right) then return -1 end
            if string.len(left) > string.len(right) then return 1 end
            if left < right then return -1 end
            if left > right then return 1 end
            return 0
        end
        local function addSmallDecimal(value, delta)
            local digits = {}
            local carry = delta
            value = normalizeDecimal(value)
            for index = string.len(value), 1, -1 do
                local digit = string.byte(value, index) - 48
                local sum = digit + (carry % 10)
                carry = math.floor(carry / 10)
                if sum >= 10 then
                    sum = sum - 10
                    carry = carry + 1
                end
                table.insert(digits, 1, string.char(sum + 48))
            end
            while carry > 0 do
                table.insert(digits, 1, string.char((carry % 10) + 48))
                carry = math.floor(carry / 10)
            end
            return table.concat(digits)
        end
        local function canAdjustCapacity(capacityKey, bucket, delta)
            local current =
                normalizeDecimal(redis.call('HGET', capacityKey, bucket))
            if delta < 0 then
                return compareDecimal(current, tostring(-delta)) >= 0
            end
            return compareDecimal(
                addSmallDecimal(current, delta),
                maxCounter) <= 0
        end
        local function addCapacity(capacityKey, bucket, delta)
            if not canAdjustCapacity(capacityKey, bucket, delta) then
                return false
            end
            local ok, updated =
                pcall(redis.call, 'HINCRBY', capacityKey, bucket, delta)
            if not ok then return false end
            if updated == 0 then redis.call('HDEL', capacityKey, bucket) end
            return true
        end
        local function requireCapacity(required, bucket, delta)
            required[bucket] = (required[bucket] or 0) + delta
        end
        local function hasRequiredCapacity(capacityKey, required)
            for bucket, delta in pairs(required) do
                if not canAdjustCapacity(capacityKey, bucket, -delta) then
                    return false
                end
            end
            return true
        end
        local function hasCapacityHeadroom(capacityKey, required)
            for bucket, delta in pairs(required) do
                if not canAdjustCapacity(capacityKey, bucket, delta) then
                    return false
                end
            end
            return true
        end
        local function descriptorMatches(
            descriptorKey, lifecycle, ownerId, kind, stableType)
            local encoded = redis.call('HGET', descriptorKey, 'json')
            if not encoded then return false end
            local ok, descriptor = pcall(cjson.decode, encoded)
            if not ok or type(descriptor) ~= 'table' then return false end
            local actualLifecycle =
                descriptor.LifecycleGeneration or descriptor.lifecycleGeneration
            local actualOwner = descriptor.OwnerId or descriptor.ownerId
            local draining = descriptor.Draining
            if draining == nil then draining = descriptor.draining end
            if tostring(actualLifecycle or '') ~= tostring(lifecycle)
                or tostring(actualOwner or '') ~= tostring(ownerId)
                or draining == true then
                return false
            end
            if tostring(kind) == '3' then
                local types =
                    descriptor.InstanceSpotTypes or descriptor.instanceSpotTypes
                if type(types) ~= 'table' then return false end
                local found = false
                for _, value in ipairs(types) do
                    if value == stableType then found = true end
                end
                if not found then return false end
            end
            return true
        end
        local function allocationCapacity(encoded)
            if not encoded then return nil, nil end
            local ok, allocation = pcall(cjson.decode, encoded)
            if not ok or type(allocation) ~= 'table' then return nil, nil end
            local mesh = allocation.MeshName or allocation.meshName
            local rid = allocation.Rid or allocation.rid
            local lifecycle = allocation.DescriptorLifecycleGeneration
                or allocation.descriptorLifecycleGeneration
            local kind = allocation.ObjectKind or allocation.objectKind
            local stableType = allocation.StableType or allocation.stableType
            local delta = allocation.CapacityDelta or allocation.capacityDelta
            if not mesh or not rid or not lifecycle or not kind
                or not stableType or not delta then
                return nil, nil
            end
            return capacityBucket(
                tostring(mesh), tostring(rid), tostring(lifecycle),
                tostring(kind), tostring(stableType)), tonumber(delta)
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
            redis.call('HGET', KEYS[6], ARGV[1]),
            redis.call('HGET', KEYS[7], ARGV[1]),
            redis.call('HGET', KEYS[8], ARGV[1])
        }
        """;

    internal const string CompareExchange = Prologue + """

        local key = ARGV[1]
        local current = redis.call('HGET', KEYS[2], key)
        if not current or current ~= ARGV[3]
            or redis.call('HGET', KEYS[8], key) ~= 'active' then
            if not current then return {'conflict-missing', nowMs} end
            return {
                'conflict-found', nowMs, current,
                redis.call('HGET', KEYS[3], key),
                redis.call('HGET', KEYS[4], key),
                redis.call('HGET', KEYS[5], key),
                redis.call('HGET', KEYS[6], key),
                redis.call('HGET', KEYS[7], key),
                redis.call('HGET', KEYS[8], key),
                redis.call('HGET', KEYS[9], key)
            }
        end
        local lease = redis.call('GET', KEYS[12])
        local leaseGeneration =
            lease and string.match(lease, '([^|]*)|') or nil
        local storedOwner = redis.call('HGET', KEYS[6], key)
        local storedOwnerGeneration = redis.call('HGET', KEYS[7], key)
        local transition = ARGV[6]
        if not leaseGeneration or leaseGeneration ~= ARGV[8]
            or transition ~= 'new-owner'
            and (storedOwner ~= ARGV[7]
                or storedOwnerGeneration ~= ARGV[8]) then
            return {
                'conflict-found', nowMs, current,
                redis.call('HGET', KEYS[3], key),
                redis.call('HGET', KEYS[4], key),
                redis.call('HGET', KEYS[5], key),
                storedOwner,
                storedOwnerGeneration,
                redis.call('HGET', KEYS[8], key),
                redis.call('HGET', KEYS[9], key)
            }
        end
        if not canIncrement(redis.call('HGET', KEYS[1], 'revision')) then
            return {'exhausted', nowMs}
        end
        if ARGV[4] == 'delete' then
            local bucket, delta =
                allocationCapacity(redis.call('HGET', KEYS[9], key))
            if not bucket or not delta
                or not canAdjustCapacity(KEYS[14], bucket, -delta) then
                return {
                    'conflict-found', nowMs, current,
                    redis.call('HGET', KEYS[3], key),
                    redis.call('HGET', KEYS[4], key),
                    redis.call('HGET', KEYS[5], key),
                    storedOwner,
                    storedOwnerGeneration,
                    redis.call('HGET', KEYS[8], key),
                    redis.call('HGET', KEYS[9], key)
                }
            end
            addCapacity(KEYS[14], bucket, -delta)
            local version = redis.call('HINCRBY', KEYS[1], 'revision', 1)
            for index = 2, 10 do redis.call('HDEL', KEYS[index], key) end
            redis.call('SREM', KEYS[11], key)
            return {'deleted', nowMs, version}
        end

        if transition == 'preserve' then
            if not current then
                return {'invalid', nowMs}
            end
        elseif transition == 'new-owner' then
            if ARGV[7] == '' or not leaseGeneration
                or leaseGeneration ~= ARGV[8] then
                return {
                    'conflict-found', nowMs, current,
                    redis.call('HGET', KEYS[3], key),
                    redis.call('HGET', KEYS[4], key),
                    redis.call('HGET', KEYS[5], key),
                    redis.call('HGET', KEYS[6], key),
                    redis.call('HGET', KEYS[7], key),
                    redis.call('HGET', KEYS[8], key),
                    redis.call('HGET', KEYS[9], key)
                }
            end
            if redis.call('HGET', KEYS[13], 'status') ~= 'reserved'
                or redis.call('HGET', KEYS[13], 'key') ~= key
                or redis.call('HGET', KEYS[13], 'version') ~= current
                or redis.call('HGET', KEYS[13], 'sourceOwner') ~= storedOwner
                or redis.call('HGET', KEYS[13], 'sourceGeneration')
                    ~= storedOwnerGeneration
                or redis.call('HGET', KEYS[13], 'sourceAllocation')
                    ~= redis.call('HGET', KEYS[9], key)
                or redis.call('HGET', KEYS[13], 'targetOwner') ~= ARGV[7]
                or redis.call('HGET', KEYS[13], 'targetGeneration') ~= ARGV[8] then
                return {
                    'conflict-found', nowMs, current,
                    redis.call('HGET', KEYS[3], key),
                    redis.call('HGET', KEYS[4], key),
                    redis.call('HGET', KEYS[5], key),
                    redis.call('HGET', KEYS[6], key),
                    redis.call('HGET', KEYS[7], key),
                    redis.call('HGET', KEYS[8], key),
                    redis.call('HGET', KEYS[9], key)
                }
            end
            local targetDescriptorKey =
                redis.call('HGET', KEYS[13], 'targetDescriptorKey')
            if not targetDescriptorKey
                or not descriptorMatches(
                    targetDescriptorKey,
                    redis.call('HGET', KEYS[13], 'targetLifecycle'),
                    ARGV[7],
                    redis.call('HGET', KEYS[13], 'objectKind'),
                    redis.call('HGET', KEYS[13], 'stableType')) then
                return {
                    'conflict-found', nowMs, current,
                    redis.call('HGET', KEYS[3], key),
                    redis.call('HGET', KEYS[4], key),
                    redis.call('HGET', KEYS[5], key),
                    redis.call('HGET', KEYS[6], key),
                    redis.call('HGET', KEYS[7], key),
                    redis.call('HGET', KEYS[8], key),
                    redis.call('HGET', KEYS[9], key)
                }
            end
            local sourceBucket =
                redis.call('HGET', KEYS[13], 'sourceCapacityBucket')
            local targetBucket =
                redis.call('HGET', KEYS[13], 'targetCapacityBucket')
            local capacityDelta =
                tonumber(redis.call('HGET', KEYS[13], 'capacityDelta'))
            if not sourceBucket or not targetBucket or not capacityDelta
                or not canAdjustCapacity(
                    KEYS[14], sourceBucket, -capacityDelta)
                or not canAdjustCapacity(
                    KEYS[15], targetBucket, -capacityDelta)
                or not canAdjustCapacity(
                    KEYS[14], targetBucket, capacityDelta) then
                return {
                    'conflict-found', nowMs, current,
                    redis.call('HGET', KEYS[3], key),
                    redis.call('HGET', KEYS[4], key),
                    redis.call('HGET', KEYS[5], key),
                    redis.call('HGET', KEYS[6], key),
                    redis.call('HGET', KEYS[7], key),
                    redis.call('HGET', KEYS[8], key),
                    redis.call('HGET', KEYS[9], key)
                }
            end
            if not canIncrement(redis.call('HGET', KEYS[1], 'owner')) then
                return {'exhausted', nowMs}
            end
        else
            return {'invalid', nowMs}
        end

        local version = redis.call('HINCRBY', KEYS[1], 'revision', 1)
        redis.call('HSET', KEYS[2], key, version)
        redis.call('HSET', KEYS[3], key, ARGV[5])
        if transition == 'new-owner' then
            local sourceBucket =
                redis.call('HGET', KEYS[13], 'sourceCapacityBucket')
            local targetBucket =
                redis.call('HGET', KEYS[13], 'targetCapacityBucket')
            local capacityDelta =
                tonumber(redis.call('HGET', KEYS[13], 'capacityDelta'))
            addCapacity(KEYS[14], sourceBucket, -capacityDelta)
            addCapacity(KEYS[15], targetBucket, -capacityDelta)
            addCapacity(KEYS[14], targetBucket, capacityDelta)
            redis.call('HSET', KEYS[5], key,
                redis.call('HINCRBY', KEYS[1], 'owner', 1))
            redis.call('HSET', KEYS[6], key, ARGV[7])
            redis.call('HSET', KEYS[7], key, ARGV[8])
            redis.call('HSET', KEYS[8], key, 'active')
            redis.call('HSET', KEYS[9], key,
                redis.call('HGET', KEYS[13], 'targetAllocation'))
            redis.call('HSET', KEYS[13], 'status', 'committed')
        end
        redis.call('SADD', KEYS[11], key)
        return {
            'stored', nowMs, version,
            redis.call('HGET', KEYS[3], key),
            redis.call('HGET', KEYS[4], key),
            redis.call('HGET', KEYS[5], key),
            redis.call('HGET', KEYS[6], key),
            redis.call('HGET', KEYS[7], key),
            redis.call('HGET', KEYS[8], key),
            redis.call('HGET', KEYS[9], key)
        }
        """;

    internal const string ReserveRelocationCapacity = Prologue + """

        local existing = redis.call('HGET', KEYS[6], 'signature')
        if existing then
            if existing == ARGV[1] then
                return {'already', nowMs, ARGV[2]}
            end
            return {'conflict', nowMs}
        end
        local version = redis.call('HGET', KEYS[1], ARGV[3])
        if not version or version ~= ARGV[4]
            or redis.call('HGET', KEYS[2], ARGV[3]) ~= ARGV[5]
            or redis.call('HGET', KEYS[3], ARGV[3]) ~= ARGV[6]
            or redis.call('HGET', KEYS[4], ARGV[3]) ~= 'active'
            or redis.call('HGET', KEYS[5], ARGV[3]) ~= ARGV[9] then
            return {'conflict', nowMs}
        end
        local targetLease = redis.call('GET', KEYS[7])
        local targetGeneration =
            targetLease and string.match(targetLease, '([^|]*)|') or nil
        if not targetGeneration or targetGeneration ~= ARGV[8] then
            return {'target-unavailable', nowMs}
        end
        if not descriptorMatches(
            KEYS[9], ARGV[13], ARGV[7], ARGV[14], ARGV[15]) then
            return {'target-unavailable', nowMs}
        end
        local sourceBucket, sourceDelta =
            allocationCapacity(ARGV[9])
        local targetBucket, targetDelta =
            allocationCapacity(ARGV[10])
        if not sourceBucket or not targetBucket
            or sourceDelta ~= tonumber(ARGV[16])
            or targetDelta ~= tonumber(ARGV[16])
            or not addCapacity(KEYS[8], targetBucket, targetDelta) then
            return {'conflict', nowMs}
        end
        redis.call('HSET', KEYS[6],
            'signature', ARGV[1],
            'fence', ARGV[2],
            'status', 'reserved',
            'key', ARGV[3],
            'version', ARGV[4],
            'sourceOwner', ARGV[5],
            'sourceGeneration', ARGV[6],
            'targetOwner', ARGV[7],
            'targetGeneration', ARGV[8],
            'sourceAllocation', ARGV[9],
            'targetAllocation', ARGV[10],
            'targetMesh', ARGV[11],
            'targetRid', ARGV[12],
            'targetLifecycle', ARGV[13],
            'objectKind', ARGV[14],
            'stableType', ARGV[15],
            'capacityDelta', ARGV[16],
            'sourceCapacityBucket', sourceBucket,
            'targetCapacityBucket', targetBucket,
            'targetDescriptorKey', ARGV[17])
        return {'reserved', nowMs, ARGV[2]}
        """;

    internal const string AbortRelocationCapacity = Prologue + """

        local status = redis.call('HGET', KEYS[1], 'status')
        if not status then return {'stale', nowMs} end
        if status == 'committed' then return {'committed', nowMs} end
        if status == 'aborted' then return {'already', nowMs} end
        if status == 'prepared' then return {'stale', nowMs} end
        local bucket = redis.call('HGET', KEYS[1], 'targetCapacityBucket')
        local delta = tonumber(redis.call('HGET', KEYS[1], 'capacityDelta'))
        if not bucket or not delta
            or not canAdjustCapacity(KEYS[2], bucket, -delta) then
            return {'stale', nowMs}
        end
        addCapacity(KEYS[2], bucket, -delta)
        redis.call('HSET', KEYS[1], 'status', 'aborted')
        return {'aborted', nowMs}
        """;

    internal const string StartScan = Prologue + """

        redis.call('DEL', KEYS[11])
        local keys = redis.call('SMEMBERS', KEYS[10])
        table.sort(keys)
        for _, key in ipairs(keys) do
            if string.sub(key, 1, string.len(ARGV[1])) == ARGV[1] then
                local version = redis.call('HGET', KEYS[1], key)
                if version then
                    redis.call('RPUSH', KEYS[11],
                        key, version,
                        redis.call('HGET', KEYS[2], key),
                        redis.call('HGET', KEYS[3], key),
                        redis.call('HGET', KEYS[4], key),
                        redis.call('HGET', KEYS[5], key),
                        redis.call('HGET', KEYS[6], key),
                        redis.call('HGET', KEYS[7], key),
                        redis.call('HGET', KEYS[8], key))
                end
            end
        end
        redis.call('PEXPIRE', KEYS[11], ARGV[2])
        local total = math.floor(redis.call('LLEN', KEYS[11]) / 9)
        local count = math.min(tonumber(ARGV[3]), total)
        local values = {}
        if count > 0 then
            values = redis.call('LRANGE', KEYS[11], 0, count * 9 - 1)
        end
        return {nowMs, total, values}
        """;

    internal const string ContinueScan = Prologue + """

        if redis.call('EXISTS', KEYS[1]) == 0 then return {'expired', nowMs} end
        redis.call('PEXPIRE', KEYS[1], ARGV[3])
        local total = math.floor(redis.call('LLEN', KEYS[1]) / 9)
        local position = tonumber(ARGV[1])
        local count = math.min(tonumber(ARGV[2]), total - position)
        local values = {}
        if count > 0 then
            values = redis.call(
                'LRANGE', KEYS[1], position * 9, (position + count) * 9 - 1)
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
                redis.call('HGET', KEYS[7], key),
                redis.call('HGET', KEYS[8], key),
                redis.call('HGET', KEYS[9], key)
            }
        end
        local lease = redis.call('GET', KEYS[11])
        local leaseGeneration =
            lease and string.match(lease, '([^|]*)|') or nil
        if not leaseGeneration or leaseGeneration ~= ARGV[4] then
            return {'owner-stale', nowMs}
        end
        if not descriptorMatches(
            KEYS[14], ARGV[7], ARGV[3], ARGV[8], ARGV[9]) then
            return {'owner-stale', nowMs}
        end
        if not canIncrement(redis.call('HGET', KEYS[1], 'revision'))
            or not canIncrement(redis.call('HGET', KEYS[1], 'object'))
            or not canIncrement(redis.call('HGET', KEYS[1], 'owner')) then
            return {'exhausted', nowMs}
        end
        local bucket = capacityBucket(
            ARGV[5], ARGV[6], ARGV[7], ARGV[8], ARGV[9])
        if not addCapacity(KEYS[13], bucket, tonumber(ARGV[11])) then
            return {'capacity-exhausted', nowMs}
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
        redis.call('HSET', KEYS[8], key, 'pending')
        redis.call('HSET', KEYS[9], key, ARGV[10])
        redis.call('SADD', KEYS[10], key)
        redis.call('HSET', KEYS[12],
            'status', 'reserved',
            'key', key,
            'version', version,
            'object', objectGeneration,
            'ownerGeneration', ownerGeneration,
            'ownerId', ARGV[3],
            'ownerLease', ARGV[4],
            'targetMesh', ARGV[5],
            'targetRid', ARGV[6],
            'targetLifecycle', ARGV[7],
            'objectKind', ARGV[8],
            'stableType', ARGV[9],
            'allocation', ARGV[10],
            'capacityDelta', ARGV[11],
            'capacityBucket', bucket)
        return {'reserved', nowMs, version, objectGeneration, ownerGeneration}
        """;

    internal const string CommitReservation = Prologue + """

        local status = redis.call('HGET', KEYS[12], 'status')
        if not status then return {'stale', nowMs} end
        if status == 'committed' then
            local key = redis.call('HGET', KEYS[12], 'key')
            return {
                'already', nowMs,
                redis.call('HGET', KEYS[2], key),
                redis.call('HGET', KEYS[3], key),
                redis.call('HGET', KEYS[4], key),
                redis.call('HGET', KEYS[5], key),
                redis.call('HGET', KEYS[6], key),
                redis.call('HGET', KEYS[7], key),
                redis.call('HGET', KEYS[8], key),
                redis.call('HGET', KEYS[9], key)
            }
        end
        if status ~= 'reserved' then return {'stale', nowMs} end
        local key = redis.call('HGET', KEYS[12], 'key')
        if key ~= ARGV[1]
            or redis.call('HGET', KEYS[2], key) ~= ARGV[2]
            or redis.call('HGET', KEYS[12], 'targetMesh') ~= ARGV[4]
            or redis.call('HGET', KEYS[12], 'targetRid') ~= ARGV[5]
            or redis.call('HGET', KEYS[12], 'targetLifecycle') ~= ARGV[6]
            or redis.call('HGET', KEYS[12], 'ownerId') ~= ARGV[7]
            or redis.call('HGET', KEYS[12], 'ownerLease') ~= ARGV[8] then
            return {'stale', nowMs}
        end
        local lease = redis.call('GET', KEYS[11])
        local leaseGeneration =
            lease and string.match(lease, '([^|]*)|') or nil
        if not leaseGeneration or leaseGeneration ~= ARGV[8] then
            return {'stale', nowMs}
        end
        if not descriptorMatches(
            KEYS[15],
            redis.call('HGET', KEYS[12], 'targetLifecycle'),
            ARGV[7],
            redis.call('HGET', KEYS[12], 'objectKind'),
            redis.call('HGET', KEYS[12], 'stableType')) then
            return {'stale', nowMs}
        end
        local capacityDelta =
            tonumber(redis.call('HGET', KEYS[12], 'capacityDelta'))
        local capacityBucket =
            redis.call('HGET', KEYS[12], 'capacityBucket')
        if not capacityDelta or not capacityBucket
            or not canAdjustCapacity(
                KEYS[14], capacityBucket, -capacityDelta)
            or not canAdjustCapacity(
                KEYS[13], capacityBucket, capacityDelta) then
            return {'stale', nowMs}
        end
        if not canIncrement(redis.call('HGET', KEYS[1], 'revision')) then
            return {'exhausted', nowMs}
        end
        addCapacity(KEYS[14], capacityBucket, -capacityDelta)
        addCapacity(KEYS[13], capacityBucket, capacityDelta)
        local version = redis.call('HINCRBY', KEYS[1], 'revision', 1)
        redis.call('HSET', KEYS[2], key, version)
        redis.call('HSET', KEYS[3], key, ARGV[3])
        redis.call('HSET', KEYS[8], key, 'active')
        redis.call('HSET', KEYS[12],
            'status', 'committed', 'finalVersion', version)
        return {
            'committed', nowMs, version,
            redis.call('HGET', KEYS[3], key),
            redis.call('HGET', KEYS[4], key),
            redis.call('HGET', KEYS[5], key),
            redis.call('HGET', KEYS[6], key),
            redis.call('HGET', KEYS[7], key),
            redis.call('HGET', KEYS[8], key),
            redis.call('HGET', KEYS[9], key)
        }
        """;

    internal const string AbortReservation = Prologue + """

        local status = redis.call('HGET', KEYS[12], 'status')
        if not status then return {'stale', nowMs} end
        if status == 'aborted' then return {'already', nowMs} end
        if status ~= 'reserved' then return {'stale', nowMs} end
        local key = redis.call('HGET', KEYS[12], 'key')
        if key ~= ARGV[1]
            or redis.call('HGET', KEYS[2], key) ~= ARGV[2]
            or redis.call('HGET', KEYS[12], 'targetMesh') ~= ARGV[3]
            or redis.call('HGET', KEYS[12], 'targetRid') ~= ARGV[4]
            or redis.call('HGET', KEYS[12], 'targetLifecycle') ~= ARGV[5]
            or redis.call('HGET', KEYS[12], 'ownerId') ~= ARGV[6]
            or redis.call('HGET', KEYS[12], 'ownerLease') ~= ARGV[7] then
            return {'stale', nowMs}
        end
        if not canIncrement(redis.call('HGET', KEYS[1], 'revision')) then
            return {'exhausted', nowMs}
        end
        local capacityDelta =
            tonumber(redis.call('HGET', KEYS[12], 'capacityDelta'))
        local capacityBucket =
            redis.call('HGET', KEYS[12], 'capacityBucket')
        if not capacityDelta or not capacityBucket
            or not canAdjustCapacity(
                KEYS[14], capacityBucket, -capacityDelta) then
            return {'stale', nowMs}
        end
        addCapacity(KEYS[14], capacityBucket, -capacityDelta)
        redis.call('HINCRBY', KEYS[1], 'revision', 1)
        for index = 2, 9 do redis.call('HDEL', KEYS[index], key) end
        redis.call('SREM', KEYS[10], key)
        redis.call('HSET', KEYS[12], 'status', 'aborted')
        return {'aborted', nowMs}
        """;

    internal const string PrepareAggregate = Prologue + """

        local status = redis.call('HGET', KEYS[11], 'status')
        if status == 'prepared' then
            if redis.call('HGET', KEYS[11], 'signature') == ARGV[1] then
                return {'already', nowMs}
            end
            return {'conflict', nowMs}
        end
        if status then return {'stale', nowMs} end
        local targetLeaseValue = redis.call('GET', KEYS[12])
        local targetLeaseGeneration =
            targetLeaseValue and string.match(targetLeaseValue, '([^|]*)|')
            or nil
        if not targetLeaseGeneration
            or targetLeaseGeneration ~= ARGV[3] then
            return {'conflict', nowMs}
        end
        local count = tonumber(ARGV[4])
        local reservationCount = tonumber(ARGV[5])
        local offset = 6
        local newOwnerCount = 0
        for index = 0, count - 1 do
            local key = ARGV[offset + index * 5]
            local expected = ARGV[offset + index * 5 + 1]
            if redis.call('HGET', KEYS[1], key) ~= expected
                or redis.call('HGET', KEYS[7], key) ~= 'active' then
                return {'conflict', nowMs}
            end
            if ARGV[offset + index * 5 + 2] == '2' then
                newOwnerCount = newOwnerCount + 1
            end
        end
        if reservationCount ~= newOwnerCount then
            return {'conflict', nowMs}
        end
        local seen = {}
        local requiredSourceCapacity = {}
        local requiredPendingCapacity = {}
        for index = 1, reservationCount do
            local capacityKey = KEYS[14 + index]
            local reservedKey = redis.call('HGET', capacityKey, 'key')
            if not reservedKey or seen[reservedKey]
                or redis.call('HGET', capacityKey, 'status') ~= 'reserved'
                or redis.call('HGET', capacityKey, 'targetOwner') ~= ARGV[2]
                or redis.call('HGET', capacityKey, 'targetGeneration') ~= ARGV[3]
                or redis.call('HGET', capacityKey, 'sourceAllocation')
                    ~= redis.call('HGET', KEYS[8], reservedKey) then
                return {'conflict', nowMs}
            end
            local targetDescriptorKey =
                redis.call('HGET', capacityKey, 'targetDescriptorKey')
            local sourceBucket =
                redis.call('HGET', capacityKey, 'sourceCapacityBucket')
            local targetBucket =
                redis.call('HGET', capacityKey, 'targetCapacityBucket')
            local delta =
                tonumber(redis.call('HGET', capacityKey, 'capacityDelta'))
            if not targetDescriptorKey
                or not descriptorMatches(
                    targetDescriptorKey,
                    redis.call('HGET', capacityKey, 'targetLifecycle'),
                    ARGV[2],
                    redis.call('HGET', capacityKey, 'objectKind'),
                    redis.call('HGET', capacityKey, 'stableType'))
                or not sourceBucket or not targetBucket or not delta
                then
                return {'conflict', nowMs}
            end
            requireCapacity(requiredSourceCapacity, sourceBucket, delta)
            requireCapacity(requiredPendingCapacity, targetBucket, delta)
            local matched = false
            for participant = 0, count - 1 do
                local base = offset + participant * 5
                if ARGV[base] == reservedKey and ARGV[base + 2] == '2'
                    and ARGV[base + 1]
                    == redis.call('HGET', capacityKey, 'version') then
                    matched = true
                end
            end
            if not matched then return {'conflict', nowMs} end
            seen[reservedKey] = true
        end
        if not hasRequiredCapacity(KEYS[13], requiredSourceCapacity)
            or not hasRequiredCapacity(KEYS[14], requiredPendingCapacity) then
            return {'conflict', nowMs}
        end
        redis.call('HSET', KEYS[11],
            'status', 'prepared',
            'signature', ARGV[1],
            'targetOwner', ARGV[2],
            'targetLease', ARGV[3],
            'count', count,
            'reservationCount', reservationCount)
        for index = 1, reservationCount do
            local capacityKey = KEYS[14 + index]
            redis.call('HSET', KEYS[11],
                'capacityKey:' .. index, capacityKey)
            redis.call('HSET', capacityKey,
                'status', 'prepared',
                'aggregateKey', KEYS[11])
        end
        for index = 0, count - 1 do
            local base = offset + index * 5
            redis.call('HSET', KEYS[11],
                'key:' .. index, ARGV[base],
                'expected:' .. index, ARGV[base + 1],
                'transition:' .. index, ARGV[base + 2],
                'payload:' .. index, ARGV[base + 3],
                'membership:' .. index, ARGV[base + 4])
        end
        return {'prepared', nowMs}
        """;

    internal const string CommitAggregate = Prologue + """

        local status = redis.call('HGET', KEYS[11], 'status')
        if status == 'committed' then return {'already', nowMs} end
        if status ~= 'prepared' then return {'stale', nowMs} end
        local targetLeaseValue = redis.call('GET', KEYS[12])
        local targetLeaseGeneration =
            targetLeaseValue and string.match(targetLeaseValue, '([^|]*)|')
            or nil
        if not targetLeaseGeneration
            or targetLeaseGeneration
                ~= redis.call('HGET', KEYS[11], 'targetLease') then
            return {'stale', nowMs}
        end
        local count = tonumber(redis.call('HGET', KEYS[11], 'count'))
        local ownerChanges = 0
        for index = 0, count - 1 do
            local key = redis.call('HGET', KEYS[11], 'key:' .. index)
            local expected = redis.call('HGET', KEYS[11], 'expected:' .. index)
            if redis.call('HGET', KEYS[1], key) ~= expected
                or redis.call('HGET', KEYS[7], key) ~= 'active' then
                return {'stale', nowMs}
            end
            local transition = redis.call('HGET', KEYS[11], 'transition:' .. index)
            if transition == '2' then
                ownerChanges = ownerChanges + 1
            end
        end
        local reservationCount =
            tonumber(redis.call('HGET', KEYS[11], 'reservationCount') or '0')
        local requiredSourceCapacity = {}
        local requiredPendingCapacity = {}
        local requiredTargetActiveCapacity = {}
        for index = 1, reservationCount do
            local capacityKey =
                redis.call('HGET', KEYS[11], 'capacityKey:' .. index)
            local reservedKey = redis.call('HGET', capacityKey, 'key')
            if redis.call('HGET', capacityKey, 'status') ~= 'prepared'
                or redis.call('HGET', capacityKey, 'aggregateKey') ~= KEYS[11]
                or redis.call('HGET', capacityKey, 'sourceAllocation')
                    ~= redis.call('HGET', KEYS[8], reservedKey) then
                return {'stale', nowMs}
            end
            local targetDescriptorKey =
                redis.call('HGET', capacityKey, 'targetDescriptorKey')
            local sourceBucket =
                redis.call('HGET', capacityKey, 'sourceCapacityBucket')
            local targetBucket =
                redis.call('HGET', capacityKey, 'targetCapacityBucket')
            local delta =
                tonumber(redis.call('HGET', capacityKey, 'capacityDelta'))
            if not targetDescriptorKey
                or not descriptorMatches(
                    targetDescriptorKey,
                    redis.call('HGET', capacityKey, 'targetLifecycle'),
                    redis.call('HGET', KEYS[11], 'targetOwner'),
                    redis.call('HGET', capacityKey, 'objectKind'),
                    redis.call('HGET', capacityKey, 'stableType'))
                or not sourceBucket or not targetBucket or not delta
                then
                return {'stale', nowMs}
            end
            requireCapacity(requiredSourceCapacity, sourceBucket, delta)
            requireCapacity(requiredPendingCapacity, targetBucket, delta)
            requireCapacity(
                requiredTargetActiveCapacity,
                targetBucket,
                delta)
        end
        if not hasRequiredCapacity(KEYS[13], requiredSourceCapacity)
            or not hasRequiredCapacity(KEYS[14], requiredPendingCapacity)
            or not hasCapacityHeadroom(
                KEYS[13], requiredTargetActiveCapacity) then
            return {'stale', nowMs}
        end
        local revision = redis.call('HGET', KEYS[10], 'revision')
        local ownerCounter = redis.call('HGET', KEYS[10], 'owner')
        for index = 1, count do
            if not canIncrement(revision) then return {'exhausted', nowMs} end
            revision = incrementDecimal(revision)
        end
        for index = 1, ownerChanges do
            if not canIncrement(ownerCounter) then return {'exhausted', nowMs} end
            ownerCounter = incrementDecimal(ownerCounter)
        end
        local targetOwner = redis.call('HGET', KEYS[11], 'targetOwner')
        local targetLease = redis.call('HGET', KEYS[11], 'targetLease')
        for index = 0, count - 1 do
            local key = redis.call('HGET', KEYS[11], 'key:' .. index)
            local transition = redis.call('HGET', KEYS[11], 'transition:' .. index)
            local version = redis.call('HINCRBY', KEYS[10], 'revision', 1)
            redis.call('HSET', KEYS[1], key, version)
            redis.call('HSET', KEYS[2], key,
                redis.call('HGET', KEYS[11], 'payload:' .. index))
            redis.call('HSET', KEYS[9], key,
                redis.call('HGET', KEYS[11], 'membership:' .. index))
            if transition == '2' then
                local ownerGeneration = redis.call('HINCRBY', KEYS[10], 'owner', 1)
                redis.call('HSET', KEYS[4], key, ownerGeneration)
                redis.call('HSET', KEYS[5], key, targetOwner)
                redis.call('HSET', KEYS[6], key, targetLease)
                for capacityIndex = 1, reservationCount do
                    local capacityKey =
                        redis.call('HGET', KEYS[11],
                            'capacityKey:' .. capacityIndex)
                    if redis.call('HGET', capacityKey, 'key') == key then
                        local sourceBucket =
                            redis.call('HGET', capacityKey, 'sourceCapacityBucket')
                        local targetBucket =
                            redis.call('HGET', capacityKey, 'targetCapacityBucket')
                        local delta =
                            tonumber(redis.call('HGET', capacityKey, 'capacityDelta'))
                        addCapacity(KEYS[13], sourceBucket, -delta)
                        addCapacity(KEYS[14], targetBucket, -delta)
                        addCapacity(KEYS[13], targetBucket, delta)
                        redis.call('HSET', KEYS[7], key, 'active')
                        redis.call('HSET', KEYS[8], key,
                            redis.call('HGET', capacityKey, 'targetAllocation'))
                    end
                end
            end
        end
        for index = 1, reservationCount do
            local capacityKey =
                redis.call('HGET', KEYS[11], 'capacityKey:' .. index)
            redis.call('HSET', capacityKey, 'status', 'committed')
        end
        redis.call('HSET', KEYS[11], 'status', 'committed')
        return {'committed', nowMs}
        """;

    internal const string AbortAggregate = Prologue + """

        local status = redis.call('HGET', KEYS[1], 'status')
        if status == 'aborted' then return {'already', nowMs} end
        if status ~= 'prepared' then return {'stale', nowMs} end
        local reservationCount =
            tonumber(redis.call('HGET', KEYS[1], 'reservationCount') or '0')
        local requiredPendingCapacity = {}
        for index = 1, reservationCount do
            local capacityKey =
                redis.call('HGET', KEYS[1], 'capacityKey:' .. index)
            local bucket =
                redis.call('HGET', capacityKey, 'targetCapacityBucket')
            local delta =
                tonumber(redis.call('HGET', capacityKey, 'capacityDelta'))
            if redis.call('HGET', capacityKey, 'status') ~= 'prepared'
                or redis.call('HGET', capacityKey, 'aggregateKey') ~= KEYS[1]
                or not bucket or not delta
                then
                return {'stale', nowMs}
            end
            requireCapacity(requiredPendingCapacity, bucket, delta)
        end
        if not hasRequiredCapacity(KEYS[2], requiredPendingCapacity) then
            return {'stale', nowMs}
        end
        for index = 1, reservationCount do
            local capacityKey =
                redis.call('HGET', KEYS[1], 'capacityKey:' .. index)
            local bucket =
                redis.call('HGET', capacityKey, 'targetCapacityBucket')
            local delta =
                tonumber(redis.call('HGET', capacityKey, 'capacityDelta'))
            addCapacity(KEYS[2], bucket, -delta)
            redis.call('HSET', capacityKey, 'status', 'aborted')
        end
        redis.call('HSET', KEYS[1], 'status', 'aborted')
        return {'aborted', nowMs}
        """;
}
