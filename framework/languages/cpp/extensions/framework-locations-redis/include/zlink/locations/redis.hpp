/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework/contracts/locations/stores.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <algorithm>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <tuple>
#include <vector>

#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
#include <sw/redis++/async_redis++.h>
#include <sw/redis++/redis++.h>
#endif

namespace zlink::framework::locations::redis
{

struct redis_location_options_t
{
    std::string connection_string = "127.0.0.1:6379";
    std::string key_prefix = "zlink:locations";
    std::chrono::milliseconds operation_timeout{2000};
};

namespace detail
{

class redis_location_scripts_t
{
  public:
    static constexpr std::string_view write_mesh_node = R"(
if redis.replicate_commands then redis.replicate_commands() end
local time = redis.call('TIME')
local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
local leaseGeneration =
    redis.call('HGET', KEYS[3], 'generation')
if redis.call('PTTL', KEYS[3]) <= 0
    or not leaseGeneration
    or tostring(leaseGeneration) ~= ARGV[4] then
    return {'stale', '0', tostring(nowMs)}
end
local exists = redis.call('EXISTS', KEYS[1]) == 1
if ARGV[1] == 'new' and exists then
    return {'conflict', '0', tostring(nowMs)}
end
if ARGV[1] ~= 'new' and ARGV[1] ~= 'renew' then
    return {'conflict', '0', tostring(nowMs)}
end
if exists then
    local currentLifecycle =
        redis.call('HGET', KEYS[4], 'lifecycleGeneration')
    local currentImmutable =
        redis.call('HGET', KEYS[4], 'immutableDigest')
    local currentOwner = redis.call('HGET', KEYS[1], 'owner')
    local currentLease =
        redis.call('HGET', KEYS[4], 'ownerLeaseGeneration')
    local currentRevision =
        tonumber(redis.call('HGET', KEYS[4], 'descriptorRevision') or '0')
    local revision = tonumber(ARGV[2])
    if currentLifecycle ~= ARGV[5]
        or currentImmutable ~= ARGV[6] then
        return {'conflict', '0', tostring(nowMs)}
    end
    if ARGV[1] == 'renew'
        and (currentOwner ~= ARGV[3]
             or currentLease ~= ARGV[4]
             or revision < currentRevision) then
        return {'stale', '0', tostring(nowMs)}
    end
    if revision == currentRevision then
        if redis.call('HGET', KEYS[1], 'json') ~= ARGV[8] then
            return {'conflict', '0', tostring(nowMs)}
        end
        return {'stored', tostring(revision),
                redis.call('HGET', KEYS[1], 'updatedAtMs') or tostring(nowMs)}
    end
end
redis.call('HSET', KEYS[1],
    'owner', ARGV[3],
    'gen', ARGV[5],
    'json', ARGV[8],
    'updatedAtMs', nowMs,
    'mesh', ARGV[10])
redis.call('HSET', KEYS[4],
    'descriptorKey', ARGV[9],
    'descriptorRevision', ARGV[2],
    'lifecycleGeneration', ARGV[5],
    'ownerId', ARGV[3],
    'ownerLeaseGeneration', ARGV[4],
    'objectRole', ARGV[11],
    'runtimeState', ARGV[12],
    'applicationVersion', ARGV[13],
    'capabilities', ARGV[14],
    'nodeActiveLimit', ARGV[15],
    'nodePendingLimit', ARGV[16],
    'immutableDigest', ARGV[6])
redis.call('SADD', KEYS[2], ARGV[9])
redis.call('SADD', KEYS[5], ARGV[9])
return {'stored', ARGV[2], tostring(nowMs)}
)";

    static constexpr std::string_view remove_mesh_node = R"(
if redis.call('EXISTS', KEYS[1]) == 0
    or redis.call('HGET', KEYS[1], 'owner') ~= ARGV[1]
    or redis.call('HGET', KEYS[3], 'ownerLeaseGeneration') ~= ARGV[2] then
    return 'stale'
end
redis.call('DEL', KEYS[1])
redis.call('DEL', KEYS[3])
redis.call('SREM', KEYS[2], ARGV[3])
redis.call('SREM', KEYS[4], ARGV[3])
return 'stored'
)";

    static constexpr std::string_view write_client_server = R"(
if redis.replicate_commands then redis.replicate_commands() end
local time = redis.call('TIME')
local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
local intent = ARGV[1]
local owner = ARGV[2]
local leaseGeneration = ARGV[3]
local lifecycle = ARGV[4]
local revision = tonumber(ARGV[5])
local immutable = ARGV[6]
local json = ARGV[7]
local lease = redis.call('HMGET', KEYS[4], 'ownerId', 'generation', 'expiresAt')
if lease[1] ~= owner or lease[2] ~= leaseGeneration
    or tonumber(lease[3] or '0') <= nowMs then
    return {'conflict', '0', tostring(nowMs)}
end

local function nextGeneration()
    local current = redis.call('HGET', KEYS[5], 'descriptorGeneration') or '0'
    if current == '9223372036854775807' then return nil end
    redis.call('HINCRBY', KEYS[5], 'descriptorGeneration', 1)
    return redis.call('HGET', KEYS[5], 'descriptorGeneration')
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
        return {'stale', '0', tostring(nowMs)}
    end
    local gen = nextGeneration()
    if not gen then return {'exhausted', '0', tostring(nowMs)} end
    store(gen)
    return {'stored', gen, tostring(nowMs)}
end

local storedOwner = redis.call('HGET', KEYS[2], 'ownerId')
local storedLeaseGeneration =
    redis.call('HGET', KEYS[2], 'ownerLeaseGeneration')
if storedOwner ~= owner or storedLeaseGeneration ~= leaseGeneration then
    if storedOwner ~= ARGV[12]
        or storedLeaseGeneration ~= ARGV[13] then
        return {'stale', '0', tostring(nowMs)}
    end
    local oldLease = redis.call(
        'HMGET', KEYS[7], 'ownerId', 'generation', 'expiresAt')
    if oldLease[1] == storedOwner
        and oldLease[2] == storedLeaseGeneration
        and tonumber(oldLease[3] or '0') > nowMs then
        return {'conflict', '0', tostring(nowMs)}
    end
    if intent ~= 'new' and intent ~= 'takeover' then
        return {'stale', '0', tostring(nowMs)}
    end
    local gen = nextGeneration()
    if not gen then return {'exhausted', '0', tostring(nowMs)} end
    redis.call('SREM', KEYS[8], ARGV[11])
    store(gen)
    return {'stored', gen, tostring(nowMs)}
end

local storedRevision =
    tonumber(redis.call('HGET', KEYS[2], 'descriptorRevision') or '0')
if redis.call('HGET', KEYS[2], 'lifecycleGeneration') == lifecycle
    and redis.call('HGET', KEYS[2], 'immutableDigest') == immutable
    and revision == storedRevision
    and redis.call('HGET', KEYS[1], 'json') == json then
    return {'stored', redis.call('HGET', KEYS[1], 'gen'),
        redis.call('HGET', KEYS[1], 'updatedAtMs')}
end
if redis.call('HGET', KEYS[2], 'lifecycleGeneration') ~= lifecycle
    or redis.call('HGET', KEYS[2], 'immutableDigest') ~= immutable
    or revision <= storedRevision then
    return {'stale', redis.call('HGET', KEYS[1], 'gen'), tostring(nowMs)}
end
local gen = redis.call('HGET', KEYS[1], 'gen')
store(gen)
return {'stored', gen, tostring(nowMs)}
)";

    static constexpr std::string_view remove_client_server = R"(
if redis.replicate_commands then redis.replicate_commands() end
local time = redis.call('TIME')
local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
local owner = redis.call('HGET', KEYS[2], 'ownerId')
local leaseGeneration =
    redis.call('HGET', KEYS[2], 'ownerLeaseGeneration')
if not owner or owner ~= ARGV[1] or leaseGeneration ~= ARGV[2] then
    return {'stale', '0', tostring(nowMs)}
end
local generation = redis.call('HGET', KEYS[1], 'gen') or '0'
redis.call('DEL', KEYS[1], KEYS[2])
redis.call('SREM', KEYS[3], ARGV[3])
redis.call('SREM', KEYS[4], ARGV[3])
redis.call('ZREM', KEYS[7], ARGV[3])
redis.call('INCR', KEYS[5])
redis.call('INCR', KEYS[6])
return {'stored', generation, tostring(nowMs)}
)";

    static constexpr std::string_view write_fanout_publisher = R"(
if redis.replicate_commands then redis.replicate_commands() end
local time = redis.call('TIME')
local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
local intent = ARGV[1]
local owner = ARGV[2]
local leaseGeneration = ARGV[3]
local lifecycle = ARGV[4]
local revision = tonumber(ARGV[5])
local immutable = ARGV[6]
local json = ARGV[7]
local lease = redis.call('HMGET', KEYS[4], 'ownerId', 'generation', 'expiresAt')
if lease[1] ~= owner or lease[2] ~= leaseGeneration
    or tonumber(lease[3] or '0') <= nowMs then
    return {'conflict', '0', tostring(nowMs)}
end

local function nextGeneration()
    local current = redis.call('HGET', KEYS[5], 'descriptorGeneration') or '0'
    if current == '9223372036854775807' then return nil end
    redis.call('HINCRBY', KEYS[5], 'descriptorGeneration', 1)
    return redis.call('HGET', KEYS[5], 'descriptorGeneration')
end

local function store(gen)
    redis.call('HSET', KEYS[1],
        'owner', owner,
        'gen', gen,
        'json', json,
        'updatedAtMs', nowMs,
        'channel', ARGV[9])
    redis.call('HSET', KEYS[2],
        'descriptorKey', ARGV[10],
        'lifecycleGeneration', lifecycle,
        'descriptorRevision', revision,
        'immutableDigest', immutable,
        'ownerId', owner,
        'ownerLeaseGeneration', leaseGeneration,
        'runtimeState', ARGV[8])
    redis.call('SADD', KEYS[3], ARGV[10])
    redis.call('SADD', KEYS[6], ARGV[10])
    redis.call('ZADD', KEYS[11], 0, ARGV[10])
    redis.call('INCR', KEYS[9])
    redis.call('INCR', KEYS[10])
end

if redis.call('EXISTS', KEYS[1]) == 0 then
    if intent ~= 'new' and intent ~= 'takeover' then
        return {'stale', '0', tostring(nowMs)}
    end
    local gen = nextGeneration()
    if not gen then return {'exhausted', '0', tostring(nowMs)} end
    store(gen)
    return {'stored', gen, tostring(nowMs)}
end

local storedOwner = redis.call('HGET', KEYS[2], 'ownerId')
local storedLeaseGeneration =
    redis.call('HGET', KEYS[2], 'ownerLeaseGeneration')
if storedOwner ~= owner or storedLeaseGeneration ~= leaseGeneration then
    if storedOwner ~= ARGV[11]
        or storedLeaseGeneration ~= ARGV[12] then
        return {'stale', '0', tostring(nowMs)}
    end
    local oldLease = redis.call(
        'HMGET', KEYS[7], 'ownerId', 'generation', 'expiresAt')
    if oldLease[1] == storedOwner
        and oldLease[2] == storedLeaseGeneration
        and tonumber(oldLease[3] or '0') > nowMs then
        return {'conflict', '0', tostring(nowMs)}
    end
    if intent ~= 'new' and intent ~= 'takeover' then
        return {'stale', '0', tostring(nowMs)}
    end
    local gen = nextGeneration()
    if not gen then return {'exhausted', '0', tostring(nowMs)} end
    redis.call('SREM', KEYS[8], ARGV[10])
    store(gen)
    return {'stored', gen, tostring(nowMs)}
end

local storedRevision =
    tonumber(redis.call('HGET', KEYS[2], 'descriptorRevision') or '0')
if redis.call('HGET', KEYS[2], 'lifecycleGeneration') == lifecycle
    and redis.call('HGET', KEYS[2], 'immutableDigest') == immutable
    and revision == storedRevision
    and redis.call('HGET', KEYS[1], 'json') == json then
    return {'stored', redis.call('HGET', KEYS[1], 'gen'),
        redis.call('HGET', KEYS[1], 'updatedAtMs')}
end
if redis.call('HGET', KEYS[2], 'lifecycleGeneration') ~= lifecycle
    or redis.call('HGET', KEYS[2], 'immutableDigest') ~= immutable then
    return {'conflict', redis.call('HGET', KEYS[1], 'gen'), tostring(nowMs)}
end
if revision == storedRevision then
    return {'conflict', redis.call('HGET', KEYS[1], 'gen'), tostring(nowMs)}
end
if revision < storedRevision then
    return {'stale', redis.call('HGET', KEYS[1], 'gen'), tostring(nowMs)}
end
local gen = redis.call('HGET', KEYS[1], 'gen')
store(gen)
return {'stored', gen, tostring(nowMs)}
)";

    static constexpr std::string_view remove_fanout_publisher = R"(
if redis.replicate_commands then redis.replicate_commands() end
local time = redis.call('TIME')
local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
local owner = redis.call('HGET', KEYS[2], 'ownerId')
local leaseGeneration =
    redis.call('HGET', KEYS[2], 'ownerLeaseGeneration')
if not owner or owner ~= ARGV[1] or leaseGeneration ~= ARGV[2] then
    return {'stale', '0', tostring(nowMs)}
end
local generation = redis.call('HGET', KEYS[1], 'gen') or '0'
redis.call('DEL', KEYS[1], KEYS[2])
redis.call('SREM', KEYS[3], ARGV[3])
redis.call('SREM', KEYS[4], ARGV[3])
redis.call('ZREM', KEYS[7], ARGV[3])
redis.call('INCR', KEYS[5])
redis.call('INCR', KEYS[6])
return {'stored', generation, tostring(nowMs)}
)";

    static constexpr std::string_view write = R"(
if redis.replicate_commands then redis.replicate_commands() end
local time = redis.call('TIME')
local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)

local intent = ARGV[1]
local owner = ARGV[2]
local currentOwner = redis.call('HGET', KEYS[1], 'owner')
if (currentOwner or '') ~= ARGV[12] then
    return {'stale', 0, nowMs}
end

local function bumpStamps()
    redis.call('INCR', KEYS[7])
    if ARGV[10] == '1' then redis.call('INCR', KEYS[8]) end
end

local function storeRow(gen)
    redis.call('HSET', KEYS[1],
        'owner', owner, 'gen', gen, 'json', ARGV[4], 'updatedAtMs', nowMs)
    if ARGV[10] == '1' then
        redis.call('HSET', KEYS[1], 'mesh', ARGV[11])
    end
    redis.call('SADD', KEYS[3], ARGV[5])
    redis.call('SADD', KEYS[5], ARGV[5])
    if currentOwner and currentOwner ~= owner then
        redis.call('SREM', KEYS[6], ARGV[5])
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
    local gen = tonumber(ARGV[3])
    redis.call('HSET', KEYS[1], 'json', ARGV[4], 'updatedAtMs', nowMs)
    bumpStamps()
    return {'stored', gen, nowMs}
end
return {'stale', 0, nowMs}
)";

    static constexpr std::string_view remove = R"(
if redis.replicate_commands then redis.replicate_commands() end
local time = redis.call('TIME')
local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)

local currentOwner = redis.call('HGET', KEYS[1], 'owner')
if not currentOwner
    or currentOwner ~= ARGV[1]
    or tonumber(redis.call('HGET', KEYS[1], 'gen')) ~= tonumber(ARGV[2]) then
    return {'stale', 0, nowMs}
end

redis.call('DEL', KEYS[1])
redis.call('SREM', KEYS[2], ARGV[3])
redis.call('SREM', KEYS[3], ARGV[3])
redis.call('INCR', KEYS[4])
if ARGV[4] == '1' then redis.call('INCR', KEYS[5]) end
return {'stored', tonumber(ARGV[2]), nowMs}
)";

    static constexpr std::string_view claim_owner_lease = R"(
if redis.replicate_commands then redis.replicate_commands() end
local time = redis.call('TIME')
local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
if redis.call('EXISTS', KEYS[1]) == 1 then
    return {'conflict', 0, nowMs, 0}
end
local current = tonumber(
    redis.call('HGET', KEYS[2], 'leaseGeneration') or '0')
if current >= 9223372036854775807 then
    return {'exhausted', 0, nowMs, 0}
end
local generation =
    redis.call('HINCRBY', KEYS[2], 'leaseGeneration', 1)
local expiresAtMs = nowMs + tonumber(ARGV[2])
redis.call('HSET', KEYS[1],
    'ownerId', ARGV[1],
    'generation', generation,
    'expiresAt', expiresAtMs)
redis.call('PEXPIRE', KEYS[1], ARGV[2])
return {'claimed', generation, nowMs, expiresAtMs}
)";

    static constexpr std::string_view read_owner_lease = R"(
if redis.replicate_commands then redis.replicate_commands() end
local time = redis.call('TIME')
local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
local pttl = redis.call('PTTL', KEYS[1])
local generation = redis.call('HGET', KEYS[1], 'generation')
local expiresAtMs = redis.call('HGET', KEYS[1], 'expiresAt')
if not generation or not expiresAtMs or pttl <= 0 then
    return {'missing', 0, nowMs, 0}
end
return {'found', tonumber(generation), nowMs, tonumber(expiresAtMs)}
)";

    static constexpr std::string_view renew_owner_lease_exact = R"(
if redis.replicate_commands then redis.replicate_commands() end
local time = redis.call('TIME')
local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
local pttl = redis.call('PTTL', KEYS[1])
local generation = redis.call('HGET', KEYS[1], 'generation')
if not generation or pttl <= 0 then
    return {'stale', nowMs, 0}
end
if tonumber(generation) ~= tonumber(ARGV[1]) then
    return {'stale', nowMs, 0}
end
local expiresAtMs = nowMs + tonumber(ARGV[2])
redis.call('HSET', KEYS[1], 'expiresAt', expiresAtMs)
redis.call('PEXPIRE', KEYS[1], ARGV[2])
return {'renewed', nowMs, expiresAtMs}
)";

    static constexpr std::string_view release_owner_lease = R"(
if redis.replicate_commands then redis.replicate_commands() end
local time = redis.call('TIME')
local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
local pttl = redis.call('PTTL', KEYS[1])
local generation = redis.call('HGET', KEYS[1], 'generation')
if not generation or pttl <= 0 then
    return {'stale', nowMs}
end
if tonumber(generation) ~= tonumber(ARGV[1]) then
    return {'stale', nowMs}
end
redis.call('DEL', KEYS[1])
return {'released', nowMs}
)";

    static constexpr std::string_view read_authority = R"(
if redis.replicate_commands then redis.replicate_commands() end
local time = redis.call('TIME')
local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
if redis.call('EXISTS', KEYS[1]) == 0 then
    return {'missing', '', '', '0', '0', '', '0',
        '', '0', '', '', '', '0', '0', '', '', '', '0',
        tostring(nowMs)}
end
return {'found',
    redis.call('HGET', KEYS[1], 'storeVersion') or '',
    redis.call('HGET', KEYS[1], 'payload') or '',
    redis.call('HGET', KEYS[1], 'objectGeneration') or '0',
    redis.call('HGET', KEYS[1], 'authorityOwnerGeneration') or '0',
    redis.call('HGET', KEYS[1], 'ownerId') or '',
    redis.call('HGET', KEYS[1], 'ownerLeaseGeneration') or '0',
    redis.call('HGET', KEYS[1], 'allocationState') or '',
    redis.call('HGET', KEYS[1], 'objectKind') or '0',
    redis.call('HGET', KEYS[1], 'stableType') or '',
    redis.call('HGET', KEYS[1], 'descriptorKey') or '',
    '',
    redis.call('HGET', KEYS[1], 'descriptorLifecycleGeneration') or '0',
    redis.call('HGET', KEYS[1], 'capacityDelta') or '0',
    redis.call('HGET', KEYS[1], 'pendingCreationReservationId') or '',
    redis.call('HGET', KEYS[1], 'pendingCreationReference') or '',
    redis.call('HGET', KEYS[1], 'pendingCreationSha256') or '',
    redis.call('HGET', KEYS[1], 'pendingCreationEncodedSize') or '0',
    tostring(nowMs)}
)";

    static constexpr std::string_view compare_exchange_authority = R"(
if redis.replicate_commands then redis.replicate_commands() end
local time = redis.call('TIME')
local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
local maxValue = '9223372036854775807'

local function atMax(key, field)
    local value = redis.call('HGET', key, field) or '0'
    return string.len(value) > string.len(maxValue)
        or (string.len(value) == string.len(maxValue) and value >= maxValue)
end
local function revisionHex(value)
    local digits = tostring(value)
    local out = ''
    local hex = '0123456789abcdef'
    repeat
        local quotient = ''
        local carry = 0
        for index = 1, string.len(digits) do
            local current =
                carry * 10 + tonumber(string.sub(digits, index, index))
            local digit = math.floor(current / 16)
            carry = current % 16
            if quotient ~= '' or digit ~= 0 then
                quotient = quotient .. tostring(digit)
            end
        end
        out = string.sub(hex, carry + 1, carry + 1) .. out
        digits = quotient == '' and '0' or quotient
    until digits == '0'
    return string.rep('0', 16 - string.len(out)) .. out
end

local function liveLease(key, ownerId, generation)
    if ownerId == '' or generation == '' or redis.call('PTTL', key) <= 0 then
        return false
    end
    return redis.call('HGET', key, 'ownerId') == ownerId
        and redis.call('HGET', key, 'generation') == tostring(generation)
end

local function findCapability(key, objectKind, stableType)
    for _, candidate in ipairs(cjson.decode(
        redis.call('HGET', key, 'capabilities') or '[]')) do
        if candidate.objectKind == objectKind
            and candidate.stableType == stableType then
            return candidate
        end
    end
    return nil
end
local function limitOr(value, fallback)
    if value == nil or value == cjson.null then return fallback end
    return tonumber(value)
end

local function snapshot(status)
    if redis.call('EXISTS', KEYS[1]) == 0 then
        return {status, 'missing', '', '', '0', '0', '', '0',
            '', '0', '', '', '', '0', '0', tostring(nowMs)}
    end
    return {status, 'found',
        redis.call('HGET', KEYS[1], 'storeVersion') or '',
        redis.call('HGET', KEYS[1], 'payload') or '',
        redis.call('HGET', KEYS[1], 'objectGeneration') or '0',
        redis.call('HGET', KEYS[1], 'authorityOwnerGeneration') or '0',
        redis.call('HGET', KEYS[1], 'ownerId') or '',
        redis.call('HGET', KEYS[1], 'ownerLeaseGeneration') or '0',
        redis.call('HGET', KEYS[1], 'allocationState') or '',
        redis.call('HGET', KEYS[1], 'objectKind') or '0',
        redis.call('HGET', KEYS[1], 'stableType') or '',
        redis.call('HGET', KEYS[1], 'descriptorKey') or '',
        '',
        redis.call('HGET', KEYS[1], 'descriptorLifecycleGeneration') or '0',
        redis.call('HGET', KEYS[1], 'capacityDelta') or '0',
        tostring(nowMs)}
end

local function archiveCurrent()
    local fields = {
        'authorityKey', 'storeVersion', 'payload',
        'objectGeneration', 'authorityOwnerGeneration',
        'ownerId', 'ownerLeaseGeneration', 'allocationState',
        'objectKind', 'stableType', 'descriptorKey',
        'descriptorLifecycleGeneration', 'capacityDelta',
        'pendingCreationReservationId', 'pendingCreationReference',
        'pendingCreationSha256', 'pendingCreationEncodedSize'
    }
    for _, field in ipairs(fields) do
        local value = redis.call('HGET', KEYS[1], field)
        if value then
            redis.call(
                'HSET', KEYS[16],
                ARGV[15] .. ':' .. field, value)
        end
    end
    redis.call('HSET', KEYS[16], ARGV[15] .. ':deleted', '0')
    redis.call('ZADD', KEYS[17], 0, ARGV[15])
    local membership =
        redis.call('HGET', KEYS[19], ARGV[10])
    if membership then
        redis.call(
            'HSET', KEYS[20],
            ARGV[15], membership)
        redis.call('ZADD', KEYS[21], 0, ARGV[15])
    end
end

local exists = redis.call('EXISTS', KEYS[1]) == 1
local currentVersion = exists
    and (redis.call('HGET', KEYS[1], 'storeVersion') or '') or ''
local expectationMatches =
    (ARGV[2] == 'missing' and not exists)
    or (ARGV[2] == 'found' and exists and currentVersion == ARGV[3])
if not expectationMatches then return snapshot('conflict') end

if ARGV[4] == 'delete' then
    local ownerId = redis.call('HGET', KEYS[1], 'ownerId') or ''
    local leaseGeneration =
        redis.call('HGET', KEYS[1], 'ownerLeaseGeneration') or ''
	    if redis.call('HGET', KEYS[1], 'allocationState') ~= 'active'
	        or not liveLease(KEYS[6], ownerId, leaseGeneration) then
	        return snapshot('conflict')
	    end
	    local delta = tonumber(redis.call('HGET', KEYS[1], 'capacityDelta') or '0')
	    local nodeActive =
	        tonumber(redis.call('HGET', KEYS[9], ARGV[11]) or '0')
	    local typeActive =
	        tonumber(redis.call('HGET', KEYS[10], ARGV[12]) or '0')
	    if nodeActive < delta or typeActive < delta then
	        return snapshot('conflict')
	    end
	    if atMax(KEYS[3], 'storeRevision') then
	        return snapshot('exhausted')
	    end
	    archiveCurrent()
	    redis.call('HINCRBY', KEYS[3], 'storeRevision', 1)
	    local version =
	        redis.call('HGET', KEYS[3], 'storeRevision')
	    redis.call('HINCRBY', KEYS[9], ARGV[11], -delta)
	    redis.call('HINCRBY', KEYS[10], ARGV[12], -delta)
    redis.call('DEL', KEYS[1])
    redis.call('HDEL', KEYS[19], ARGV[10])
    redis.call('HSET', KEYS[18], ARGV[10], version)
    local deletedRevision = revisionHex(version)
    redis.call('HSET', KEYS[16],
        deletedRevision .. ':deleted', '1',
        deletedRevision .. ':authorityKey', ARGV[1])
    redis.call('ZADD', KEYS[17], 0, deletedRevision)
    return {'deleted', 'missing', tostring(version), '', '0', '0', '', '0',
        '', '0', '', '', '', '0', '0', tostring(nowMs)}
end

local transition = ARGV[5]
local ownerId = ''
local leaseGeneration = ''
local objectGeneration = '0'
local ownerGeneration = '0'
if transition == 'new_owner' then
    if not exists
        or redis.call('HGET', KEYS[1], 'allocationState') ~= 'active'
        or not liveLease(KEYS[7], ARGV[7], ARGV[8]) then
        return snapshot('conflict')
    end
    local currentOwner = redis.call('HGET', KEYS[1], 'ownerId') or ''
    local currentLease =
        redis.call('HGET', KEYS[1], 'ownerLeaseGeneration') or ''
    local targetCapability = findCapability(
        KEYS[15],
        redis.call('HGET', KEYS[8], 'objectKind'),
        redis.call('HGET', KEYS[8], 'stableType'))
    if redis.call('EXISTS', KEYS[8]) == 0
        or redis.call('HGET', KEYS[8], 'status') ~= 'reserved'
        or redis.call('HGET', KEYS[8], 'authorityKey') ~= ARGV[1]
        or redis.call('HGET', KEYS[8], 'expectedVersion') ~= currentVersion
        or redis.call('HGET', KEYS[8], 'sourceOwnerId') ~= currentOwner
        or redis.call('HGET', KEYS[8], 'sourceLeaseGeneration') ~= currentLease
        or redis.call('HGET', KEYS[8], 'objectKind')
            ~= redis.call('HGET', KEYS[1], 'objectKind')
        or redis.call('HGET', KEYS[8], 'stableType')
            ~= redis.call('HGET', KEYS[1], 'stableType')
        or redis.call('HGET', KEYS[8], 'sourceDescriptorKey')
            ~= redis.call('HGET', KEYS[1], 'descriptorKey')
        or redis.call('HGET', KEYS[8], 'sourceLifecycleGeneration')
            ~= redis.call('HGET', KEYS[1], 'descriptorLifecycleGeneration')
        or redis.call('HGET', KEYS[8], 'capacityDelta')
            ~= redis.call('HGET', KEYS[1], 'capacityDelta')
        or redis.call('HGET', KEYS[8], 'targetOwnerId') ~= ARGV[7]
        or redis.call('HGET', KEYS[8], 'targetLeaseGeneration') ~= ARGV[8]
        or redis.call('EXISTS', KEYS[15]) == 0
        or redis.call('HGET', KEYS[15], 'descriptorKey')
            ~= redis.call('HGET', KEYS[8], 'targetDescriptorKey')
        or redis.call('HGET', KEYS[15], 'lifecycleGeneration')
            ~= redis.call('HGET', KEYS[8], 'targetLifecycleGeneration')
        or redis.call('HGET', KEYS[15], 'ownerId') ~= ARGV[7]
        or redis.call('HGET', KEYS[15], 'ownerLeaseGeneration') ~= ARGV[8]
        or redis.call('HGET', KEYS[15], 'runtimeState') ~= '1'
        or redis.call('HGET', KEYS[15], 'objectRole') ~= '2'
        or not targetCapability then
	        return snapshot('conflict')
	    end
	    local delta =
	        tonumber(redis.call('HGET', KEYS[8], 'capacityDelta') or '0')
	    local sourceNodeActive =
	        tonumber(redis.call('HGET', KEYS[9], ARGV[11]) or '0')
	    local sourceTypeActive =
	        tonumber(redis.call('HGET', KEYS[10], ARGV[12]) or '0')
	    local targetNodePending =
	        tonumber(redis.call('HGET', KEYS[11], ARGV[13]) or '0')
	    local targetTypePending =
	        tonumber(redis.call('HGET', KEYS[12], ARGV[14]) or '0')
	    local targetNodeActive =
	        tonumber(redis.call('HGET', KEYS[13], ARGV[13]) or '0')
	    local targetTypeActive =
	        tonumber(redis.call('HGET', KEYS[14], ARGV[14]) or '0')
	    local targetNodeActiveLimit = tonumber(redis.call(
	        'HGET', KEYS[15], 'nodeActiveLimit') or '0')
	    local targetTypeActiveLimit =
	        limitOr(targetCapability.activeLimit, targetNodeActiveLimit)
	    if sourceNodeActive < delta or sourceTypeActive < delta
	        or targetNodePending < delta or targetTypePending < delta
	        or delta > targetNodeActiveLimit
	        or targetNodeActive > targetNodeActiveLimit - delta
	        or delta > targetTypeActiveLimit
	        or targetTypeActive > targetTypeActiveLimit - delta then
	        return snapshot('conflict')
	    end
    if atMax(KEYS[3], 'storeRevision')
        or atMax(KEYS[5], 'authorityOwnerGeneration') then
        return snapshot('exhausted')
    end
    archiveCurrent()
    objectGeneration =
        redis.call('HGET', KEYS[1], 'objectGeneration') or '0'
    redis.call(
        'HINCRBY', KEYS[5], 'authorityOwnerGeneration', 1)
    ownerGeneration =
        redis.call('HGET', KEYS[5], 'authorityOwnerGeneration')
    ownerId = ARGV[7]
    leaseGeneration = ARGV[8]
else
    if not exists
        or redis.call('HGET', KEYS[1], 'allocationState') ~= 'active' then
        return snapshot('conflict')
    end
    ownerId = redis.call('HGET', KEYS[1], 'ownerId') or ''
    leaseGeneration =
        redis.call('HGET', KEYS[1], 'ownerLeaseGeneration') or ''
    if not liveLease(KEYS[6], ownerId, leaseGeneration) then
        return snapshot('conflict')
    end
    if atMax(KEYS[3], 'storeRevision') then
        return snapshot('exhausted')
    end
    archiveCurrent()
    objectGeneration =
        redis.call('HGET', KEYS[1], 'objectGeneration') or '0'
    ownerGeneration =
        redis.call('HGET', KEYS[1], 'authorityOwnerGeneration') or '0'
end

redis.call('HINCRBY', KEYS[3], 'storeRevision', 1)
local version = redis.call('HGET', KEYS[3], 'storeRevision')
redis.call('HSET', KEYS[1],
    'authorityKey', ARGV[1],
    'storeVersion', version,
    'payload', ARGV[6],
    'objectGeneration', objectGeneration,
    'authorityOwnerGeneration', ownerGeneration,
    'ownerId', ownerId,
    'ownerLeaseGeneration', leaseGeneration)
redis.call('ZADD', KEYS[2], 0, ARGV[10])
if transition == 'new_owner' then
    local delta = tonumber(redis.call('HGET', KEYS[8], 'capacityDelta') or '0')
    redis.call('HINCRBY', KEYS[9], ARGV[11], -delta)
    redis.call('HINCRBY', KEYS[10], ARGV[12], -delta)
    redis.call('HINCRBY', KEYS[11], ARGV[13], -delta)
    redis.call('HINCRBY', KEYS[12], ARGV[14], -delta)
    redis.call('HINCRBY', KEYS[13], ARGV[13], delta)
    redis.call('HINCRBY', KEYS[14], ARGV[14], delta)
    redis.call('HSET', KEYS[8], 'status', 'committed')
    redis.call('HSET', KEYS[1],
        'allocationState', 'active',
        'objectKind', redis.call('HGET', KEYS[8], 'objectKind'),
        'stableType', redis.call('HGET', KEYS[8], 'stableType'),
        'descriptorKey', redis.call('HGET', KEYS[8], 'targetDescriptorKey'),
        'descriptorLifecycleGeneration',
            redis.call('HGET', KEYS[8], 'targetLifecycleGeneration'),
        'capacityDelta', redis.call('HGET', KEYS[8], 'capacityDelta'))
end
return {'stored', 'found', version, ARGV[6],
    objectGeneration, ownerGeneration, ownerId,
    tostring(leaseGeneration),
    redis.call('HGET', KEYS[1], 'allocationState') or '',
    redis.call('HGET', KEYS[1], 'objectKind') or '0',
    redis.call('HGET', KEYS[1], 'stableType') or '',
    redis.call('HGET', KEYS[1], 'descriptorKey') or '',
    '',
    redis.call('HGET', KEYS[1], 'descriptorLifecycleGeneration') or '0',
    redis.call('HGET', KEYS[1], 'capacityDelta') or '0',
    tostring(nowMs)}
)";

    static constexpr std::string_view reserve_relocation_capacity = R"(
if redis.replicate_commands then redis.replicate_commands() end
local function liveLease(key, generation)
    if redis.call('PTTL', key) <= 0 then return false end
    return redis.call('HGET', key, 'generation') == tostring(generation)
end
local function findCapability(key, objectKind, stableType)
    for _, candidate in ipairs(cjson.decode(
        redis.call('HGET', key, 'capabilities') or '[]')) do
        if candidate.objectKind == objectKind
            and candidate.stableType == stableType then
            return candidate
        end
    end
    return nil
