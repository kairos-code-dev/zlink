/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include <zlink.h>

#include <vector>

namespace zlink
{
namespace spot_route_bridge_codec_internal
{

enum packet_kind_t
{
    packet_kind_none,
    packet_kind_send,
    packet_kind_request
};

struct decoded_relay_t
{
    packet_kind_t kind;
    zlink_routing_id_t target_spot_rid;
    zlink_msg_t *payload_parts;
    size_t payload_part_count;
    zlink_msg_t *envelope_parts;
    size_t envelope_part_count;
};

packet_kind_t decode_relay_kind (zlink_msg_t *parts_, size_t part_count_);
bool decode_relay_parts (zlink_msg_t *parts_, size_t part_count_, decoded_relay_t *out_);
bool build_send_relay_parts (const zlink_routing_id_t *target_spot_rid_,
                             zlink_msg_t *parts_,
                             size_t part_count_,
                             std::vector<zlink_msg_t> *out_);
bool build_request_relay_parts (const zlink_routing_id_t *target_spot_rid_,
                                zlink_msg_t *parts_,
                                size_t part_count_,
                                std::vector<zlink_msg_t> *out_);
void close_decoded_relay_envelope (decoded_relay_t *relay_);
void close_decoded_relay_parts (decoded_relay_t *relay_);
void close_relay_parts (std::vector<zlink_msg_t> *parts_);

}
}
