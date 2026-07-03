/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/locations/location.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <algorithm>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <condition_variable>
#include <deque>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
#include <sw/redis++/async_redis++.h>
#endif

namespace zlink::framework::locations::redis
{

struct redis_location_options_t
{
    std::string connection_string = "127.0.0.1:6379";
    std::string key_prefix = "zlink:locations";
};

namespace detail
{

class redis_location_scripts_t
{
  public:
    static constexpr std::string_view write = R"(
if redis.replicate_commands then redis.replicate_commands() end
local time = redis.call('TIME')
local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)

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
redis.call('SREM', ARGV[4] .. currentOwner, ARGV[3])
redis.call('INCR', ARGV[5])
if ARGV[6] ~= '' then redis.call('INCR', ARGV[6]) end
return {'stored', tonumber(ARGV[2]), nowMs}
)";

    static constexpr std::string_view remove_by_owner = R"(
if redis.replicate_commands then redis.replicate_commands() end
local rowKeys = redis.call('SMEMBERS', KEYS[1])
local removed = 0
for _, rowKey in ipairs(rowKeys) do
    local rowHash = ARGV[1] .. rowKey
    local mesh = redis.call('HGET', rowHash, 'mesh')
    if redis.call('DEL', rowHash) == 1 then
        removed = removed + 1
        redis.call('SREM', KEYS[2], rowKey)
        if mesh then
            redis.call('INCR', ARGV[2] .. ':' .. mesh)
        end
        redis.call('INCR', ARGV[2])
    end
end
redis.call('DEL', KEYS[1])
return removed
)";

    static constexpr std::string_view renew_lease = R"(
if redis.replicate_commands then redis.replicate_commands() end
local time = redis.call('TIME')
local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)

redis.call('SET', KEYS[1], ARGV[2] .. '|' .. nowMs, 'PX', ARGV[3])
redis.call('SADD', KEYS[2], ARGV[1])
return nowMs
)";

    static constexpr std::string_view remove_lease = R"(
if redis.replicate_commands then redis.replicate_commands() end
local time = redis.call('TIME')
local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)

redis.call('DEL', KEYS[1])
redis.call('SREM', KEYS[2], ARGV[1])
return nowMs
)";

    static constexpr std::string_view list_leases = R"(