end
local function limitOr(value, fallback)
    if value == nil or value == cjson.null then return fallback end
    return tonumber(value)
end

if redis.call('EXISTS', KEYS[4]) == 1 then
    if redis.call('HGET', KEYS[4], 'requestFingerprint') == ARGV[1] then
        return {'already', redis.call('HGET', KEYS[4], 'fence') or ''}
    end
    return {'conflict', ''}
end
if redis.call('EXISTS', KEYS[1]) == 0
    or redis.call('HGET', KEYS[1], 'storeVersion') ~= ARGV[4]
    or redis.call('HGET', KEYS[1], 'ownerId') ~= ARGV[8]
    or redis.call('HGET', KEYS[1], 'ownerLeaseGeneration') ~= ARGV[9]
    or redis.call('HGET', KEYS[1], 'allocationState') ~= 'active'
    or redis.call('HGET', KEYS[1], 'objectKind') ~= ARGV[5]
    or redis.call('HGET', KEYS[1], 'stableType') ~= ARGV[6]
    or redis.call('HGET', KEYS[1], 'descriptorKey') ~= ARGV[22]
    or redis.call('HGET', KEYS[1], 'descriptorLifecycleGeneration') ~= ARGV[7]
    or redis.call('HGET', KEYS[1], 'capacityDelta') ~= ARGV[13] then
    return {'conflict', ''}
end
local capability = findCapability(KEYS[9], ARGV[5], ARGV[6])
if not liveLease(KEYS[3], ARGV[12])
    or redis.call('EXISTS', KEYS[9]) == 0
    or redis.call('HGET', KEYS[9], 'descriptorKey') ~= ARGV[23]
    or redis.call('HGET', KEYS[9], 'lifecycleGeneration') ~= ARGV[10]
    or redis.call('HGET', KEYS[9], 'ownerId') ~= ARGV[11]
    or redis.call('HGET', KEYS[9], 'ownerLeaseGeneration') ~= ARGV[12]
    or redis.call('HGET', KEYS[9], 'runtimeState') ~= '1'
    or redis.call('HGET', KEYS[9], 'objectRole') ~= '2'
    or not capability then
    return {'unavailable', ''}
end
local nodePending =
    tonumber(redis.call('HGET', KEYS[5], ARGV[20]) or '0')
local typePending =
    tonumber(redis.call('HGET', KEYS[6], ARGV[21]) or '0')
local nodeActive =
    tonumber(redis.call('HGET', KEYS[7], ARGV[20]) or '0')
local typeActive =
    tonumber(redis.call('HGET', KEYS[8], ARGV[21]) or '0')
local delta = tonumber(ARGV[13])
local nodePendingLimit =
    tonumber(redis.call('HGET', KEYS[9], 'nodePendingLimit') or '0')
local nodeActiveLimit =
    tonumber(redis.call('HGET', KEYS[9], 'nodeActiveLimit') or '0')
local typePendingLimit =
    limitOr(capability.pendingLimit, nodePendingLimit)
local typeActiveLimit =
    limitOr(capability.activeLimit, nodeActiveLimit)
if delta <= 0
    or delta > nodePendingLimit
    or nodePending > nodePendingLimit - delta
    or delta > typePendingLimit
    or typePending > typePendingLimit - delta
    or delta > nodeActiveLimit
    or nodeActive > nodeActiveLimit - delta
    or delta > typeActiveLimit
    or typeActive > typeActiveLimit - delta then
    return {'exhausted', ''}
end
redis.call('HINCRBY', KEYS[5], ARGV[20], delta)
redis.call('HINCRBY', KEYS[6], ARGV[21], delta)
redis.call('HSET', KEYS[4],
    'status', 'reserved',
    'fence', ARGV[2],
    'requestFingerprint', ARGV[1],
    'authorityKey', ARGV[3],
    'expectedVersion', ARGV[4],
    'objectKind', ARGV[5],
    'stableType', ARGV[6],
    'sourceOwnerId', ARGV[8],
    'sourceLeaseGeneration', ARGV[9],
    'sourceMesh', ARGV[14],
    'sourceNode', ARGV[15],
    'sourceDescriptorKey', ARGV[22],
    'sourceLifecycleGeneration', ARGV[7],
    'targetOwnerId', ARGV[11],
    'targetLeaseGeneration', ARGV[12],
    'targetMesh', ARGV[16],
	    'targetNode', ARGV[17],
	    'targetDescriptorKey', ARGV[23],
	    'targetLifecycleGeneration', ARGV[10],
	    'capacityDelta', ARGV[13])
return {'reserved', ARGV[2]}
)";

    static constexpr std::string_view abort_relocation_capacity = R"(
if redis.replicate_commands then redis.replicate_commands() end
if redis.call('EXISTS', KEYS[1]) == 0 then return 'stale' end
local status = redis.call('HGET', KEYS[1], 'status')
if status == 'committed' then return 'already_committed' end
if status == 'aborted' then return 'already_aborted' end
if status ~= 'reserved' then return 'stale' end
local delta = tonumber(redis.call('HGET', KEYS[1], 'capacityDelta') or '0')
local nodePending =
    tonumber(redis.call('HGET', KEYS[2], ARGV[1]) or '0')
local typePending =
    tonumber(redis.call('HGET', KEYS[3], ARGV[2]) or '0')
if nodePending < delta or typePending < delta then
    return 'stale'
end
redis.call('HINCRBY', KEYS[2], ARGV[1], -delta)
redis.call('HINCRBY', KEYS[3], ARGV[2], -delta)
redis.call('HSET', KEYS[1], 'status', 'aborted')
return 'aborted'
)";

    static constexpr std::string_view reserve_object = R"(
if redis.replicate_commands then redis.replicate_commands() end
local time = redis.call('TIME')
local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
local maxValue = '9223372036854775807'
local function atMax(key, field)
    local value = redis.call('HGET', key, field) or '0'
    return string.len(value) > string.len(maxValue)
        or (string.len(value) == string.len(maxValue) and value >= maxValue)
end
local function liveLease(key, generation)
    if redis.call('PTTL', key) <= 0 then return false end
    return redis.call('HGET', key, 'generation') == tostring(generation)
end
local function findCapability(key, objectKind, stableType)
    for _, candidate in ipairs(cjson.decode(
        redis.call('HGET', key, 'capabilities') or '[]')) do
        if candidate.objectKind == objectKind
            and candidate.stableType == stableType then
            return candidate
        end
    end
    return nil
end
local function limitOr(value, fallback)
    if value == nil or value == cjson.null then
        return fallback
    end
    return tonumber(value)
end
local function snapshot(status)
    return {status,
        redis.call('HGET', KEYS[1], 'storeVersion') or '',
        redis.call('HGET', KEYS[1], 'payload') or '',
        redis.call('HGET', KEYS[1], 'objectGeneration') or '0',
        redis.call('HGET', KEYS[1], 'authorityOwnerGeneration') or '0',
        redis.call('HGET', KEYS[1], 'ownerId') or '',
        redis.call('HGET', KEYS[1], 'ownerLeaseGeneration') or '0',
        redis.call('HGET', KEYS[1], 'allocationState') or '',
        redis.call('HGET', KEYS[1], 'objectKind') or '0',
        redis.call('HGET', KEYS[1], 'stableType') or '',
        redis.call('HGET', KEYS[1], 'descriptorKey') or '',
        '',
        redis.call('HGET', KEYS[1], 'descriptorLifecycleGeneration') or '0',
        redis.call('HGET', KEYS[1], 'capacityDelta') or '0',
        redis.call('HGET', KEYS[1], 'pendingCreationReservationId') or '',
        redis.call('HGET', KEYS[1], 'pendingCreationReference') or '',
        redis.call('HGET', KEYS[1], 'pendingCreationSha256') or '',
        redis.call('HGET', KEYS[1], 'pendingCreationEncodedSize') or '0',
        tostring(nowMs)}
end
if redis.call('EXISTS', KEYS[1]) == 1 then
    if redis.call('HGET', KEYS[1], 'stableType') ~= ARGV[3] then
        return snapshot('type_mismatch')
    end
    if redis.call('HGET', KEYS[1], 'allocationState') == 'pending' then
        return snapshot('conflict')
    end
    return snapshot('already_exists')
end
local capability = findCapability(KEYS[12], ARGV[11], ARGV[3])
local profileFound = ARGV[13] == ''
if capability and not profileFound then
    for _, profile in ipairs(capability.placementProfiles or {}) do
        if profile == ARGV[13] then
            profileFound = true
            break
        end
    end
end
if not liveLease(KEYS[6], ARGV[5])
    or redis.call('EXISTS', KEYS[12]) == 0
    or redis.call('HGET', KEYS[12], 'descriptorKey') ~= ARGV[19]
    or redis.call('HGET', KEYS[12], 'lifecycleGeneration') ~= ARGV[10]
    or redis.call('HGET', KEYS[12], 'ownerId') ~= ARGV[4]
    or redis.call('HGET', KEYS[12], 'ownerLeaseGeneration') ~= ARGV[5]
    or redis.call('HGET', KEYS[12], 'runtimeState') ~= '1'
    or redis.call('HGET', KEYS[12], 'objectRole') ~= '2'
    or not capability
    or not profileFound then
    return {'conflict', '', '', '0', '0', '', '0',
        '', '0', '', '', '', '0', '0', '', '', '', '0',
        tostring(nowMs)}
end
local nodePending =
    tonumber(redis.call('HGET', KEYS[7], ARGV[17]) or '0')
local typePending =
    tonumber(redis.call('HGET', KEYS[8], ARGV[18]) or '0')
local nodeActive =
    tonumber(redis.call('HGET', KEYS[9], ARGV[17]) or '0')
local typeActive =
    tonumber(redis.call('HGET', KEYS[10], ARGV[18]) or '0')
local delta = tonumber(ARGV[7])
local nodePendingLimit =
    tonumber(redis.call('HGET', KEYS[12], 'nodePendingLimit') or '0')
local nodeActiveLimit =
    tonumber(redis.call('HGET', KEYS[12], 'nodeActiveLimit') or '0')
local typePendingLimit =
    limitOr(capability.pendingLimit, nodePendingLimit)
local typeActiveLimit =
    limitOr(capability.activeLimit, nodeActiveLimit)
if delta <= 0
    or delta > nodePendingLimit
    or nodePending > nodePendingLimit - delta
    or delta > typePendingLimit
    or typePending > typePendingLimit - delta
    or delta > nodeActiveLimit
    or nodeActive > nodeActiveLimit - delta
    or delta > typeActiveLimit
    or typeActive > typeActiveLimit - delta then
    return {'capacity', '', '', '0', '0', '', '0',
        '', '0', '', '', '', '0', '0', '', '', '', '0',
        tostring(nowMs)}
end
if atMax(KEYS[3], 'storeRevision')
    or atMax(KEYS[4], 'objectGeneration')
    or atMax(KEYS[5], 'authorityOwnerGeneration') then
    return {'exhausted', '', '', '0', '0', '', '0',
        '', '0', '', '', '', '0', '0', '', '', '', '0',
        tostring(nowMs)}
end
redis.call('HINCRBY', KEYS[3], 'storeRevision', 1)
redis.call('HINCRBY', KEYS[4], 'objectGeneration', 1)
redis.call(
    'HINCRBY', KEYS[5], 'authorityOwnerGeneration', 1)
local version =
    redis.call('HGET', KEYS[3], 'storeRevision')
local objectGeneration =
    redis.call('HGET', KEYS[4], 'objectGeneration')
local ownerGeneration =
    redis.call('HGET', KEYS[5], 'authorityOwnerGeneration')
redis.call('HSET', KEYS[1],
    'authorityKey', ARGV[1],
    'storeVersion', version,
    'payload', ARGV[6],
    'objectGeneration', objectGeneration,
    'authorityOwnerGeneration', ownerGeneration,
    'ownerId', ARGV[4],
    'ownerLeaseGeneration', ARGV[5],
    'allocationState', 'pending',
    'objectKind', ARGV[11],
    'stableType', ARGV[3],
    'descriptorKey', ARGV[19],
    'descriptorLifecycleGeneration', ARGV[10],
    'capacityDelta', ARGV[7],
    'pendingCreationReservationId', ARGV[16],
    'pendingCreationReference', ARGV[20],
    'pendingCreationSha256', ARGV[21],
    'pendingCreationEncodedSize', ARGV[22])
redis.call('ZADD', KEYS[2], 0, ARGV[15])
redis.call('HDEL', KEYS[13], ARGV[15])
redis.call('HINCRBY', KEYS[7], ARGV[17], delta)
redis.call('HINCRBY', KEYS[8], ARGV[18], delta)
redis.call('HSET', KEYS[11],
    'status', 'prepared',
    'requestFingerprint', ARGV[2],
    'reservationId', ARGV[16],
    'authorityKey', ARGV[1],
    'expectedVersion', version,
    'objectGeneration', objectGeneration,
    'authorityOwnerGeneration', ownerGeneration,
    'targetMesh', ARGV[8],
    'targetNode', ARGV[9],
    'targetLifecycleGeneration', ARGV[10],
    'targetOwnerId', ARGV[4],
    'targetLeaseGeneration', ARGV[5],
    'objectKind', ARGV[11],
    'stableType', ARGV[3],
    'capacityDelta', ARGV[7],
    'contentReference', ARGV[20],
    'requestSha256', ARGV[21],
    'requestEncodedSize', ARGV[22])
return {'reserved', version, ARGV[6], objectGeneration,
    ownerGeneration, ARGV[4], ARGV[5],
    'pending', ARGV[11], ARGV[3], ARGV[19], '',
    ARGV[10], ARGV[7], ARGV[16], ARGV[20], ARGV[21],
    ARGV[22], tostring(nowMs)}
)";

    static constexpr std::string_view commit_object = R"(
if redis.replicate_commands then redis.replicate_commands() end
local time = redis.call('TIME')
local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
local function findCapability(key, objectKind, stableType)
    for _, candidate in ipairs(cjson.decode(
        redis.call('HGET', key, 'capabilities') or '[]')) do
        if candidate.objectKind == objectKind
            and candidate.stableType == stableType then
            return candidate
        end
    end
    return nil
end
local function limitOr(value, fallback)
    if value == nil or value == cjson.null then
        return fallback
    end
    return tonumber(value)
end
local function snapshot(status)
    if redis.call('EXISTS', KEYS[1]) == 0 then
        return {status, 'missing', '', '', '0', '0', '', '0',
            '', '0', '', '', '', '0', '0', tostring(nowMs)}
    end
    return {status, 'found',
        redis.call('HGET', KEYS[1], 'storeVersion') or '',
        redis.call('HGET', KEYS[1], 'payload') or '',
        redis.call('HGET', KEYS[1], 'objectGeneration') or '0',
        redis.call('HGET', KEYS[1], 'authorityOwnerGeneration') or '0',
        redis.call('HGET', KEYS[1], 'ownerId') or '',
        redis.call('HGET', KEYS[1], 'ownerLeaseGeneration') or '0',
        redis.call('HGET', KEYS[1], 'allocationState') or '',
        redis.call('HGET', KEYS[1], 'objectKind') or '0',
        redis.call('HGET', KEYS[1], 'stableType') or '',
        redis.call('HGET', KEYS[1], 'descriptorKey') or '',
        '',
        redis.call('HGET', KEYS[1], 'descriptorLifecycleGeneration') or '0',
        redis.call('HGET', KEYS[1], 'capacityDelta') or '0',
        tostring(nowMs)}
end
local function archiveCurrent()
    local fields = {
        'authorityKey', 'storeVersion', 'payload',
        'objectGeneration', 'authorityOwnerGeneration',
        'ownerId', 'ownerLeaseGeneration', 'allocationState',
        'objectKind', 'stableType', 'descriptorKey',
        'descriptorLifecycleGeneration', 'capacityDelta',
        'pendingCreationReservationId', 'pendingCreationReference',
        'pendingCreationSha256', 'pendingCreationEncodedSize'
    }
    for _, field in ipairs(fields) do
        local value = redis.call('HGET', KEYS[1], field)
        if value then
            redis.call(
                'HSET', KEYS[10],
                ARGV[16] .. ':' .. field, value)
        end
    end
    redis.call('HSET', KEYS[10], ARGV[16] .. ':deleted', '0')
    redis.call('ZADD', KEYS[11], 0, ARGV[16])
    local membership =
        redis.call('HGET', KEYS[13], ARGV[17])
    if membership then
        redis.call(
            'HSET', KEYS[14],
            ARGV[16], membership)
        redis.call('ZADD', KEYS[15], 0, ARGV[16])
    end
end
if redis.call('EXISTS', KEYS[2]) == 0
    or redis.call('HGET', KEYS[2], 'reservationId') ~= ARGV[1]
    or redis.call('HGET', KEYS[2], 'expectedVersion') ~= ARGV[2]
    or redis.call('HGET', KEYS[2], 'objectGeneration') ~= ARGV[3]
    or redis.call('HGET', KEYS[2], 'authorityOwnerGeneration') ~= ARGV[4]
    or redis.call('HGET', KEYS[2], 'targetMesh') ~= ARGV[5]
    or redis.call('HGET', KEYS[2], 'targetNode') ~= ARGV[6]
    or redis.call('HGET', KEYS[2], 'targetLifecycleGeneration') ~= ARGV[7]
    or redis.call('HGET', KEYS[2], 'targetOwnerId') ~= ARGV[8]
    or redis.call('HGET', KEYS[2], 'targetLeaseGeneration') ~= ARGV[9]
    or redis.call('HGET', KEYS[2], 'capacityDelta') ~= ARGV[10] then
    return snapshot('stale')
end
local status = redis.call('HGET', KEYS[2], 'status')
if status == 'committed' then return snapshot('already_committed') end
if status ~= 'prepared' then return snapshot('stale') end
if redis.call('EXISTS', KEYS[1]) == 0
    or redis.call('HGET', KEYS[1], 'storeVersion')
        ~= redis.call('HGET', KEYS[2], 'expectedVersion') then
    return snapshot('conflict')
end
local ownerId = redis.call('HGET', KEYS[2], 'targetOwnerId') or ''
local generation =
    redis.call('HGET', KEYS[2], 'targetLeaseGeneration') or ''
local stored = redis.call('HGET', KEYS[4], 'generation')
local capability = findCapability(
    KEYS[9],
    redis.call('HGET', KEYS[1], 'objectKind'),
    redis.call('HGET', KEYS[1], 'stableType'))
if redis.call('PTTL', KEYS[4]) <= 0
    or not stored or tostring(stored) ~= tostring(generation)
    or redis.call('EXISTS', KEYS[9]) == 0
    or redis.call('HGET', KEYS[9], 'descriptorKey')
        ~= redis.call('HGET', KEYS[1], 'descriptorKey')
    or redis.call('HGET', KEYS[9], 'lifecycleGeneration') ~= ARGV[7]
    or redis.call('HGET', KEYS[9], 'ownerId') ~= ARGV[8]
    or redis.call('HGET', KEYS[9], 'ownerLeaseGeneration') ~= ARGV[9]
    or redis.call('HGET', KEYS[9], 'runtimeState') ~= '1'
    or redis.call('HGET', KEYS[9], 'objectRole') ~= '2'
    or not capability then
    return snapshot('conflict')
end
local current =
    redis.call('HGET', KEYS[3], 'storeRevision') or '0'
if current == '9223372036854775807' then return snapshot('exhausted') end
local delta = tonumber(redis.call('HGET', KEYS[2], 'capacityDelta') or '0')
local nodePending =
    tonumber(redis.call('HGET', KEYS[5], ARGV[14]) or '0')
local typePending =
    tonumber(redis.call('HGET', KEYS[6], ARGV[15]) or '0')
local nodeActive =
    tonumber(redis.call('HGET', KEYS[7], ARGV[14]) or '0')
local typeActive =
    tonumber(redis.call('HGET', KEYS[8], ARGV[15]) or '0')
local nodeActiveLimit =
    tonumber(redis.call('HGET', KEYS[9], 'nodeActiveLimit') or '0')
local typeActiveLimit =
    limitOr(capability.activeLimit, nodeActiveLimit)
if nodePending < delta or typePending < delta
    or delta > nodeActiveLimit
    or nodeActive > nodeActiveLimit - delta
    or delta > typeActiveLimit
    or typeActive > typeActiveLimit - delta then
    return snapshot('conflict')
end
archiveCurrent()
redis.call('HINCRBY', KEYS[3], 'storeRevision', 1)
local version =
    redis.call('HGET', KEYS[3], 'storeRevision')
redis.call('HSET', KEYS[1],
    'storeVersion', version, 'payload', ARGV[11],
    'allocationState', 'active')
redis.call('HDEL', KEYS[1],
    'pendingCreationReservationId', 'pendingCreationReference',
    'pendingCreationSha256', 'pendingCreationEncodedSize')
redis.call('HINCRBY', KEYS[5], ARGV[14], -delta)
redis.call('HINCRBY', KEYS[6], ARGV[15], -delta)
redis.call('HINCRBY', KEYS[7], ARGV[14], delta)
redis.call('HINCRBY', KEYS[8], ARGV[15], delta)
redis.call('HSET', KEYS[2], 'status', 'committed')
return snapshot('committed')
)";

    static constexpr std::string_view abort_object = R"(
if redis.replicate_commands then redis.replicate_commands() end
local time = redis.call('TIME')
local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
local function revisionHex(value)
    local digits = tostring(value)
    local out = ''
    local hex = '0123456789abcdef'
    repeat
        local quotient = ''
        local carry = 0
        for index = 1, string.len(digits) do
            local current =
                carry * 10 + tonumber(string.sub(digits, index, index))
            local digit = math.floor(current / 16)
            carry = current % 16
            if quotient ~= '' or digit ~= 0 then
                quotient = quotient .. tostring(digit)
            end
        end
        out = string.sub(hex, carry + 1, carry + 1) .. out
        digits = quotient == '' and '0' or quotient
    until digits == '0'
    return string.rep('0', 16 - string.len(out)) .. out
end
local function snapshot(status)
    if redis.call('EXISTS', KEYS[1]) == 0 then
        return {status, 'missing', '', '', '0', '0', '', '0',
            '', '0', '', '', '', '0', '0', tostring(nowMs)}
    end
    return {status, 'found',
        redis.call('HGET', KEYS[1], 'storeVersion') or '',
        redis.call('HGET', KEYS[1], 'payload') or '',
        redis.call('HGET', KEYS[1], 'objectGeneration') or '0',
        redis.call('HGET', KEYS[1], 'authorityOwnerGeneration') or '0',
        redis.call('HGET', KEYS[1], 'ownerId') or '',
        redis.call('HGET', KEYS[1], 'ownerLeaseGeneration') or '0',
        redis.call('HGET', KEYS[1], 'allocationState') or '',
        redis.call('HGET', KEYS[1], 'objectKind') or '0',
        redis.call('HGET', KEYS[1], 'stableType') or '',
        redis.call('HGET', KEYS[1], 'descriptorKey') or '',
        '',
        redis.call('HGET', KEYS[1], 'descriptorLifecycleGeneration') or '0',
        redis.call('HGET', KEYS[1], 'capacityDelta') or '0',
        tostring(nowMs)}
end
local function archiveCurrent()
    local fields = {
        'authorityKey', 'storeVersion', 'payload',
        'objectGeneration', 'authorityOwnerGeneration',
        'ownerId', 'ownerLeaseGeneration', 'allocationState',
        'objectKind', 'stableType', 'descriptorKey',
        'descriptorLifecycleGeneration', 'capacityDelta',
        'pendingCreationReservationId', 'pendingCreationReference',
        'pendingCreationSha256', 'pendingCreationEncodedSize'
    }
    for _, field in ipairs(fields) do
        local value = redis.call('HGET', KEYS[1], field)
        if value then
            redis.call(
                'HSET', KEYS[7],
                ARGV[15] .. ':' .. field, value)
        end
    end
    redis.call('HSET', KEYS[7], ARGV[15] .. ':deleted', '0')
    redis.call('ZADD', KEYS[8], 0, ARGV[15])
    local membership =
        redis.call('HGET', KEYS[10], ARGV[12])
    if membership then
        redis.call(
            'HSET', KEYS[11],
            ARGV[15], membership)
        redis.call('ZADD', KEYS[12], 0, ARGV[15])
    end
end
if redis.call('EXISTS', KEYS[2]) == 0
    or redis.call('HGET', KEYS[2], 'reservationId') ~= ARGV[1]
    or redis.call('HGET', KEYS[2], 'expectedVersion') ~= ARGV[2]
    or redis.call('HGET', KEYS[2], 'objectGeneration') ~= ARGV[3]
    or redis.call('HGET', KEYS[2], 'authorityOwnerGeneration') ~= ARGV[4]
    or redis.call('HGET', KEYS[2], 'targetMesh') ~= ARGV[5]
    or redis.call('HGET', KEYS[2], 'targetNode') ~= ARGV[6]
    or redis.call('HGET', KEYS[2], 'targetLifecycleGeneration') ~= ARGV[7]
    or redis.call('HGET', KEYS[2], 'targetOwnerId') ~= ARGV[8]
    or redis.call('HGET', KEYS[2], 'targetLeaseGeneration') ~= ARGV[9]
    or redis.call('HGET', KEYS[2], 'capacityDelta') ~= ARGV[10] then
    return snapshot('stale')
end
local status = redis.call('HGET', KEYS[2], 'status')
if status == 'aborted' then return snapshot('already_aborted') end
if status ~= 'prepared' then return snapshot('stale') end
if redis.call('EXISTS', KEYS[1]) == 0
    or redis.call('HGET', KEYS[1], 'storeVersion')
        ~= redis.call('HGET', KEYS[2], 'expectedVersion') then
    return snapshot('conflict')
end
local delta = tonumber(redis.call('HGET', KEYS[2], 'capacityDelta') or '0')
local nodePending =
    tonumber(redis.call('HGET', KEYS[4], ARGV[13]) or '0')
local typePending =
    tonumber(redis.call('HGET', KEYS[5], ARGV[14]) or '0')
if nodePending < delta or typePending < delta then
    return snapshot('conflict')
end
local storeRevision =
    redis.call('HGET', KEYS[6], 'storeRevision') or '0'
if storeRevision == '9223372036854775807' then
    return snapshot('conflict')
end
archiveCurrent()
local deleteVersion =
    redis.call('HINCRBY', KEYS[6], 'storeRevision', 1)
redis.call('HINCRBY', KEYS[4], ARGV[13], -delta)
redis.call('HINCRBY', KEYS[5], ARGV[14], -delta)
redis.call('DEL', KEYS[1])
redis.call('HDEL', KEYS[10], ARGV[12])
redis.call('HSET', KEYS[9], ARGV[12], deleteVersion)
local deletedRevision = revisionHex(deleteVersion)
redis.call('HSET', KEYS[7],
    deletedRevision .. ':deleted', '1',
    deletedRevision .. ':authorityKey',
    redis.call('HGET', KEYS[2], 'authorityKey'))
redis.call('ZADD', KEYS[8], 0, deletedRevision)
redis.call('HSET', KEYS[2], 'status', 'aborted')
return {'aborted', 'missing', '', '', '0', '0', '', '0',
    '', '0', '', '', '', '0', '0', tostring(nowMs)}
)";

    static constexpr std::string_view prepare_aggregate = R"(
if redis.replicate_commands then redis.replicate_commands() end
local function liveLease(key, generation)
    if redis.call('PTTL', key) <= 0 then return false end
    return redis.call('HGET', key, 'generation') == tostring(generation)
end
local function findCapability(key, objectKind, stableType)
    for _, candidate in ipairs(cjson.decode(
        redis.call('HGET', key, 'capabilities') or '[]')) do
        if candidate.objectKind == objectKind
            and candidate.stableType == stableType then
            return candidate
        end
    end
    return nil
end
if redis.call('EXISTS', KEYS[1]) == 1 then
    if redis.call('HGET', KEYS[1], 'fingerprint') == ARGV[1] then
        return 'already_prepared'
    end
    return 'stale'
end
local count = tonumber(ARGV[6])
if count <= 0 or count > 1024 or not liveLease(KEYS[4], ARGV[5]) then
    return 'conflict'
end
local ownerTransitionCount = 0
for i = 1, count do
    local keyBase = 5 + (i - 1) * 14
    local argBase = 8 + (i - 1) * 11
    local authorityKey = KEYS[keyBase]
    local reservationKey = KEYS[keyBase + 1]
    local authorityName = ARGV[argBase]
    local expectedVersion = ARGV[argBase + 1]
    local transition = ARGV[argBase + 2]
    local fence = ARGV[argBase + 5]
    if redis.call('EXISTS', authorityKey) == 0
        or redis.call('HGET', authorityKey, 'storeVersion') ~= expectedVersion then
        return 'conflict'
    end
    if transition == 'new_owner' then
        ownerTransitionCount = ownerTransitionCount + 1
        local sourceOwner = redis.call('HGET', authorityKey, 'ownerId') or ''
        local sourceGeneration =
            redis.call('HGET', authorityKey, 'ownerLeaseGeneration') or ''
        local targetCapability = findCapability(
            KEYS[keyBase + 8],
            redis.call('HGET', reservationKey, 'objectKind'),
            redis.call('HGET', reservationKey, 'stableType'))
        if redis.call('EXISTS', reservationKey) == 0
            or redis.call('HGET', reservationKey, 'status') ~= 'reserved'
            or redis.call('HGET', reservationKey, 'fence') ~= fence
            or redis.call('HGET', reservationKey, 'authorityKey') ~= authorityName
            or redis.call('HGET', reservationKey, 'expectedVersion') ~= expectedVersion
            or redis.call('HGET', reservationKey, 'sourceOwnerId') ~= sourceOwner
            or redis.call('HGET', reservationKey, 'sourceLeaseGeneration') ~= sourceGeneration
            or redis.call('HGET', reservationKey, 'objectKind')
                ~= redis.call('HGET', authorityKey, 'objectKind')
            or redis.call('HGET', reservationKey, 'stableType')
                ~= redis.call('HGET', authorityKey, 'stableType')
            or redis.call('HGET', reservationKey, 'sourceDescriptorKey')
                ~= redis.call('HGET', authorityKey, 'descriptorKey')
            or redis.call('HGET', reservationKey, 'sourceLifecycleGeneration')
                ~= redis.call('HGET', authorityKey, 'descriptorLifecycleGeneration')
            or redis.call('HGET', reservationKey, 'capacityDelta')
                ~= redis.call('HGET', authorityKey, 'capacityDelta')
            or redis.call('HGET', authorityKey, 'allocationState') ~= 'active'
            or redis.call('HGET', reservationKey, 'targetOwnerId') ~= ARGV[4]
            or redis.call('HGET', reservationKey, 'targetLeaseGeneration') ~= ARGV[5]
            or redis.call('EXISTS', KEYS[keyBase + 8]) == 0
            or redis.call('HGET', KEYS[keyBase + 8], 'descriptorKey')
                ~= redis.call('HGET', reservationKey, 'targetDescriptorKey')
            or redis.call('HGET', KEYS[keyBase + 8], 'lifecycleGeneration')
                ~= redis.call('HGET', reservationKey, 'targetLifecycleGeneration')
            or redis.call('HGET', KEYS[keyBase + 8], 'ownerId') ~= ARGV[4]
            or redis.call('HGET', KEYS[keyBase + 8], 'ownerLeaseGeneration') ~= ARGV[5]
            or redis.call('HGET', KEYS[keyBase + 8], 'runtimeState') ~= '1'
            or redis.call('HGET', KEYS[keyBase + 8], 'objectRole') ~= '2'
            or not targetCapability then
            return 'conflict'
        end
        local delta = tonumber(
            redis.call('HGET', reservationKey, 'capacityDelta') or '0')
        local sourceNodeActive = tonumber(
            redis.call('HGET', KEYS[keyBase + 2], ARGV[argBase + 6]) or '0')
        local sourceTypeActive = tonumber(
            redis.call('HGET', KEYS[keyBase + 3], ARGV[argBase + 7]) or '0')
        local targetNodePending = tonumber(
            redis.call('HGET', KEYS[keyBase + 4], ARGV[argBase + 8]) or '0')
        local targetTypePending = tonumber(
            redis.call('HGET', KEYS[keyBase + 5], ARGV[argBase + 9]) or '0')
        if sourceNodeActive < delta or sourceTypeActive < delta
            or targetNodePending < delta
            or targetTypePending < delta then
            return 'conflict'
        end
    elseif fence ~= '' then
        return 'conflict'
    end
end
redis.call('HSET', KEYS[1],
    'status', 'prepared', 'fingerprint', ARGV[1],
    'aggregateId', ARGV[2], 'aggregateGeneration', ARGV[3],
    'targetOwnerId', ARGV[4], 'targetLeaseGeneration', ARGV[5],
    'participantCount', ARGV[6],
    'ownerTransitionCount', tostring(ownerTransitionCount),
    'inventoryDigest', ARGV[7])
for i = 1, count do
    local keyBase = 5 + (i - 1) * 14
    local argBase = 8 + (i - 1) * 11
    local prefix = 'p:' .. (i - 1) .. ':'
    redis.call('HSET', KEYS[1],
        prefix .. 'authorityKey', ARGV[argBase],
        prefix .. 'expectedVersion', ARGV[argBase + 1],
        prefix .. 'transition', ARGV[argBase + 2],
        prefix .. 'payload', ARGV[argBase + 3],
        prefix .. 'membership', ARGV[argBase + 4],
        prefix .. 'fence', ARGV[argBase + 5],
        prefix .. 'sourceNodeField', ARGV[argBase + 6],
        prefix .. 'sourceTypeField', ARGV[argBase + 7],
        prefix .. 'targetNodeField', ARGV[argBase + 8],
        prefix .. 'targetTypeField', ARGV[argBase + 9],
        prefix .. 'expectedRevisionHex', ARGV[argBase + 10])
    if ARGV[argBase + 2] == 'new_owner' then
        redis.call('HSET', KEYS[keyBase + 1],
            'status', 'prepared',
            'aggregateId', ARGV[2],
            'aggregateGeneration', ARGV[3])
    end
end
return 'prepared'
)";

    static constexpr std::string_view commit_aggregate = R"(
if redis.replicate_commands then redis.replicate_commands() end
local time = redis.call('TIME')
local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
local maxValue = '9223372036854775807'
local function subtractSmall(value, count)
    local digits = {}
    local borrow = count
    for index = string.len(value), 1, -1 do
        local digit = string.byte(value, index) - string.byte('0')
        local subtract = borrow % 10
        borrow = math.floor(borrow / 10)
        digit = digit - subtract
        if digit < 0 then
            digit = digit + 10
            borrow = borrow + 1
        end
        table.insert(digits, 1, string.char(string.byte('0') + digit))
    end
    return table.concat(digits)
end
local function remaining(key, field, count)
    local value = redis.call('HGET', key, field) or '0'
    value = string.gsub(value, '^0+', '')
    if value == '' then value = '0' end
    local limit = subtractSmall(maxValue, count)
    return string.len(value) < string.len(limit)
        or (string.len(value) == string.len(limit) and value <= limit)
