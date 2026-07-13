/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

/* TicTacToe room route store.
 *
 * 공통 sample spec §6.2: `tictactoe:rooms:{RoomId}` hash에 RouteChannelId, OwnerNodeRid,
 * SpotRid, SpotKind를 기록한다. Redis 접근은 redis-plus-plus를 사용하고 RESP를 샘플에서
 * 직접 구현하지 않는다. 없는 room id는 fallback 생성 없이 route-not-found 오류로 올린다. */

#include "sample_names.hpp"
#include "sample_topology.hpp"

#include <sw/redis++/redis++.h>

#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace zlink::samples::tictactoe
{

struct room_route_t
{
    std::string route_channel_id;
    std::string owner_node_rid;
    std::string spot_rid;
    std::string spot_kind = sample_names_t::match_spot;
};

class redis_room_route_store_t
{
  public:
    explicit redis_room_route_store_t (sample_topology_t topology) :
        _topology (std::move (topology)), _redis (connection_uri (_topology.redis_endpoint))
    {
    }

    void save (const room_route_t &route)
    {
        const std::unordered_map<std::string, std::string> fields{
          {"RouteChannelId", route.route_channel_id},
          {"OwnerNodeRid", route.owner_node_rid},
          {"SpotRid", route.spot_rid},
          {"SpotKind", route.spot_kind}};
        _redis.hset (key (route.spot_rid), fields.begin (), fields.end ());
    }

    room_route_t require (std::string_view spot_rid)
    {
        std::unordered_map<std::string, std::string> stored;
        _redis.hgetall (key (std::string (spot_rid)),
                        std::inserter (stored, stored.begin ()));
        if (stored.empty ()) {
            throw std::runtime_error ("TicTacToe room route was not found in Redis.");
        }
        return {field (stored, "RouteChannelId"), field (stored, "OwnerNodeRid"),
                field (stored, "SpotRid"), field (stored, "SpotKind")};
    }

  private:
    std::string key (const std::string &spot_rid) const
    {
        return _topology.redis_key_prefix + spot_rid;
    }

    static std::string field (const std::unordered_map<std::string, std::string> &stored,
                              const std::string &name)
    {
        const auto found = stored.find (name);
        if (found == stored.end ()) {
            throw std::runtime_error ("TicTacToe room route is missing field " + name + ".");
        }
        return found->second;
    }

    /* 샘플 설정은 tcp://host:port를 쓰고 redis-plus-plus도 tcp:// URI를 받는다. */
    static std::string connection_uri (std::string endpoint)
    {
        if (endpoint.rfind ("redis://", 0) == 0) {
            endpoint = "tcp://" + endpoint.substr (8);
        } else if (endpoint.rfind ("tcp://", 0) != 0) {
            endpoint = "tcp://" + endpoint;
        }
        if (endpoint.substr (6).find (':') == std::string::npos) {
            throw std::runtime_error ("Redis endpoint must use host:port.");
        }
        return endpoint;
    }

    sample_topology_t _topology;
    sw::redis::Redis _redis;
};

} // namespace zlink::samples::tictactoe