if redis.replicate_commands then redis.replicate_commands() end
local time = redis.call('TIME')
local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)

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
    static std::string encode_peer_key (const peer_location_key_t &key)
    {
        const auto identity = key.node_rid ? key.node_rid->to_hex ()
                                          : key.endpoint.value_or (std::string{});
        return encode (to_canonical_string (key.auto_connect_type), key.mesh_name,
                       to_canonical_string (key.role), identity);
    }

    static std::string encode_spot_key (const spot_location_key_t &key)
    {
        return encode (key.mesh_name, key.spot_rid.to_hex ());
    }

    static std::string encode_actor_key (const actor_location_key_t &key)
    {
        return encode (key.actor_type, key.actor_id);
    }

    static std::string encode_route_key (const route_location_key_t &key)
    {
        return encode (std::to_string (static_cast<int> (key.route_kind)), key.route_key);
    }

    static std::string row_key (std::string_view prefix,
                                location_kind_t kind,
                                std::string_view row_key)
    {
        return join (prefix, "row", kind_tag (kind), row_key);
    }

    static std::string generation_key (std::string_view prefix,
                                       location_kind_t kind,
                                       std::string_view row_key)
    {
        return join (prefix, "gen", kind_tag (kind), row_key);
    }

    static std::string keys_key (std::string_view prefix, location_kind_t kind)
    {
        return join (prefix, "keys", kind_tag (kind));
    }

    static std::string owner_key (std::string_view prefix,
                                  location_kind_t kind,
                                  std::string_view owner_id)
    {
        return join (prefix, "own", kind_tag (kind), owner_id);
    }

    static std::string lease_key (std::string_view prefix, std::string_view owner_id)
    {
        return join (prefix, "lease", owner_id);
    }

    static std::string leases_key (std::string_view prefix) { return join (prefix, "leases"); }

    static std::string stamp_key (std::string_view prefix,
                                  location_kind_t kind,
                                  std::optional<std::string_view> mesh_name = std::nullopt)
    {
        return mesh_name ? join (prefix, "stamp", kind_tag (kind), *mesh_name)
                         : join (prefix, "stamp", kind_tag (kind));
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
    static std::string encode_peer (const peer_location_t &row)
    {
        nlohmann::json json;
        json["AutoConnectType"] = static_cast<int> (row.auto_connect_type);
        json["MeshName"] = row.mesh_name;
        json["NodeRid"] = row.node_rid ? nlohmann::json (row.node_rid->to_hex ())
                                       : nlohmann::json (nullptr);
        json["Role"] = static_cast<int> (row.role);
        json["Endpoint"] = row.endpoint;
        json["Weight"] = row.weight;
        json["Value"] = row.value;
        json["Metadata"] = row.metadata.empty () ? nlohmann::json (nullptr)
                                                 : nlohmann::json (row.metadata);
        json["Capabilities"] = row.capabilities.empty () ? nlohmann::json (nullptr)
                                                         : nlohmann::json (row.capabilities);
        json["OwnerId"] = row.owner_id;
        json["Generation"] = row.generation;
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
        row.value = json.at ("Value").get<std::int64_t> ();
        if (json.contains ("Metadata") && !json.at ("Metadata").is_null ()) {
            row.metadata = json.at ("Metadata").get<std::map<std::string, std::string>> ();
        }
        if (json.contains ("Capabilities") && !json.at ("Capabilities").is_null ()) {
            row.capabilities = json.at ("Capabilities").get<std::vector<std::string>> ();
        }
        row.owner_id = json.at ("OwnerId").get<std::string> ();
        row.generation = json.at ("Generation").get<std::int64_t> ();
        return row;
    }

    static std::string encode_spot (const spot_location_t &row)
    {
        nlohmann::json json;
        json["MeshName"] = row.mesh_name;
        json["SpotRid"] = row.spot_rid.to_hex ();
        json["SpotType"] = row.spot_type ? nlohmann::json (*row.spot_type)
                                         : nlohmann::json (nullptr);
        json["NodeRid"] = row.node_rid.to_hex ();
        json["SpotKind"] = static_cast<int> (row.spot_kind);
        json["RouteEndpoint"] = row.route_endpoint ? nlohmann::json (*row.route_endpoint)
                                                   : nlohmann::json (nullptr);
        json["OwnerId"] = row.owner_id;
        json["Generation"] = row.generation;
        return json.dump ();
    }

    static spot_location_t decode_spot (std::string_view value)
    {
        const auto json = nlohmann::json::parse (value);
        spot_location_t row;
        row.mesh_name = json.at ("MeshName").get<std::string> ();
        row.spot_rid = zlink::routing_id_t::from_hex (json.at ("SpotRid").get<std::string> ());
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
        return row;
    }

    static std::string encode_actor (const actor_location_t &row)
    {
        nlohmann::json json;
        json["ActorType"] = row.actor_type;
        json["ActorId"] = row.actor_id;
        json["ActorRef"] = row.actor_ref;
        json["NodeRid"] = row.node_rid.to_hex ();
        json["Generation"] = row.generation;
        json["LocationKind"] = static_cast<int> (row.location_kind);
        json["SpotRid"] = row.spot_rid ? nlohmann::json (row.spot_rid->to_hex ())
                                       : nlohmann::json (nullptr);
        json["SpotKind"] = static_cast<int> (row.spot_kind);
        json["OwnerId"] = row.owner_id;
        return json.dump ();
    }

    static actor_location_t decode_actor (std::string_view value)
    {
        const auto json = nlohmann::json::parse (value);
        actor_location_t row;
        row.actor_type = json.at ("ActorType").get<std::string> ();
        row.actor_id = json.at ("ActorId").get<std::string> ();
        row.actor_ref = json.at ("ActorRef").get<std::string> ();
        row.node_rid = zlink::routing_id_t::from_hex (json.at ("NodeRid").get<std::string> ());
        row.generation = json.at ("Generation").get<std::int64_t> ();
        row.location_kind = static_cast<zlink::spot_kind> (json.at ("LocationKind").get<int> ());
        if (!json.at ("SpotRid").is_null ()) {
            row.spot_rid =
              zlink::routing_id_t::from_hex (json.at ("SpotRid").get<std::string> ());
        }
        row.spot_kind = static_cast<zlink::spot_kind> (json.at ("SpotKind").get<int> ());
        row.owner_id = json.at ("OwnerId").get<std::string> ();
        return row;
    }

    static std::string encode_route (const route_location_t &row)
    {
        nlohmann::json json;
        json["RouteKind"] = static_cast<int> (row.route_kind);
        json["RouteKey"] = row.route_key;
        json["OwnerNodeRid"] = row.owner_node_rid.to_hex ();
        json["OwnerId"] = row.owner_id;
        json["Generation"] = row.generation;
        json["Value"] = base64_encode (row.value);
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
        return row;
    }

  private:
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
                                     public location_change_stamp_store_t
{
  public:
    explicit redis_location_store_t (redis_location_options_t options = {}) :
        _options (std::move (options))
    {
    }

    const redis_location_options_t &options () const noexcept { return _options; }

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

    task_t<std::int64_t> remove_peers_by_owner (std::string owner_id) override
    {
        return remove_by_owner (location_kind_t::peer, std::move (owner_id));
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

    task_t<std::int64_t> remove_spots_by_owner (std::string owner_id) override
    {
        return remove_by_owner (location_kind_t::spot, std::move (owner_id));
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
          actor_location_key_t{actor.actor_type, actor.actor_id});
        return write_row (location_kind_t::actor, row_key, std::nullopt, actor.owner_id,
                          actor.generation, detail::redis_location_row_codec_t::encode_actor (actor),
                          intent);
    }

    task_t<location_write_result_t> remove_actor (actor_location_key_t key,
                                                  location_owner_token_t owner) override
    {
        return remove_row (location_kind_t::actor,
                           detail::redis_location_key_schema_t::encode_actor_key (key),
                           std::nullopt, std::move (owner));
    }

    task_t<std::int64_t> remove_actors_by_owner (std::string owner_id) override
    {
        return remove_by_owner (location_kind_t::actor, std::move (owner_id));
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

    task_t<std::int64_t> remove_routes_by_owner (std::string owner_id) override
    {
        return remove_by_owner (location_kind_t::route, std::move (owner_id));
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

    task_t<location_write_result_t> renew_owner_lease (
      std::string owner_id, zlink::routing_id_t node_rid, std::chrono::milliseconds lease_ttl) override
    {
        return renew_lease (std::move (owner_id), std::move (node_rid), lease_ttl);
    }

    task_t<location_write_result_t> remove_owner_lease (std::string owner_id) override
    {
        return remove_lease (std::move (owner_id));
    }

    task_t<owner_lease_snapshot_t> list_owner_leases () override
    {
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<owner_lease_snapshot_t> ([this] {
            owner_lease_snapshot_t snapshot;
            snapshot.store_now = redis_time ();
            auto owners = client ()
                            .smembers<std::vector<std::string>> (
                              detail::redis_location_key_schema_t::leases_key (_options.key_prefix))
                            .get ();
            for (const auto &owner_id : owners) {
                const auto lease_key =
                  detail::redis_location_key_schema_t::lease_key (_options.key_prefix, owner_id);
                const auto remaining_ms = client ().pttl (lease_key).get ();
                if (remaining_ms < 0) {
                    client ().srem (
                      detail::redis_location_key_schema_t::leases_key (_options.key_prefix),
                      owner_id)
                      .get ();
                    continue;
                }
                const auto value = client ().get (lease_key).get ();
                if (!value) {
                    continue;
                }
                auto lease = parse_lease_value (owner_id, *value);
                lease.lease_expires_at = snapshot.store_now
                                          + std::chrono::milliseconds (remaining_ms);
                snapshot.leases.push_back (std::move (lease));
            }
            return snapshot;
        });
#else
        return unavailable_read<owner_lease_snapshot_t> ();
#endif
    }

    task_t<std::int64_t> get_change_stamp (location_change_stamp_scope_t scope) override
    {
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<std::int64_t> ([this, scope = std::move (scope)] {
            const auto value =
              client ()
                .get (detail::redis_location_key_schema_t::stamp_key (
                  _options.key_prefix, scope.kind, optional_view (scope.mesh_name)))
                .get ();
            return value ? std::stoll (*value) : 0;
        });
#else
        (void) scope;
        return unavailable_read<std::int64_t> ();
#endif
    }

  private:
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
            const auto keys = std::vector<std::string>{
              detail::redis_location_key_schema_t::row_key (_options.key_prefix, kind, row_key),
              detail::redis_location_key_schema_t::generation_key (_options.key_prefix, kind,
                                                                   row_key),
              detail::redis_location_key_schema_t::keys_key (_options.key_prefix, kind)};
            const auto args = std::vector<std::string>{
              intent_name (intent),
              owner_id,
              std::to_string (generation),
              json,
              row_key,
              lease_key_prefix (),
              owner_key_prefix (kind),
              detail::redis_location_key_schema_t::stamp_key (_options.key_prefix, kind,
                                                              optional_view (mesh_name)),
              mesh_name ? detail::redis_location_key_schema_t::stamp_key (_options.key_prefix,
                                                                          kind, std::nullopt)
                        : std::string{},
              mesh_name ? "1" : "0",
              mesh_name.value_or (std::string{})};
            const auto result = client ()
                                  .eval<std::tuple<std::string, long long, long long>> (
                                    std::string (detail::redis_location_scripts_t::write),
                                    keys.begin (), keys.end (), args.begin (), args.end ())
                                  .get ();
            return detail::redis_location_script_result_t::write_result (
              std::get<0> (result), static_cast<std::int64_t> (std::get<1> (result)),
              static_cast<std::int64_t> (std::get<2> (result)));
          }
          catch (const sw::redis::Error &) {
              return unavailable_write_result ();
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
            const auto keys = std::vector<std::string>{
              detail::redis_location_key_schema_t::row_key (_options.key_prefix, kind, row_key),
              detail::redis_location_key_schema_t::keys_key (_options.key_prefix, kind)};
            const auto args = std::vector<std::string>{
              owner.owner_id,
              std::to_string (owner.generation),
              row_key,
              owner_key_prefix (kind),
              detail::redis_location_key_schema_t::stamp_key (_options.key_prefix, kind,
                                                              optional_view (mesh_name)),
              mesh_name ? detail::redis_location_key_schema_t::stamp_key (_options.key_prefix,
                                                                          kind, std::nullopt)
                        : std::string{}};
            const auto result = client ()
                                  .eval<std::tuple<std::string, long long, long long>> (
                                    std::string (detail::redis_location_scripts_t::remove),
                                    keys.begin (), keys.end (), args.begin (), args.end ())
                                  .get ();
            return detail::redis_location_script_result_t::write_result (
              std::get<0> (result), static_cast<std::int64_t> (std::get<1> (result)),
              static_cast<std::int64_t> (std::get<2> (result)));
          }
          catch (const sw::redis::Error &) {
              return unavailable_write_result ();
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

    task_t<location_write_result_t> renew_lease (std::string owner_id,
                                                 zlink::routing_id_t node_rid,
                                                 std::chrono::milliseconds lease_ttl)
    {
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<location_write_result_t> ([this, owner_id = std::move (owner_id),
                                                         node_rid = std::move (node_rid),
                                                         lease_ttl] {
          try {
            const auto ttl = std::max<std::int64_t> (1, lease_ttl.count ());
            const auto keys = std::vector<std::string>{
              detail::redis_location_key_schema_t::lease_key (_options.key_prefix, owner_id),
              detail::redis_location_key_schema_t::leases_key (_options.key_prefix)};
            const auto args =
              std::vector<std::string>{owner_id, node_rid.to_hex (), std::to_string (ttl)};
            const auto now_ms = client ()
                                  .eval<long long> (
                                    std::string (detail::redis_location_scripts_t::renew_lease),
                                    keys.begin (), keys.end (), args.begin (), args.end ())
                                  .get ();
            return location_write_result_t::stored (
              0, detail::redis_location_script_result_t::from_unix_ms (
                   static_cast<std::int64_t> (now_ms)));
          }
          catch (const sw::redis::Error &) {
              return unavailable_write_result ();
          }
        });
#else
        (void) owner_id;
        (void) node_rid;
        (void) lease_ttl;
        return unavailable_write ();
#endif
    }

    task_t<location_write_result_t> remove_lease (std::string owner_id)
    {
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<location_write_result_t> ([this, owner_id = std::move (owner_id)] {
          try {
            const auto keys = std::vector<std::string>{
              detail::redis_location_key_schema_t::lease_key (_options.key_prefix, owner_id),
              detail::redis_location_key_schema_t::leases_key (_options.key_prefix)};
            const auto args = std::vector<std::string>{owner_id};
            const auto now_ms = client ()
                                  .eval<long long> (
                                    std::string (detail::redis_location_scripts_t::remove_lease),
                                    keys.begin (), keys.end (), args.begin (), args.end ())
                                  .get ();
            return location_write_result_t::stored (
              0, detail::redis_location_script_result_t::from_unix_ms (
                   static_cast<std::int64_t> (now_ms)));
          }
          catch (const sw::redis::Error &) {
              return unavailable_write_result ();
          }
        });
#else
        (void) owner_id;
        return unavailable_write ();
#endif
    }

    task_t<std::int64_t> remove_by_owner (location_kind_t kind, std::string owner_id)
    {
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<std::int64_t> ([this, kind, owner_id = std::move (owner_id)] {
            const auto keys = std::vector<std::string>{
              detail::redis_location_key_schema_t::owner_key (_options.key_prefix, kind, owner_id),
              detail::redis_location_key_schema_t::keys_key (_options.key_prefix, kind)};
            const auto args = std::vector<std::string>{
              row_key_prefix (kind),
              detail::redis_location_key_schema_t::stamp_key (_options.key_prefix, kind,
                                                              std::nullopt)};
            const auto removed = client ()
                                   .eval<long long> (
                                     std::string (detail::redis_location_scripts_t::remove_by_owner),
                                     keys.begin (), keys.end (), args.begin (), args.end ())
                                   .get ();
            return static_cast<std::int64_t> (removed);
        });
#else
        (void) kind;
        (void) owner_id;
        return unavailable_read<std::int64_t> ();
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
            auto row_keys = client ()
                              .smembers<std::vector<std::string>> (
                                detail::redis_location_key_schema_t::keys_key (_options.key_prefix,
                                                                               kind))
                              .get ();
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
            std::vector<std::string> row_keys;
            std::optional<std::string> continuation;
            auto all_keys = client ()
                              .smembers<std::vector<std::string>> (
                                detail::redis_location_key_schema_t::keys_key (_options.key_prefix,
                                                                               kind))
                              .get ();
            const auto offset = parse_offset (page.continuation_token);
            const auto page_size =
              page.page_size > 0 ? static_cast<std::size_t> (page.page_size) : all_keys.size ();
            for (std::size_t i = offset; i < all_keys.size () && row_keys.size () < page_size; ++i) {
                row_keys.push_back (std::move (all_keys[i]));
            }
            const auto next = offset + row_keys.size ();
            if (next < all_keys.size ()) {
                continuation = std::to_string (next);
            }

            location_page_t<TRow> result;
            result.continuation_token = std::move (continuation);
            for (const auto &row_key : row_keys) {
                auto row = load_row<TRow> (kind, row_key, decode);
                if (row && matches (*row, filter)) {
                    result.items.push_back (std::move (*row));
                }
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
        auto fields =
          client ()
            .hmget<std::vector<sw::redis::OptionalString>> (
              detail::redis_location_key_schema_t::row_key (_options.key_prefix, kind, row_key),
              {"json", "gen", "updatedAtMs"})
            .get ();
        if (fields.size () < 3 || !fields[0]) {
            return std::nullopt;
        }
        auto row = decode (*fields[0]);
        if (fields[1]) {
            row.generation = std::stoll (*fields[1]);
        }
        if (fields[2]) {
            row.updated_at =
              detail::redis_location_script_result_t::from_unix_ms (std::stoll (*fields[2]));
        }
        if (!owner_is_live (row.owner_id)) {
            return std::nullopt;
        }
        return row;
    }

    bool owner_is_live (const std::string &owner_id)
    {
        if (owner_id.empty ()) {
            return false;
        }
        const auto remaining_ms =
          client ()
            .pttl (detail::redis_location_key_schema_t::lease_key (_options.key_prefix, owner_id))
            .get ();
        return remaining_ms > 0;
    }

    std::chrono::system_clock::time_point redis_time ()
    {
        const auto parts = client ().command<std::vector<std::string>> ("TIME").get ();
        if (parts.size () < 2) {
            throw sw::redis::Error ("invalid Redis TIME reply");
        }
        const auto seconds = std::stoll (parts[0]);
        const auto microseconds = std::stoll (parts[1]);
        return std::chrono::system_clock::time_point{
          std::chrono::seconds (seconds) + std::chrono::microseconds (microseconds)};
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
    sw::redis::AsyncRedis &client ()
    {
        std::lock_guard lock (_client_gate);
        if (!_client) {
            sw::redis::Uri uri (normalize_connection_string (_options.connection_string));
            _event_loop = std::make_shared<sw::redis::EventLoop> ();
            auto connection_options = uri.connection_options ();
            connection_options.connect_timeout = std::chrono::milliseconds (500);
            connection_options.socket_timeout = std::chrono::milliseconds (500);
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
    static std::size_t parse_offset (const std::optional<std::string> &value)
    {
        if (!value) {
            return 0;
        }
        try {
            return static_cast<std::size_t> (std::stoull (*value));
        }
        catch (...) {
            return 0;
        }
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
        return (!filter.actor_type || row.actor_type == *filter.actor_type)
               && (!filter.node_rid || row.node_rid == *filter.node_rid)
               && (!filter.spot_rid || (row.spot_rid && *row.spot_rid == *filter.spot_rid))
               && (!filter.location_kind || row.location_kind == *filter.location_kind);
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
        return task_t<location_write_result_t> (result_t<location_write_result_t>::success (
          unavailable_write_result ()));
    }

    static location_write_result_t unavailable_write_result ()
    {
        return location_write_result_t{location_write_status_t::store_unavailable, 0, {}};
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