end
local function liveLease(key, generation)
    if redis.call('PTTL', key) <= 0 then return false end
    return redis.call('HGET', key, 'generation') == tostring(generation)
end
local function findCapability(key, objectKind, stableType)
    for _, candidate in ipairs(cjson.decode(
        redis.call('HGET', key, 'capabilities') or '[]')) do
        if candidate.objectKind == objectKind
            and candidate.stableType == stableType then
            return candidate
        end
    end
    return nil
end
local function limitOr(value, fallback)
    if value == nil or value == cjson.null then return fallback end
    return tonumber(value)
end
if redis.call('EXISTS', KEYS[1]) == 0 then return 'stale' end
local status = redis.call('HGET', KEYS[1], 'status')
if status == 'committed' then return 'already_committed' end
if status ~= 'prepared'
    or redis.call('HGET', KEYS[1], 'aggregateGeneration') ~= ARGV[1] then
    return 'stale'
end
local count = tonumber(redis.call('HGET', KEYS[1], 'participantCount') or '0')
local ownerTransitionCount =
    tonumber(redis.call('HGET', KEYS[1], 'ownerTransitionCount') or '0')
local targetGeneration =
    redis.call('HGET', KEYS[1], 'targetLeaseGeneration') or ''
if not liveLease(KEYS[4], targetGeneration) then
    return 'stale'
end
if not remaining(KEYS[2], 'storeRevision', count)
    or not remaining(
        KEYS[3], 'authorityOwnerGeneration',
        ownerTransitionCount) then
    return 'generation_exhausted'
end
for i = 0, count - 1 do
    local prefix = 'p:' .. i .. ':'
    local keyBase = 5 + i * 14
    local authorityKey = KEYS[keyBase]
    local reservationKey = KEYS[keyBase + 1]
    local expectedVersion = redis.call('HGET', KEYS[1], prefix .. 'expectedVersion')
    local transition = redis.call('HGET', KEYS[1], prefix .. 'transition')
    if redis.call('EXISTS', authorityKey) == 0
        or redis.call('HGET', authorityKey, 'storeVersion') ~= expectedVersion then
        return 'stale'
    end
    if transition == 'new_owner' then
        local sourceOwner = redis.call('HGET', authorityKey, 'ownerId') or ''
        local sourceGeneration =
            redis.call('HGET', authorityKey, 'ownerLeaseGeneration') or ''
        local targetDescriptorKey = KEYS[keyBase + 8]
        local targetCapability = findCapability(
            targetDescriptorKey,
            redis.call('HGET', reservationKey, 'objectKind'),
            redis.call('HGET', reservationKey, 'stableType'))
        if redis.call('HGET', reservationKey, 'status') ~= 'prepared'
            or redis.call('HGET', reservationKey, 'aggregateId')
                ~= redis.call('HGET', KEYS[1], 'aggregateId')
            or redis.call('HGET', reservationKey, 'aggregateGeneration')
                ~= redis.call('HGET', KEYS[1], 'aggregateGeneration')
            or redis.call('HGET', reservationKey, 'sourceOwnerId') ~= sourceOwner
            or redis.call('HGET', reservationKey, 'sourceLeaseGeneration') ~= sourceGeneration
            or redis.call('HGET', reservationKey, 'objectKind')
                ~= redis.call('HGET', authorityKey, 'objectKind')
            or redis.call('HGET', reservationKey, 'stableType')
                ~= redis.call('HGET', authorityKey, 'stableType')
            or redis.call('HGET', reservationKey, 'sourceDescriptorKey')
                ~= redis.call('HGET', authorityKey, 'descriptorKey')
            or redis.call('HGET', reservationKey, 'sourceLifecycleGeneration')
                ~= redis.call('HGET', authorityKey, 'descriptorLifecycleGeneration')
            or redis.call('HGET', reservationKey, 'capacityDelta')
                ~= redis.call('HGET', authorityKey, 'capacityDelta')
            or redis.call('HGET', authorityKey, 'allocationState') ~= 'active'
            or redis.call('EXISTS', targetDescriptorKey) == 0
            or redis.call('HGET', targetDescriptorKey, 'descriptorKey')
                ~= redis.call('HGET', reservationKey, 'targetDescriptorKey')
            or redis.call('HGET', targetDescriptorKey, 'lifecycleGeneration')
                ~= redis.call('HGET', reservationKey, 'targetLifecycleGeneration')
            or redis.call('HGET', targetDescriptorKey, 'ownerId')
                ~= redis.call('HGET', KEYS[1], 'targetOwnerId')
            or redis.call('HGET', targetDescriptorKey, 'ownerLeaseGeneration')
                ~= targetGeneration
            or redis.call('HGET', targetDescriptorKey, 'runtimeState') ~= '1'
            or redis.call('HGET', targetDescriptorKey, 'objectRole') ~= '2'
            or not targetCapability then
	            return 'stale'
	        end
	        local delta =
	            tonumber(redis.call('HGET', reservationKey, 'capacityDelta') or '0')
	        local sourceNodeField =
	            redis.call('HGET', KEYS[1], prefix .. 'sourceNodeField')
	        local sourceTypeField =
	            redis.call('HGET', KEYS[1], prefix .. 'sourceTypeField')
	        local targetNodeField =
	            redis.call('HGET', KEYS[1], prefix .. 'targetNodeField')
	        local targetTypeField =
	            redis.call('HGET', KEYS[1], prefix .. 'targetTypeField')
	        local sourceNodeActive = tonumber(
	            redis.call('HGET', KEYS[keyBase + 2], sourceNodeField) or '0')
	        local sourceTypeActive = tonumber(
	            redis.call('HGET', KEYS[keyBase + 3], sourceTypeField) or '0')
	        local targetNodePending = tonumber(
	            redis.call('HGET', KEYS[keyBase + 4], targetNodeField) or '0')
	        local targetTypePending = tonumber(
	            redis.call('HGET', KEYS[keyBase + 5], targetTypeField) or '0')
	        local targetNodeActive = tonumber(
	            redis.call('HGET', KEYS[keyBase + 6], targetNodeField) or '0')
	        local targetTypeActive = tonumber(
	            redis.call('HGET', KEYS[keyBase + 7], targetTypeField) or '0')
	        local nodeActiveLimit = tonumber(
	            redis.call('HGET', targetDescriptorKey, 'nodeActiveLimit') or '0')
	        local typeActiveLimit =
	            limitOr(targetCapability.activeLimit, nodeActiveLimit)
	        if sourceNodeActive < delta or sourceTypeActive < delta
	            or targetNodePending < delta or targetTypePending < delta
	            or delta > nodeActiveLimit
	            or targetNodeActive > nodeActiveLimit - delta
	            or delta > typeActiveLimit
	            or targetTypeActive > typeActiveLimit - delta then
	            return 'stale'
	        end
	    end
end
for i = 0, count - 1 do
    local prefix = 'p:' .. i .. ':'
    local keyBase = 5 + i * 14
    local authorityKey = KEYS[keyBase]
    local transition = redis.call('HGET', KEYS[1], prefix .. 'transition')
    local revisionHex =
        redis.call('HGET', KEYS[1], prefix .. 'expectedRevisionHex')
    local fields = {
        'authorityKey', 'payload', 'storeVersion',
        'objectGeneration', 'authorityOwnerGeneration',
        'ownerId', 'ownerLeaseGeneration', 'allocationState',
        'objectKind', 'stableType', 'descriptorKey',
        'descriptorLifecycleGeneration', 'capacityDelta',
        'pendingCreationReservationId', 'pendingCreationReference',
        'pendingCreationSha256', 'pendingCreationEncodedSize'
    }
    for _, field in ipairs(fields) do
        local value = redis.call('HGET', authorityKey, field)
        if value then
            redis.call(
                'HSET', KEYS[keyBase + 9],
                revisionHex .. ':' .. field, value)
        end
    end
    redis.call(
        'HSET', KEYS[keyBase + 9],
        revisionHex .. ':deleted', '0')
    redis.call('ZADD', KEYS[keyBase + 10], 0, revisionHex)
    local authorityName =
        redis.call('HGET', KEYS[1], prefix .. 'authorityKey')
    local oldMembership =
        redis.call('HGET', KEYS[keyBase + 11], authorityName)
    if oldMembership then
        redis.call(
            'HSET', KEYS[keyBase + 12],
            revisionHex, oldMembership)
        redis.call(
            'ZADD', KEYS[keyBase + 13], 0, revisionHex)
    end
    redis.call('HINCRBY', KEYS[2], 'storeRevision', 1)
    local version =
        redis.call('HGET', KEYS[2], 'storeRevision')
    local ownerGeneration =
        redis.call('HGET', authorityKey, 'authorityOwnerGeneration') or '0'
    if transition == 'new_owner' then
        redis.call(
            'HINCRBY', KEYS[3],
            'authorityOwnerGeneration', 1)
        ownerGeneration =
            redis.call(
                'HGET', KEYS[3],
                'authorityOwnerGeneration')
    end
    redis.call('HSET', authorityKey,
        'storeVersion', version,
        'payload', redis.call('HGET', KEYS[1], prefix .. 'payload') or '',
        'authorityOwnerGeneration', ownerGeneration)
    redis.call(
        'HSET', KEYS[keyBase + 11], authorityName,
        redis.call('HGET', KEYS[1], prefix .. 'membership') or '')
    if transition == 'new_owner' then
        local reservationKey = KEYS[keyBase + 1]
        local delta = tonumber(redis.call('HGET', reservationKey, 'capacityDelta') or '0')
        local sourceNodeField =
            redis.call('HGET', KEYS[1], prefix .. 'sourceNodeField')
        local sourceTypeField =
            redis.call('HGET', KEYS[1], prefix .. 'sourceTypeField')
        local targetNodeField =
            redis.call('HGET', KEYS[1], prefix .. 'targetNodeField')
        local targetTypeField =
            redis.call('HGET', KEYS[1], prefix .. 'targetTypeField')
        redis.call('HINCRBY', KEYS[keyBase + 2], sourceNodeField, -delta)
        redis.call('HINCRBY', KEYS[keyBase + 3], sourceTypeField, -delta)
        redis.call('HINCRBY', KEYS[keyBase + 4], targetNodeField, -delta)
        redis.call('HINCRBY', KEYS[keyBase + 5], targetTypeField, -delta)
        redis.call('HINCRBY', KEYS[keyBase + 6], targetNodeField, delta)
        redis.call('HINCRBY', KEYS[keyBase + 7], targetTypeField, delta)
        redis.call('HSET', reservationKey, 'status', 'committed')
        redis.call('HSET', authorityKey,
            'ownerId', redis.call('HGET', KEYS[1], 'targetOwnerId'),
            'ownerLeaseGeneration', targetGeneration,
            'allocationState', 'active',
            'objectKind', redis.call('HGET', reservationKey, 'objectKind'),
            'stableType', redis.call('HGET', reservationKey, 'stableType'),
            'descriptorKey', redis.call('HGET', reservationKey, 'targetDescriptorKey'),
            'descriptorLifecycleGeneration',
                redis.call('HGET', reservationKey, 'targetLifecycleGeneration'),
            'capacityDelta', redis.call('HGET', reservationKey, 'capacityDelta'))
    end
end
redis.call('HSET', KEYS[1], 'status', 'committed')
return 'committed'
)";

    static constexpr std::string_view abort_aggregate = R"(
if redis.replicate_commands then redis.replicate_commands() end
if redis.call('EXISTS', KEYS[1]) == 0 then return 'stale' end
local status = redis.call('HGET', KEYS[1], 'status')
if status == 'aborted' then return 'already_aborted' end
if status == 'committed' then return 'stale' end
if status ~= 'prepared'
    or redis.call('HGET', KEYS[1], 'aggregateGeneration') ~= ARGV[1] then
    return 'stale'
end
local count = tonumber(redis.call('HGET', KEYS[1], 'participantCount') or '0')
for i = 0, count - 1 do
    local prefix = 'p:' .. i .. ':'
    local keyBase = 2 + i * 3
    local reservationKey = KEYS[keyBase]
    if reservationKey and reservationKey ~= ''
        and redis.call('HGET', reservationKey, 'status') == 'prepared'
        and redis.call('HGET', reservationKey, 'aggregateId')
            == redis.call('HGET', KEYS[1], 'aggregateId')
        and redis.call('HGET', reservationKey, 'aggregateGeneration')
            == redis.call('HGET', KEYS[1], 'aggregateGeneration') then
        local delta = tonumber(redis.call('HGET', reservationKey, 'capacityDelta') or '0')
        local targetNodeField =
            redis.call('HGET', KEYS[1], prefix .. 'targetNodeField')
        local targetTypeField =
            redis.call('HGET', KEYS[1], prefix .. 'targetTypeField')
        local nodePending = tonumber(
            redis.call('HGET', KEYS[keyBase + 1], targetNodeField) or '0')
        local typePending = tonumber(
            redis.call('HGET', KEYS[keyBase + 2], targetTypeField) or '0')
        if nodePending < delta or typePending < delta then
            return 'stale'
        end
        redis.call('HINCRBY', KEYS[keyBase + 1], targetNodeField, -delta)
        redis.call('HINCRBY', KEYS[keyBase + 2], targetTypeField, -delta)
        redis.call('HSET', reservationKey, 'status', 'aborted')
    end
end
redis.call('HSET', KEYS[1], 'status', 'aborted')
return 'aborted'
)";
};

class redis_location_script_result_t
{
  public:
    static location_write_result_t write_result (std::string_view status,
                                                 std::int64_t generation,
                                                 std::int64_t updated_at_ms)
    {
        if (status == "stored") {
            return location_write_result_t::stored (generation, from_unix_ms (updated_at_ms));
        }
        if (status == "conflict") {
            return {location_write_status_t::rejected_conflict, 0, {}};
        }
        return {location_write_status_t::ignored_stale, 0, {}};
    }

    static std::chrono::system_clock::time_point from_unix_ms (std::int64_t unix_ms)
    {
        return std::chrono::system_clock::time_point{std::chrono::milliseconds{unix_ms}};
    }
};

class redis_location_key_schema_t
{
  public:
    static std::string domain_prefix (std::string_view prefix)
    {
        if (prefix.empty () || prefix.find ('{') != std::string_view::npos
            || prefix.find ('}') != std::string_view::npos)
            throw std::invalid_argument (
              "redis location key prefix must be non-empty and must not contain braces");
        return join (prefix, "{zlink-location-v1}");
    }

    static std::string encode_mesh_node_key (
      const mesh_node_descriptor_key_t &key)
    {
        return encode (key.mesh_name, key.rid.to_hex ());
    }

    static mesh_node_descriptor_key_t decode_mesh_node_key (
      std::string_view encoded)
    {
        const auto parts = decode (encoded, 2);
        return {
          parts[0],
          zlink::routing_id_t::from_hex (parts[1])};
    }

    static std::string mesh_node_key (
      std::string_view prefix,
      std::string_view mesh_name,
      std::string_view rid)
    {
        const auto canonical = encode (
          mesh_name,
          zlink::routing_id_t::from (std::string (rid)).to_hex ());
        return join (
          domain_prefix (prefix), "descriptor", "mesh",
          sha256_hex (canonical));
    }

    static std::string mesh_node_admission_key (
      std::string_view prefix,
      std::string_view mesh_name,
      std::string_view rid)
    {
        const auto canonical = encode (
          mesh_name,
          zlink::routing_id_t::from (std::string (rid)).to_hex ());
        return join (
          domain_prefix (prefix), "descriptor-admission", "mesh",
          sha256_hex (canonical));
    }

    static std::string mesh_node_keys_key (
      std::string_view prefix,
      std::string_view)
    {
        return join (domain_prefix (prefix), "descriptor", "mesh", "index");
    }

    static std::string mesh_node_owner_keys_key (
      std::string_view prefix,
      std::string_view owner_id,
      std::int64_t lease_generation)
    {
        std::string token (owner_id);
        token.push_back ('\0');
        token += std::to_string (lease_generation);
        return join (
          domain_prefix (prefix), "descriptor", "mesh", "owner",
          sha256_hex (token));
    }

    static std::string encode_client_server_key (
      const client_server_server_descriptor_key_t &key)
    {
        return encode (key.channel_name, key.server_rid.to_hex ());
    }

    static client_server_server_descriptor_key_t
    decode_client_server_key (std::string_view encoded)
    {
        const auto parts = decode (encoded, 2);
        return {
          parts[0],
          zlink::routing_id_t::from_hex (parts[1])};
    }

    static std::string client_server_key (
      std::string_view prefix, std::string_view canonical_key)
    {
        return join (
          domain_prefix (prefix), "descriptor", "client-server",
          sha256_hex (canonical_key));
    }

    static std::string client_server_admission_key (
      std::string_view prefix, std::string_view canonical_key)
    {
        return join (
          domain_prefix (prefix), "descriptor-admission",
          "client-server", sha256_hex (canonical_key));
    }

    static std::string client_server_keys_key (
      std::string_view prefix)
    {
        return join (
          domain_prefix (prefix), "descriptor", "client-server",
          "index");
    }

    static std::string client_server_channel_keys_key (
      std::string_view prefix, std::string_view channel_name)
    {
        return join (
          domain_prefix (prefix), "descriptor", "client-server",
          "channel", sha256_hex (channel_name));
    }

    static std::string client_server_owner_keys_key (
      std::string_view prefix, std::string_view owner_id,
      std::int64_t lease_generation)
    {
        std::string token (owner_id);
        token.push_back ('\0');
        token += std::to_string (lease_generation);
        return join (
          domain_prefix (prefix), "descriptor", "client-server",
          "owner", sha256_hex (token));
    }

    static std::string client_server_stamp_key (
      std::string_view prefix,
      std::optional<std::string_view> channel_name = std::nullopt)
    {
        return channel_name
                 ? join (
                     prefix, "stamp", "channel-server",
                     *channel_name)
                 : join (
                     prefix, "stamp", "channel-server");
    }

    static std::string encode_fanout_publisher_key (
      const fanout_publisher_descriptor_key_t &key)
    {
        return encode (
          key.channel_name, key.publisher_rid.to_hex ());
    }

    static fanout_publisher_descriptor_key_t
    decode_fanout_publisher_key (std::string_view encoded)
    {
        const auto parts = decode (encoded, 2);
        return {
          parts[0],
          zlink::routing_id_t::from_hex (parts[1])};
    }

    static std::string fanout_publisher_key (
      std::string_view prefix, std::string_view canonical_key)
    {
        return join (
          domain_prefix (prefix), "descriptor",
          "fanout-publisher", sha256_hex (canonical_key));
    }

    static std::string fanout_publisher_admission_key (
      std::string_view prefix, std::string_view canonical_key)
    {
        return join (
          domain_prefix (prefix), "descriptor-admission",
          "fanout-publisher", sha256_hex (canonical_key));
    }

    static std::string fanout_publisher_keys_key (
      std::string_view prefix)
    {
        return join (
          domain_prefix (prefix), "descriptor",
          "fanout-publisher", "index");
    }

    static std::string fanout_publisher_channel_keys_key (
      std::string_view prefix, std::string_view channel_name)
    {
        return join (
          domain_prefix (prefix), "descriptor",
          "fanout-publisher", "channel",
          sha256_hex (channel_name));
    }

    static std::string fanout_publisher_owner_keys_key (
      std::string_view prefix, std::string_view owner_id,
      std::int64_t lease_generation)
    {
        std::string token (owner_id);
        token.push_back ('\0');
        token += std::to_string (lease_generation);
        return join (
          domain_prefix (prefix), "descriptor",
          "fanout-publisher", "owner",
          sha256_hex (token));
    }

    static std::string fanout_publisher_stamp_key (
      std::string_view prefix,
      std::optional<std::string_view> channel_name = std::nullopt)
    {
        return channel_name
                 ? join (
                     prefix, "stamp", "fanout-publisher",
                     *channel_name)
                 : join (
                     prefix, "stamp", "fanout-publisher");
    }

    static std::string encode_peer_key (const peer_location_key_t &key)
    {
        const auto identity = key.node_rid ? key.node_rid->to_hex ()
                                          : key.endpoint.value_or (std::string{});
        return encode (canonical_auto_connect_type (key.auto_connect_type), key.mesh_name,
                       canonical_role (key.role), identity);
    }

    static std::string encode_spot_key (const spot_location_key_t &key)
    {
        return encode (key.mesh_name, key.spot_rid.to_hex ());
    }

    static std::string encode_actor_key (const actor_location_key_t &key)
    {
        return encode (key.mesh_name, key.actor_id);
    }

    static std::string encode_route_key (const route_location_key_t &key)
    {
        return encode (std::to_string (static_cast<int> (key.route_kind)), key.route_key);
    }

    static location_key_t decode_key (location_kind_t kind, std::string_view encoded)
    {
        switch (kind) {
            case location_kind_t::peer:
                return decode_peer_key (encoded);
            case location_kind_t::spot:
                return decode_spot_key (encoded);
            case location_kind_t::actor:
                return decode_actor_key (encoded);
            case location_kind_t::route:
                return decode_route_key (encoded);
            default:
                throw std::invalid_argument ("unknown location kind");
        }
    }

    static std::string row_key (std::string_view prefix,
                                location_kind_t kind,
                                std::string_view row_key)
    {
        return join (
          domain_prefix (prefix), "row", kind_tag (kind), row_key);
    }

    static std::string generation_key (std::string_view prefix,
                                       location_kind_t kind,
                                       std::string_view row_key)
    {
        return join (
          domain_prefix (prefix), "gen", kind_tag (kind), row_key);
    }

    static std::string keys_key (std::string_view prefix, location_kind_t kind)
    {
        return join (domain_prefix (prefix), "keys", kind_tag (kind));
    }

    static std::string owner_key (std::string_view prefix,
                                  location_kind_t kind,
                                  std::string_view owner_id)
    {
        return join (
          domain_prefix (prefix), "own", kind_tag (kind), owner_id);
    }

    static std::string lease_key (std::string_view prefix, std::string_view owner_id)
    {
        return join (
          domain_prefix (prefix), "owner-lease", sha256_hex (owner_id));
    }

    static std::string owner_lease_generation_key (
      std::string_view prefix)
    {
        return join (domain_prefix (prefix), "counter");
    }

    static std::string schema_key (std::string_view prefix)
    {
        return join (domain_prefix (prefix), "schema");
    }

    static std::string authority_key (
      std::string_view prefix,
      std::string_view key)
    {
        return join (
          domain_prefix (prefix), "authority", "current",
          sha256_hex (key));
    }

    static std::string authority_keys_key (
      std::string_view prefix)
    {
        return join (
          domain_prefix (prefix), "authority", "key-index");
    }

    static std::string authority_index_member (
      std::string_view authority_key)
    {
        static constexpr char alphabet[] =
          "0123456789abcdef";
        std::string result;
        result.reserve (authority_key.size () * 2);
        for (const auto value : authority_key) {
            const auto byte =
              static_cast<unsigned char> (value);
            result.push_back (alphabet[byte >> 4]);
            result.push_back (alphabet[byte & 0x0f]);
        }
        return result;
    }

    static std::string authority_history_key (
      std::string_view prefix,
      std::string_view key)
    {
        return join (
          domain_prefix (prefix), "authority", "history",
          sha256_hex (key));
    }

    static std::string authority_history_revisions_key (
      std::string_view prefix,
      std::string_view key)
    {
        return join (
          domain_prefix (prefix), "authority",
          "history-revisions", sha256_hex (key));
    }

    static std::string authority_index_gc_key (
      std::string_view prefix)
    {
        return join (
          domain_prefix (prefix), "authority", "index-gc");
    }

    static std::string membership_current_key (
      std::string_view prefix)
    {
        return join (
          domain_prefix (prefix), "membership", "current");
    }

    static std::string membership_history_key (
      std::string_view prefix,
      std::string_view key)
    {
        return join (
          domain_prefix (prefix), "membership", "history",
          sha256_hex (key));
    }

    static std::string membership_history_revisions_key (
      std::string_view prefix,
      std::string_view key)
    {
        return join (
          domain_prefix (prefix), "membership",
          "history-revisions", sha256_hex (key));
    }

    static std::string decode_authority_index_member (
      std::string_view member)
    {
        if ((member.size () % 2) != 0)
            throw std::invalid_argument (
              "authority index member must be lower hex");
        auto nibble = [] (char value) -> unsigned char {
            if (value >= '0' && value <= '9')
                return static_cast<unsigned char> (value - '0');
            if (value >= 'a' && value <= 'f')
                return static_cast<unsigned char> (
                  value - 'a' + 10);
            throw std::invalid_argument (
              "authority index member must be lower hex");
        };
        std::string result;
        result.reserve (member.size () / 2);
        for (std::size_t index = 0; index < member.size ();
             index += 2)
            result.push_back (static_cast<char> (
              (nibble (member[index]) << 4)
              | nibble (member[index + 1])));
        return result;
    }

    static std::string authority_scan_key (
      std::string_view prefix,
      std::string_view scan_id)
    {
        return join (
          domain_prefix (prefix), "scan", scan_id);
    }

    static std::string authority_scans_expiry_key (
      std::string_view prefix)
    {
        return join (
          domain_prefix (prefix), "scans", "expiry");
    }

    static std::string authority_scans_watermark_key (
      std::string_view prefix)
    {
        return join (
          domain_prefix (prefix), "scans", "watermark");
    }

    static std::string authority_store_revision_key (
      std::string_view prefix)
    {
        return join (domain_prefix (prefix), "counter");
    }

    static std::string authority_object_generation_key (
      std::string_view prefix)
    {
        return join (domain_prefix (prefix), "counter");
    }

    static std::string authority_owner_generation_key (
      std::string_view prefix)
    {
        return join (domain_prefix (prefix), "counter");
    }

    static std::string creation_reservation_key (
      std::string_view prefix,
      std::string_view key)
    {
        return join (domain_prefix (prefix), "creation", key);
    }

    static std::string relocation_capacity_reservation_key (
      std::string_view prefix,
      std::string_view fence)
    {
        return join (domain_prefix (prefix), "relocation", fence);
    }

    static std::string capacity_node_active_key (
      std::string_view prefix)
    {
        return join (
          domain_prefix (prefix), "capacity", "node", "active");
    }

    static std::string capacity_node_pending_key (
      std::string_view prefix)
    {
        return join (
          domain_prefix (prefix), "capacity", "node", "pending");
    }

    static std::string capacity_type_active_key (
      std::string_view prefix)
    {
        return join (
          domain_prefix (prefix), "capacity", "type", "active");
    }

    static std::string capacity_type_pending_key (
      std::string_view prefix)
    {
        return join (
          domain_prefix (prefix), "capacity", "type", "pending");
    }

    static std::string capacity_node_field (
      std::string_view mesh_name,
      std::string_view node_rid,
      std::uint64_t lifecycle_generation)
    {
        const auto descriptor_key = encode (
          mesh_name,
          zlink::routing_id_t::from (
            std::string (node_rid))
            .to_hex ());
        return encode (
          descriptor_key,
          std::to_string (lifecycle_generation));
    }

    static std::string capacity_type_field (
      std::string_view mesh_name,
      std::string_view node_rid,
      std::uint64_t lifecycle_generation,
      placement_object_kind_t object_kind,
      std::string_view stable_type)
    {
        const auto descriptor_key = encode (
          mesh_name,
          zlink::routing_id_t::from (
            std::string (node_rid))
            .to_hex ());
        return encode (
          descriptor_key,
          std::to_string (lifecycle_generation),
          object_kind_token (object_kind),
          stable_type);
    }

    static std::string object_kind_token (
      placement_object_kind_t kind)
    {
        switch (kind) {
            case placement_object_kind_t::actor:
                return "actor";
            case placement_object_kind_t::user_spot:
                return "user_spot";
            case placement_object_kind_t::instance_spot:
                return "instance_spot";
            default:
                throw std::invalid_argument (
                  "unknown placement object kind");
        }
    }

    static placement_object_kind_t parse_object_kind_token (
      std::string_view token)
    {
        if (token == "actor")
            return placement_object_kind_t::actor;
        if (token == "user_spot")
            return placement_object_kind_t::user_spot;
        if (token == "instance_spot")
            return placement_object_kind_t::instance_spot;
        throw std::invalid_argument (
          "unknown placement object kind token: "
          + std::string (token));
    }

    static std::string aggregate_key (
      std::string_view prefix,
      std::string_view aggregate_id,
      std::uint64_t aggregate_generation)
    {
        return join (
          domain_prefix (prefix), "aggregate", aggregate_id,
          std::to_string (aggregate_generation));
    }

    static std::string stamp_key (std::string_view prefix,
                                  location_kind_t kind,
                                  std::optional<std::string_view> mesh_name = std::nullopt)
    {
        return mesh_name
                 ? join (
                     domain_prefix (prefix), "stamp",
                     kind_tag (kind), *mesh_name)
                 : join (
                     domain_prefix (prefix), "stamp",
                     kind_tag (kind));
    }

