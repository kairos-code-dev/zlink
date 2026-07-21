/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework/contracts/locations/keys.hpp>

#include "runtime/locations/location_value_codec.hpp"

#include <string>

namespace zlink::framework::runtime
{

class location_key_codec_t
{
  public:
    static std::string encode_peer_key (const peer_location_key_t &key)
    {
        const auto identity = key.node_rid.has_value () ? key.node_rid->to_hex ()
                                                        : key.endpoint.value_or (std::string{});
        return encode (location_value_codec_t::to_canonical_string (key.auto_connect_type),
                       key.mesh_name, location_value_codec_t::to_canonical_string (key.role),
                       identity);
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

    static std::optional<std::string> normalize_actor_type (
      std::optional<std::string> actor_type)
    {
        return actor_type;
    }

  private:
    template <typename... TSegments> static std::string encode (const TSegments &...segments)
    {
        std::string result;
        (append_segment (result, segments), ...);
        return result;
    }

    static void append_segment (std::string &result, const std::string &segment)
    {
        result += std::to_string (segment.size ());
        result += ':';
        result += segment;
    }
};

} // namespace zlink::framework::runtime
