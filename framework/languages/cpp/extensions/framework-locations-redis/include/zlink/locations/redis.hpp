/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework/contracts/locations/stores.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <algorithm>
#include <iomanip>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <sstream>
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
    std::chrono::milliseconds operation_timeout{2000};
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
local removed = 0
for i = 1, 4 do
    local ownerKey = KEYS[i]
    local keySet = KEYS[i + 4]
    local rowPrefix = ARGV[i]
    local stampBase = ARGV[i + 4]
    local rowKeys = redis.call('SMEMBERS', ownerKey)
    for _, rowKey in ipairs(rowKeys) do
        local rowHash = rowPrefix .. rowKey
        local mesh = redis.call('HGET', rowHash, 'mesh')
        if redis.call('DEL', rowHash) == 1 then
            removed = removed + 1
            redis.call('SREM', keySet, rowKey)
            if mesh then
                redis.call('INCR', stampBase .. ':' .. mesh)
            end
            redis.call('INCR', stampBase)
        end
    end
    redis.call('DEL', ownerKey)
end
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

local removed = redis.call('DEL', KEYS[1])
redis.call('SREM', KEYS[2], ARGV[1])
return {nowMs, removed}
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
        out[#out + 1] = tostring(pttl)
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
        return encode (canonical_auto_connect_type (key.auto_connect_type), key.mesh_name,
                       canonical_role (key.role), identity);
    }

    static std::string encode_spot_key (const spot_location_key_t &key)
    {
        return encode (key.mesh_name, key.spot_rid.to_hex ());
    }

    static std::string encode_actor_key (const actor_location_key_t &key)
    {
        return encode (key.actor_id);
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
        const auto segments = decode (encoded, 1);
        return actor_location_key_t{segments[0]};
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
        json["ActorId"] = row.actor_id;
        json["ActorType"] = row.actor_type ? nlohmann::json (*row.actor_type)
                                           : nlohmann::json (nullptr);
        if (row.actor_ref) {
            nlohmann::ordered_json actor_ref;
            actor_ref["nodeRid"] =
              zlink::routing_id_t::from (std::string (row.actor_ref->node_rid ().value ()))
                .to_hex ();
            actor_ref["actorId"] = std::string (row.actor_ref->actor_id ());
            actor_ref["generation"] = row.actor_ref->generation ();
            json["ActorRef"] = std::move (actor_ref);
        } else {
            json["ActorRef"] = nullptr;
        }
        json["NodeRid"] = row.node_rid.to_hex ();
        json["LocationKind"] = static_cast<int> (row.location_kind);
        json["SpotMeshName"] = row.spot_mesh_name;
        json["SpotRid"] = row.spot_rid ? nlohmann::json (row.spot_rid->to_hex ())
                                       : nlohmann::json (nullptr);
        json["OwnerId"] = row.owner_id;
        json["Generation"] = row.generation;
        json["UpdatedAt"] = format_updated_at (row.updated_at);
        return json.dump ();
    }

    static actor_location_t decode_actor (std::string_view value)
    {
        const auto json = nlohmann::json::parse (value);
        actor_location_t row;
        if (json.contains ("ActorType") && !json.at ("ActorType").is_null ()) {
            row.actor_type = json.at ("ActorType").get<std::string> ();
        }
        row.actor_id = json.at ("ActorId").get<std::string> ();
        if (json.contains ("ActorRef") && !json.at ("ActorRef").is_null ()) {
            const auto &actor_ref = json.at ("ActorRef");
            row.actor_ref = actor_ref_t{
              node_rid_t::from_string (
                zlink::routing_id_t::from_hex (
                  read_actor_ref_string (actor_ref, "nodeRid", "NodeRid"))
                  .to_string ()),
              json.contains ("ActorType") && !json.at ("ActorType").is_null ()
                ? json.at ("ActorType").get<std::string> ()
                : std::string {},
              read_actor_ref_string (actor_ref, "actorId", "ActorId"),
              read_actor_ref_generation (actor_ref)};
        }
        row.node_rid = zlink::routing_id_t::from_hex (json.at ("NodeRid").get<std::string> ());
        row.generation = json.at ("Generation").get<std::int64_t> ();
        row.location_kind = static_cast<zlink::spot_kind> (json.at ("LocationKind").get<int> ());
        if (json.contains ("SpotMeshName") && !json.at ("SpotMeshName").is_null ()) {
            row.spot_mesh_name = json.at ("SpotMeshName").get<std::string> ();
        }
        if (!json.at ("SpotRid").is_null ()) {
            row.spot_rid =
              zlink::routing_id_t::from_hex (json.at ("SpotRid").get<std::string> ());
        }
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

    static std::string read_actor_ref_string (const nlohmann::json &json,
                                              const char *preferred_name,
                                              const char *legacy_name)
    {
        if (json.contains (preferred_name)) {
            return json.at (preferred_name).get<std::string> ();
        }
        return json.at (legacy_name).get<std::string> ();
    }

    static std::uint64_t read_actor_ref_generation (const nlohmann::json &json)
    {
        if (json.contains ("generation")) {
            return json.at ("generation").get<std::uint64_t> ();
        }
        return json.at ("Generation").get<std::uint64_t> ();
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
          actor_location_key_t{actor.actor_id});
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

    task_t<owner_lease_renewal_t> renew_owner_lease (
      std::string owner_id, zlink::routing_id_t node_rid, std::chrono::milliseconds lease_ttl) override
    {
        return renew_lease (std::move (owner_id), std::move (node_rid), lease_ttl);
    }

    task_t<bool> remove_owner_lease (std::string owner_id) override
    {
        return remove_lease (std::move (owner_id));
    }

    task_t<owner_lease_snapshot_t> list_owner_leases () override
    {
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<owner_lease_snapshot_t> ([this] {
          try {
            const auto keys = std::vector<std::string>{
              detail::redis_location_key_schema_t::leases_key (_options.key_prefix)};
            const auto args = std::vector<std::string>{lease_key_prefix ()};
            const auto result = redis_get (
              client ().eval<std::tuple<long long, std::vector<std::string>>> (
                std::string (detail::redis_location_scripts_t::list_leases), keys.begin (),
                keys.end (), args.begin (), args.end ()));
            owner_lease_snapshot_t snapshot;
            snapshot.store_now = detail::redis_location_script_result_t::from_unix_ms (
              static_cast<std::int64_t> (std::get<0> (result)));
            const auto &values = std::get<1> (result);
            for (std::size_t index = 0; index + 2 < values.size (); index += 3) {
                auto lease = parse_lease_value (values[index], values[index + 1]);
                lease.lease_expires_at = snapshot.store_now + std::chrono::milliseconds (
                                                               std::stoll (values[index + 2]));
                snapshot.leases.push_back (std::move (lease));
            }
            return snapshot;
          }
          catch (const sw::redis::Error &error) {
              throw framework_exception_t (framework_error_kind_t::request_failed, error.what (),
                                           true);
          }
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
              redis_get (client ().get (detail::redis_location_key_schema_t::stamp_key (
                _options.key_prefix, scope.kind, optional_view (scope.mesh_name))));
            return value ? std::stoll (*value) : 0;
        });
#else
        (void) scope;
        return unavailable_read<std::int64_t> ();
#endif
    }

    task_t<std::int64_t> remove_all_by_owner (std::string owner_id) override
    {
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<std::int64_t> ([this, owner_id = std::move (owner_id)] {
            const auto keys = std::vector<std::string>{
              detail::redis_location_key_schema_t::owner_key (_options.key_prefix,
                                                              location_kind_t::peer, owner_id),
              detail::redis_location_key_schema_t::owner_key (_options.key_prefix,
                                                              location_kind_t::spot, owner_id),
              detail::redis_location_key_schema_t::owner_key (_options.key_prefix,
                                                              location_kind_t::actor, owner_id),
              detail::redis_location_key_schema_t::owner_key (_options.key_prefix,
                                                              location_kind_t::route, owner_id),
              detail::redis_location_key_schema_t::keys_key (_options.key_prefix,
                                                             location_kind_t::peer),
              detail::redis_location_key_schema_t::keys_key (_options.key_prefix,
                                                             location_kind_t::spot),
              detail::redis_location_key_schema_t::keys_key (_options.key_prefix,
                                                             location_kind_t::actor),
              detail::redis_location_key_schema_t::keys_key (_options.key_prefix,
                                                             location_kind_t::route)};
            const auto args = std::vector<std::string>{
              row_key_prefix (location_kind_t::peer),
              row_key_prefix (location_kind_t::spot),
              row_key_prefix (location_kind_t::actor),
              row_key_prefix (location_kind_t::route),
              detail::redis_location_key_schema_t::stamp_key (_options.key_prefix,
                                                              location_kind_t::peer, std::nullopt),
              detail::redis_location_key_schema_t::stamp_key (_options.key_prefix,
                                                              location_kind_t::spot, std::nullopt),
              detail::redis_location_key_schema_t::stamp_key (_options.key_prefix,
                                                              location_kind_t::actor, std::nullopt),
              detail::redis_location_key_schema_t::stamp_key (_options.key_prefix,
                                                              location_kind_t::route, std::nullopt)};
            const auto removed = redis_get (
              client ().eval<long long> (
                std::string (detail::redis_location_scripts_t::remove_by_owner), keys.begin (),
                keys.end (), args.begin (), args.end ()));
            return static_cast<std::int64_t> (removed);
        });
#else
        (void) owner_id;
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

    task_t<owner_lease_renewal_t> renew_lease (std::string owner_id,
                                               zlink::routing_id_t node_rid,
                                               std::chrono::milliseconds lease_ttl)
    {
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<owner_lease_renewal_t> ([this, owner_id = std::move (owner_id),
                                                       node_rid = std::move (node_rid),
                                                       lease_ttl] {
          try {
            const auto ttl_ms = std::max<std::int64_t> (1, lease_ttl.count ());
            const auto keys = std::vector<std::string>{
              detail::redis_location_key_schema_t::lease_key (_options.key_prefix, owner_id),
              detail::redis_location_key_schema_t::leases_key (_options.key_prefix)};
            const auto args =
              std::vector<std::string>{owner_id, node_rid.to_hex (), std::to_string (ttl_ms)};
            const auto now_ms = redis_get (
              client ().eval<long long> (
                std::string (detail::redis_location_scripts_t::renew_lease), keys.begin (),
                keys.end (), args.begin (), args.end ()));
            const auto store_now = detail::redis_location_script_result_t::from_unix_ms (
              static_cast<std::int64_t> (now_ms));
            return owner_lease_renewal_t{store_now + std::chrono::milliseconds (ttl_ms),
                                         store_now};
          }
          catch (const sw::redis::Error &error) {
              throw framework_exception_t (framework_error_kind_t::request_failed, error.what (),
                                           true);
          }
        });
#else
        (void) owner_id;
        (void) node_rid;
        (void) lease_ttl;
        return unavailable_read<owner_lease_renewal_t> ();
#endif
    }

    task_t<bool> remove_lease (std::string owner_id)
    {
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
        return _worker.submit<bool> ([this, owner_id = std::move (owner_id)] {
          try {
            const auto keys = std::vector<std::string>{
              detail::redis_location_key_schema_t::lease_key (_options.key_prefix, owner_id),
              detail::redis_location_key_schema_t::leases_key (_options.key_prefix)};
            const auto args = std::vector<std::string>{owner_id};
            const auto result = redis_get (
              client ().eval<std::tuple<long long, long long>> (
                std::string (detail::redis_location_scripts_t::remove_lease), keys.begin (),
                keys.end (), args.begin (), args.end ()));
            return std::get<1> (result) > 0;
          }
          catch (const sw::redis::Error &error) {
              throw framework_exception_t (framework_error_kind_t::request_failed, error.what (),
                                           true);
          }
        });
#else
        (void) owner_id;
        return unavailable_read<bool> ();
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
        if (fields[1]) {
            row.generation = std::stoll (*fields[1]);
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
        return (!filter.actor_type || (row.actor_type && *row.actor_type == *filter.actor_type))
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