  public:
    static std::string sha256_hex (std::string_view input)
    {
        static constexpr std::array<std::uint32_t, 64> constants{
          0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
          0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
          0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
          0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
          0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
          0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
          0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
          0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
          0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
          0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
          0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
          0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
          0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
          0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
          0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
          0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
        std::vector<std::uint8_t> bytes (
          input.begin (), input.end ());
        const auto bit_length =
          static_cast<std::uint64_t> (bytes.size ()) * 8u;
        bytes.push_back (0x80u);
        while ((bytes.size () % 64u) != 56u)
            bytes.push_back (0);
        for (int shift = 56; shift >= 0; shift -= 8)
            bytes.push_back (
              static_cast<std::uint8_t> (bit_length >> shift));
        std::array<std::uint32_t, 8> state{
          0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
          0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
        for (std::size_t offset = 0; offset < bytes.size ();
             offset += 64) {
            std::array<std::uint32_t, 64> words{};
            for (std::size_t index = 0; index < 16; ++index) {
                const auto at = offset + index * 4;
                words[index] =
                  (static_cast<std::uint32_t> (bytes[at]) << 24u)
                  | (static_cast<std::uint32_t> (bytes[at + 1])
                     << 16u)
                  | (static_cast<std::uint32_t> (bytes[at + 2])
                     << 8u)
                  | static_cast<std::uint32_t> (bytes[at + 3]);
            }
            for (std::size_t index = 16; index < 64; ++index) {
                const auto s0 =
                  std::rotr (words[index - 15], 7)
                  ^ std::rotr (words[index - 15], 18)
                  ^ (words[index - 15] >> 3);
                const auto s1 =
                  std::rotr (words[index - 2], 17)
                  ^ std::rotr (words[index - 2], 19)
                  ^ (words[index - 2] >> 10);
                words[index] = words[index - 16] + s0
                               + words[index - 7] + s1;
            }
            auto [a, b, c, d, e, f, g, h] = state;
            for (std::size_t index = 0; index < 64; ++index) {
                const auto s1 =
                  std::rotr (e, 6) ^ std::rotr (e, 11)
                  ^ std::rotr (e, 25);
                const auto choice = (e & f) ^ (~e & g);
                const auto first =
                  h + s1 + choice + constants[index]
                  + words[index];
                const auto s0 =
                  std::rotr (a, 2) ^ std::rotr (a, 13)
                  ^ std::rotr (a, 22);
                const auto majority =
                  (a & b) ^ (a & c) ^ (b & c);
                const auto second = s0 + majority;
                h = g;
                g = f;
                f = e;
                e = d + first;
                d = c;
                c = b;
                b = a;
                a = first + second;
            }
            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;
            state[4] += e;
            state[5] += f;
            state[6] += g;
            state[7] += h;
        }
        static constexpr char alphabet[] = "0123456789abcdef";
        std::string result;
        result.reserve (64);
        for (const auto value : state)
            for (int shift = 28; shift >= 0; shift -= 4)
                result.push_back (
                  alphabet[(value >> shift) & 0x0fu]);
        return result;
    }

  private:
    template <typename... TSegments> static std::string encode (const TSegments &...segments)
    {
        std::string result;
        (append_segment (result, segments), ...);
        return result;
    }

    static void append_segment (std::string &result, std::string_view segment)
    {
        result += std::to_string (segment.size ());
        result += ':';
        result.append (segment);
    }

    static peer_location_key_t decode_peer_key (std::string_view encoded)
    {
        const auto segments = decode (encoded, 4);
        const auto &identity = segments[3];
        std::optional<zlink::routing_id_t> node_rid;
        std::optional<std::string> endpoint;
        if (!identity.empty ()) {
            node_rid = zlink::routing_id_t::from_hex (identity);
        } else {
            endpoint = identity;
        }
        return peer_location_key_t{parse_auto_connect_type (segments[0]), segments[1],
                                   parse_role (segments[2]), node_rid, endpoint};
    }

    static spot_location_key_t decode_spot_key (std::string_view encoded)
    {
        const auto segments = decode (encoded, 2);
        return spot_location_key_t{segments[0], zlink::routing_id_t::from_hex (segments[1])};
    }

    static actor_location_key_t decode_actor_key (std::string_view encoded)
    {
        const auto segments = decode (encoded, 2);
        return actor_location_key_t{segments[0], segments[1]};
    }

    static route_location_key_t decode_route_key (std::string_view encoded)
    {
        const auto segments = decode (encoded, 2);
        return route_location_key_t{static_cast<route_kind_t> (std::stoi (segments[0])),
                                    segments[1]};
    }

    static std::vector<std::string> decode (std::string_view encoded, std::size_t expected_count)
    {
        std::vector<std::string> segments;
        segments.reserve (expected_count);
        std::size_t offset = 0;
        while (segments.size () < expected_count) {
            const auto colon = encoded.find (':', offset);
            if (colon == std::string_view::npos) {
                throw std::invalid_argument ("encoded location key segment length is missing");
            }
            const auto length_text = std::string (encoded.substr (offset, colon - offset));
            const auto length = static_cast<std::size_t> (std::stoull (length_text));
            const auto start = colon + 1;
            const auto end = start + length;
            if (end > encoded.size ()) {
                throw std::invalid_argument ("encoded location key segment length is invalid");
            }
            segments.emplace_back (encoded.substr (start, length));
            offset = end;
        }
        if (offset != encoded.size ()) {
            throw std::invalid_argument ("encoded location key has trailing data");
        }
        return segments;
    }

    static std::string canonical_auto_connect_type (location_auto_connect_type_t type)
    {
        switch (type) {
            case location_auto_connect_type_t::route_mesh:
                return "route-mesh";
            case location_auto_connect_type_t::client_server:
                return "client-server";
            case location_auto_connect_type_t::dealer_mesh:
                return "dealer-mesh";
            case location_auto_connect_type_t::fanout:
                return "fanout";
            case location_auto_connect_type_t::spot_mesh:
                return "spot-mesh";
            default:
                throw std::invalid_argument ("unknown location auto-connect type");
        }
    }

    static std::string canonical_role (location_role_t role)
    {
        switch (role) {
            case location_role_t::spot:
                return "spot";
            case location_role_t::router:
                return "router";
            case location_role_t::dealer:
                return "dealer";
            case location_role_t::pub:
                return "pub";
            case location_role_t::sub:
                return "sub";
            default:
                throw std::invalid_argument ("unknown location role");
        }
    }

    static location_auto_connect_type_t parse_auto_connect_type (std::string_view value)
    {
        if (value == "route-mesh") {
            return location_auto_connect_type_t::route_mesh;
        }
        if (value == "client-server") {
            return location_auto_connect_type_t::client_server;
        }
        if (value == "dealer-mesh") {
            return location_auto_connect_type_t::dealer_mesh;
        }
        if (value == "fanout") {
            return location_auto_connect_type_t::fanout;
        }
        if (value == "spot-mesh") {
            return location_auto_connect_type_t::spot_mesh;
        }
        throw std::invalid_argument ("unknown location auto-connect type");
    }

    static location_role_t parse_role (std::string_view value)
    {
        if (value == "spot") {
            return location_role_t::spot;
        }
        if (value == "router") {
            return location_role_t::router;
        }
        if (value == "dealer") {
            return location_role_t::dealer;
        }
        if (value == "pub") {
            return location_role_t::pub;
        }
        if (value == "sub") {
            return location_role_t::sub;
        }
        throw std::invalid_argument ("unknown location role");
    }

    static std::string kind_tag (location_kind_t kind)
    {
        switch (kind) {
            case location_kind_t::peer:
                return "peer";
            case location_kind_t::spot:
                return "spot";
            case location_kind_t::actor:
                return "actor";
            case location_kind_t::route:
                return "route";
            default:
                throw std::invalid_argument ("unknown location kind");
        }
    }

    template <typename... TSegments> static std::string join (const TSegments &...segments)
    {
        std::string result;
        bool first = true;
        auto append = [&] (std::string_view segment) {
            if (!first) {
                result += ':';
            }
            first = false;
            result.append (segment);
        };
        (append (segments), ...);
        return result;
    }
};

class redis_location_row_codec_t
{
  public:
    static std::string encode_mesh_node (
      const mesh_node_descriptor_t &row)
    {
        nlohmann::ordered_json json;
        json["MeshName"] = row.mesh_name;
        json["Rid"] = row.rid.to_hex ();
        json["LifecycleGeneration"] =
          row.lifecycle_generation;
        json["DescriptorRevision"] =
          row.descriptor_revision;
        json["Endpoint"] = row.endpoint;
        json["ChannelWeights"] = row.channel_weights;
        json["SecurityIdentity"] = row.security_identity;
        json["OwnerId"] = row.owner_id;
        json["LeaseGeneration"] = row.lease_generation;
        json["UpdatedAt"] = format_updated_at (row.updated_at);
        json["ApplicationVersion"] = row.application_version;
        nlohmann::ordered_json capabilities =
          nlohmann::ordered_json::array ();
        for (const auto &capability :
             row.object_capabilities) {
            nlohmann::ordered_json value;
            value["ObjectKind"] =
              static_cast<int> (capability.object_kind);
            value["StableType"] = capability.stable_type;
            value["Policy"] =
              static_cast<int> (capability.policy);
            value["HasSnapshotAdapter"] =
              capability.has_snapshot_adapter;
            value["PlacementProfiles"] =
              std::vector<std::string> (
                capability.placement_profiles.begin (),
                capability.placement_profiles.end ());
            value["ActiveLimit"] =
              capability.active_limit
                ? nlohmann::json (*capability.active_limit)
                : nlohmann::json (nullptr);
            value["PendingLimit"] =
              capability.pending_limit
                ? nlohmann::json (*capability.pending_limit)
                : nlohmann::json (nullptr);
            capabilities.push_back (std::move (value));
        }
        json["ObjectCapabilities"] = std::move (capabilities);
        json["MaintenanceWave"] =
          row.maintenance_wave
            ? nlohmann::json (*row.maintenance_wave)
            : nlohmann::json (nullptr);
        json["State"] = static_cast<int> (row.state);
        json["ObjectRole"] = static_cast<int> (row.object_role);
        json["PlacementWeight"] = row.placement_weight;
        json["Capacity"] = {
          {"Active", row.object_capacity.active},
          {"Pending", row.object_capacity.pending},
          {"ActiveLimit", row.object_capacity.active_limit},
          {"PendingLimit", row.object_capacity.pending_limit}};
        return json.dump ();
    }

    static mesh_node_descriptor_t decode_mesh_node (
      std::string_view value)
    {
        const auto json = nlohmann::json::parse (value);
        mesh_node_descriptor_t row;
        row.mesh_name = json.at ("MeshName").get<std::string> ();
        row.rid = zlink::routing_id_t::from_hex (
          json.at ("Rid").get<std::string> ());
        row.lifecycle_generation =
          json.at ("LifecycleGeneration").get<std::uint64_t> ();
        row.descriptor_revision =
          json.at ("DescriptorRevision").get<std::uint64_t> ();
        row.endpoint = json.at ("Endpoint").get<std::string> ();
        row.channel_weights =
          json.at ("ChannelWeights")
            .get<std::map<std::string, int>> ();
        row.security_identity =
          json.at ("SecurityIdentity").get<std::string> ();
        row.owner_id = json.at ("OwnerId").get<std::string> ();
        row.lease_generation =
          json.at ("LeaseGeneration").get<std::int64_t> ();
        if (json.contains ("UpdatedAt")
            && !json.at ("UpdatedAt").is_null ())
            row.updated_at = parse_updated_at (
              json.at ("UpdatedAt").get<std::string> ());
        row.application_version =
          json.at ("ApplicationVersion").get<std::int64_t> ();
        for (const auto &item :
             json.at ("ObjectCapabilities")) {
            object_capability_t capability;
            capability.object_kind =
              static_cast<placement_object_kind_t> (
                item.at ("ObjectKind").get<int> ());
            capability.stable_type =
              item.at ("StableType").get<std::string> ();
            capability.policy =
              static_cast<maintenance_policy_kind_t> (
                item.at ("Policy").get<int> ());
            capability.has_snapshot_adapter =
              item.at ("HasSnapshotAdapter").get<bool> ();
            for (const auto &profile :
                 item.at ("PlacementProfiles"))
                capability.placement_profiles.insert (
                  profile.get<std::string> ());
            if (!item.at ("ActiveLimit").is_null ())
                capability.active_limit =
                  item.at ("ActiveLimit")
                    .get<std::uint32_t> ();
            if (!item.at ("PendingLimit").is_null ())
                capability.pending_limit =
                  item.at ("PendingLimit")
                    .get<std::uint32_t> ();
            row.object_capabilities.push_back (
              std::move (capability));
        }
        row.object_role =
          static_cast<object_role_t> (
            json.at ("ObjectRole").get<int> ());
        row.placement_weight =
          json.at ("PlacementWeight").get<std::uint8_t> ();
        const auto &capacity = json.at ("Capacity");
        row.object_capacity.active =
          capacity.at ("Active").get<std::uint32_t> ();
        row.object_capacity.pending =
          capacity.at ("Pending").get<std::uint32_t> ();
        row.object_capacity.active_limit =
          capacity.at ("ActiveLimit").get<std::uint32_t> ();
        row.object_capacity.pending_limit =
          capacity.at ("PendingLimit").get<std::uint32_t> ();
        if (!json.at ("MaintenanceWave").is_null ())
            row.maintenance_wave =
              json.at ("MaintenanceWave").get<std::string> ();
        row.state =
          static_cast<framework_runtime_state_t> (
            json.at ("State").get<int> ());
        return row;
    }

    static bool unsigned_utf8_less (
      std::string_view left, std::string_view right)
    {
        return std::lexicographical_compare (
          left.begin (), left.end (), right.begin (), right.end (),
          [] (char lhs, char rhs) {
              return static_cast<unsigned char> (lhs)
                     < static_cast<unsigned char> (rhs);
          });
    }

    static std::string object_role_token (object_role_t value)
    {
        switch (value) {
        case object_role_t::none: return "none";
        case object_role_t::client: return "client";
        case object_role_t::server: return "server";
        }
        throw std::invalid_argument ("unknown object role");
    }

    static std::string policy_token (maintenance_policy_kind_t value)
    {
        switch (value) {
        case maintenance_policy_kind_t::disabled: return "disabled";
        case maintenance_policy_kind_t::recreate: return "recreate";
        case maintenance_policy_kind_t::snapshot: return "snapshot";
        }
        throw std::invalid_argument ("unknown maintenance policy");
    }

    static std::string mesh_node_immutable_preimage (
      const mesh_node_descriptor_t &row)
    {
        std::vector<std::string> segments{
          "zlink-mesh-node-immutable-v1", row.mesh_name,
          row.rid.to_hex (), std::to_string (row.lifecycle_generation),
          row.endpoint};
        std::vector<std::string> channels;
        channels.reserve (row.channel_weights.size ());
        for (const auto &[name, _] : row.channel_weights)
            channels.push_back (name);
        std::sort (
          channels.begin (), channels.end (), unsigned_utf8_less);
        segments.push_back (std::to_string (channels.size ()));
        segments.insert (
          segments.end (), channels.begin (), channels.end ());
        segments.push_back (row.security_identity);
        segments.push_back (std::to_string (row.application_version));
        segments.push_back (object_role_token (row.object_role));
        segments.push_back (
          std::to_string (row.object_capacity.active_limit));
        segments.push_back (
          std::to_string (row.object_capacity.pending_limit));
        auto capabilities = row.object_capabilities;
        std::sort (
          capabilities.begin (), capabilities.end (),
          [] (const object_capability_t &left,
              const object_capability_t &right) {
              const auto left_kind = redis_location_key_schema_t::
                object_kind_token (left.object_kind);
              const auto right_kind = redis_location_key_schema_t::
                object_kind_token (right.object_kind);
              if (left_kind != right_kind)
                  return unsigned_utf8_less (
                    left_kind, right_kind);
              return unsigned_utf8_less (
                left.stable_type, right.stable_type);
          });
        segments.push_back (
          std::to_string (capabilities.size ()));
        for (const auto &capability : capabilities) {
            segments.push_back (
              redis_location_key_schema_t::object_kind_token (
                capability.object_kind));
            segments.push_back (capability.stable_type);
            segments.push_back (policy_token (capability.policy));
            segments.push_back (
              capability.has_snapshot_adapter ? "1" : "0");
            auto profiles = std::vector<std::string> (
              capability.placement_profiles.begin (),
              capability.placement_profiles.end ());
            std::sort (
              profiles.begin (), profiles.end (),
              unsigned_utf8_less);
            segments.push_back (
              std::to_string (profiles.size ()));
            segments.insert (
              segments.end (), profiles.begin (), profiles.end ());
            segments.push_back (
              capability.active_limit
                ? std::to_string (*capability.active_limit)
                : std::string{});
            segments.push_back (
              capability.pending_limit
                ? std::to_string (*capability.pending_limit)
                : std::string{});
        }
        std::string preimage;
        for (const auto &segment : segments) {
            preimage += std::to_string (segment.size ());
            preimage += ':';
            preimage += segment;
        }
        return preimage;
    }

    static std::string mesh_node_immutable_digest (
      const mesh_node_descriptor_t &row)
    {
        return redis_location_key_schema_t::sha256_hex (
          mesh_node_immutable_preimage (row));
    }

    static std::string encode_mesh_node_admission_capabilities (
      const mesh_node_descriptor_t &row)
    {
        auto capabilities = row.object_capabilities;
        std::sort (
          capabilities.begin (), capabilities.end (),
          [] (const object_capability_t &left,
              const object_capability_t &right) {
              const auto left_kind = redis_location_key_schema_t::
                object_kind_token (left.object_kind);
              const auto right_kind = redis_location_key_schema_t::
                object_kind_token (right.object_kind);
              if (left_kind != right_kind)
                  return unsigned_utf8_less (
                    left_kind, right_kind);
              return unsigned_utf8_less (
                left.stable_type, right.stable_type);
          });
        nlohmann::ordered_json json =
          nlohmann::ordered_json::array ();
        for (const auto &capability : capabilities) {
            nlohmann::ordered_json value;
            value["objectKind"] =
              redis_location_key_schema_t::
                object_kind_token (
                  capability.object_kind);
            value["stableType"] = capability.stable_type;
            value["policy"] = policy_token (capability.policy);
            value["hasSnapshotAdapter"] =
              capability.has_snapshot_adapter;
            auto profiles = std::vector<std::string> (
              capability.placement_profiles.begin (),
              capability.placement_profiles.end ());
            std::sort (
              profiles.begin (), profiles.end (),
              unsigned_utf8_less);
            value["placementProfiles"] = std::move (profiles);
            value["activeLimit"] =
              capability.active_limit
                ? nlohmann::json (*capability.active_limit)
                : nlohmann::json (nullptr);
            value["pendingLimit"] =
              capability.pending_limit
                ? nlohmann::json (*capability.pending_limit)
                : nlohmann::json (nullptr);
            json.push_back (std::move (value));
        }
        return json.dump ();
    }

    static std::string encode_client_server (
      const client_server_server_descriptor_t &row)
    {
        nlohmann::ordered_json json;
        json["ChannelName"] = row.channel_name;
        json["ServerRid"] = row.server_rid.to_hex ();
        json["LifecycleGeneration"] = row.lifecycle_generation;
        json["DescriptorRevision"] = row.descriptor_revision;
        json["Endpoint"] = row.endpoint;
        json["Weight"] = row.weight;
        json["State"] = runtime_state_name (row.state);
        json["SecurityIdentity"] = row.security_identity;
        json["OwnerId"] = row.owner_id;
        json["OwnerLeaseGeneration"] = row.lease_generation;
        json["UpdatedAt"] = format_updated_at (row.updated_at);
        return json.dump ();
    }

    static client_server_server_descriptor_t
    decode_client_server (std::string_view value)
    {
        const auto json = nlohmann::json::parse (value);
        client_server_server_descriptor_t row;
        row.channel_name =
          json.at ("ChannelName").get<std::string> ();
        row.server_rid = zlink::routing_id_t::from_hex (
          json.at ("ServerRid").get<std::string> ());
        row.lifecycle_generation =
          json.at ("LifecycleGeneration").get<std::uint64_t> ();
        row.descriptor_revision =
          json.at ("DescriptorRevision").get<std::uint64_t> ();
        row.endpoint = json.at ("Endpoint").get<std::string> ();
        row.weight = json.at ("Weight").get<int> ();
        row.state = parse_runtime_state (
          json.at ("State").get<std::string> ());
        row.security_identity =
          json.at ("SecurityIdentity").get<std::string> ();
        row.owner_id = json.at ("OwnerId").get<std::string> ();
        row.lease_generation =
          json.at ("OwnerLeaseGeneration").get<std::int64_t> ();
        row.updated_at = parse_updated_at (
          json.at ("UpdatedAt").get<std::string> ());
        return row;
    }

    static std::string client_server_immutable_preimage (
      const client_server_server_descriptor_t &row)
    {
        const std::array<std::string, 6> segments{
          "zlink-client-server-immutable-v1",
          row.channel_name,
          row.server_rid.to_hex (),
          std::to_string (row.lifecycle_generation),
          row.endpoint,
          row.security_identity};
        std::string preimage;
        for (const auto &segment : segments) {
            preimage += std::to_string (segment.size ());
            preimage += ':';
            preimage += segment;
        }
        return preimage;
    }

    static std::string client_server_immutable_digest (
      const client_server_server_descriptor_t &row)
    {
        return redis_location_key_schema_t::sha256_hex (
          client_server_immutable_preimage (row));
    }

    static std::string encode_fanout_publisher (
      const fanout_publisher_descriptor_t &row)
    {
        nlohmann::ordered_json json;
        json["ChannelName"] = row.channel_name;
        json["PublisherRid"] =
          row.publisher_rid.to_hex ();
        json["LifecycleGeneration"] =
          row.lifecycle_generation;
        json["DescriptorRevision"] =
          row.descriptor_revision;
        json["Endpoint"] = row.endpoint;
        json["State"] = runtime_state_name (row.state);
        json["SecurityIdentity"] =
          row.security_identity;
        json["OwnerId"] = row.owner_id;
        json["OwnerLeaseGeneration"] =
          row.lease_generation;
        json["UpdatedAt"] =
          format_updated_at (row.updated_at);
        return json.dump ();
    }

    static fanout_publisher_descriptor_t
    decode_fanout_publisher (std::string_view value)
    {
        const auto json = nlohmann::json::parse (value);
        fanout_publisher_descriptor_t row;
        row.channel_name =
          json.at ("ChannelName").get<std::string> ();
        row.publisher_rid =
          zlink::routing_id_t::from_hex (
            json.at ("PublisherRid").get<std::string> ());
        row.lifecycle_generation =
          json.at ("LifecycleGeneration")
            .get<std::uint64_t> ();
        row.descriptor_revision =
          json.at ("DescriptorRevision")
            .get<std::uint64_t> ();
        row.endpoint =
          json.at ("Endpoint").get<std::string> ();
        row.state = parse_runtime_state (
          json.at ("State").get<std::string> ());
        row.security_identity =
          json.at ("SecurityIdentity").get<std::string> ();
        row.owner_id =
          json.at ("OwnerId").get<std::string> ();
        row.lease_generation =
          json.at ("OwnerLeaseGeneration")
            .get<std::int64_t> ();
        row.updated_at = parse_updated_at (
          json.at ("UpdatedAt").get<std::string> ());
        return row;
    }

    static std::string fanout_publisher_immutable_preimage (
      const fanout_publisher_descriptor_t &row)
    {
        const std::array<std::string, 6> segments{
          "zlink-fanout-publisher-immutable-v1",
          row.channel_name,
          row.publisher_rid.to_hex (),
          std::to_string (row.lifecycle_generation),
          row.endpoint,
          row.security_identity};
        std::string preimage;
        for (const auto &segment : segments) {
            preimage += std::to_string (segment.size ());
            preimage += ':';
            preimage += segment;
        }
        return preimage;
    }

    static std::string fanout_publisher_immutable_digest (
      const fanout_publisher_descriptor_t &row)
    {
        return redis_location_key_schema_t::sha256_hex (
          fanout_publisher_immutable_preimage (row));
    }

    static std::string encode_peer (const peer_location_t &row)
    {
        nlohmann::ordered_json json;
        json["AutoConnectType"] = static_cast<int> (row.auto_connect_type);
        json["MeshName"] = row.mesh_name;
        json["NodeRid"] = row.node_rid ? nlohmann::json (row.node_rid->to_hex ())
                                       : nlohmann::json (nullptr);
        json["Role"] = static_cast<int> (row.role);
        json["Endpoint"] = row.endpoint;
        json["Weight"] = row.weight;
        json["Draining"] = row.draining;
        json["Value"] = row.value;
        json["Metadata"] = row.metadata.empty () ? nlohmann::json (nullptr)
                                                 : nlohmann::json (row.metadata);
        json["Capabilities"] = row.capabilities.empty () ? nlohmann::json (nullptr)
                                                         : nlohmann::json (row.capabilities);
        json["OwnerId"] = row.owner_id;
        json["Generation"] = row.generation;
        json["UpdatedAt"] = format_updated_at (row.updated_at);
        return json.dump ();
    }

    static peer_location_t decode_peer (std::string_view value)
    {
        const auto json = nlohmann::json::parse (value);
        peer_location_t row;
        row.auto_connect_type =
          static_cast<location_auto_connect_type_t> (json.at ("AutoConnectType").get<int> ());
        row.mesh_name = json.at ("MeshName").get<std::string> ();
        if (!json.at ("NodeRid").is_null ()) {
            row.node_rid =
              zlink::routing_id_t::from_hex (json.at ("NodeRid").get<std::string> ());
        }
        row.role = static_cast<location_role_t> (json.at ("Role").get<int> ());
        row.endpoint = json.at ("Endpoint").get<std::string> ();
        row.weight = json.at ("Weight").get<std::uint32_t> ();
        row.draining = json.contains ("Draining") && !json.at ("Draining").is_null ()
                         && json.at ("Draining").get<bool> ();
        row.value = json.at ("Value").get<std::int64_t> ();
        if (json.contains ("Metadata") && !json.at ("Metadata").is_null ()) {
            row.metadata = json.at ("Metadata").get<std::map<std::string, std::string>> ();
        }
        if (json.contains ("Capabilities") && !json.at ("Capabilities").is_null ()) {
            row.capabilities = json.at ("Capabilities").get<std::vector<std::string>> ();
        }
        row.owner_id = json.at ("OwnerId").get<std::string> ();
        row.generation = json.at ("Generation").get<std::int64_t> ();
        if (json.contains ("UpdatedAt") && !json.at ("UpdatedAt").is_null ()) {
            row.updated_at = parse_updated_at (json.at ("UpdatedAt").get<std::string> ());
        }
        return row;
    }

    static std::string encode_spot (const spot_location_t &row)
    {
        nlohmann::ordered_json json;
        json["MeshName"] = row.mesh_name;
        json["SpotRid"] = row.spot_rid.to_hex ();
        json["SpotGeneration"] = row.spot_generation;
        json["SpotType"] = row.spot_type ? nlohmann::json (*row.spot_type)
                                         : nlohmann::json (nullptr);
        json["NodeRid"] = row.node_rid.to_hex ();
        json["SpotKind"] = static_cast<int> (row.spot_kind);
        json["RouteEndpoint"] = row.route_endpoint ? nlohmann::json (*row.route_endpoint)
                                                   : nlohmann::json (nullptr);
        json["OwnerId"] = row.owner_id;
        json["Generation"] = row.generation;
        json["UpdatedAt"] = format_updated_at (row.updated_at);
        return json.dump ();
    }

    static spot_location_t decode_spot (std::string_view value)
    {
        const auto json = nlohmann::json::parse (value);
        spot_location_t row;
        row.mesh_name = json.at ("MeshName").get<std::string> ();
        row.spot_rid = zlink::routing_id_t::from_hex (json.at ("SpotRid").get<std::string> ());
        row.spot_generation = json.value ("SpotGeneration", std::uint64_t{0});
        if (!json.at ("SpotType").is_null ()) {
            row.spot_type = json.at ("SpotType").get<std::string> ();
        }
        row.node_rid = zlink::routing_id_t::from_hex (json.at ("NodeRid").get<std::string> ());
        row.spot_kind = static_cast<zlink::spot_kind> (json.at ("SpotKind").get<int> ());
        if (!json.at ("RouteEndpoint").is_null ()) {
            row.route_endpoint = json.at ("RouteEndpoint").get<std::string> ();
        }
        row.owner_id = json.at ("OwnerId").get<std::string> ();
        row.generation = json.at ("Generation").get<std::int64_t> ();
        if (json.contains ("UpdatedAt") && !json.at ("UpdatedAt").is_null ()) {
            row.updated_at = parse_updated_at (json.at ("UpdatedAt").get<std::string> ());
        }
        return row;
    }

    static std::string encode_actor (const actor_location_t &row)
    {
        nlohmann::ordered_json json;
        json["MeshName"] = row.mesh_name;
        json["ActorId"] = row.actor_id;
        json["ActorType"] = row.actor_type;
        nlohmann::ordered_json actor_ref;
        actor_ref["nodeRid"] =
          zlink::routing_id_t::from (std::string (row.actor_ref.node_rid ().value ())).to_hex ();
        actor_ref["actorId"] = std::string (row.actor_ref.actor_id ());
        actor_ref["generation"] = row.actor_ref.generation ();
        json["ActorRef"] = std::move (actor_ref);
        json["OwnerNodeRid"] = row.owner_node_rid.to_hex ();
        json["OwnerNodeGeneration"] = row.owner_node_generation;
        json["SpotRid"] = row.spot_rid.to_hex ();
        json["SpotGeneration"] = row.spot_generation;
        json["SpotKind"] = static_cast<int> (row.spot_kind);
        json["MembershipEpoch"] = row.membership_epoch;
        json["OwnerId"] = row.owner_id;
        json["UpdatedAt"] = format_updated_at (row.updated_at);
        return json.dump ();
    }

    static actor_location_t decode_actor (std::string_view value)
    {
        const auto json = nlohmann::json::parse (value);
        actor_location_t row;
        row.mesh_name = json.at ("MeshName").get<std::string> ();
        row.actor_id = json.at ("ActorId").get<std::string> ();
        row.actor_type = json.at ("ActorType").get<std::string> ();
        const auto &actor_ref = json.at ("ActorRef");
        row.actor_ref = actor_ref_t{
          node_rid_t::from_string (
            zlink::routing_id_t::from_hex (actor_ref.at ("nodeRid").get<std::string> ())
              .to_string ()),
          row.actor_type,
          actor_ref.at ("actorId").get<std::string> (),
          actor_ref.at ("generation").get<std::uint64_t> ()};
        row.owner_node_rid =
          zlink::routing_id_t::from_hex (json.at ("OwnerNodeRid").get<std::string> ());
        row.owner_node_generation = json.at ("OwnerNodeGeneration").get<std::uint64_t> ();
        row.spot_rid = zlink::routing_id_t::from_hex (json.at ("SpotRid").get<std::string> ());
        row.spot_generation = json.at ("SpotGeneration").get<std::uint64_t> ();
        row.spot_kind = static_cast<zlink::spot_kind> (json.at ("SpotKind").get<int> ());
        row.membership_epoch = json.at ("MembershipEpoch").get<std::uint64_t> ();
        row.owner_id = json.at ("OwnerId").get<std::string> ();
        if (json.contains ("UpdatedAt") && !json.at ("UpdatedAt").is_null ()) {
            row.updated_at = parse_updated_at (json.at ("UpdatedAt").get<std::string> ());
        }
        return row;
    }

    static std::string encode_route (const route_location_t &row)
    {
        nlohmann::ordered_json json;
        json["RouteKind"] = static_cast<int> (row.route_kind);
        json["RouteKey"] = row.route_key;
        json["OwnerNodeRid"] = row.owner_node_rid.to_hex ();
        json["OwnerId"] = row.owner_id;
        json["Generation"] = row.generation;
        json["Value"] = base64_encode (row.value);
        json["UpdatedAt"] = format_updated_at (row.updated_at);
        return json.dump ();
    }

    static route_location_t decode_route (std::string_view value)
    {
        const auto json = nlohmann::json::parse (value);
        route_location_t row;
        row.route_kind = static_cast<route_kind_t> (json.at ("RouteKind").get<int> ());
        row.route_key = json.at ("RouteKey").get<std::string> ();
        row.owner_node_rid =
          zlink::routing_id_t::from_hex (json.at ("OwnerNodeRid").get<std::string> ());
        row.owner_id = json.at ("OwnerId").get<std::string> ();
        row.generation = json.at ("Generation").get<std::int64_t> ();
        row.value = base64_decode (json.at ("Value").get<std::string> ());
        if (json.contains ("UpdatedAt") && !json.at ("UpdatedAt").is_null ()) {
            row.updated_at = parse_updated_at (json.at ("UpdatedAt").get<std::string> ());
        }
        return row;
    }

  private:
    static std::string runtime_state_name (
      framework_runtime_state_t state)
    {
        switch (state) {
        case framework_runtime_state_t::preparing:
            return "Preparing";
        case framework_runtime_state_t::serving:
            return "Serving";
        case framework_runtime_state_t::retiring:
            return "Retiring";
        case framework_runtime_state_t::draining:
            return "Draining";
        case framework_runtime_state_t::stopped:
            return "Stopped";
        case framework_runtime_state_t::error:
            return "Error";
        }
        throw std::invalid_argument (
          "unknown Framework runtime state");
    }

    static framework_runtime_state_t parse_runtime_state (
      std::string_view state)
    {
        if (state == "Preparing")
            return framework_runtime_state_t::preparing;
        if (state == "Serving")
            return framework_runtime_state_t::serving;
        if (state == "Retiring")
            return framework_runtime_state_t::retiring;
        if (state == "Draining")
            return framework_runtime_state_t::draining;
        if (state == "Stopped")
            return framework_runtime_state_t::stopped;
        if (state == "Error")
            return framework_runtime_state_t::error;
        throw std::invalid_argument (
          "unknown Framework runtime state");
    }

    static std::string format_updated_at (std::chrono::system_clock::time_point value)
    {
        if (value == std::chrono::system_clock::time_point{}) {
            return "0001-01-01T00:00:00+00:00";
        }

        const auto millis =
          std::chrono::duration_cast<std::chrono::milliseconds> (value.time_since_epoch ());
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds> (millis);
        const auto fractional = millis - seconds;
        const auto time = std::chrono::system_clock::to_time_t (
          std::chrono::system_clock::time_point (seconds));
        std::tm tm{};
#if defined(_WIN32)
        gmtime_s (&tm, &time);
#else
        gmtime_r (&time, &tm);
#endif
        std::ostringstream output;
        output << std::put_time (&tm, "%Y-%m-%dT%H:%M:%S");
        if (fractional.count () != 0) {
            output << '.' << std::setw (3) << std::setfill ('0') << fractional.count ();
        }
        output << "+00:00";
        return output.str ();
    }

    static std::chrono::system_clock::time_point parse_updated_at (const std::string &value)
    {
        if (value == "0001-01-01T00:00:00+00:00") {
            return {};
        }
        if (value.size () < 25 || value.substr (19) != "+00:00") {
            return {};
        }
        std::tm tm{};
        std::istringstream input (value.substr (0, 19));
        input >> std::get_time (&tm, "%Y-%m-%dT%H:%M:%S");
        if (input.fail ()) {
            return {};
        }
#if defined(_WIN32)
        const auto time = _mkgmtime (&tm);
#else
        const auto time = timegm (&tm);
#endif
        return std::chrono::system_clock::from_time_t (time);
    }

    static std::string base64_encode (const std::vector<std::uint8_t> &value)
    {
        static constexpr char alphabet[] =
          "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string result;
        result.reserve (((value.size () + 2) / 3) * 4);
        for (std::size_t i = 0; i < value.size (); i += 3) {
            const auto a = value[i];
            const auto b = i + 1 < value.size () ? value[i + 1] : 0;
            const auto c = i + 2 < value.size () ? value[i + 2] : 0;
            result.push_back (alphabet[a >> 2]);
            result.push_back (alphabet[((a & 0x03u) << 4u) | (b >> 4u)]);
            result.push_back (i + 1 < value.size ()
                                ? alphabet[((b & 0x0fu) << 2u) | (c >> 6u)]
                                : '=');
            result.push_back (i + 2 < value.size () ? alphabet[c & 0x3fu] : '=');
        }
        return result;
    }

    static std::vector<std::uint8_t> base64_decode (const std::string &value)
    {
        auto decode = [] (char ch) -> int {
            if (ch >= 'A' && ch <= 'Z') {
                return ch - 'A';
            }
            if (ch >= 'a' && ch <= 'z') {
                return ch - 'a' + 26;
            }
            if (ch >= '0' && ch <= '9') {
                return ch - '0' + 52;
            }
            if (ch == '+') {
                return 62;
            }
            if (ch == '/') {
                return 63;
            }
            if (ch == '=') {
                return 0;
            }
            throw std::invalid_argument ("invalid base64 character");
        };

        if ((value.size () % 4u) != 0u) {
            throw std::invalid_argument ("invalid base64 length");
        }
        std::vector<std::uint8_t> result;
        result.reserve ((value.size () / 4) * 3);
        for (std::size_t i = 0; i < value.size (); i += 4) {
            const auto a = decode (value[i]);
            const auto b = decode (value[i + 1]);
            const auto c = decode (value[i + 2]);
            const auto d = decode (value[i + 3]);
            result.push_back (static_cast<std::uint8_t> ((a << 2) | (b >> 4)));
            if (value[i + 2] != '=') {
                result.push_back (static_cast<std::uint8_t> (((b & 0x0f) << 4) | (c >> 2)));
            }
            if (value[i + 3] != '=') {
                result.push_back (static_cast<std::uint8_t> (((c & 0x03) << 6) | d));
            }
        }
        return result;
    }
};

} // namespace detail

#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
class redis_location_worker_t
{
  public:
    redis_location_worker_t () : _thread ([this] { run (); }) {}

    ~redis_location_worker_t ()
    {
        {
            std::lock_guard lock (_gate);
            _stopping = true;
        }
        _ready.notify_all ();
        if (_thread.joinable ()) {
            _thread.join ();
        }
    }

    redis_location_worker_t (const redis_location_worker_t &) = delete;
    redis_location_worker_t &operator= (const redis_location_worker_t &) = delete;

    template <typename T, typename TFunc> task_t<T> submit (TFunc &&func)
    {
        zlink::framework::detail::task_completion_source_t<T> completion;
        auto task = completion.task ();
        {
            std::lock_guard lock (_gate);
            _queue.emplace_back (
              [completion, func = std::forward<TFunc> (func)] () mutable {
                  try {
                      completion.complete (result_t<T>::success (func ()));
                  }
                  catch (const framework_exception_t &error) {
                      completion.complete (result_t<T>::failure (
                        error.kind (), error.what (), error.is_retriable ()));
                  }
                  catch (const std::exception &error) {
                      completion.complete (
                        result_t<T>::failure (framework_error_kind_t::request_failed,
                                              error.what (), true));
                  }
                  catch (...) {
                      completion.complete (
                        result_t<T>::failure (framework_error_kind_t::request_failed,
                                              "redis worker failure", true));
                  }
              });
        }
        _ready.notify_one ();
        return task;
    }

  private:
    void run ()
    {
        for (;;) {
            std::function<void ()> work;
            {
                std::unique_lock lock (_gate);
                _ready.wait (lock, [&] { return _stopping || !_queue.empty (); });
                if (_stopping && _queue.empty ()) {
                    return;
                }
                work = std::move (_queue.front ());
                _queue.pop_front ();
            }
            work ();
        }
    }

    std::mutex _gate;
    std::condition_variable _ready;
    std::deque<std::function<void ()>> _queue;
    bool _stopping = false;
    std::thread _thread;
};
#endif

class redis_location_store_t final : public location_store_t,
                                     public client_server_location_store_t,
                                     public fanout_location_store_t,
                                     public location_change_stamp_store_t
{
  public:
    explicit redis_location_store_t (redis_location_options_t options = {}) :
        _options (std::move (options))
    {
        (void) detail::redis_location_key_schema_t::
          domain_prefix (_options.key_prefix);
    }

    const redis_location_options_t &options () const noexcept { return _options; }

    task_t<location_write_result_t> update_mesh_node (
      mesh_node_descriptor_t descriptor,
      location_write_intent_t intent) override
    {
        if (!valid_mesh_node_descriptor (descriptor))
            throw std::invalid_argument (
              "mesh node descriptor is incomplete");
        if (detail::redis_location_row_codec_t::
              encode_mesh_node (descriptor)
              .size ()
            > 1024u * 1024u)
            throw std::invalid_argument (
              "mesh node descriptor exceeds 1 MiB");
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<location_write_result_t> (
          [this, descriptor = std::move (descriptor), intent] {
              try {
                  const auto encoded_rid =
                    descriptor.rid.to_hex ();
                  const auto row_id =
                    detail::redis_location_key_schema_t::
                      encode_mesh_node_key (
                        {descriptor.mesh_name,
                         descriptor.rid});
                  const auto keys = std::vector<std::string>{
                    detail::redis_location_key_schema_t::
                      mesh_node_key (
                        _options.key_prefix,
                        descriptor.mesh_name,
                        descriptor.rid.to_string ()),
                    detail::redis_location_key_schema_t::
                      mesh_node_keys_key (
                        _options.key_prefix,
                        descriptor.mesh_name),
                    detail::redis_location_key_schema_t::
                      lease_key (
                        _options.key_prefix,
                        descriptor.owner_id),
                    detail::redis_location_key_schema_t::
                      mesh_node_admission_key (
                        _options.key_prefix,
                        descriptor.mesh_name,
                        descriptor.rid.to_string ()),
                    detail::redis_location_key_schema_t::
                      mesh_node_owner_keys_key (
                        _options.key_prefix,
                        descriptor.owner_id,
                        descriptor.lease_generation)};
                  auto args = std::vector<std::string>{
                    intent_name (intent),
                    std::to_string (
                      descriptor.descriptor_revision),
                    descriptor.owner_id,
                    std::to_string (
                      descriptor.lease_generation),
                    std::to_string (
                      descriptor.lifecycle_generation),
                    detail::redis_location_row_codec_t::
                      mesh_node_immutable_digest (
                        descriptor),
                    lease_key_prefix (),
                    detail::redis_location_row_codec_t::
                      encode_mesh_node (descriptor),
                    row_id,
                    descriptor.mesh_name,
                    std::to_string (
                      static_cast<int> (
                        descriptor.object_role)),
                    std::to_string (
                      descriptor.placement_weight == 0
                        ? 0
                        : static_cast<int> (
                            descriptor.state)),
                    std::to_string (
                      descriptor.application_version),
                    detail::redis_location_row_codec_t::
                      encode_mesh_node_admission_capabilities (
                        descriptor),
                    std::to_string (
                      descriptor.object_capacity
                        .active_limit),
                    std::to_string (
                      descriptor.object_capacity
                        .pending_limit)};
                  const auto result = redis_get (
                    client ().eval<std::tuple<
                      std::string,
                      std::string,
                      std::string>> (
                      std::string (
                        detail::redis_location_scripts_t::
                          write_mesh_node),
                      keys.begin (), keys.end (), args.begin (),
                      args.end ()));
                  const auto &status = std::get<0> (result);
                  const auto now =
                    detail::redis_location_script_result_t::
                      from_unix_ms (
                        static_cast<std::int64_t> (
                          std::stoll (
                            std::get<2> (result))));
                  if (status == "stored")
                      return location_write_result_t::stored (
                        std::stoll (std::get<1> (result)), now);
                  return location_write_result_t{
                    status == "conflict"
                      ? location_write_status_t::
                          rejected_conflict
                      : location_write_status_t::ignored_stale,
                    0,
                    now};
              }
              catch (const sw::redis::Error &error) {
                  throw framework_exception_t (
                    framework_error_kind_t::request_failed,
                    error.what (), true);
              }
          });
#else
        (void) descriptor;
        (void) intent;
        return unavailable_write ();
#endif
    }

    task_t<location_write_status_t> remove_mesh_node (
      mesh_node_descriptor_key_t key,
      location_owner_token_t owner) override
    {
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<location_write_status_t> (
          [this, key = std::move (key),
           owner = std::move (owner)] {
              try {
                  const auto row_id =
                    detail::redis_location_key_schema_t::
                      encode_mesh_node_key (key);
                  const auto keys = std::vector<std::string>{
                    detail::redis_location_key_schema_t::
                      mesh_node_key (
                        _options.key_prefix, key.mesh_name,
                        key.rid.to_string ()),
                    detail::redis_location_key_schema_t::
                      mesh_node_keys_key (
                        _options.key_prefix, key.mesh_name),
                    detail::redis_location_key_schema_t::
                      mesh_node_admission_key (
                        _options.key_prefix, key.mesh_name,
                        key.rid.to_string ()),
                    detail::redis_location_key_schema_t::
                      mesh_node_owner_keys_key (
                        _options.key_prefix,
                        owner.owner_id,
                        owner.lease_generation)};
                  const auto args = std::vector<std::string>{
                    owner.owner_id,
                    std::to_string (owner.lease_generation),
                    row_id};
                  const auto result = redis_get (
                    client ().eval<std::string> (
                      std::string (
                        detail::redis_location_scripts_t::
                          remove_mesh_node),
                      keys.begin (), keys.end (), args.begin (),
                      args.end ()));
                  return result == "stored"
                           ? location_write_status_t::stored
                           : location_write_status_t::
                               ignored_stale;
              }
              catch (const sw::redis::Error &error) {
                  throw framework_exception_t (
                    framework_error_kind_t::request_failed,
                    error.what (), true);
              }
          });
#else
        (void) key;
        (void) owner;
        return unavailable_read<location_write_status_t> ();
#endif
    }

    task_t<location_page_t<mesh_node_descriptor_t>>
    list_mesh_nodes (std::string mesh_name,
                     location_page_request_t page = {}) override
    {
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<
          location_page_t<mesh_node_descriptor_t>> (
          [this, mesh_name = std::move (mesh_name), page] {
              try {
                  if (page.page_size < 0
                      || page.page_size > 1000)
                      throw std::invalid_argument (
                        "descriptor page size must be 1..1000");
                  const auto limit =
                    page.page_size == 0
                      ? std::size_t{1000}
                      : static_cast<std::size_t> (
                          page.page_size);
                  auto scan = parse_scan_state (
                    page.continuation_token);
                  location_page_t<mesh_node_descriptor_t> result;
                  std::size_t encoded_size = 0;
                  const auto set_key =
                    detail::redis_location_key_schema_t::
                      mesh_node_keys_key (
                        _options.key_prefix, mesh_name);
                  while (result.items.size () < limit) {
                      if (scan.pending_keys.empty ()) {
                          if (scan.started && scan.cursor == 0)
                              break;
                          const auto reply = redis_get (
                            client ().command<std::tuple<
                              std::string,
                              std::vector<std::string>>> (
                              "SSCAN", set_key,
                              std::to_string (scan.cursor),
                              "COUNT",
                              std::to_string (
                                std::max<std::size_t> (
                                  1, limit
                                       - result.items.size ()))));
                          scan.cursor =
                            std::stoull (std::get<0> (reply));
                          scan.started = true;
                          scan.pending_keys =
                            std::move (std::get<1> (reply));
                          if (scan.pending_keys.empty ())
                              continue;
                      }
                      auto id =
                        std::move (scan.pending_keys.back ());
                      scan.pending_keys.pop_back ();
                      const auto descriptor_key =
                        detail::redis_location_key_schema_t::
                          decode_mesh_node_key (id);
                      if (descriptor_key.mesh_name
                          != mesh_name)
                          continue;
                      const auto json = redis_get (
                        client ().hget (
                          detail::redis_location_key_schema_t::
                            mesh_node_key (
                              _options.key_prefix,
                              mesh_name,
                              descriptor_key.rid.to_string ()),
                          "json"));
                      if (json) {
                          if (!result.items.empty ()
                              && encoded_size + json->size ()
                                   > 4u * 1024u * 1024u) {
                              scan.pending_keys.push_back (
                                std::move (id));
                              break;
                          }
                          result.items.push_back (
                            detail::redis_location_row_codec_t::
                              decode_mesh_node (*json));
                          encoded_size += json->size ();
                      }
                  }
                  if (!scan.pending_keys.empty ()
                      || scan.cursor != 0)
                      result.continuation_token =
                        encode_scan_state (scan);
                  return result;
              }
              catch (const sw::redis::Error &error) {
                  throw framework_exception_t (
                    framework_error_kind_t::request_failed,
                    error.what (), true);
              }
          });
#else
        (void) mesh_name;
        (void) page;
        return unavailable_read<
          location_page_t<mesh_node_descriptor_t>> ();
#endif
    }

    task_t<location_write_result_t> update_client_server (
      client_server_server_descriptor_t descriptor,
      location_write_intent_t intent) override
    {
        if (!valid_client_server_descriptor (descriptor))
            throw std::invalid_argument (
              "ClientServer descriptor is incomplete");
        const auto json =
          detail::redis_location_row_codec_t::
            encode_client_server (descriptor);
        if (json.size () > 1024u * 1024u)
            throw std::invalid_argument (
              "ClientServer descriptor exceeds 1 MiB");
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<location_write_result_t> (
          [this, descriptor = std::move (descriptor),
           intent, json] {
              try {
                  const auto canonical_key =
                    detail::redis_location_key_schema_t::
                      encode_client_server_key (
                        {descriptor.channel_name,
                         descriptor.server_rid});
                  const auto admission_key =
                    detail::redis_location_key_schema_t::
                      client_server_admission_key (
                        _options.key_prefix, canonical_key);
                  const auto current = redis_get (
                    client ().hmget<std::vector<
                      sw::redis::OptionalString>> (
                      admission_key,
                      {"ownerId", "ownerLeaseGeneration"}));
                  const auto current_owner =
                    current.size () > 0 && current[0]
                      ? *current[0]
                      : std::string{};
                  const auto current_lease =
                    current.size () > 1 && current[1]
                      ? *current[1]
                      : std::string{};
                  const auto placeholder =
                    detail::redis_location_key_schema_t::
                      schema_key (_options.key_prefix);
                  const auto old_owner_index =
                    current_owner.empty ()
                      ? placeholder
                      : detail::redis_location_key_schema_t::
                          client_server_owner_keys_key (
                            _options.key_prefix,
                            current_owner,
                            std::stoll (current_lease));
                  const auto keys = std::vector<std::string>{
                    detail::redis_location_key_schema_t::
                      client_server_key (
                        _options.key_prefix, canonical_key),
                    admission_key,
                    detail::redis_location_key_schema_t::
                      client_server_keys_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      lease_key (
                        _options.key_prefix,
                        descriptor.owner_id),
                    detail::redis_location_key_schema_t::
                      owner_lease_generation_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      client_server_owner_keys_key (
                        _options.key_prefix,
                        descriptor.owner_id,
                        descriptor.lease_generation),
                    current_owner.empty ()
                      ? placeholder
                      : detail::redis_location_key_schema_t::
                          lease_key (
                            _options.key_prefix,
                            current_owner),
                    old_owner_index,
                    detail::redis_location_key_schema_t::
                      client_server_stamp_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      client_server_stamp_key (
                        _options.key_prefix,
                        descriptor.channel_name),
                    detail::redis_location_key_schema_t::
                      client_server_channel_keys_key (
                        _options.key_prefix,
                        descriptor.channel_name)};
                  const auto args = std::vector<std::string>{
                    intent_name (intent),
                    descriptor.owner_id,
                    std::to_string (
                      descriptor.lease_generation),
                    std::to_string (
                      descriptor.lifecycle_generation),
                    std::to_string (
                      descriptor.descriptor_revision),
                    detail::redis_location_row_codec_t::
                      client_server_immutable_digest (
                        descriptor),
                    json,
                    std::to_string (
                      static_cast<int> (
                        descriptor.state)),
                    std::to_string (descriptor.weight),
                    descriptor.channel_name,
                    canonical_key,
                    current_owner,
                    current_lease};
                  const auto result = redis_get (
                    client ().eval<std::tuple<
                      std::string,
                      std::string,
                      std::string>> (
                      std::string (
                        detail::redis_location_scripts_t::
                          write_client_server),
                      keys.begin (), keys.end (),
                      args.begin (), args.end ()));
                  return detail::
                    redis_location_script_result_t::
                      write_result (
                        std::get<0> (result),
                        std::stoll (std::get<1> (result)),
                        std::stoll (std::get<2> (result)));
              }
              catch (const sw::redis::Error &error) {
                  throw framework_exception_t (
                    framework_error_kind_t::request_failed,
                    error.what (), true);
              }
          });
#else
        (void) descriptor;
        (void) intent;
        return unavailable_write ();
#endif
    }

    task_t<location_write_status_t> remove_client_server (
      client_server_server_descriptor_key_t key,
      location_owner_token_t owner) override
    {
        if (!valid_descriptor_text (key.channel_name)
            || key.server_rid.size () == 0
            || !valid_descriptor_text (owner.owner_id)
            || owner.lease_generation <= 0)
            throw std::invalid_argument (
              "ClientServer descriptor removal is incomplete");
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<location_write_status_t> (
          [this, key = std::move (key),
           owner = std::move (owner)] {
              try {
                  const auto canonical_key =
                    detail::redis_location_key_schema_t::
                      encode_client_server_key (key);
                  const auto keys = std::vector<std::string>{
                    detail::redis_location_key_schema_t::
                      client_server_key (
                        _options.key_prefix, canonical_key),
                    detail::redis_location_key_schema_t::
                      client_server_admission_key (
                        _options.key_prefix, canonical_key),
                    detail::redis_location_key_schema_t::
                      client_server_keys_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      client_server_owner_keys_key (
                        _options.key_prefix,
                        owner.owner_id,
                        owner.lease_generation),
                    detail::redis_location_key_schema_t::
                      client_server_stamp_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      client_server_stamp_key (
                        _options.key_prefix,
                        key.channel_name),
                    detail::redis_location_key_schema_t::
                      client_server_channel_keys_key (
                        _options.key_prefix,
                        key.channel_name)};
                  const auto args = std::vector<std::string>{
                    owner.owner_id,
                    std::to_string (
                      owner.lease_generation),
                    canonical_key};
                  const auto result = redis_get (
                    client ().eval<std::tuple<
                      std::string,
                      std::string,
                      std::string>> (
                      std::string (
                        detail::redis_location_scripts_t::
                          remove_client_server),
                      keys.begin (), keys.end (),
                      args.begin (), args.end ()));
                  return std::get<0> (result) == "stored"
                           ? location_write_status_t::stored
                           : location_write_status_t::
                               ignored_stale;
              }
              catch (const sw::redis::Error &error) {
                  throw framework_exception_t (
                    framework_error_kind_t::request_failed,
                    error.what (), true);
              }
          });
#else
        (void) key;
        (void) owner;
        return unavailable_read<location_write_status_t> ();
#endif
    }

    task_t<location_page_t<
      client_server_server_descriptor_t>>
    list_client_servers (
      std::string channel_name,
      location_page_request_t page = {}) override
    {
        if (!valid_descriptor_text (channel_name))
            throw std::invalid_argument (
              "ClientServer channel name must contain 1..255 bytes without NUL");
        if (page.page_size < 1 || page.page_size > 1000)
            throw std::invalid_argument (
              "ClientServer descriptor page size must be 1..1000");
        const auto offset =
          decode_client_server_page_token (
            page.continuation_token, channel_name);
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<location_page_t<
          client_server_server_descriptor_t>> (
          [this, channel_name = std::move (channel_name),
           page, offset] {
              try {
                  const auto members = redis_get (
                    client ().command<
                      std::vector<std::string>> (
                      "ZRANGE",
                      detail::redis_location_key_schema_t::
                        client_server_channel_keys_key (
                          _options.key_prefix,
                          channel_name),
                      std::to_string (offset),
                      std::to_string (
                        offset
                        + static_cast<std::size_t> (
                          page.page_size))));
                  location_page_t<
                    client_server_server_descriptor_t> result;
                  std::size_t encoded_bytes = 0;
                  std::size_t consumed = 0;
                  for (const auto &canonical_key : members) {
                      if (result.items.size ()
                          == static_cast<std::size_t> (
                            page.page_size))
                          break;
                      const auto fields = redis_get (
                        client ().hmget<std::vector<
                          sw::redis::OptionalString>> (
                          detail::redis_location_key_schema_t::
                            client_server_key (
                              _options.key_prefix,
                              canonical_key),
                          {"json", "gen", "updatedAtMs"}));
                      if (fields.size () < 3 || !fields[0]) {
                          ++consumed;
                          continue;
                      }
                      const auto row_bytes =
                        fields[0]->size ();
                      if (!result.items.empty ()
                          && encoded_bytes + row_bytes
                               > 4u * 1024u * 1024u)
                          break;
                      auto descriptor =
                        detail::redis_location_row_codec_t::
                          decode_client_server (*fields[0]);
                      if (descriptor.channel_name
                          != channel_name) {
                          ++consumed;
                          continue;
                      }
                      if (fields[2])
                          descriptor.updated_at =
                            detail::
                              redis_location_script_result_t::
                                from_unix_ms (
                                  std::stoll (*fields[2]));
                      result.items.push_back (
                        std::move (descriptor));
                      encoded_bytes += row_bytes;
                      ++consumed;
                  }
                  if (consumed < members.size ()
                      || members.size ()
                           > static_cast<std::size_t> (
                             page.page_size))
                      result.continuation_token =
                        encode_client_server_page_token (
                          channel_name, offset + consumed);
                  return result;
              }
              catch (const sw::redis::Error &error) {
                  throw framework_exception_t (
                    framework_error_kind_t::request_failed,
                    error.what (), true);
              }
          });
#else
        (void) channel_name;
        (void) page;
        (void) offset;
        return unavailable_read<location_page_t<
          client_server_server_descriptor_t>> ();
#endif
    }

    task_t<location_write_result_t>
    update_fanout_publisher (
      fanout_publisher_descriptor_t descriptor,
      location_write_intent_t intent) override
    {
        if (!valid_fanout_publisher_descriptor (
              descriptor))
            throw std::invalid_argument (
              "fanout publisher descriptor is incomplete");
        const auto json =
          detail::redis_location_row_codec_t::
            encode_fanout_publisher (descriptor);
        if (json.size () > 1024u * 1024u)
            throw std::invalid_argument (
              "fanout publisher descriptor exceeds 1 MiB");
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<location_write_result_t> (
          [this, descriptor = std::move (descriptor),
           intent, json] {
              try {
                  const auto canonical_key =
                    detail::redis_location_key_schema_t::
                      encode_fanout_publisher_key (
                        {descriptor.channel_name,
                         descriptor.publisher_rid});
                  const auto admission_key =
                    detail::redis_location_key_schema_t::
                      fanout_publisher_admission_key (
                        _options.key_prefix,
                        canonical_key);
                  const auto current = redis_get (
                    client ().hmget<std::vector<
                      sw::redis::OptionalString>> (
                      admission_key,
                      {"ownerId",
                       "ownerLeaseGeneration"}));
                  const auto current_owner =
                    current.size () > 0 && current[0]
                      ? *current[0]
                      : std::string{};
                  const auto current_lease =
                    current.size () > 1 && current[1]
                      ? *current[1]
                      : std::string{};
                  const auto placeholder =
                    detail::redis_location_key_schema_t::
                      schema_key (_options.key_prefix);
                  const auto old_owner_index =
                    current_owner.empty ()
                      ? placeholder
                      : detail::redis_location_key_schema_t::
                          fanout_publisher_owner_keys_key (
                            _options.key_prefix,
                            current_owner,
                            std::stoll (current_lease));
                  const auto keys =
                    std::vector<std::string>{
                      detail::redis_location_key_schema_t::
                        fanout_publisher_key (
                          _options.key_prefix,
                          canonical_key),
                      admission_key,
                      detail::redis_location_key_schema_t::
                        fanout_publisher_keys_key (
                          _options.key_prefix),
                      detail::redis_location_key_schema_t::
                        lease_key (
                          _options.key_prefix,
                          descriptor.owner_id),
                      detail::redis_location_key_schema_t::
                        owner_lease_generation_key (
                          _options.key_prefix),
                      detail::redis_location_key_schema_t::
                        fanout_publisher_owner_keys_key (
                          _options.key_prefix,
                          descriptor.owner_id,
                          descriptor.lease_generation),
                      current_owner.empty ()
                        ? placeholder
                        : detail::redis_location_key_schema_t::
                            lease_key (
                              _options.key_prefix,
                              current_owner),
                      old_owner_index,
                      detail::redis_location_key_schema_t::
                        fanout_publisher_stamp_key (
                          _options.key_prefix),
                      detail::redis_location_key_schema_t::
                        fanout_publisher_stamp_key (
                          _options.key_prefix,
                          descriptor.channel_name),
                      detail::redis_location_key_schema_t::
                        fanout_publisher_channel_keys_key (
                          _options.key_prefix,
                          descriptor.channel_name)};
                  const auto args =
                    std::vector<std::string>{
                      intent_name (intent),
                      descriptor.owner_id,
                      std::to_string (
                        descriptor.lease_generation),
                      std::to_string (
                        descriptor.lifecycle_generation),
                      std::to_string (
                        descriptor.descriptor_revision),
                      detail::redis_location_row_codec_t::
                        fanout_publisher_immutable_digest (
                          descriptor),
                      json,
                      std::to_string (
                        static_cast<int> (
                          descriptor.state)),
                      descriptor.channel_name,
                      canonical_key,
                      current_owner,
                      current_lease};
                  const auto result = redis_get (
                    client ().eval<std::tuple<
                      std::string,
                      std::string,
                      std::string>> (
                      std::string (
                        detail::redis_location_scripts_t::
                          write_fanout_publisher),
                      keys.begin (), keys.end (),
                      args.begin (), args.end ()));
                  return detail::
                    redis_location_script_result_t::
                      write_result (
                        std::get<0> (result),
                        std::stoll (std::get<1> (result)),
                        std::stoll (std::get<2> (result)));
              }
              catch (const sw::redis::Error &error) {
                  throw framework_exception_t (
                    framework_error_kind_t::request_failed,
                    error.what (), true);
              }
          });
#else
        (void) descriptor;
        (void) intent;
        return unavailable_write ();
#endif
    }

    task_t<location_write_status_t>
    remove_fanout_publisher (
      fanout_publisher_descriptor_key_t key,
      location_owner_token_t owner) override
    {
        if (!valid_descriptor_text (key.channel_name)
            || key.publisher_rid.size () == 0
            || !valid_descriptor_text (owner.owner_id)
            || owner.lease_generation <= 0)
            throw std::invalid_argument (
              "fanout publisher descriptor removal is incomplete");
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<location_write_status_t> (
          [this, key = std::move (key),
           owner = std::move (owner)] {
              try {
                  const auto canonical_key =
                    detail::redis_location_key_schema_t::
                      encode_fanout_publisher_key (key);
                  const auto keys =
                    std::vector<std::string>{
                      detail::redis_location_key_schema_t::
                        fanout_publisher_key (
                          _options.key_prefix,
                          canonical_key),
                      detail::redis_location_key_schema_t::
                        fanout_publisher_admission_key (
                          _options.key_prefix,
                          canonical_key),
                      detail::redis_location_key_schema_t::
                        fanout_publisher_keys_key (
                          _options.key_prefix),
                      detail::redis_location_key_schema_t::
                        fanout_publisher_owner_keys_key (
                          _options.key_prefix,
                          owner.owner_id,
                          owner.lease_generation),
                      detail::redis_location_key_schema_t::
                        fanout_publisher_stamp_key (
                          _options.key_prefix),
                      detail::redis_location_key_schema_t::
                        fanout_publisher_stamp_key (
                          _options.key_prefix,
                          key.channel_name),
                      detail::redis_location_key_schema_t::
                        fanout_publisher_channel_keys_key (
                          _options.key_prefix,
                          key.channel_name)};
                  const auto args =
                    std::vector<std::string>{
                      owner.owner_id,
                      std::to_string (
                        owner.lease_generation),
                      canonical_key};
                  const auto result = redis_get (
                    client ().eval<std::tuple<
                      std::string,
                      std::string,
                      std::string>> (
                      std::string (
                        detail::redis_location_scripts_t::
                          remove_fanout_publisher),
                      keys.begin (), keys.end (),
                      args.begin (), args.end ()));
                  return std::get<0> (result) == "stored"
                           ? location_write_status_t::stored
                           : location_write_status_t::
                               ignored_stale;
              }
              catch (const sw::redis::Error &error) {
                  throw framework_exception_t (
                    framework_error_kind_t::request_failed,
                    error.what (), true);
              }
          });
#else
        (void) key;
        (void) owner;
        return unavailable_read<location_write_status_t> ();
#endif
    }

    task_t<location_page_t<
      fanout_publisher_descriptor_t>>
    list_fanout_publishers (
      std::string channel_name,
      location_page_request_t page = {}) override
    {
        if (!valid_descriptor_text (channel_name))
            throw std::invalid_argument (
              "fanout channel name must contain 1..255 bytes without NUL");
        if (page.page_size < 1 || page.page_size > 1000)
            throw std::invalid_argument (
              "fanout publisher descriptor page size must be 1..1000");
        const auto offset =
          decode_fanout_publisher_page_token (
            page.continuation_token, channel_name);
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<location_page_t<
          fanout_publisher_descriptor_t>> (
          [this, channel_name = std::move (channel_name),
           page, offset] {
              try {
                  const auto members = redis_get (
                    client ().command<
                      std::vector<std::string>> (
                      "ZRANGE",
                      detail::redis_location_key_schema_t::
                        fanout_publisher_channel_keys_key (
                          _options.key_prefix,
                          channel_name),
                      std::to_string (offset),
                      std::to_string (
                        offset
                        + static_cast<std::size_t> (
                          page.page_size))));
                  location_page_t<
                    fanout_publisher_descriptor_t> result;
                  std::size_t encoded_bytes = 0;
                  std::size_t consumed = 0;
                  for (const auto &canonical_key : members) {
                      if (result.items.size ()
                          == static_cast<std::size_t> (
                            page.page_size))
                          break;
                      const auto fields = redis_get (
                        client ().hmget<std::vector<
                          sw::redis::OptionalString>> (
                          detail::redis_location_key_schema_t::
                            fanout_publisher_key (
                              _options.key_prefix,
                              canonical_key),
                          {"json", "gen", "updatedAtMs"}));
                      if (fields.size () < 3 || !fields[0]) {
                          ++consumed;
                          continue;
                      }
                      auto descriptor =
                        detail::redis_location_row_codec_t::
                          decode_fanout_publisher (
                            *fields[0]);
                      if (descriptor.channel_name
                          != channel_name) {
                          ++consumed;
                          continue;
                      }
                      if (fields[2])
                          descriptor.updated_at =
                            detail::
                              redis_location_script_result_t::
                                from_unix_ms (
                                  std::stoll (*fields[2]));
                      const auto row_bytes =
                        detail::redis_location_row_codec_t::
                          encode_fanout_publisher (
                            descriptor)
                          .size ();
                      if (!result.items.empty ()
                          && encoded_bytes + row_bytes
                               > 4u * 1024u * 1024u)
                          break;
                      result.items.push_back (
                        std::move (descriptor));
                      encoded_bytes += row_bytes;
                      ++consumed;
                  }
                  if (consumed < members.size ()
                      || members.size ()
                           > static_cast<std::size_t> (
                             page.page_size))
                      result.continuation_token =
                        encode_fanout_publisher_page_token (
                          channel_name,
                          offset + consumed);
                  return result;
              }
              catch (const sw::redis::Error &error) {
                  throw framework_exception_t (
                    framework_error_kind_t::request_failed,
                    error.what (), true);
              }
          });
#else
        (void) channel_name;
        (void) page;
        (void) offset;
        return unavailable_read<location_page_t<
          fanout_publisher_descriptor_t>> ();
#endif
    }

    task_t<location_write_result_t> update_peer (peer_location_t peer,
                                                 location_write_intent_t intent) override
    {
        const auto row_key = detail::redis_location_key_schema_t::encode_peer_key (
          peer_location_key_t{peer.auto_connect_type, peer.mesh_name, peer.role, peer.node_rid,
                              peer.endpoint});
        return write_row (location_kind_t::peer, row_key, peer.mesh_name, peer.owner_id,
                          peer.generation, detail::redis_location_row_codec_t::encode_peer (peer),
                          intent);
    }

    task_t<location_write_result_t> remove_peer (peer_location_key_t key,
                                                 location_owner_token_t owner) override
    {
        return remove_row (location_kind_t::peer,
                           detail::redis_location_key_schema_t::encode_peer_key (key),
                           key.mesh_name, std::move (owner));
    }

    task_t<std::vector<peer_location_t>> list_peers (peer_location_filter_t filter) override
    {
        return list_unpaged<peer_location_t> (
          location_kind_t::peer, std::move (filter),
          [] (std::string_view json) { return detail::redis_location_row_codec_t::decode_peer (json); });
    }

    task_t<location_write_result_t> update_spot (spot_location_t spot,
                                                 location_write_intent_t intent) override
    {
        const auto row_key = detail::redis_location_key_schema_t::encode_spot_key (
          spot_location_key_t{spot.mesh_name, spot.spot_rid});
        return write_row (location_kind_t::spot, row_key, spot.mesh_name, spot.owner_id,
                          spot.generation, detail::redis_location_row_codec_t::encode_spot (spot),
                          intent);
    }

    task_t<location_write_result_t> remove_spot (spot_location_key_t key,
                                                 location_owner_token_t owner) override
    {
        return remove_row (location_kind_t::spot,
                           detail::redis_location_key_schema_t::encode_spot_key (key),
                           key.mesh_name, std::move (owner));
    }

    task_t<std::optional<spot_location_t>> resolve_spot (spot_location_key_t key) override
    {
        return resolve_row<spot_location_t> (
          location_kind_t::spot, detail::redis_location_key_schema_t::encode_spot_key (key),
          [] (std::string_view json) { return detail::redis_location_row_codec_t::decode_spot (json); });
    }

    task_t<location_page_t<spot_location_t>>
    list_spots (spot_location_filter_t filter, location_page_request_t page = {}) override
    {
        return list_paged<spot_location_t> (
          location_kind_t::spot, std::move (filter), page,
          [] (std::string_view json) { return detail::redis_location_row_codec_t::decode_spot (json); });
    }

    task_t<location_write_result_t> update_actor (actor_location_t actor,
                                                  location_write_intent_t intent) override
    {
        const auto row_key = detail::redis_location_key_schema_t::encode_actor_key (
          actor_location_key_t{actor.mesh_name, actor.actor_id});
        return write_row (location_kind_t::actor, row_key, actor.mesh_name, actor.owner_id,
                          0, detail::redis_location_row_codec_t::encode_actor (actor),
                          intent);
    }

    task_t<location_write_result_t> remove_actor (actor_location_key_t key,
                                                  location_owner_token_t owner) override
    {
        return remove_row (location_kind_t::actor,
                           detail::redis_location_key_schema_t::encode_actor_key (key),
                           key.mesh_name, std::move (owner));
    }

    task_t<std::optional<actor_location_t>> resolve_actor (actor_location_key_t key) override
    {
        return resolve_row<actor_location_t> (
          location_kind_t::actor, detail::redis_location_key_schema_t::encode_actor_key (key),
          [] (std::string_view json) { return detail::redis_location_row_codec_t::decode_actor (json); });
    }

    task_t<location_page_t<actor_location_t>>
    list_actors (actor_location_filter_t filter, location_page_request_t page = {}) override
    {
        return list_paged<actor_location_t> (
          location_kind_t::actor, std::move (filter), page,
          [] (std::string_view json) { return detail::redis_location_row_codec_t::decode_actor (json); });
    }

    task_t<location_write_result_t> update_route (route_location_t route,
                                                  location_write_intent_t intent) override
    {
        const auto row_key = detail::redis_location_key_schema_t::encode_route_key (
          route_location_key_t{route.route_kind, route.route_key});
        return write_row (location_kind_t::route, row_key, std::nullopt, route.owner_id,
                          route.generation, detail::redis_location_row_codec_t::encode_route (route),
                          intent);
    }

    task_t<location_write_result_t> remove_route (route_location_key_t key,
                                                  location_owner_token_t owner) override
    {
        return remove_row (location_kind_t::route,
                           detail::redis_location_key_schema_t::encode_route_key (key),
                           std::nullopt, std::move (owner));
    }

    task_t<std::optional<route_location_t>> resolve_route (route_location_key_t key) override
    {
        return resolve_row<route_location_t> (
          location_kind_t::route, detail::redis_location_key_schema_t::encode_route_key (key),
          [] (std::string_view json) { return detail::redis_location_row_codec_t::decode_route (json); });
    }

    task_t<location_page_t<route_location_t>>
    list_routes (route_location_filter_t filter, location_page_request_t page = {}) override
    {
        return list_paged<route_location_t> (
          location_kind_t::route, std::move (filter), page,
          [] (std::string_view json) { return detail::redis_location_row_codec_t::decode_route (json); });
    }

    task_t<owner_lease_claim_result_t> claim_owner_lease (
      std::string owner_id,
      std::chrono::milliseconds lease_ttl) override
    {
        if (owner_id.empty () || lease_ttl.count () <= 0)
            throw std::invalid_argument (
              "owner lease claim is incomplete");
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<owner_lease_claim_result_t> (
          [this, owner_id = std::move (owner_id), lease_ttl] {
              try {
                  const auto keys = std::vector<std::string>{
                    detail::redis_location_key_schema_t::lease_key (
                      _options.key_prefix, owner_id),
                    detail::redis_location_key_schema_t::
                      owner_lease_generation_key (
                        _options.key_prefix)};
                  const auto args = std::vector<std::string>{
                    owner_id,
                    std::to_string (lease_ttl.count ())};
                  const auto result = redis_get (
                    client ().eval<std::tuple<
                      std::string,
                      long long,
                      long long,
                      long long>> (
                      std::string (
                        detail::redis_location_scripts_t::
                          claim_owner_lease),
                      keys.begin (), keys.end (), args.begin (),
                      args.end ()));
                  const auto &status = std::get<0> (result);
                  if (status == "conflict")
                      return owner_lease_claim_result_t{
                        owner_lease_conflict_t{}};
                  if (status == "exhausted")
                      return owner_lease_claim_result_t{
                        owner_lease_generation_exhausted_t{}};
                  if (status != "claimed")
                      throw sw::redis::Error (
                        "invalid owner lease claim result");
                  return owner_lease_claim_result_t{
                    owner_lease_claimed_t{
                      {owner_id,
                       static_cast<std::int64_t> (
                         std::get<1> (result))},
                      detail::redis_location_script_result_t::
                        from_unix_ms (
                          static_cast<std::int64_t> (
                            std::get<3> (result))),
                      detail::redis_location_script_result_t::
                        from_unix_ms (
                          static_cast<std::int64_t> (
                            std::get<2> (result)))}};
              }
              catch (const sw::redis::Error &error) {
                  throw framework_exception_t (
                    framework_error_kind_t::request_failed,
                    error.what (), true);
              }
          });
#else
        (void) owner_id;
        (void) lease_ttl;
        return unavailable_read<owner_lease_claim_result_t> ();
#endif
    }

    task_t<owner_lease_read_result_t> read_owner_lease (
      std::string owner_id) override
    {
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<owner_lease_read_result_t> (
          [this, owner_id = std::move (owner_id)] {
              try {
                  const auto keys = std::vector<std::string>{
                    detail::redis_location_key_schema_t::lease_key (
                      _options.key_prefix, owner_id)};
                  const auto args = std::vector<std::string>{};
                  const auto result = redis_get (
                    client ().eval<std::tuple<
                      std::string,
                      long long,
                      long long,
                      long long>> (
                      std::string (
                        detail::redis_location_scripts_t::
                          read_owner_lease),
                      keys.begin (), keys.end (), args.begin (),
                      args.end ()));
                  if (std::get<0> (result) == "missing")
                      return owner_lease_read_result_t{
                        owner_lease_missing_t{}};
                  if (std::get<0> (result) != "found")
                      throw sw::redis::Error (
                        "invalid owner lease read result");
                  return owner_lease_read_result_t{
                    owner_lease_found_t{
                      {owner_id,
                       static_cast<std::int64_t> (
                         std::get<1> (result))},
                      detail::redis_location_script_result_t::
                        from_unix_ms (
                          static_cast<std::int64_t> (
                            std::get<3> (result))),
                      detail::redis_location_script_result_t::
                        from_unix_ms (
                          static_cast<std::int64_t> (
                            std::get<2> (result)))}};
              }
              catch (const sw::redis::Error &error) {
                  throw framework_exception_t (
                    framework_error_kind_t::request_failed,
                    error.what (), true);
              }
          });
#else
        (void) owner_id;
        return unavailable_read<owner_lease_read_result_t> ();
#endif
    }

    task_t<owner_lease_renew_result_t> renew_owner_lease (
      location_owner_token_t token,
      std::chrono::milliseconds lease_ttl) override
    {
        if (lease_ttl.count () <= 0)
            throw std::invalid_argument (
              "owner lease TTL must be positive");
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<owner_lease_renew_result_t> (
          [this, token = std::move (token), lease_ttl] {
              try {
                  const auto keys = std::vector<std::string>{
                    detail::redis_location_key_schema_t::lease_key (
                      _options.key_prefix, token.owner_id)};
                  const auto args = std::vector<std::string>{
                    std::to_string (token.lease_generation),
                    std::to_string (lease_ttl.count ())};
                  const auto result = redis_get (
                    client ().eval<std::tuple<
                      std::string,
                      long long,
                      long long>> (
                      std::string (
                        detail::redis_location_scripts_t::
                          renew_owner_lease_exact),
                      keys.begin (), keys.end (), args.begin (),
                      args.end ()));
                  if (std::get<0> (result) == "stale")
                      return owner_lease_renew_result_t{
                        owner_lease_stale_t{}};
                  if (std::get<0> (result) != "renewed")
                      throw sw::redis::Error (
                        "invalid owner lease renew result");
                  return owner_lease_renew_result_t{
                    owner_lease_renewed_t{
                      detail::redis_location_script_result_t::
                        from_unix_ms (
                          static_cast<std::int64_t> (
                            std::get<2> (result))),
                      detail::redis_location_script_result_t::
                        from_unix_ms (
                          static_cast<std::int64_t> (
                            std::get<1> (result)))}};
              }
              catch (const sw::redis::Error &error) {
                  throw framework_exception_t (
                    framework_error_kind_t::request_failed,
                    error.what (), true);
              }
          });
#else
        (void) token;
        (void) lease_ttl;
        return unavailable_read<owner_lease_renew_result_t> ();
#endif
    }

    task_t<owner_lease_release_result_t> release_owner_lease (
      location_owner_token_t token) override
    {
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<owner_lease_release_result_t> (
          [this, token = std::move (token)] {
              try {
                  const auto keys = std::vector<std::string>{
                    detail::redis_location_key_schema_t::lease_key (
                      _options.key_prefix, token.owner_id)};
                  const auto args = std::vector<std::string>{
                    std::to_string (token.lease_generation)};
                  const auto result = redis_get (
                    client ().eval<
                      std::tuple<std::string, long long>> (
                      std::string (
                        detail::redis_location_scripts_t::
                          release_owner_lease),
                      keys.begin (), keys.end (), args.begin (),
                      args.end ()));
                  if (std::get<0> (result) == "stale")
                      return owner_lease_release_result_t{
                        owner_lease_stale_t{}};
                  if (std::get<0> (result) != "released")
                      throw sw::redis::Error (
                        "invalid owner lease release result");
                  return owner_lease_release_result_t{
                    owner_lease_released_t{}};
              }
              catch (const sw::redis::Error &error) {
                  throw framework_exception_t (
                    framework_error_kind_t::request_failed,
                    error.what (), true);
              }
          });
#else
        (void) token;
        return unavailable_read<owner_lease_release_result_t> ();
#endif
    }

    task_t<authority_read_result_t> read_authority (
      authority_key_t key,
      std::stop_token cancellation = {}) override
    {
        if (cancellation.stop_requested ())
            return cancelled<authority_read_result_t> ();
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<authority_read_result_t> (
          [this, key = std::move (key)] {
              try {
                  return read_authority_sync (key);
              }
              catch (const sw::redis::Error &error) {
                  throw framework_exception_t (
                    framework_error_kind_t::request_failed,
                    error.what (), true);
              }
          });
#else
        (void) key;
        return unavailable_read<authority_read_result_t> ();
#endif
    }

    task_t<authority_compare_exchange_result_t>
    compare_exchange_authority (
      authority_key_t key,
      std::string expected_store_version,
      authority_mutation_t mutation,
      std::stop_token cancellation = {}) override
    {
        if (cancellation.stop_requested ())
            return cancelled<
              authority_compare_exchange_result_t> ();
        validate_authority_mutation (mutation);
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<
          authority_compare_exchange_result_t> (
          [this, key = std::move (key),
           expected_store_version =
             std::move (expected_store_version),
           mutation = std::move (mutation)] () mutable {
              try {
                  const auto current =
                    read_authority_sync (key);
                  const auto *snapshot =
                    std::get_if<authority_snapshot_t> (
                      &current);
                  const auto *put =
                    std::get_if<authority_put_t> (&mutation);
                  const auto target_owner =
                    put && put->target_owner
                      ? *put->target_owner
                      : location_owner_token_t{};
                  const auto fence =
                    put && put->relocation_capacity_fence
                      ? put->relocation_capacity_fence->value
                      : std::string{};
                  const auto current_owner =
                    snapshot
                      ? snapshot->owner
                      : location_owner_token_t{};
                  const auto source_allocation =
                    snapshot
                      ? snapshot->allocation
                      : placement_allocation_t{};
                  auto target_mesh =
                    source_allocation.mesh_name;
                  auto target_node =
                    std::string (
                      source_allocation.node_rid.value ());
                  auto target_lifecycle =
                    source_allocation
                      .node_lifecycle_generation;
                  auto target_kind =
                    source_allocation.object_kind;
                  auto target_type =
                    source_allocation.stable_type;
                  if (!fence.empty ()) {
                      const auto reservation_key =
                        detail::redis_location_key_schema_t::
                          relocation_capacity_reservation_key (
                            _options.key_prefix, fence);
                      const auto fields = redis_get (
                        client ().hmget<
                          std::vector<
                            sw::redis::OptionalString>> (
                          reservation_key,
                          {"targetMesh",
                           "targetNode",
                           "targetLifecycleGeneration",
                           "objectKind",
                           "stableType"}));
                      if (fields.size () == 5
                          && std::all_of (
                            fields.begin (), fields.end (),
                            [] (const auto &value) {
                                return static_cast<bool> (
                                  value);
                            })) {
                          target_mesh = *fields[0];
                          target_node = *fields[1];
                          target_lifecycle =
                            std::stoull (*fields[2]);
                          target_kind =
                            detail::
                              redis_location_key_schema_t::
                                parse_object_kind_token (
                                  *fields[3]);
                          target_type = *fields[4];
                      }
                  }
                  const auto keys = std::vector<std::string>{
                    detail::redis_location_key_schema_t::
                      authority_key (
                        _options.key_prefix, key.value),
                    detail::redis_location_key_schema_t::
                      authority_keys_key (_options.key_prefix),
                    detail::redis_location_key_schema_t::
                      authority_store_revision_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      authority_object_generation_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      authority_owner_generation_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      lease_key (
                        _options.key_prefix,
                        current_owner.owner_id),
                    detail::redis_location_key_schema_t::
                      lease_key (
                        _options.key_prefix,
                        target_owner.owner_id),
                    detail::redis_location_key_schema_t::
                      relocation_capacity_reservation_key (
                        _options.key_prefix, fence),
                    detail::redis_location_key_schema_t::
                      capacity_node_active_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      capacity_type_active_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      capacity_node_pending_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      capacity_type_pending_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      capacity_node_active_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      capacity_type_active_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      mesh_node_admission_key (
                        _options.key_prefix,
                        target_mesh,
                        target_node),
                    detail::redis_location_key_schema_t::
                      authority_history_key (
                        _options.key_prefix, key.value),
                    detail::redis_location_key_schema_t::
                      authority_history_revisions_key (
                        _options.key_prefix, key.value),
                    detail::redis_location_key_schema_t::
                      authority_index_gc_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      membership_current_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      membership_history_key (
                        _options.key_prefix, key.value),
                    detail::redis_location_key_schema_t::
                      membership_history_revisions_key (
                        _options.key_prefix, key.value)};
                  std::string mutation_name = "delete";
                  std::string transition = "preserve";
                  std::string payload;
                  if (put) {
                      mutation_name = "put";
                      transition = transition_name (
                        put->generation_transition);
                      payload = bytes_to_string (put->payload);
                  }
                  const auto args = std::vector<std::string>{
                    key.value,
                    "found",
                    expected_store_version,
                    mutation_name,
                    transition,
                    std::move (payload),
                    target_owner.owner_id,
                    std::to_string (
                      target_owner.lease_generation),
                    fence,
                    detail::redis_location_key_schema_t::
                      authority_index_member (key.value),
                    detail::redis_location_key_schema_t::
                      capacity_node_field (
                        source_allocation.mesh_name,
                        source_allocation.node_rid.value (),
                        source_allocation
                          .node_lifecycle_generation),
                    detail::redis_location_key_schema_t::
                      capacity_type_field (
                        source_allocation.mesh_name,
                        source_allocation.node_rid.value (),
                        source_allocation
                          .node_lifecycle_generation,
                        source_allocation.object_kind,
                        source_allocation.stable_type),
                    detail::redis_location_key_schema_t::
                      capacity_node_field (
                        target_mesh, target_node,
                        target_lifecycle),
                    detail::redis_location_key_schema_t::
                      capacity_type_field (
                        target_mesh, target_node,
                        target_lifecycle, target_kind,
                        target_type),
                    revision_lower_hex (
                      expected_store_version)};
                  const auto result = redis_get (
                    client ().eval<std::vector<std::string>> (
                      std::string (
                        detail::redis_location_scripts_t::
                          compare_exchange_authority),
                      keys.begin (), keys.end (), args.begin (),
                      args.end ()));
                  if (result.size () != 16)
                      throw sw::redis::Error (
                        "invalid authority CAS result");
                  if (result[0] == "exhausted")
                      return authority_compare_exchange_result_t{
                        authority_generation_exhausted_t{}};
                  if (result[0] == "conflict")
                      return authority_compare_exchange_result_t{
                        authority_conflict_t{
                          parse_cas_authority_result (result)}};
                  if (result[0] == "deleted")
                      return authority_compare_exchange_result_t{
                        authority_deleted_t{
                          result[2],
                          detail::redis_location_script_result_t::
                            from_unix_ms (
                              std::stoll (result[15]))}};
                  if (result[0] != "stored")
                      throw sw::redis::Error (
                        "unknown authority CAS result");
                  return authority_compare_exchange_result_t{
                    authority_stored_t{
                      std::get<authority_snapshot_t> (
                        parse_cas_authority_result (
                          result))}};
              }
              catch (const sw::redis::Error &error) {
                  throw framework_exception_t (
                    framework_error_kind_t::request_failed,
                    error.what (), true);
              }
          });
#else
        (void) key;
        (void) expectation;
        (void) mutation;
        return unavailable_read<
          authority_compare_exchange_result_t> ();
#endif
    }

    task_t<authority_scan_result_t> list_authorities (
      std::string prefix,
      std::optional<authority_scan_cursor_t> cursor,
      std::size_t limit,
      std::stop_token cancellation = {}) override
    {
        if (cancellation.stop_requested ())
            return cancelled<authority_scan_result_t> ();
        if (limit == 0 || limit > 1000)
            throw std::invalid_argument (
              "authority scan limit must be 1..1000");
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<authority_scan_result_t> (
          [this, prefix = std::move (prefix),
           cursor = std::move (cursor), limit] () mutable {
              try {
                  std::string scan_id;
                  std::uint64_t sequence = 0;
                  std::string last_member;
                  std::string watermark;
                  const auto scan_watermark_key =
                    detail::redis_location_key_schema_t::
                      authority_scans_watermark_key (
                        _options.key_prefix);
                  if (!cursor) {
                      cleanup_expired_scans_sync ();
                      const auto nonce = redis_get (
                        client ().command<long long> (
                          "HINCRBY",
                          detail::redis_location_key_schema_t::
                            authority_store_revision_key (
                              _options.key_prefix),
                          "scanGeneration", "1"));
                      std::ostringstream encoded;
                      encoded << std::hex << std::setfill ('0')
                              << std::setw (32)
                              << static_cast<std::uint64_t> (
                                   nonce);
                      scan_id = encoded.str ();
                      const auto stored_watermark = redis_get (
                        client ().hget (
                          detail::redis_location_key_schema_t::
                            authority_store_revision_key (
                              _options.key_prefix),
                          "storeRevision"));
                      watermark =
                        stored_watermark
                          ? *stored_watermark
                          : "0";
                      const auto scan_key =
                        detail::redis_location_key_schema_t::
                          authority_scan_key (
                            _options.key_prefix, scan_id);
                      const auto watermark_member =
                        revision_lower_hex (watermark) + ":"
                        + scan_id;
                      (void) redis_get (
                        client ().command<long long> (
                          "HSET", scan_key, "prefix", prefix,
                          "watermark", watermark, "last", "",
                          "sequence", "0",
                          "watermarkMember",
                          watermark_member));
                      (void) redis_get (
                        client ().command<long long> (
                          "PEXPIRE", scan_key, "60000"));
                      (void) redis_get (
                        client ().command<long long> (
                          "ZADD", scan_watermark_key, "0",
                          watermark_member));
                      (void) redis_get (
                        client ().command<long long> (
                          "ZADD",
                          detail::redis_location_key_schema_t::
                            authority_scans_expiry_key (
                              _options.key_prefix),
                          std::to_string (
                            redis_time_ms_sync () + 60000),
                          scan_id));
                  } else {
                      if (!parse_authority_cursor (
                            cursor->encoded (), scan_id,
                            sequence))
                          return authority_scan_result_t{
                            authority_scan_expired_t{}};
                      const auto fields = redis_get (
                        client ().hmget<
                          std::vector<
                            sw::redis::OptionalString>> (
                          detail::redis_location_key_schema_t::
                            authority_scan_key (
                              _options.key_prefix, scan_id),
                          {"prefix", "watermark", "last",
                           "sequence"}));
                      if (fields.size () != 4
                          || !fields[0] || !fields[1]
                          || !fields[2] || !fields[3]
                          || *fields[0] != prefix
                          || std::stoull (*fields[3])
                               != sequence)
                          return authority_scan_result_t{
                            authority_scan_expired_t{}};
                      watermark = *fields[1];
                      last_member = *fields[2];
                  }
                  const auto encoded_prefix =
                    detail::redis_location_key_schema_t::
                      authority_index_member (prefix);
                  const auto lower =
                    last_member.empty ()
                      ? "[" + encoded_prefix
                      : "(" + last_member;
                  const auto upper =
                    "(" + encoded_prefix + "g";
                  auto members = redis_get (
                    client ().command<
                      std::vector<std::string>> (
                      "ZRANGEBYLEX",
                      detail::redis_location_key_schema_t::
                        authority_keys_key (
                          _options.key_prefix),
                      lower, upper, "LIMIT", "0",
                      std::to_string (limit + 1)));
                  authority_page_t page;
                  std::size_t encoded_size = 0;
                  std::size_t consumed = 0;
                  for (; consumed < members.size ()
                         && page.items.size () < limit;
                       ++consumed) {
                      const auto authority_key =
                        detail::redis_location_key_schema_t::
                          decode_authority_index_member (
                            members[consumed]);
                      const auto read =
                        read_authority_at_sync (
                          authority_key_t{authority_key},
                          watermark);
                      const auto *snapshot =
                        std::get_if<authority_snapshot_t> (
                          &read);
                      if (!snapshot)
                      {
                          last_member = members[consumed];
                          continue;
                      }
                      const auto item_size =
                        authority_key.size ()
                        + snapshot->payload.size ();
                      if (!page.items.empty ()
                          && encoded_size + item_size
                               > 4u * 1024u * 1024u)
                          break;
                      last_member = members[consumed];
                      page.items.push_back (
                        {{authority_key}, *snapshot});
                      encoded_size += item_size;
                  }
                  const auto has_more =
                    consumed < members.size ()
                    || members.size () > limit;
                  const auto scan_key =
                    detail::redis_location_key_schema_t::
                      authority_scan_key (
                        _options.key_prefix, scan_id);
                  if (has_more) {
                      ++sequence;
                      (void) redis_get (
                        client ().command<long long> (
                          "HSET", scan_key, "last",
                          last_member, "sequence",
                          std::to_string (sequence)));
                      (void) redis_get (
                        client ().command<long long> (
                          "PEXPIRE", scan_key, "60000"));
                      (void) redis_get (
                        client ().command<long long> (
                          "ZADD",
                          detail::redis_location_key_schema_t::
                            authority_scans_expiry_key (
                              _options.key_prefix),
                          std::to_string (
                            redis_time_ms_sync () + 60000),
                          scan_id));
                      page.next_cursor =
                        authority_scan_cursor_t{
                          scan_id + ":"
                          + std::to_string (sequence)};
                  } else {
                      const auto watermark_member =
                        revision_lower_hex (watermark) + ":"
                        + scan_id;
                      (void) redis_get (
                        client ().del (scan_key));
                      (void) redis_get (
                        client ().command<long long> (
                          "ZREM", scan_watermark_key,
                          watermark_member));
                      (void) redis_get (
                        client ().command<long long> (
                          "ZREM",
                          detail::redis_location_key_schema_t::
                            authority_scans_expiry_key (
                              _options.key_prefix),
                          scan_id));
                      gc_authority_history_sync ();
                  }
                  return authority_scan_result_t{
                    std::move (page)};
              }
              catch (const sw::redis::Error &error) {
                  throw framework_exception_t (
                    framework_error_kind_t::request_failed,
                    error.what (), true);
              }
          });
#else
        (void) prefix;
        (void) cursor;
        (void) limit;
        return unavailable_read<authority_scan_result_t> ();
#endif
    }

    task_t<relocation_capacity_reserve_result_t>
    reserve_relocation_capacity (
      relocation_capacity_reserve_request_t request,
      std::stop_token cancellation = {}) override
    {
        if (cancellation.stop_requested ())
            return cancelled<
              relocation_capacity_reserve_result_t> ();
        if (all_zero (request.reservation_id)
            || request.key.value.empty ()
            || request.expected_store_version.empty ()
            || request.stable_type.empty ()
            || request.capacity_delta == 0
            || request.capacity_delta
                 > static_cast<std::uint32_t> (
                   std::numeric_limits<std::int32_t>::max ()))
            throw std::invalid_argument (
              "relocation capacity reservation is incomplete");
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<
          relocation_capacity_reserve_result_t> (
          [this, request = std::move (request)] {
              try {
                  const auto fence =
                    reservation_id_key (
                      request.reservation_id);
                  const auto keys = std::vector<std::string>{
                    detail::redis_location_key_schema_t::
                      authority_key (
                        _options.key_prefix,
                        request.key.value),
                    detail::redis_location_key_schema_t::
                      lease_key (
                        _options.key_prefix,
                        request.source.owner.owner_id),
                    detail::redis_location_key_schema_t::
                      lease_key (
                        _options.key_prefix,
                        request.target.owner.owner_id),
                    detail::redis_location_key_schema_t::
                      relocation_capacity_reservation_key (
                        _options.key_prefix, fence),
                    detail::redis_location_key_schema_t::
                      capacity_node_pending_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      capacity_type_pending_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      capacity_node_active_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      capacity_type_active_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      mesh_node_admission_key (
                        _options.key_prefix,
                        request.target.mesh_name,
                        request.target.node_rid.value ())};
                  const auto args = std::vector<std::string>{
                    relocation_capacity_fingerprint (request),
                    fence,
                    request.key.value,
                    request.expected_store_version,
                    detail::redis_location_key_schema_t::
                      object_kind_token (
                        request.object_kind),
                    request.stable_type,
                    std::to_string (
                      request.source
                        .node_lifecycle_generation),
                    request.source.owner.owner_id,
                    std::to_string (
                      request.source.owner
                        .lease_generation),
                    std::to_string (
                      request.target
                        .node_lifecycle_generation),
                    request.target.owner.owner_id,
                    std::to_string (
                      request.target.owner
                        .lease_generation),
                    std::to_string (
                      request.capacity_delta),
                    request.source.mesh_name,
                    std::string (
                      request.source.node_rid.value ()),
                    request.target.mesh_name,
                    std::string (
                      request.target.node_rid.value ()),
                    std::string{},
                    std::string{},
                    detail::redis_location_key_schema_t::
                      capacity_node_field (
                        request.target.mesh_name,
                        request.target.node_rid.value (),
                        request.target
                          .node_lifecycle_generation),
                    detail::redis_location_key_schema_t::
                      capacity_type_field (
                        request.target.mesh_name,
                        request.target.node_rid.value (),
                        request.target
                          .node_lifecycle_generation,
                        request.object_kind,
                        request.stable_type),
                    detail::redis_location_key_schema_t::
                      encode_mesh_node_key (
                        {request.source.mesh_name,
                         zlink::routing_id_t::from (
                           std::string (
                             request.source.node_rid.value ()))}),
                    detail::redis_location_key_schema_t::
                      encode_mesh_node_key (
                        {request.target.mesh_name,
                         zlink::routing_id_t::from (
                           std::string (
                             request.target.node_rid.value ()))})};
                  const auto result = redis_get (
                    client ().eval<
                      std::tuple<std::string, std::string>> (
                      std::string (
                        detail::redis_location_scripts_t::
                          reserve_relocation_capacity),
                      keys.begin (), keys.end (), args.begin (),
                      args.end ()));
                  const auto &status = std::get<0> (result);
                  if (status == "reserved")
                      return relocation_capacity_reserve_result_t{
                        relocation_capacity_reserved_t{
                          {std::get<1> (result)}}};
                  if (status == "already")
                      return relocation_capacity_reserve_result_t{
                        relocation_capacity_already_reserved_t{
                          {std::get<1> (result)}}};
                  if (status == "unavailable")
                      return relocation_capacity_reserve_result_t{
                        relocation_capacity_target_unavailable_t{}};
                  if (status == "exhausted")
                      return relocation_capacity_reserve_result_t{
                        relocation_capacity_exhausted_t{}};
                  if (status == "conflict")
                      return relocation_capacity_reserve_result_t{
                        relocation_capacity_conflict_t{
                          read_authority_sync (
                            request.key)}};
                  throw sw::redis::Error (
                    "invalid relocation capacity reserve result");
              }
              catch (const sw::redis::Error &error) {
                  throw framework_exception_t (
                    framework_error_kind_t::request_failed,
                    error.what (), true);
              }
          });
#else
        (void) request;
        return unavailable_read<
          relocation_capacity_reserve_result_t> ();
#endif
    }

    task_t<object_reserve_result_t> reserve (
      object_reserve_request_t request,
      std::stop_token cancellation = {}) override
    {
        if (cancellation.stop_requested ())
            return cancelled<object_reserve_result_t> ();
        if (request.creating_payload.size () > 1024u * 1024u
            || request.intent.request_encoded_size
                 > 1024u * 1024u
            || request.pending_capacity_delta == 0
            || request.pending_capacity_delta
                 > static_cast<std::uint32_t> (
                   std::numeric_limits<std::int32_t>::max ()))
            throw std::invalid_argument (
              "object reservation payload exceeds 1 MiB");
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<object_reserve_result_t> (
          [this, request = std::move (request)] {
              try {
                  const auto key = object_key (request.key);
                  const auto reservation_id =
                    random_lower_hex_128 ();
                  const auto keys = std::vector<std::string>{
                    detail::redis_location_key_schema_t::
                      authority_key (_options.key_prefix, key),
                    detail::redis_location_key_schema_t::
                      authority_keys_key (_options.key_prefix),
                    detail::redis_location_key_schema_t::
                      authority_store_revision_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      authority_object_generation_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      authority_owner_generation_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      lease_key (
                        _options.key_prefix,
                        request.target.owner.owner_id),
                    detail::redis_location_key_schema_t::
                      capacity_node_pending_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      capacity_type_pending_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      capacity_node_active_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      capacity_type_active_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      creation_reservation_key (
                        _options.key_prefix,
                        reservation_id),
                    detail::redis_location_key_schema_t::
                      mesh_node_admission_key (
                        _options.key_prefix,
                        request.target.mesh_name,
                        request.target.node_rid.value ()),
                    detail::redis_location_key_schema_t::
                      authority_index_gc_key (
                        _options.key_prefix)};
                  const auto args = std::vector<std::string>{
                    key,
                    object_reservation_fingerprint (request),
                    request.intent.stable_type,
                    request.target.owner.owner_id,
                    std::to_string (
                      request.target.owner.lease_generation),
                    bytes_to_string (
                      request.creating_payload),
                    std::to_string (
                      request.pending_capacity_delta),
                    request.target.mesh_name,
                    std::string (
                      request.target.node_rid.value ()),
                    std::to_string (
                      request.target
                        .node_lifecycle_generation),
                    detail::redis_location_key_schema_t::
                      object_kind_token (
                        request.key.kind),
                    std::string{},
                    request.intent.placement_profile
                      ? std::string (
                          request.intent.placement_profile->value ())
                      : std::string{},
                    std::string{},
                    detail::redis_location_key_schema_t::
                      authority_index_member (key),
                    reservation_id,
                    detail::redis_location_key_schema_t::
                      capacity_node_field (
                        request.target.mesh_name,
                        request.target.node_rid.value (),
                        request.target
                          .node_lifecycle_generation),
                    detail::redis_location_key_schema_t::
                      capacity_type_field (
                        request.target.mesh_name,
                        request.target.node_rid.value (),
                        request.target
                          .node_lifecycle_generation,
                        request.key.kind,
                        request.intent.stable_type),
                    detail::redis_location_key_schema_t::
                      encode_mesh_node_key (
                        {request.target.mesh_name,
                         zlink::routing_id_t::from (
                           std::string (
                             request.target.node_rid.value ()))}),
                    request.intent.request_content_reference,
                    byte_array_key (
                      request.intent.request_sha256),
                    std::to_string (
                      request.intent.request_encoded_size)};
                  const auto result = redis_get (
                    client ().eval<std::vector<std::string>> (
                      std::string (
                        detail::redis_location_scripts_t::
                          reserve_object),
                      keys.begin (), keys.end (), args.begin (),
                      args.end ()));
                  if (result.size () != 19)
                      throw sw::redis::Error (
                        "invalid object reserve result");
                  const auto snapshot =
                    parse_object_snapshot_result (result);
                  if (result[0] == "already_exists")
                      return object_reserve_result_t{
                        object_already_exists_t{snapshot}};
                  if (result[0] == "type_mismatch")
                      return object_reserve_result_t{
                        object_type_mismatch_t{snapshot}};
                  if (result[0] == "capacity")
                      return object_reserve_result_t{
                        object_placement_capacity_exhausted_t{}};
                  if (result[0] == "conflict"
                      && !result[1].empty ())
                      return object_reserve_result_t{
                        object_reserve_conflict_t{
                          snapshot}};
                  if (result[0] == "conflict")
                      return object_reserve_result_t{
                        object_reserve_conflict_t{
                          authority_missing_t{
                            snapshot.store_now}}};
                  if (result[0] == "exhausted")
                      return object_reserve_result_t{
                        authority_generation_exhausted_t{}};
                  if (result[0] != "reserved")
                      throw sw::redis::Error (
                        "unknown object reserve result");
                  object_reservation_fence_t fence{
                    reservation_id,
                    result[1],
                    std::stoull (result[3]),
                    std::stoull (result[4]),
                    request.target,
                    request.pending_capacity_delta};
                  return object_reserve_result_t{
                    object_reserved_t{
                      std::move (fence), snapshot}};
              }
              catch (const sw::redis::Error &error) {
                  throw framework_exception_t (
                    framework_error_kind_t::request_failed,
                    error.what (), true);
              }
          });
#else
        (void) request;
        return unavailable_read<object_reserve_result_t> ();
#endif
    }

    task_t<object_commit_result_t> commit (
      object_commit_request_t request,
      std::stop_token cancellation = {}) override
    {
        if (cancellation.stop_requested ())
            return cancelled<object_commit_result_t> ();
        if (request.ready_payload.size () > 1024u * 1024u)
            throw std::invalid_argument (
              "object commit payload exceeds 1 MiB");
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<object_commit_result_t> (
          [this, request = std::move (request)] {
              try {
                  const auto key = object_key (request.key);
                  const auto stored_before_commit =
                    read_authority_sync (
                      authority_key_t{key});
                  const auto *snapshot_before_commit =
                    std::get_if<authority_snapshot_t> (
                      &stored_before_commit);
                  const auto stable_type =
                    snapshot_before_commit
                      ? snapshot_before_commit
                          ->allocation.stable_type
                      : std::string{};
                  const auto keys = std::vector<std::string>{
                    detail::redis_location_key_schema_t::
                      authority_key (_options.key_prefix, key),
                    detail::redis_location_key_schema_t::
                      creation_reservation_key (
                        _options.key_prefix,
                        request.fence.reservation_id),
                    detail::redis_location_key_schema_t::
                      authority_store_revision_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      lease_key (
                        _options.key_prefix,
                        request.fence.target.owner.owner_id),
                    detail::redis_location_key_schema_t::
                      capacity_node_pending_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      capacity_type_pending_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      capacity_node_active_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      capacity_type_active_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      mesh_node_admission_key (
                        _options.key_prefix,
                        request.fence.target.mesh_name,
                        request.fence.target.node_rid.value ()),
                    detail::redis_location_key_schema_t::
                      authority_history_key (
                        _options.key_prefix, key),
                    detail::redis_location_key_schema_t::
                      authority_history_revisions_key (
                        _options.key_prefix, key),
                    detail::redis_location_key_schema_t::
                      authority_index_gc_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      membership_current_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      membership_history_key (
                        _options.key_prefix, key),
                    detail::redis_location_key_schema_t::
                      membership_history_revisions_key (
                        _options.key_prefix, key)};
                  auto args = object_fence_args (
                    request.fence);
                  args.push_back (
                    bytes_to_string (request.ready_payload));
                  args.push_back (
                    detail::redis_location_key_schema_t::
                      object_kind_token (request.key.kind));
                  args.push_back (
                    stable_type);
                  args.push_back (
                    detail::redis_location_key_schema_t::
                      capacity_node_field (
                        request.fence.target.mesh_name,
                        request.fence.target.node_rid.value (),
                        request.fence.target
                          .node_lifecycle_generation));
                  args.push_back (
                    detail::redis_location_key_schema_t::
                      capacity_type_field (
                        request.fence.target.mesh_name,
                        request.fence.target.node_rid.value (),
                        request.fence.target
                          .node_lifecycle_generation,
                        request.key.kind, stable_type));
                  args.push_back (
                    revision_lower_hex (
                      request.fence.expected_store_version));
                  args.push_back (
                    detail::redis_location_key_schema_t::
                      authority_index_member (key));
                  const auto result = redis_get (
                    client ().eval<std::vector<std::string>> (
                      std::string (
                        detail::redis_location_scripts_t::
                          commit_object),
                      keys.begin (), keys.end (), args.begin (),
                      args.end ()));
                  if (result.size () != 16)
                      throw sw::redis::Error (
                        "invalid object commit result");
                  if (result[0] == "stale")
                      return object_commit_result_t{
                        object_commit_stale_t{}};
                  if (result[0] == "exhausted")
                      return object_commit_result_t{
                        authority_generation_exhausted_t{}};
                  const auto current =
                    parse_cas_authority_result (result);
                  if (result[0] == "conflict")
                      return object_commit_result_t{
                        object_commit_conflict_t{current}};
                  const auto &snapshot =
                    std::get<authority_snapshot_t> (current);
                  if (result[0] == "already_committed")
                      return object_commit_result_t{
                        object_already_committed_t{snapshot}};
                  if (result[0] != "committed")
                      throw sw::redis::Error (
                        "unknown object commit result");
                  return object_commit_result_t{
                    object_committed_t{snapshot}};
              }
              catch (const sw::redis::Error &error) {
                  throw framework_exception_t (
                    framework_error_kind_t::request_failed,
                    error.what (), true);
              }
          });
#else
        (void) request;
        return unavailable_read<object_commit_result_t> ();
#endif
    }

    task_t<object_abort_result_t> abort (
      object_abort_request_t request,
      std::stop_token cancellation = {}) override
    {
        if (cancellation.stop_requested ())
            return cancelled<object_abort_result_t> ();
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<object_abort_result_t> (
          [this, request = std::move (request)] {
              try {
                  const auto key = object_key (request.key);
                  const auto current =
                    read_authority_sync (
                      authority_key_t{key});
                  const auto *snapshot =
                    std::get_if<authority_snapshot_t> (
                      &current);
                  const auto stable_type =
                    snapshot
                      ? snapshot->allocation.stable_type
                      : std::string{};
                  const auto keys = std::vector<std::string>{
                    detail::redis_location_key_schema_t::
                      authority_key (_options.key_prefix, key),
                    detail::redis_location_key_schema_t::
                      creation_reservation_key (
                        _options.key_prefix,
                        request.fence.reservation_id),
                    detail::redis_location_key_schema_t::
                      authority_keys_key (_options.key_prefix),
                    detail::redis_location_key_schema_t::
                      capacity_node_pending_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      capacity_type_pending_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      authority_store_revision_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      authority_history_key (
                        _options.key_prefix, key),
                    detail::redis_location_key_schema_t::
                      authority_history_revisions_key (
                        _options.key_prefix, key),
                    detail::redis_location_key_schema_t::
                      authority_index_gc_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      membership_current_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      membership_history_key (
                        _options.key_prefix, key),
                    detail::redis_location_key_schema_t::
                      membership_history_revisions_key (
                        _options.key_prefix, key)};
                  auto args = object_fence_args (
                    request.fence);
                  args.push_back (key);
                  args.push_back (
                    detail::redis_location_key_schema_t::
                      authority_index_member (key));
                  args.push_back (
                    detail::redis_location_key_schema_t::
                      capacity_node_field (
                        request.fence.target.mesh_name,
                        request.fence.target.node_rid.value (),
                        request.fence.target
                          .node_lifecycle_generation));
                  args.push_back (
                    detail::redis_location_key_schema_t::
                      capacity_type_field (
                        request.fence.target.mesh_name,
                        request.fence.target.node_rid.value (),
                        request.fence.target
                          .node_lifecycle_generation,
                        request.key.kind, stable_type));
                  args.push_back (
                    revision_lower_hex (
                      request.fence.expected_store_version));
                  const auto result = redis_get (
                    client ().eval<std::vector<std::string>> (
                      std::string (
                        detail::redis_location_scripts_t::
                          abort_object),
                      keys.begin (), keys.end (), args.begin (),
                      args.end ()));
                  if (result.size () != 16)
                      throw sw::redis::Error (
                        "invalid object abort result");
                  if (result[0] == "stale")
                      return object_abort_result_t{
                        object_abort_stale_t{}};
                  if (result[0] == "already_aborted")
                      return object_abort_result_t{
                        object_already_aborted_t{}};
                  if (result[0] == "conflict")
                      return object_abort_result_t{
                        object_abort_conflict_t{
                          parse_cas_authority_result (
                            result)}};
                  if (result[0] != "aborted")
                      throw sw::redis::Error (
                        "unknown object abort result");
                  return object_abort_result_t{
                    object_aborted_t{}};
              }
              catch (const sw::redis::Error &error) {
                  throw framework_exception_t (
                    framework_error_kind_t::request_failed,
                    error.what (), true);
              }
          });
#else
        (void) request;
        return unavailable_read<object_abort_result_t> ();
#endif
    }

    task_t<aggregate_prepare_result_t> prepare_aggregate (
      aggregate_prepare_request_t request,
      std::stop_token cancellation = {}) override
    {
        if (cancellation.stop_requested ())
            return cancelled<aggregate_prepare_result_t> ();
        validate_aggregate_prepare_request (request);
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<aggregate_prepare_result_t> (
          [this, request = std::move (request)] {
              try {
                  const auto aggregate_id =
                    byte_array_key (
                      request.aggregate_id.value);
                  std::map<std::string, std::string>
                    fence_by_authority;
                  for (const auto &fence :
                       request.target_reservations) {
                      const auto reservation_key =
                        detail::redis_location_key_schema_t::
                          relocation_capacity_reservation_key (
                            _options.key_prefix, fence.value);
                      const auto authority = redis_get (
                        client ().hget (
                          reservation_key,
                          "authorityKey"));
                      if (!authority
                          || !fence_by_authority
                                .emplace (
                                  *authority, fence.value)
                                .second)
                          return aggregate_prepare_result_t{
                            aggregate_prepare_conflict_t{}};
                  }
                  std::size_t new_owner_count = 0;
                  auto keys = std::vector<std::string>{
                    detail::redis_location_key_schema_t::
                      aggregate_key (
                        _options.key_prefix, aggregate_id,
                        request.aggregate_generation),
                    detail::redis_location_key_schema_t::
                      authority_store_revision_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      authority_owner_generation_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      lease_key (
                        _options.key_prefix,
                        request.target_owner.owner_id)};
                  auto args = std::vector<std::string>{
                    aggregate_fingerprint (request),
                    aggregate_id,
                    std::to_string (
                      request.aggregate_generation),
                    request.target_owner.owner_id,
                    std::to_string (
                      request.target_owner.lease_generation),
                    std::to_string (
                      request.participants.size ()),
                    byte_array_key (
                      request.inventory_digest.value)};
                  for (const auto &participant :
                       request.participants) {
                      const auto authority_key =
                        detail::redis_location_key_schema_t::
                          authority_key (
                            _options.key_prefix,
                            participant.key.value);
                      const auto current_owner = redis_get (
                        client ().hmget<
                          std::vector<
                            sw::redis::OptionalString>> (
                          authority_key,
                          {"ownerId",
                           "ownerLeaseGeneration",
                           "descriptorKey",
                           "descriptorLifecycleGeneration",
                           "objectKind",
                           "stableType"}));
                      const auto owner_id =
                        !current_owner.empty ()
                            && current_owner[0]
                          ? *current_owner[0]
                          : std::string{};
                      const auto source_descriptor =
                        current_owner.size () > 2
                            && current_owner[2]
                          ? *current_owner[2]
                          : std::string{};
                      const auto decoded_source =
                        source_descriptor.empty ()
                          ? mesh_node_descriptor_key_t{}
                          : detail::
                              redis_location_key_schema_t::
                                decode_mesh_node_key (
                                  source_descriptor);
                      const auto source_mesh =
                        decoded_source.mesh_name;
                      const auto source_node =
                        decoded_source.rid.to_string ();
                      const auto source_lifecycle =
                        current_owner.size () > 3
                            && current_owner[3]
                          ? std::stoull (*current_owner[3])
                          : 0;
                      const auto source_kind =
                        current_owner.size () > 4
                            && current_owner[4]
                          ? detail::
                              redis_location_key_schema_t::
                                parse_object_kind_token (
                                  *current_owner[4])
                          : placement_object_kind_t::actor;
                      const auto source_type =
                        current_owner.size () > 5
                            && current_owner[5]
                          ? *current_owner[5]
                          : std::string{};
                      std::string fence;
                      auto target_mesh = source_mesh;
                      auto target_node = source_node;
                      auto target_lifecycle =
                        source_lifecycle;
                      auto target_kind = source_kind;
                      auto target_type = source_type;
                      if (participant.owner_transition
                          == authority_generation_transition_t::
                               new_owner) {
                          ++new_owner_count;
                          const auto found =
                            fence_by_authority.find (
                              participant.key.value);
                          if (found
                              == fence_by_authority.end ())
                              return aggregate_prepare_result_t{
                                aggregate_prepare_conflict_t{}};
                          fence = found->second;
                          const auto reservation_fields =
                            redis_get (
                              client ().hmget<
                                std::vector<
                                  sw::redis::OptionalString>> (
                                detail::
                                  redis_location_key_schema_t::
                                    relocation_capacity_reservation_key (
                                      _options.key_prefix,
                                      fence),
                                {"targetMesh",
                                 "targetNode",
                                 "targetLifecycleGeneration",
                                 "objectKind",
                                 "stableType"}));
                          if (reservation_fields.size ()
                                != 5
                              || std::any_of (
                                reservation_fields.begin (),
                                reservation_fields.end (),
                                [] (const auto &value) {
                                    return !static_cast<bool> (
                                      value);
                                }))
                              return aggregate_prepare_result_t{
                                aggregate_prepare_conflict_t{}};
                          target_mesh =
                            *reservation_fields[0];
                          target_node =
                            *reservation_fields[1];
                          target_lifecycle =
                            std::stoull (
                              *reservation_fields[2]);
                          target_kind =
                            detail::
                              redis_location_key_schema_t::
                                parse_object_kind_token (
                                  *reservation_fields[3]);
                          target_type =
                            *reservation_fields[4];
                      }
                      const auto reservation_key =
                        detail::redis_location_key_schema_t::
                          relocation_capacity_reservation_key (
                            _options.key_prefix, fence);
                      keys.push_back (authority_key);
                      keys.push_back (reservation_key);
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          capacity_node_active_key (
                            _options.key_prefix));
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          capacity_type_active_key (
                            _options.key_prefix));
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          capacity_node_pending_key (
                            _options.key_prefix));
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          capacity_type_pending_key (
                            _options.key_prefix));
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          capacity_node_active_key (
                            _options.key_prefix));
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          capacity_type_active_key (
                            _options.key_prefix));
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          mesh_node_admission_key (
                            _options.key_prefix,
                            target_mesh,
                            target_node));
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          authority_history_key (
                            _options.key_prefix,
                            participant.key.value));
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          authority_history_revisions_key (
                            _options.key_prefix,
                            participant.key.value));
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          membership_current_key (
                            _options.key_prefix));
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          membership_history_key (
                            _options.key_prefix,
                            participant.key.value));
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          membership_history_revisions_key (
                            _options.key_prefix,
                            participant.key.value));
                      args.push_back (participant.key.value);
                      args.push_back (
                        participant.expected_store_version);
                      args.push_back (transition_name (
                        participant.owner_transition));
                      args.push_back (bytes_to_string (
                        participant.authority_payload));
                      args.push_back (bytes_to_string (
                        participant.membership_mutation));
                      args.push_back (fence);
                      args.push_back (
                        detail::redis_location_key_schema_t::
                          capacity_node_field (
                            source_mesh, source_node,
                            source_lifecycle));
                      args.push_back (
                        detail::redis_location_key_schema_t::
                          capacity_type_field (
                            source_mesh, source_node,
                            source_lifecycle, source_kind,
                            source_type));
                      args.push_back (
                        detail::redis_location_key_schema_t::
                          capacity_node_field (
                            target_mesh, target_node,
                            target_lifecycle));
                      args.push_back (
                        detail::redis_location_key_schema_t::
                          capacity_type_field (
                            target_mesh, target_node,
                            target_lifecycle, target_kind,
                            target_type));
                      args.push_back (
                        revision_lower_hex (
                          participant.expected_store_version));
                  }
                  if (new_owner_count
                      != request.target_reservations.size ())
                      return aggregate_prepare_result_t{
                        aggregate_prepare_conflict_t{}};
                  const auto status = redis_get (
                    client ().eval<std::string> (
                      std::string (
                        detail::redis_location_scripts_t::
                          prepare_aggregate),
                      keys.begin (), keys.end (), args.begin (),
                      args.end ()));
                  const aggregate_fence_t fence{
                    request.aggregate_id,
                    request.aggregate_generation};
                  if (status == "prepared")
                      return aggregate_prepare_result_t{
                        aggregate_prepared_t{fence}};
                  if (status == "already_prepared")
                      return aggregate_prepare_result_t{
                        aggregate_already_prepared_t{fence}};
                  if (status == "stale")
                      return aggregate_prepare_result_t{
                        aggregate_prepare_stale_t{}};
                  return aggregate_prepare_result_t{
                    aggregate_prepare_conflict_t{}};
              }
              catch (const sw::redis::Error &error) {
                  throw framework_exception_t (
                    framework_error_kind_t::request_failed,
                    error.what (), true);
              }
          });
#else
        (void) request;
        return unavailable_read<aggregate_prepare_result_t> ();
#endif
    }

    task_t<aggregate_commit_result_t> commit_aggregate (
      aggregate_fence_t fence,
      std::stop_token cancellation = {}) override
    {
        if (cancellation.stop_requested ())
            return cancelled<aggregate_commit_result_t> ();
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<aggregate_commit_result_t> (
          [this, fence = std::move (fence)] {
              try {
                  const auto aggregate_id =
                    byte_array_key (fence.aggregate_id.value);
                  const auto aggregate_key =
                    detail::redis_location_key_schema_t::
                      aggregate_key (
                        _options.key_prefix, aggregate_id,
                        fence.aggregate_generation);
                  const auto header = redis_get (
                    client ().hmget<
                      std::vector<
                        sw::redis::OptionalString>> (
                      aggregate_key,
                      {"targetOwnerId",
                       "participantCount"}));
                  const auto target_owner =
                    header.size () > 0 && header[0]
                      ? *header[0]
                      : std::string{};
                  const auto participant_count =
                    header.size () > 1 && header[1]
                      ? std::stoull (*header[1])
                      : 0;
                  auto keys = std::vector<std::string>{
                    aggregate_key,
                    detail::redis_location_key_schema_t::
                      authority_store_revision_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      authority_owner_generation_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      lease_key (
                        _options.key_prefix,
                        target_owner)};
                  for (std::size_t index = 0;
                       index < participant_count; ++index) {
                      const auto prefix =
                        "p:" + std::to_string (index) + ":";
                      const auto fields = redis_get (
                        client ().hmget<
                          std::vector<
                            sw::redis::OptionalString>> (
                          aggregate_key,
                          {prefix + "authorityKey",
                           prefix + "fence"}));
                      const auto authority =
                        fields.size () > 0 && fields[0]
                          ? *fields[0]
                          : std::string{};
                      const auto reservation =
                        fields.size () > 1 && fields[1]
                          ? *fields[1]
                          : std::string{};
                      auto target_mesh = std::string{};
                      auto target_node = std::string{};
                      if (!reservation.empty ()) {
                          const auto target = redis_get (
                            client ().hmget<
                              std::vector<
                                sw::redis::OptionalString>> (
                              detail::
                                redis_location_key_schema_t::
                                  relocation_capacity_reservation_key (
                                    _options.key_prefix,
                                    reservation),
                              {"targetMesh", "targetNode"}));
                          if (target.size () == 2
                              && target[0] && target[1]) {
                              target_mesh = *target[0];
                              target_node = *target[1];
                          }
                      }
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          authority_key (
                            _options.key_prefix, authority));
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          relocation_capacity_reservation_key (
                            _options.key_prefix, reservation));
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          capacity_node_active_key (
                            _options.key_prefix));
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          capacity_type_active_key (
                            _options.key_prefix));
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          capacity_node_pending_key (
                            _options.key_prefix));
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          capacity_type_pending_key (
                            _options.key_prefix));
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          capacity_node_active_key (
                            _options.key_prefix));
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          capacity_type_active_key (
                            _options.key_prefix));
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          mesh_node_admission_key (
                            _options.key_prefix,
                            target_mesh, target_node));
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          authority_history_key (
                            _options.key_prefix, authority));
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          authority_history_revisions_key (
                            _options.key_prefix, authority));
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          membership_current_key (
                            _options.key_prefix));
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          membership_history_key (
                            _options.key_prefix, authority));
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          membership_history_revisions_key (
                            _options.key_prefix, authority));
                  }
                  const auto args = std::vector<std::string>{
                    std::to_string (
                      fence.aggregate_generation)};
                  const auto status = redis_get (
                    client ().eval<std::string> (
                      std::string (
                        detail::redis_location_scripts_t::
                          commit_aggregate),
                      keys.begin (), keys.end (), args.begin (),
                      args.end ()));
                  if (status == "committed")
                      return aggregate_commit_result_t::committed;
                  if (status == "already_committed")
                      return aggregate_commit_result_t::
                        already_committed;
                  if (status == "generation_exhausted")
                      return aggregate_commit_result_t::
                        generation_exhausted;
                  return aggregate_commit_result_t::stale;
              }
              catch (const sw::redis::Error &error) {
                  throw framework_exception_t (
                    framework_error_kind_t::request_failed,
                    error.what (), true);
              }
          });
#else
        (void) fence;
        return unavailable_read<aggregate_commit_result_t> ();
#endif
    }

    task_t<aggregate_abort_result_t> abort_aggregate (
      aggregate_fence_t fence,
      std::stop_token cancellation = {}) override
    {
        if (cancellation.stop_requested ())
            return cancelled<aggregate_abort_result_t> ();
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<aggregate_abort_result_t> (
          [this, fence = std::move (fence)] {
              try {
                  const auto aggregate_key =
                    detail::redis_location_key_schema_t::
                      aggregate_key (
                        _options.key_prefix,
                        byte_array_key (
                          fence.aggregate_id.value),
                        fence.aggregate_generation);
                  const auto count = redis_get (
                    client ().hget (
                      aggregate_key, "participantCount"));
                  const auto participant_count =
                    count ? std::stoull (*count) : 0;
                  auto keys =
                    std::vector<std::string>{aggregate_key};
                  for (std::size_t index = 0;
                       index < participant_count; ++index) {
                      const auto fence_value = redis_get (
                        client ().hget (
                          aggregate_key,
                          "p:" + std::to_string (index)
                            + ":fence"));
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          relocation_capacity_reservation_key (
                            _options.key_prefix,
                            fence_value ? *fence_value
                                        : std::string{}));
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          capacity_node_pending_key (
                            _options.key_prefix));
                      keys.push_back (
                        detail::redis_location_key_schema_t::
                          capacity_type_pending_key (
                            _options.key_prefix));
                  }
                  const auto args = std::vector<std::string>{
                    std::to_string (
                      fence.aggregate_generation)};
                  const auto status = redis_get (
                    client ().eval<std::string> (
                      std::string (
                        detail::redis_location_scripts_t::
                          abort_aggregate),
                      keys.begin (), keys.end (), args.begin (),
                      args.end ()));
                  if (status == "aborted")
                      return aggregate_abort_result_t::aborted;
                  if (status == "already_aborted")
                      return aggregate_abort_result_t::
                        already_aborted;
                  return aggregate_abort_result_t::stale;
              }
              catch (const sw::redis::Error &error) {
                  throw framework_exception_t (
                    framework_error_kind_t::request_failed,
                    error.what (), true);
              }
          });
#else
        (void) fence;
        return unavailable_read<aggregate_abort_result_t> ();
#endif
    }

    task_t<relocation_capacity_abort_result_t>
    abort_relocation_capacity (
      relocation_capacity_fence_t fence,
      std::stop_token cancellation = {}) override
    {
        if (cancellation.stop_requested ())
            return cancelled<
              relocation_capacity_abort_result_t> ();
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<
          relocation_capacity_abort_result_t> (
          [this, fence = std::move (fence)] {
              try {
                  const auto reservation_key =
                    detail::redis_location_key_schema_t::
                      relocation_capacity_reservation_key (
                        _options.key_prefix, fence.value);
                  const auto target = redis_get (
                    client ().hmget<
                      std::vector<
                        sw::redis::OptionalString>> (
                      reservation_key,
                      {"targetMesh",
                       "targetNode",
                       "targetLifecycleGeneration",
                       "objectKind",
                       "stableType"}));
                  const auto has_target =
                    target.size () == 5
                    && std::all_of (
                      target.begin (), target.end (),
                      [] (const auto &value) {
                          return static_cast<bool> (
                            value);
                      });
                  const auto keys = std::vector<std::string>{
                    reservation_key,
                    detail::redis_location_key_schema_t::
                      capacity_node_pending_key (
                        _options.key_prefix),
                    detail::redis_location_key_schema_t::
                      capacity_type_pending_key (
                        _options.key_prefix)};
                  const auto node_field =
                    detail::redis_location_key_schema_t::
                      capacity_node_field (
                        has_target ? *target[0]
                                   : std::string{},
                        has_target ? *target[1]
                                   : std::string{},
                        has_target
                          ? std::stoull (*target[2])
                          : 0);
                  const auto type_field =
                    detail::redis_location_key_schema_t::
                      capacity_type_field (
                        has_target ? *target[0]
                                   : std::string{},
                        has_target ? *target[1]
                                   : std::string{},
                        has_target
                          ? std::stoull (*target[2])
                          : 0,
                        has_target
                          ? detail::
                              redis_location_key_schema_t::
                                parse_object_kind_token (
                                  *target[3])
                          : placement_object_kind_t::actor,
                        has_target ? *target[4]
                                   : std::string{});
                  const auto args =
                    std::vector<std::string>{
                      node_field, type_field};
                  const auto status = redis_get (
                    client ().eval<std::string> (
                      std::string (
                        detail::redis_location_scripts_t::
                          abort_relocation_capacity),
                      keys.begin (), keys.end (), args.begin (),
                      args.end ()));
                  if (status == "aborted")
                      return relocation_capacity_abort_result_t::
                        aborted;
                  if (status == "already_aborted")
                      return relocation_capacity_abort_result_t::
                        already_aborted;
                  if (status == "already_committed")
                      return relocation_capacity_abort_result_t::
                        already_committed;
                  return relocation_capacity_abort_result_t::stale;
              }
              catch (const sw::redis::Error &error) {
                  throw framework_exception_t (
                    framework_error_kind_t::request_failed,
                    error.what (), true);
              }
          });
#else
        (void) fence;
        return unavailable_read<
          relocation_capacity_abort_result_t> ();
#endif
    }

    task_t<std::int64_t> get_change_stamp (location_change_stamp_scope_t scope) override
    {
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<std::int64_t> ([this, scope = std::move (scope)] {
            const auto value =
              redis_get (client ().get (detail::redis_location_key_schema_t::stamp_key (
                _options.key_prefix, scope.kind, optional_view (scope.mesh_name))));
            return value ? std::stoll (*value) : 0;
        });
#else
        (void) scope;
        return unavailable_read<std::int64_t> ();
#endif
    }

    task_t<std::int64_t> remove_all_by_owner (
      location_owner_token_t owner) override
    {
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<std::int64_t> (
          [this, owner = std::move (owner)] {
            sw::redis::Redis redis (
              normalize_connection_string (
                _options.connection_string));
            const auto lease_key =
              detail::redis_location_key_schema_t::
                lease_key (
                  _options.key_prefix, owner.owner_id);
            const auto lease_owner =
              redis.hget (lease_key, "ownerId");
            const auto lease_generation =
              redis.hget (lease_key, "generation");
            if (redis.pttl (lease_key) <= 0
                || !lease_owner
                || *lease_owner != owner.owner_id
                || !lease_generation
                || *lease_generation
                     != std::to_string (
                       owner.lease_generation))
                return std::int64_t{0};
            std::int64_t removed = 0;
            for (const auto kind :
                 {location_kind_t::peer,
                  location_kind_t::spot,
                  location_kind_t::actor,
                  location_kind_t::route}) {
                const auto owner_key =
                  detail::redis_location_key_schema_t::
                    owner_key (
                      _options.key_prefix, kind,
                      owner.owner_id);
                for (;;) {
                    std::vector<std::string> rows;
                    redis.spop (
                      owner_key, 25,
                      std::back_inserter (rows));
                    if (rows.empty ())
                        break;
                    for (const auto &row : rows) {
                        const auto physical =
                          detail::
                            redis_location_key_schema_t::
                              row_key (
                                _options.key_prefix, kind,
                                row);
                        const auto stored_owner =
                          redis.hget (physical, "owner");
                        if (!stored_owner
                            || *stored_owner
                                 != owner.owner_id)
                            continue;
                        const auto mesh =
                          redis.hget (physical, "mesh");
                        if (redis.del (physical) != 1)
                            continue;
                        ++removed;
                        redis.srem (
                          detail::
                            redis_location_key_schema_t::
                              keys_key (
                                _options.key_prefix, kind),
                          row);
                        redis.incr (
                          detail::
                            redis_location_key_schema_t::
                              stamp_key (
                                _options.key_prefix, kind,
                                std::nullopt));
                        if (mesh)
                            redis.incr (
                              detail::
                                redis_location_key_schema_t::
                                  stamp_key (
                                    _options.key_prefix,
                                    kind, *mesh));
                    }
                }
            }

            const auto descriptor_owner_index =
              detail::redis_location_key_schema_t::
                client_server_owner_keys_key (
                  _options.key_prefix,
                  owner.owner_id,
                  owner.lease_generation);
            std::vector<std::string> descriptor_keys;
            redis.smembers (
              descriptor_owner_index,
              std::back_inserter (descriptor_keys));
            for (const auto &canonical_key :
                 descriptor_keys) {
                const auto row_key =
                  detail::redis_location_key_schema_t::
                    client_server_key (
                      _options.key_prefix,
                      canonical_key);
                const auto admission_key =
                  detail::redis_location_key_schema_t::
                    client_server_admission_key (
                      _options.key_prefix,
                      canonical_key);
                const auto stored_owner =
                  redis.hget (
                    admission_key, "ownerId");
                const auto stored_generation =
                  redis.hget (
                    admission_key,
                    "ownerLeaseGeneration");
                const auto channel =
                  redis.hget (row_key, "channel");
                if (!stored_owner
                    || *stored_owner != owner.owner_id
                    || !stored_generation
                    || *stored_generation
                         != std::to_string (
                           owner.lease_generation)
                    || !channel) {
                    redis.srem (
                      descriptor_owner_index,
                      canonical_key);
                    continue;
                }
                const auto keys =
                  std::vector<std::string>{
                    row_key,
                    admission_key,
                    detail::
                      redis_location_key_schema_t::
                        client_server_keys_key (
                          _options.key_prefix),
                    descriptor_owner_index,
                    detail::
                      redis_location_key_schema_t::
                        client_server_stamp_key (
                          _options.key_prefix),
                    detail::
                      redis_location_key_schema_t::
                        client_server_stamp_key (
                          _options.key_prefix,
                          *channel),
                    detail::
                      redis_location_key_schema_t::
                        client_server_channel_keys_key (
                          _options.key_prefix,
                          *channel)};
                const auto args =
                  std::vector<std::string>{
                    owner.owner_id,
                    std::to_string (
                      owner.lease_generation),
                    canonical_key};
                const auto result = redis_get (
                  client ().eval<std::tuple<
                    std::string,
                    std::string,
                    std::string>> (
                    std::string (
                      detail::
                        redis_location_scripts_t::
                          remove_client_server),
                    keys.begin (), keys.end (),
                    args.begin (), args.end ()));
                if (std::get<0> (result) == "stored")
                    ++removed;
            }
            redis.del (descriptor_owner_index);

            const auto fanout_owner_index =
              detail::redis_location_key_schema_t::
                fanout_publisher_owner_keys_key (
                  _options.key_prefix,
                  owner.owner_id,
                  owner.lease_generation);
            std::vector<std::string> fanout_keys;
            redis.smembers (
              fanout_owner_index,
              std::back_inserter (fanout_keys));
            for (const auto &canonical_key : fanout_keys) {
                const auto row_key =
                  detail::redis_location_key_schema_t::
                    fanout_publisher_key (
                      _options.key_prefix,
                      canonical_key);
                const auto admission_key =
                  detail::redis_location_key_schema_t::
                    fanout_publisher_admission_key (
                      _options.key_prefix,
                      canonical_key);
                const auto stored_owner =
                  redis.hget (
                    admission_key, "ownerId");
                const auto stored_generation =
                  redis.hget (
                    admission_key,
                    "ownerLeaseGeneration");
                const auto channel =
                  redis.hget (row_key, "channel");
                if (!stored_owner
                    || *stored_owner != owner.owner_id
                    || !stored_generation
                    || *stored_generation
                         != std::to_string (
                           owner.lease_generation)
                    || !channel) {
                    redis.srem (
                      fanout_owner_index,
                      canonical_key);
                    continue;
                }
                const auto keys =
                  std::vector<std::string>{
                    row_key,
                    admission_key,
                    detail::
                      redis_location_key_schema_t::
                        fanout_publisher_keys_key (
                          _options.key_prefix),
                    fanout_owner_index,
                    detail::
                      redis_location_key_schema_t::
                        fanout_publisher_stamp_key (
                          _options.key_prefix),
                    detail::
                      redis_location_key_schema_t::
                        fanout_publisher_stamp_key (
                          _options.key_prefix,
                          *channel),
                    detail::
                      redis_location_key_schema_t::
                        fanout_publisher_channel_keys_key (
                          _options.key_prefix,
                          *channel)};
                const auto args =
                  std::vector<std::string>{
                    owner.owner_id,
                    std::to_string (
                      owner.lease_generation),
                    canonical_key};
                const auto result = redis_get (
                  client ().eval<std::tuple<
                    std::string,
                    std::string,
                    std::string>> (
                    std::string (
                      detail::
                        redis_location_scripts_t::
                          remove_fanout_publisher),
                    keys.begin (), keys.end (),
                    args.begin (), args.end ()));
                if (std::get<0> (result) == "stored")
                    ++removed;
            }
            redis.del (fanout_owner_index);
            return removed;
        });
#else
        (void) owner;
        return unavailable_read<std::int64_t> ();
#endif
    }

  private:
    static bool valid_descriptor_text (
      std::string_view value) noexcept
    {
        return !value.empty () && value.size () <= 255
               && value.find ('\0') == std::string_view::npos;
    }

    static bool valid_client_server_descriptor (
      const client_server_server_descriptor_t &descriptor) noexcept
    {
        const auto state =
          static_cast<unsigned int> (descriptor.state);
        return valid_descriptor_text (
                 descriptor.channel_name)
               && descriptor.server_rid.size () > 0
               && descriptor.lifecycle_generation > 0
               && descriptor.lifecycle_generation
                    <= static_cast<std::uint64_t> (
                      std::numeric_limits<
                        std::int64_t>::max ())
               && descriptor.descriptor_revision > 0
               && descriptor.descriptor_revision
                    <= static_cast<std::uint64_t> (
                      std::numeric_limits<
                        std::int64_t>::max ())
               && valid_descriptor_text (
                 descriptor.endpoint)
               && descriptor.weight >= 0
               && descriptor.weight <= 100
               && state
                    <= static_cast<unsigned int> (
                      framework_runtime_state_t::error)
               && valid_descriptor_text (
                 descriptor.security_identity)
               && valid_descriptor_text (
                 descriptor.owner_id)
               && descriptor.lease_generation > 0;
    }

    static std::size_t decode_client_server_page_token (
      const std::optional<std::string> &token,
      std::string_view channel_name)
    {
        if (!token)
            return 0;
        if (token->size () > 4096)
            throw std::invalid_argument (
              "ClientServer descriptor continuation token is invalid");
        try {
            const auto value =
              nlohmann::json::parse (*token);
            if (value.at ("kind").get<std::string> ()
                  != "client-server-v1"
                || value.at ("channelName")
                     .get<std::string> ()
                     != channel_name
                || !value.at ("offset")
                      .is_number_unsigned ())
                throw std::invalid_argument (
                  "invalid token");
            const auto offset =
              value.at ("offset")
                .get<std::uint64_t> ();
            if (offset
                > static_cast<std::uint64_t> (
                  std::numeric_limits<
                    std::size_t>::max () - 1000))
                throw std::invalid_argument (
                  "invalid token");
            return static_cast<std::size_t> (
              offset);
        }
        catch (...) {
            throw std::invalid_argument (
              "ClientServer descriptor continuation token is invalid");
        }
    }

    static std::string encode_client_server_page_token (
      std::string_view channel_name,
      std::size_t offset)
    {
        return nlohmann::ordered_json{
          {"kind", "client-server-v1"},
          {"channelName", channel_name},
          {"offset", offset}}
          .dump ();
    }

    static bool valid_fanout_publisher_descriptor (
      const fanout_publisher_descriptor_t &descriptor) noexcept
    {
        const auto state =
          static_cast<unsigned int> (descriptor.state);
        return valid_descriptor_text (
                 descriptor.channel_name)
               && descriptor.publisher_rid.size () > 0
               && descriptor.lifecycle_generation > 0
               && descriptor.lifecycle_generation
                    <= static_cast<std::uint64_t> (
                      std::numeric_limits<
                        std::int64_t>::max ())
               && descriptor.descriptor_revision > 0
               && descriptor.descriptor_revision
                    <= static_cast<std::uint64_t> (
                      std::numeric_limits<
                        std::int64_t>::max ())
               && valid_descriptor_text (
                 descriptor.endpoint)
               && state
                    <= static_cast<unsigned int> (
                      framework_runtime_state_t::error)
               && valid_descriptor_text (
                 descriptor.security_identity)
               && valid_descriptor_text (
                 descriptor.owner_id)
               && descriptor.lease_generation > 0;
    }

    static std::size_t
    decode_fanout_publisher_page_token (
      const std::optional<std::string> &token,
      std::string_view channel_name)
    {
        if (!token)
            return 0;
        if (token->size () > 4096)
            throw std::invalid_argument (
              "fanout publisher descriptor continuation token is invalid");
        try {
            const auto value =
              nlohmann::json::parse (*token);
            if (value.at ("kind").get<std::string> ()
                  != "fanout-publisher-v1"
                || value.at ("channelName")
                     .get<std::string> ()
                     != channel_name
                || !value.at ("offset")
                      .is_number_unsigned ())
                throw std::invalid_argument (
                  "invalid token");
            const auto offset =
              value.at ("offset")
                .get<std::uint64_t> ();
            if (offset
                > static_cast<std::uint64_t> (
                  std::numeric_limits<
                    std::size_t>::max () - 1000))
                throw std::invalid_argument (
                  "invalid token");
            return static_cast<std::size_t> (
              offset);
        }
        catch (...) {
            throw std::invalid_argument (
              "fanout publisher descriptor continuation token is invalid");
        }
    }

    static std::string
    encode_fanout_publisher_page_token (
      std::string_view channel_name,
      std::size_t offset)
    {
        return nlohmann::ordered_json{
          {"kind", "fanout-publisher-v1"},
          {"channelName", channel_name},
          {"offset", offset}}
          .dump ();
    }

    static std::size_t parse_page_offset (
      const std::string &value)
    {
        try {
            return static_cast<std::size_t> (
              std::stoull (value));
        }
        catch (...) {
            return 0;
        }
    }

    static bool valid_mesh_node_descriptor (
      const mesh_node_descriptor_t &descriptor)
    {
        if (descriptor.mesh_name.empty ()
            || descriptor.rid.size () == 0
            || descriptor.lifecycle_generation == 0
            || descriptor.descriptor_revision == 0
            || descriptor.descriptor_revision
                 > static_cast<std::uint64_t> (
                   std::numeric_limits<std::int64_t>::max ())
            || descriptor.endpoint.empty ()
            || descriptor.application_version < 0
            || descriptor.placement_weight > 100
            || descriptor.object_capacity.active
                 > descriptor.object_capacity.active_limit
            || descriptor.object_capacity.pending
                 > descriptor.object_capacity.pending_limit
            || descriptor.object_capacity.active_limit == 0
            || descriptor.object_capacity.pending_limit == 0
            || descriptor.object_capacity.active_limit
                 > static_cast<std::uint32_t> (
                   std::numeric_limits<std::int32_t>::max ())
            || descriptor.object_capacity.pending_limit
                 > static_cast<std::uint32_t> (
                   std::numeric_limits<std::int32_t>::max ())
            || descriptor.security_identity.empty ()
            || descriptor.owner_id.empty ()
            || descriptor.lease_generation <= 0
            || descriptor.object_capabilities.size () > 1024
            || (descriptor.object_role != object_role_t::server
                && !descriptor.object_capabilities.empty ()))
            return false;
        std::pair<int, std::string> previous;
        bool first = true;
        for (const auto &capability :
             descriptor.object_capabilities) {
            if (capability.stable_type.empty ()
                || capability.placement_profiles.size () > 1024
                || ((capability.policy
                       == maintenance_policy_kind_t::snapshot)
                    != capability.has_snapshot_adapter)
                || (capability.active_limit
                    && (*capability.active_limit == 0
                        || *capability.active_limit
                             > static_cast<std::uint32_t> (
                               std::numeric_limits<
                                 std::int32_t>::max ())))
                || (capability.pending_limit
                    && (*capability.pending_limit == 0
                        || *capability.pending_limit
                             > static_cast<std::uint32_t> (
                               std::numeric_limits<
                                 std::int32_t>::max ()))))
                return false;
            const auto key = std::make_pair (
              static_cast<int> (capability.object_kind),
              capability.stable_type);
            if (!first && previous >= key)
                return false;
            previous = key;
            first = false;
        }
        return true;
    }

    static void validate_authority_mutation (
      const authority_mutation_t &mutation)
    {
        const auto *put =
          std::get_if<authority_put_t> (&mutation);
        if (!put)
            return;
        const auto transition = put->generation_transition;
        if ((transition == authority_generation_transition_t::preserve
             && (put->target_owner
                 || put->relocation_capacity_fence))
            || (transition == authority_generation_transition_t::new_owner
                && (!put->target_owner
                    || !put->relocation_capacity_fence)))
            throw std::invalid_argument (
              "authority owner or relocation capacity fence does not match generation transition");
    }

    static std::string transition_name (
      authority_generation_transition_t transition)
    {
        switch (transition) {
        case authority_generation_transition_t::preserve:
            return "preserve";
        case authority_generation_transition_t::new_owner:
            return "new_owner";
        }
        throw std::invalid_argument (
          "unknown authority generation transition");
    }

    static std::string bytes_to_string (
      const std::vector<std::byte> &value)
    {
        std::string result;
        result.reserve (value.size ());
        for (const auto item : value)
            result.push_back (
              static_cast<char> (
                std::to_integer<unsigned char> (item)));
        return result;
    }

    static std::vector<std::byte> string_to_bytes (
      std::string_view value)
    {
        std::vector<std::byte> result;
        result.reserve (value.size ());
        for (const auto item : value)
            result.push_back (
              static_cast<std::byte> (
                static_cast<unsigned char> (item)));
        return result;
    }

    static std::string bytes_to_hex (
      const std::vector<std::byte> &value)
    {
        static constexpr char hex[] =
          "0123456789abcdef";
        std::string result;
        result.reserve (value.size () * 2);
        for (const auto item : value) {
            const auto byte =
              std::to_integer<unsigned char> (item);
            result.push_back (hex[byte >> 4]);
            result.push_back (hex[byte & 0x0f]);
        }
        return result;
    }

    static std::string random_lower_hex_128 ()
    {
        static thread_local std::mt19937_64 generator{
          std::random_device{} ()};
        static constexpr char alphabet[] =
          "0123456789abcdef";
        std::string result (32, '0');
        for (std::size_t offset = 0; offset < result.size ();
             offset += 16) {
            auto value = generator ();
            for (std::size_t index = 0; index < 16; ++index) {
                result[offset + 15 - index] =
                  alphabet[value & 0x0fu];
                value >>= 4u;
            }
        }
        if (std::all_of (
              result.begin (), result.end (),
              [] (char value) { return value == '0'; }))
            result.back () = '1';
        return result;
    }

    static std::string revision_lower_hex (
      std::string_view decimal)
    {
        const auto value = std::stoull (
          std::string (decimal));
        if (value
            > static_cast<std::uint64_t> (
              std::numeric_limits<std::int64_t>::max ()))
            throw std::invalid_argument (
              "store revision exceeds 2^63-1");
        std::ostringstream result;
        result << std::hex << std::setfill ('0')
               << std::setw (16) << value;
        return result.str ();
    }

    static bool all_zero (
      const std::array<std::byte, 16> &value)
    {
        return std::all_of (
          value.begin (), value.end (),
          [] (std::byte item) {
              return item == std::byte{0};
          });
    }

    static std::string reservation_id_key (
      const std::array<std::byte, 16> &value)
    {
        static constexpr char hex[] =
          "0123456789abcdef";
        std::string result;
        result.reserve (32);
        for (const auto item : value) {
            const auto byte =
              std::to_integer<unsigned char> (item);
            result.push_back (hex[byte >> 4]);
            result.push_back (hex[byte & 0x0f]);
        }
        return result;
    }

    static std::string relocation_capacity_fingerprint (
      const relocation_capacity_reserve_request_t &request)
    {
        nlohmann::ordered_json value;
        value["reservationId"] =
          reservation_id_key (request.reservation_id);
        value["authorityKey"] = request.key.value;
        value["expectedVersion"] =
          request.expected_store_version;
        value["objectKind"] =
          static_cast<int> (request.object_kind);
        value["stableType"] = request.stable_type;
        value["source"] = {
          {"mesh", request.source.mesh_name},
          {"node", std::string (
             request.source.node_rid.value ())},
          {"lifecycle",
           request.source.node_lifecycle_generation},
          {"owner", request.source.owner.owner_id},
          {"lease",
           request.source.owner.lease_generation}};
        value["target"] = {
          {"mesh", request.target.mesh_name},
          {"node", std::string (
             request.target.node_rid.value ())},
          {"lifecycle",
           request.target.node_lifecycle_generation},
          {"owner", request.target.owner.owner_id},
          {"lease",
           request.target.owner.lease_generation}};
        value["capacityDelta"] = request.capacity_delta;
        return value.dump ();
    }

    static std::string object_key (
      const object_creation_key_t &key)
    {
        return std::to_string (
                 static_cast<int> (key.kind))
               + ":" + key.global_id;
    }

    template <std::size_t Size>
    static std::string byte_array_key (
      const std::array<std::byte, Size> &value)
    {
        static constexpr char hex[] =
          "0123456789abcdef";
        std::string result;
        result.reserve (Size * 2);
        for (const auto item : value) {
            const auto byte =
              std::to_integer<unsigned char> (item);
            result.push_back (hex[byte >> 4]);
            result.push_back (hex[byte & 0x0f]);
        }
        return result;
    }

    static std::string object_reservation_fingerprint (
      const object_reserve_request_t &request)
    {
        nlohmann::ordered_json value;
        value["kind"] =
          static_cast<int> (request.key.kind);
        value["globalId"] = request.key.global_id;
        value["stableType"] = request.intent.stable_type;
        value["placementProfile"] =
          request.intent.placement_profile
            ? nlohmann::json (
                std::string (
                  request.intent.placement_profile->value ()))
            : nlohmann::json (nullptr);
        value["affinityKey"] =
          request.intent.affinity_key
            ? nlohmann::json (
                std::string (
                  request.intent.affinity_key->value ()))
            : nlohmann::json (nullptr);
        value["contentReference"] =
          request.intent.request_content_reference;
        value["sha256"] =
          byte_array_key (request.intent.request_sha256);
        value["encodedSize"] =
          request.intent.request_encoded_size;
        value["target"] = {
          {"mesh", request.target.mesh_name},
          {"node", std::string (
             request.target.node_rid.value ())},
          {"lifecycle",
           request.target.node_lifecycle_generation},
          {"owner", request.target.owner.owner_id},
          {"lease",
           request.target.owner.lease_generation}};
        value["capacityDelta"] =
          request.pending_capacity_delta;
        value["creatingPayload"] =
          bytes_to_hex (request.creating_payload);
        return value.dump ();
    }

    static std::vector<std::string> object_fence_args (
      const object_reservation_fence_t &fence)
    {
        return {
          fence.reservation_id,
          fence.expected_store_version,
          std::to_string (fence.object_generation),
          std::to_string (
            fence.authority_owner_generation),
          fence.target.mesh_name,
          std::string (fence.target.node_rid.value ()),
          std::to_string (
            fence.target.node_lifecycle_generation),
          fence.target.owner.owner_id,
          std::to_string (
            fence.target.owner.lease_generation),
          std::to_string (
            fence.pending_capacity_delta)};
    }

    static authority_snapshot_t parse_object_snapshot_result (
      const std::vector<std::string> &result)
    {
        return {
          result[1],
          string_to_bytes (result[2]),
          std::stoull (result[3]),
          std::stoull (result[4]),
          {result[5], std::stoll (result[6])},
          detail::redis_location_script_result_t::
            from_unix_ms (std::stoll (result[18])),
          parse_allocation (result, 7),
          parse_pending_creation (result, 14)};
    }

    static std::optional<pending_object_creation_t>
    parse_pending_creation (
      const std::vector<std::string> &result,
      std::size_t offset)
    {
        if (result[offset].empty ()
            && result[offset + 1].empty ()
            && result[offset + 2].empty ()
            && (result[offset + 3].empty ()
                || result[offset + 3] == "0"))
            return std::nullopt;
        if (result[offset].empty ()
            || result[offset + 2].size () != 64)
            throw std::invalid_argument (
              "Pending creation projection is incomplete");
        std::array<std::byte, 32> sha256{};
        const auto hex_value = [] (char value) -> unsigned {
            if (value >= '0' && value <= '9')
                return static_cast<unsigned> (value - '0');
            if (value >= 'a' && value <= 'f')
                return static_cast<unsigned> (
                  value - 'a' + 10);
            if (value >= 'A' && value <= 'F')
                return static_cast<unsigned> (
                  value - 'A' + 10);
            throw std::invalid_argument (
              "Pending creation SHA-256 is not hexadecimal");
        };
        for (std::size_t index = 0; index < sha256.size ();
             ++index)
            sha256[index] = static_cast<std::byte> (
              (hex_value (result[offset + 2][index * 2]) << 4u)
              | hex_value (
                result[offset + 2][index * 2 + 1]));
        const auto encoded_size =
          std::stoull (result[offset + 3]);
        if (encoded_size > 1024u * 1024u)
            throw std::invalid_argument (
              "Pending creation encoded size exceeds 1 MiB");
        return pending_object_creation_t{
          result[offset],
          result[offset + 1],
          sha256,
          static_cast<std::uint32_t> (encoded_size)};
    }

    static void validate_aggregate_prepare_request (
      const aggregate_prepare_request_t &request)
    {
        if (all_zero (request.aggregate_id.value)
            || request.aggregate_generation == 0
            || request.aggregate_generation
                 > static_cast<std::uint64_t> (
                   std::numeric_limits<std::int64_t>::max ())
            || request.participants.empty ()
            || request.participants.size () > 1024
            || request.target_owner.owner_id.empty ()
            || request.target_owner.lease_generation <= 0)
            throw std::invalid_argument (
              "aggregate prepare request is incomplete");
        std::string previous;
        for (const auto &participant : request.participants) {
            if (!previous.empty ()
                && participant.key.value <= previous)
                throw std::invalid_argument (
                  "aggregate participants must be sorted and unique");
            previous = participant.key.value;
        }
        if (aggregate_fingerprint (request).size ()
            > 1024u * 1024u)
            throw std::invalid_argument (
              "aggregate prepare request exceeds 1 MiB");
    }

    static std::string aggregate_fingerprint (
      const aggregate_prepare_request_t &request)
    {
        nlohmann::ordered_json value;
        value["aggregateId"] =
          byte_array_key (request.aggregate_id.value);
        value["generation"] =
          request.aggregate_generation;
        value["inventoryDigest"] =
          byte_array_key (request.inventory_digest.value);
        value["targetOwner"] = {
          {"id", request.target_owner.owner_id},
          {"lease",
           request.target_owner.lease_generation}};
        value["participants"] =
          nlohmann::ordered_json::array ();
        for (const auto &participant : request.participants)
            value["participants"].push_back ({
              {"key", participant.key.value},
              {"expectedVersion",
               participant.expected_store_version},
              {"transition",
               transition_name (
                 participant.owner_transition)},
              {"payload",
               bytes_to_hex (
                 participant.authority_payload)},
              {"membership",
               bytes_to_hex (
                 participant.membership_mutation)}});
        value["reservations"] =
          nlohmann::ordered_json::array ();
        for (const auto &fence :
             request.target_reservations)
            value["reservations"].push_back (
              fence.value);
        return value.dump ();
    }

    template <typename T> static task_t<T> cancelled ()
    {
        return task_t<T> (
          zlink::framework::detail::boundary_failure<T> (
            zlink::framework::detail::boundary_error_t::cancelled,
            "Redis Location Store operation was cancelled"));
    }

#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
    authority_read_result_t read_authority_sync (
      const authority_key_t &key)
    {
        const auto keys = std::vector<std::string>{
          detail::redis_location_key_schema_t::authority_key (
            _options.key_prefix, key.value)};
        const auto args = std::vector<std::string>{};
        const auto result = redis_get (
          client ().eval<std::vector<std::string>> (
            std::string (
              detail::redis_location_scripts_t::read_authority),
            keys.begin (), keys.end (), args.begin (),
            args.end ()));
        if (result.size () != 19)
            throw sw::redis::Error (
              "invalid authority read result");
        const auto now =
          detail::redis_location_script_result_t::from_unix_ms (
            std::stoll (result[18]));
        if (result[0] == "missing")
            return authority_missing_t{now};
        if (result[0] != "found")
            throw sw::redis::Error (
              "unknown authority read result");
        return authority_snapshot_t{
          result[1],
          string_to_bytes (result[2]),
          std::stoull (result[3]),
          std::stoull (result[4]),
          {result[5], std::stoll (result[6])},
          now,
          parse_allocation (result, 7),
          parse_pending_creation (result, 14)};
    }

    authority_read_result_t read_authority_at_sync (
      const authority_key_t &key,
      std::string_view watermark)
    {
        const auto index_member =
          detail::redis_location_key_schema_t::
            authority_index_member (key.value);
        const auto tombstone = redis_get (
          client ().hget (
            detail::redis_location_key_schema_t::
              authority_index_gc_key (_options.key_prefix),
            index_member));
        const auto current = read_authority_sync (key);
        const auto store_now =
          std::visit (
            [] (const auto &value) {
                return value.store_now;
            },
            current);
        if (tombstone
            && std::stoull (*tombstone)
                 <= std::stoull (std::string (watermark)))
            return authority_missing_t{store_now};
        if (const auto *snapshot =
              std::get_if<authority_snapshot_t> (&current);
            snapshot
            && std::stoull (snapshot->store_version)
                 <= std::stoull (std::string (watermark)))
            return *snapshot;
        const auto watermark_hex =
          revision_lower_hex (watermark);
        const auto revisions = redis_get (
          client ().command<std::vector<std::string>> (
            "ZREVRANGEBYLEX",
            detail::redis_location_key_schema_t::
              authority_history_revisions_key (
                _options.key_prefix, key.value),
            "[" + watermark_hex, "-", "LIMIT", "0", "1"));
        if (revisions.empty ())
            return authority_missing_t{store_now};
        const auto &revision = revisions.front ();
        static constexpr std::array<std::string_view, 16>
          fields{
            "storeVersion", "payload", "objectGeneration",
            "authorityOwnerGeneration", "ownerId",
            "ownerLeaseGeneration", "allocationState",
            "objectKind", "stableType", "descriptorKey",
            "descriptorLifecycleGeneration",
            "capacityDelta", "pendingCreationReservationId",
            "pendingCreationReference", "pendingCreationSha256",
            "pendingCreationEncodedSize"};
        std::vector<std::string> names;
        names.reserve (fields.size ());
        for (const auto field : fields)
            names.push_back (
              revision + ":" + std::string (field));
        const auto values = redis_get (
          client ().hmget<
            std::vector<sw::redis::OptionalString>> (
            detail::redis_location_key_schema_t::
              authority_history_key (
                _options.key_prefix, key.value),
            names.begin (), names.end ()));
        if (values.size () != fields.size ()
            || std::any_of (
              values.begin (), values.begin () + 12,
              [] (const auto &value) {
                  return !static_cast<bool> (value);
              }))
            return authority_missing_t{store_now};
        std::vector<std::string> encoded{
          *values[0], *values[1], *values[2], *values[3],
          *values[4], *values[5], *values[6], *values[7],
          *values[8], *values[9], std::string{},
          *values[10], *values[11],
          values[12] ? *values[12] : std::string{},
          values[13] ? *values[13] : std::string{},
          values[14] ? *values[14] : std::string{},
          values[15] ? *values[15] : std::string{"0"}};
        return authority_snapshot_t{
          encoded[0],
          string_to_bytes (encoded[1]),
          std::stoull (encoded[2]),
          std::stoull (encoded[3]),
          {encoded[4], std::stoll (encoded[5])},
          store_now,
          parse_allocation (encoded, 6),
          parse_pending_creation (encoded, 13)};
    }

    std::int64_t redis_time_ms_sync ()
    {
        const auto value = redis_get (
          client ().command<std::vector<std::string>> (
            "TIME"));
        if (value.size () != 2)
            throw sw::redis::Error (
              "invalid Redis TIME result");
        return std::stoll (value[0]) * 1000
               + std::stoll (value[1]) / 1000;
    }

    void cleanup_expired_scans_sync ()
    {
        const auto expiry_key =
          detail::redis_location_key_schema_t::
            authority_scans_expiry_key (
              _options.key_prefix);
        const auto watermark_key =
          detail::redis_location_key_schema_t::
            authority_scans_watermark_key (
              _options.key_prefix);
        const auto expired = redis_get (
          client ().command<std::vector<std::string>> (
            "ZRANGEBYSCORE", expiry_key, "-inf",
            std::to_string (redis_time_ms_sync ()), "LIMIT",
            "0", "100"));
        for (const auto &scan_id : expired) {
            const auto scan_key =
              detail::redis_location_key_schema_t::
                authority_scan_key (
                  _options.key_prefix, scan_id);
            const auto member = redis_get (
              client ().hget (
                scan_key, "watermarkMember"));
            if (member)
                (void) redis_get (
                  client ().command<long long> (
                    "ZREM", watermark_key, *member));
            (void) redis_get (client ().del (scan_key));
            (void) redis_get (
              client ().command<long long> (
                "ZREM", expiry_key, scan_id));
        }
    }

    void gc_authority_history_sync ()
    {
        cleanup_expired_scans_sync ();
        const auto watermark_members = redis_get (
          client ().command<std::vector<std::string>> (
            "ZRANGE",
            detail::redis_location_key_schema_t::
              authority_scans_watermark_key (
                _options.key_prefix),
            "0", "0"));
        const auto minimum_hex =
          watermark_members.empty ()
            ? std::string{"ffffffffffffffff"}
            : watermark_members.front ().substr (0, 16);
        const auto tombstones = redis_get (
          client ().command<std::tuple<
            std::string, std::vector<std::string>>> (
            "HSCAN",
            detail::redis_location_key_schema_t::
              authority_index_gc_key (
                _options.key_prefix),
            "0", "COUNT", "100"));
        const auto &entries = std::get<1> (tombstones);
        for (std::size_t index = 0;
             index + 1 < entries.size (); index += 2) {
            const auto delete_hex =
              revision_lower_hex (entries[index + 1]);
            if (delete_hex >= minimum_hex)
                continue;
            const auto authority =
              detail::redis_location_key_schema_t::
                decode_authority_index_member (
                  entries[index]);
            (void) redis_get (
              client ().command<long long> (
                "ZREM",
                detail::redis_location_key_schema_t::
                  authority_keys_key (
                    _options.key_prefix),
                entries[index]));
            (void) redis_get (
              client ().command<long long> (
                "HDEL",
                detail::redis_location_key_schema_t::
                  authority_index_gc_key (
                    _options.key_prefix),
                entries[index]));
            (void) redis_get (
              client ().del (
                detail::redis_location_key_schema_t::
                  authority_history_key (
                    _options.key_prefix, authority)));
            (void) redis_get (
              client ().del (
                detail::redis_location_key_schema_t::
                  authority_history_revisions_key (
                    _options.key_prefix, authority)));
            (void) redis_get (
              client ().del (
                detail::redis_location_key_schema_t::
                  membership_history_key (
                    _options.key_prefix, authority)));
            (void) redis_get (
              client ().del (
                detail::redis_location_key_schema_t::
                  membership_history_revisions_key (
                    _options.key_prefix, authority)));
        }
    }

    static authority_read_result_t parse_cas_authority_result (
      const std::vector<std::string> &result)
    {
        const auto now =
          detail::redis_location_script_result_t::from_unix_ms (
            std::stoll (result[15]));
        if (result[1] == "missing")
            return authority_missing_t{now};
        return authority_snapshot_t{
          result[2],
          string_to_bytes (result[3]),
          std::stoull (result[4]),
          std::stoull (result[5]),
          {result[6], std::stoll (result[7])},
          now,
          parse_allocation (result, 8)};
    }

    static placement_allocation_t parse_allocation (
      const std::vector<std::string> &result,
      std::size_t offset)
    {
        const auto object_kind =
          (result[offset].empty ()
           && (result[offset + 1].empty ()
               || result[offset + 1] == "0"))
            ? placement_object_kind_t::actor
            : detail::redis_location_key_schema_t::
                parse_object_kind_token (
                  result[offset + 1]);
        std::string mesh = result[offset + 3];
        std::string node_value = result[offset + 4];
        if (!mesh.empty () && node_value.empty ()) {
            auto decoded =
              detail::redis_location_key_schema_t::
                decode_mesh_node_key (mesh);
            mesh = std::move (decoded.mesh_name);
            node_value = decoded.rid.to_string ();
        }
        node_rid_t node;
        if (!node_value.empty ())
            node = node_rid_t::from_string (
              node_value);
        return {
          result[offset] == "active"
            ? placement_capacity_state_t::active
            : placement_capacity_state_t::pending,
          object_kind,
          result[offset + 2],
          std::move (mesh),
          std::move (node),
          std::stoull (result[offset + 5]),
          static_cast<std::uint32_t> (
            std::stoul (result[offset + 6]))};
    }

    static bool parse_authority_cursor (
      std::string_view cursor,
      std::string &scan_id,
      std::uint64_t &sequence)
    {
        const auto separator = cursor.find (':');
        if (separator == std::string_view::npos)
            return false;
        scan_id = std::string (cursor.substr (0, separator));
        try {
            sequence = std::stoull (
              std::string (cursor.substr (separator + 1)));
            return true;
        }
        catch (...) {
            return false;
        }
    }

#endif

    static std::string intent_name (location_write_intent_t intent)
    {
        switch (intent) {
            case location_write_intent_t::new_claim:
                return "new";
            case location_write_intent_t::renew:
                return "renew";
            case location_write_intent_t::takeover:
                return "takeover";
            default:
                throw std::invalid_argument ("unknown location write intent");
        }
    }

    task_t<location_write_result_t> write_row (location_kind_t kind,
                                               const std::string &row_key,
                                               std::optional<std::string> mesh_name,
                                               const std::string &owner_id,
                                               std::int64_t generation,
                                               const std::string &json,
                                               location_write_intent_t intent)
    {
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<location_write_result_t> ([this, kind, row_key, mesh_name,
                                                         owner_id, generation, json, intent] {
          try {
            const auto row_physical_key =
              detail::redis_location_key_schema_t::row_key (
                _options.key_prefix, kind, row_key);
            const auto existing_owner = redis_get (
              client ().hget (row_physical_key, "owner"));
            const auto current_owner =
              existing_owner
                ? *existing_owner : std::string{};
            const auto scoped_stamp =
              detail::redis_location_key_schema_t::stamp_key (
                _options.key_prefix, kind,
                optional_view (mesh_name));
            const auto global_stamp =
              detail::redis_location_key_schema_t::stamp_key (
                _options.key_prefix, kind, std::nullopt);
            const auto keys = std::vector<std::string>{
              row_physical_key,
              detail::redis_location_key_schema_t::generation_key (_options.key_prefix, kind,
                                                                   row_key),
              detail::redis_location_key_schema_t::keys_key (_options.key_prefix, kind),
              detail::redis_location_key_schema_t::lease_key (
                _options.key_prefix, current_owner),
              detail::redis_location_key_schema_t::owner_key (
                _options.key_prefix, kind, owner_id),
              detail::redis_location_key_schema_t::owner_key (
                _options.key_prefix, kind, current_owner),
              scoped_stamp,
              global_stamp};
            const auto args = std::vector<std::string>{
              intent_name (intent),
              owner_id,
              std::to_string (generation),
              json,
              row_key,
              std::string{},
              std::string{},
              std::string{},
              std::string{},
              mesh_name ? "1" : "0",
              mesh_name.value_or (std::string{}),
              current_owner};
            const auto result = redis_get (
              client ().eval<std::tuple<std::string, long long, long long>> (
                std::string (detail::redis_location_scripts_t::write), keys.begin (), keys.end (),
                args.begin (), args.end ()));
            return detail::redis_location_script_result_t::write_result (
              std::get<0> (result), static_cast<std::int64_t> (std::get<1> (result)),
              static_cast<std::int64_t> (std::get<2> (result)));
          }
          catch (const sw::redis::Error &error) {
              throw framework_exception_t (framework_error_kind_t::request_failed, error.what (),
                                           true);
          }
        });
#else
        (void) kind;
        (void) row_key;
        (void) mesh_name;
        (void) owner_id;
        (void) generation;
        (void) json;
        (void) intent;
        return unavailable_write ();
#endif
    }

    task_t<location_write_result_t> remove_row (location_kind_t kind,
                                                const std::string &row_key,
                                                std::optional<std::string> mesh_name,
                                                location_owner_token_t owner)
    {
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<location_write_result_t> ([this, kind, row_key, mesh_name,
                                                         owner = std::move (owner)] {
          try {
            const auto scoped_stamp =
              detail::redis_location_key_schema_t::stamp_key (
                _options.key_prefix, kind,
                optional_view (mesh_name));
            const auto global_stamp =
              detail::redis_location_key_schema_t::stamp_key (
                _options.key_prefix, kind, std::nullopt);
            const auto keys = std::vector<std::string>{
              detail::redis_location_key_schema_t::row_key (_options.key_prefix, kind, row_key),
              detail::redis_location_key_schema_t::keys_key (_options.key_prefix, kind),
              detail::redis_location_key_schema_t::owner_key (
                _options.key_prefix, kind, owner.owner_id),
              scoped_stamp,
              global_stamp};
            const auto args = std::vector<std::string>{
              owner.owner_id,
              std::to_string (owner.generation),
              row_key,
              mesh_name ? "1" : "0"};
            const auto result = redis_get (
              client ().eval<std::tuple<std::string, long long, long long>> (
                std::string (detail::redis_location_scripts_t::remove), keys.begin (),
                keys.end (), args.begin (), args.end ()));
            return detail::redis_location_script_result_t::write_result (
              std::get<0> (result), static_cast<std::int64_t> (std::get<1> (result)),
              static_cast<std::int64_t> (std::get<2> (result)));
          }
          catch (const sw::redis::Error &error) {
              throw framework_exception_t (framework_error_kind_t::request_failed, error.what (),
                                           true);
          }
        });
#else
        (void) kind;
        (void) row_key;
        (void) mesh_name;
        (void) owner;
        return unavailable_write ();
#endif
    }

    template <typename TRow, typename Decode>
    task_t<std::optional<TRow>> resolve_row (location_kind_t kind,
                                             const std::string &row_key,
                                             Decode decode)
    {
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<std::optional<TRow>> (
          [this, kind, row_key, decode = std::move (decode)] () mutable {
              return load_row<TRow> (kind, row_key, decode);
          });
#else
        (void) kind;
        (void) row_key;
        (void) decode;
        return unavailable_read<std::optional<TRow>> ();
#endif
    }

    template <typename TRow, typename TFilter, typename Decode>
    task_t<std::vector<TRow>> list_unpaged (location_kind_t kind, TFilter filter, Decode decode)
    {
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<std::vector<TRow>> (
          [this, kind, filter = std::move (filter), decode = std::move (decode)] () mutable {
            auto row_keys = redis_get (
              client ().smembers<std::vector<std::string>> (
                detail::redis_location_key_schema_t::keys_key (_options.key_prefix, kind)));
            std::vector<TRow> rows;
            for (const auto &row_key : row_keys) {
                auto row = load_row<TRow> (kind, row_key, decode);
                if (row && matches (*row, filter)) {
                    rows.push_back (std::move (*row));
                }
            }
            return rows;
          });
#else
        (void) kind;
        (void) filter;
        (void) decode;
        return unavailable_read<std::vector<TRow>> ();
#endif
    }

    template <typename TRow, typename TFilter, typename Decode>
    task_t<location_page_t<TRow>> list_paged (location_kind_t kind,
                                              TFilter filter,
                                              location_page_request_t page,
                                              Decode decode)
    {
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<location_page_t<TRow>> (
          [this, kind, filter = std::move (filter), page, decode = std::move (decode)] () mutable {
            location_page_t<TRow> result;
            auto scan = parse_scan_state (page.continuation_token);
            const auto limited = page.page_size > 0;
            const auto page_size = limited ? static_cast<std::size_t> (page.page_size) : 0;
            const auto set_key =
              detail::redis_location_key_schema_t::keys_key (_options.key_prefix, kind);
            while (!limited || result.items.size () < page_size) {
                if (scan.pending_keys.empty ()) {
                    if (scan.started && scan.cursor == 0) {
                        break;
                    }
                    const auto remaining = limited ? page_size - result.items.size () : 100;
                    const auto reply = redis_get (
                      client ().command<std::tuple<std::string, std::vector<std::string>>> (
                        "SSCAN", set_key, std::to_string (scan.cursor), "COUNT",
                        std::to_string (std::max<std::size_t> (1, remaining))));
                    scan.cursor = std::stoull (std::get<0> (reply));
                    scan.started = true;
                    scan.pending_keys = std::move (std::get<1> (reply));
                    if (scan.pending_keys.empty ()) {
                        continue;
                    }
                }

                auto row_key = std::move (scan.pending_keys.back ());
                scan.pending_keys.pop_back ();
                auto row = load_row<TRow> (kind, row_key, decode);
                if (row && matches (*row, filter)) {
                    result.items.push_back (std::move (*row));
                }
            }
            if (!scan.pending_keys.empty () || scan.cursor != 0) {
                result.continuation_token = encode_scan_state (scan);
            }
            return result;
          });
#else
        (void) kind;
        (void) filter;
        (void) page;
        (void) decode;
        return unavailable_read<location_page_t<TRow>> ();
#endif
    }

#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
    template <typename TRow, typename Decode>
    std::optional<TRow> load_row (location_kind_t kind, const std::string &row_key, Decode decode)
    {
        auto fields = redis_get (
          client ().hmget<std::vector<sw::redis::OptionalString>> (
            detail::redis_location_key_schema_t::row_key (_options.key_prefix, kind, row_key),
            {"json", "gen", "updatedAtMs"}));
        if (fields.size () < 3 || !fields[0]) {
            return std::nullopt;
        }
        auto row = decode (*fields[0]);
        if constexpr (requires { row.generation; }) {
          if (fields[1]) {
            row.generation = std::stoll (*fields[1]);
          }
        }
        if (fields[2]) {
            row.updated_at =
              detail::redis_location_script_result_t::from_unix_ms (std::stoll (*fields[2]));
        }
        return row;
    }

    static owner_lease_t parse_lease_value (std::string owner_id, const std::string &value)
    {
        const auto separator = value.find ('|');
        if (separator == std::string::npos) {
            throw sw::redis::Error ("invalid Redis owner lease value");
        }
        owner_lease_t lease;
        lease.owner_id = std::move (owner_id);
        lease.node_rid = zlink::routing_id_t::from_hex (value.substr (0, separator));
        lease.updated_at = detail::redis_location_script_result_t::from_unix_ms (
          std::stoll (value.substr (separator + 1)));
        return lease;
    }
#endif

    static std::string normalize_connection_string (const std::string &connection_string)
    {
        return connection_string.find ("://") == std::string::npos
                 ? "tcp://" + connection_string
                 : connection_string;
    }

#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
    template <typename T> T redis_get (sw::redis::Future<T> future)
    {
        if (future.wait_for (_options.operation_timeout) != std::future_status::ready) {
            throw sw::redis::TimeoutError ("redis location operation timed out");
        }
        return future.get ();
    }

    sw::redis::AsyncRedis &client ()
    {
        std::lock_guard lock (_client_gate);
        if (!_client) {
            sw::redis::Uri uri (normalize_connection_string (_options.connection_string));
            _event_loop = std::make_shared<sw::redis::EventLoop> ();
            auto connection_options = uri.connection_options ();
            connection_options.connect_timeout = std::chrono::milliseconds (500);
            _client = std::make_unique<sw::redis::AsyncRedis> (
              connection_options, sw::redis::ConnectionPoolOptions{}, _event_loop);
        }
        return *_client;
    }
#endif

    static std::optional<std::string_view> optional_view (const std::optional<std::string> &value)
    {
        return value ? std::optional<std::string_view> (*value) : std::nullopt;
    }

    std::string owner_key_prefix (location_kind_t kind) const
    {
        return detail::redis_location_key_schema_t::owner_key (_options.key_prefix, kind, "");
    }

    std::string row_key_prefix (location_kind_t kind) const
    {
        return detail::redis_location_key_schema_t::row_key (_options.key_prefix, kind, "");
    }

    std::string lease_key_prefix () const
    {
        return detail::redis_location_key_schema_t::lease_key (_options.key_prefix, "");
    }

#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
    struct redis_scan_state_t
    {
        unsigned long long cursor = 0;
        bool started = false;
        std::vector<std::string> pending_keys;
    };

    static redis_scan_state_t parse_scan_state (const std::optional<std::string> &token)
    {
        if (!token) {
            return {};
        }
        try {
            const auto json = nlohmann::json::parse (*token);
            redis_scan_state_t state;
            state.cursor = std::stoull (json.at ("cursor").get<std::string> ());
            state.started = true;
            state.pending_keys = json.at ("pending").get<std::vector<std::string>> ();
            return state;
        }
        catch (...) {
            return {};
        }
    }

    static std::string encode_scan_state (const redis_scan_state_t &state)
    {
        return nlohmann::json{{"cursor", std::to_string (state.cursor)},
                              {"pending", state.pending_keys}}
          .dump ();
    }
#endif

    static bool matches (const peer_location_t &row, const peer_location_filter_t &filter)
    {
        return (!filter.auto_connect_type || row.auto_connect_type == *filter.auto_connect_type)
               && (!filter.mesh_name || row.mesh_name == *filter.mesh_name)
               && (!filter.role || row.role == *filter.role)
               && (!filter.node_rid || (row.node_rid && *row.node_rid == *filter.node_rid))
               && (!filter.endpoint || row.endpoint == *filter.endpoint);
    }

    static bool matches (const spot_location_t &row, const spot_location_filter_t &filter)
    {
        return (!filter.mesh_name || row.mesh_name == *filter.mesh_name)
               && (!filter.spot_type || (row.spot_type && *row.spot_type == *filter.spot_type))
               && (!filter.node_rid || row.node_rid == *filter.node_rid)
               && (!filter.spot_kind || row.spot_kind == *filter.spot_kind);
    }

    static bool matches (const actor_location_t &row, const actor_location_filter_t &filter)
    {
        return (!filter.mesh_name || row.mesh_name == *filter.mesh_name)
               && (!filter.actor_type || row.actor_type == *filter.actor_type)
               && (!filter.owner_node_rid || row.owner_node_rid == *filter.owner_node_rid)
               && (!filter.spot_rid || row.spot_rid == *filter.spot_rid)
               && (!filter.spot_kind || row.spot_kind == *filter.spot_kind);
    }

    static bool matches (const route_location_t &row, const route_location_filter_t &filter)
    {
        return (!filter.route_kind || row.route_kind == *filter.route_kind)
               && (!filter.owner_node_rid || row.owner_node_rid == *filter.owner_node_rid)
               && (!filter.owner_id || row.owner_id == *filter.owner_id);
    }

    template <typename T> static task_t<T> completed (T value)
    {
        return task_t<T> (result_t<T>::success (std::move (value)));
    }

    static task_t<location_write_result_t> unavailable_write ()
    {
        return task_t<location_write_result_t> (result_t<location_write_result_t>::failure (
          framework_error_kind_t::request_failed,
          "redis-plus-plus client is not available in this build",
          true));
    }

    template <typename T> static task_t<T> unavailable_read ()
    {
        return task_t<T> (result_t<T>::failure (
          framework_error_kind_t::request_failed,
          "redis-plus-plus client is not available in this build",
          true));
    }

    redis_location_options_t _options;
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
    std::mutex _client_gate;
    sw::redis::EventLoopSPtr _event_loop;
    std::unique_ptr<sw::redis::AsyncRedis> _client;
    redis_location_worker_t _worker;
#endif
};

} // namespace zlink::framework::locations::redis
