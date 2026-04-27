/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/request_reply_protocol_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"
#include "core/multipart_send_txn.hpp"
#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_runtime.hpp"

namespace
{
using zlink::spot_reqrep_internal::find_router_state_by_rid;
using zlink::spot_reqrep_internal::parsed_spot_envelope_t;
using zlink::spot_reqrep_internal::resolve_spot_node_routing_id;

enum : uint8_t
{
    zmp_spot_routed_protocol_id = 0x02,
    zmp_protocol_version = 0x01,
    zmp_packed_protocol_version = 0x02,
    zmp_spot_class = 0x01,
    zmp_router_class = 0x02
};

const size_t spot_routed_control_part_count = 8;

bool spot_route_stats_enabled ()
{
    return std::getenv ("ZLINK_DEBUG_SPOT_ROUTE_STATS") != NULL;
}

struct spot_route_stats_t
{
    std::atomic<unsigned long long> publish_count;
    std::atomic<unsigned long long> publish_ns;

    spot_route_stats_t () : publish_count (0), publish_ns (0) {}
};

spot_route_stats_t g_spot_route_stats;

bool assign_routing_id_value_local (const char *data_,
                                    size_t size_,
                                    zlink_routing_id_t *out_rid_)
{
    if (!out_rid_)
        return false;
    memset (out_rid_, 0, sizeof (*out_rid_));
    if (!data_ || size_ == 0)
        return true;
    if (size_ > sizeof (out_rid_->data))
        return false;
    memcpy (out_rid_->data, data_, size_);
    out_rid_->size = static_cast<uint8_t> (size_);
    return true;
}

bool parse_packed_spot_routed_envelope (zlink_msg_t *parts_,
                                        size_t part_count_,
                                        parsed_spot_envelope_t *out_)
{
    const size_t packed_header_prefix_size = 20;
    zlink::msg_t *first_frame = reinterpret_cast<zlink::msg_t *> (&parts_[0]);
    if (!first_frame->check ())
        return false;

    const unsigned char *data =
      static_cast<const unsigned char *> (zlink_msg_data (&parts_[0]));
    const size_t size = zlink_msg_size (&parts_[0]);
    if (size < packed_header_prefix_size
        || data[0] != zmp_spot_routed_protocol_id
        || data[1] != zmp_packed_protocol_version
        || (data[2] != zmp_spot_class && data[2] != zmp_router_class)
        || (data[3] != zmp_spot_class && data[3] != zmp_router_class)) {
        return false;
    }

    const uint32_t source_node_size =
      zlink::request_reply::decode_u32_be (data + 4);
    const uint32_t source_endpoint_size =
      zlink::request_reply::decode_u32_be (data + 8);
    const uint32_t destination_node_size =
      zlink::request_reply::decode_u32_be (data + 12);
    const uint32_t destination_endpoint_size =
      zlink::request_reply::decode_u32_be (data + 16);
    const size_t total_header_size =
      packed_header_prefix_size + static_cast<size_t> (source_node_size)
      + static_cast<size_t> (source_endpoint_size)
      + static_cast<size_t> (destination_node_size)
      + static_cast<size_t> (destination_endpoint_size);
    if (size < total_header_size)
        return false;

    const char *cursor =
      reinterpret_cast<const char *> (data) + packed_header_prefix_size;
    out_->source_class = data[2];
    out_->destination_class = data[3];
    out_->source_node_rid.assign (cursor, source_node_size);
    if (!assign_routing_id_value_local (
          cursor, source_node_size, &out_->source_node_rid_value)) {
        return false;
    }
    cursor += source_node_size;
    out_->source_endpoint_rid.assign (cursor, source_endpoint_size);
    if (!assign_routing_id_value_local (
          cursor, source_endpoint_size, &out_->source_endpoint_rid_value)) {
        return false;
    }
    cursor += source_endpoint_size;
    out_->destination_node_rid.assign (cursor, destination_node_size);
    if (!assign_routing_id_value_local (
          cursor, destination_node_size, &out_->destination_node_rid_value)) {
        return false;
    }
    cursor += destination_node_size;
    out_->destination_endpoint_rid.assign (cursor, destination_endpoint_size);
    if (!assign_routing_id_value_local (
          cursor,
          destination_endpoint_size,
          &out_->destination_endpoint_rid_value)) {
        return false;
    }
    out_->payload_parts = parts_ + 1;
    out_->payload_part_count = part_count_ - 1;
    return true;
}

bool parse_legacy_spot_routed_envelope (zlink_msg_t *parts_,
                                        size_t part_count_,
                                        parsed_spot_envelope_t *out_)
{
    if (part_count_ < spot_routed_control_part_count)
        return false;

    zlink::msg_t *protocol_id = reinterpret_cast<zlink::msg_t *> (&parts_[0]);
    if (!protocol_id->check ()
        || !zlink::request_reply::frame_is_single_byte_value (
          &parts_[0], zmp_spot_routed_protocol_id)
        || !zlink::request_reply::frame_is_single_byte_value (
          &parts_[1], zmp_protocol_version)) {
        return false;
    }
    if (!zlink::request_reply::frame_is_single_byte_value (&parts_[2],
                                                           zmp_spot_class)
        && !zlink::request_reply::frame_is_single_byte_value (&parts_[2],
                                                              zmp_router_class))
        return false;
    if (!zlink::request_reply::frame_is_single_byte_value (&parts_[5],
                                                           zmp_spot_class)
        && !zlink::request_reply::frame_is_single_byte_value (&parts_[5],
                                                              zmp_router_class))
        return false;

    out_->source_class =
      static_cast<const unsigned char *> (zlink_msg_data (&parts_[2]))[0];
    out_->source_node_rid.assign (
      static_cast<const char *> (zlink_msg_data (&parts_[3])),
      zlink_msg_size (&parts_[3]));
    if (!assign_routing_id_value_local (
          static_cast<const char *> (zlink_msg_data (&parts_[3])),
          zlink_msg_size (&parts_[3]),
          &out_->source_node_rid_value)) {
        return false;
    }
    out_->source_endpoint_rid.assign (
      static_cast<const char *> (zlink_msg_data (&parts_[4])),
      zlink_msg_size (&parts_[4]));
    if (!assign_routing_id_value_local (
          static_cast<const char *> (zlink_msg_data (&parts_[4])),
          zlink_msg_size (&parts_[4]),
          &out_->source_endpoint_rid_value)) {
        return false;
    }
    out_->destination_class =
      static_cast<const unsigned char *> (zlink_msg_data (&parts_[5]))[0];
    out_->destination_node_rid.assign (
      static_cast<const char *> (zlink_msg_data (&parts_[6])),
      zlink_msg_size (&parts_[6]));
    if (!assign_routing_id_value_local (
          static_cast<const char *> (zlink_msg_data (&parts_[6])),
          zlink_msg_size (&parts_[6]),
          &out_->destination_node_rid_value)) {
        return false;
    }
    out_->destination_endpoint_rid.assign (
      static_cast<const char *> (zlink_msg_data (&parts_[7])),
      zlink_msg_size (&parts_[7]));
    if (!assign_routing_id_value_local (
          static_cast<const char *> (zlink_msg_data (&parts_[7])),
          zlink_msg_size (&parts_[7]),
          &out_->destination_endpoint_rid_value)) {
        return false;
    }
    out_->payload_parts = parts_ + spot_routed_control_part_count;
    out_->payload_part_count = part_count_ - spot_routed_control_part_count;
    return true;
}
}

bool zlink::spot_reqrep_internal::parse_spot_routed_envelope (
  zlink_msg_t *parts_,
  size_t part_count_,
  parsed_spot_envelope_t *out_)
{
    if (!parts_ || !out_ || part_count_ == 0)
        return false;

    memset (&out_->source_node_rid_value, 0, sizeof (out_->source_node_rid_value));
    memset (&out_->source_endpoint_rid_value,
            0,
            sizeof (out_->source_endpoint_rid_value));
    memset (&out_->destination_node_rid_value,
            0,
            sizeof (out_->destination_node_rid_value));
    memset (&out_->destination_endpoint_rid_value,
            0,
            sizeof (out_->destination_endpoint_rid_value));

    return parse_packed_spot_routed_envelope (parts_, part_count_, out_)
           || parse_legacy_spot_routed_envelope (parts_, part_count_, out_);
}

bool zlink::spot_reqrep_internal::resolve_spot_node_routing_id (
  spot_node_t *node_,
  std::string *out_)
{
    if (!node_ || !out_) {
        errno = EFAULT;
        return false;
    }

    zlink_routing_id_t node_rid;
    memset (&node_rid, 0, sizeof (node_rid));
    if (node_->node_routing_id (&node_rid) != 0 || node_rid.size == 0)
        return false;

    out_->assign (reinterpret_cast<const char *> (node_rid.data), node_rid.size);
    return true;
}

bool zlink::spot_reqrep_internal::should_process_spot_routed_locally (
  spot_node_t *node_,
  const parsed_spot_envelope_t &envelope_)
{
    if (!node_)
        return false;

    if (envelope_.destination_class == zmp_router_class) {
        return static_cast<bool> (
          find_router_state_by_rid (envelope_.destination_endpoint_rid));
    }

    std::string local_node_rid;
    return resolve_spot_node_routing_id (node_, &local_node_rid)
           && local_node_rid == envelope_.destination_node_rid;
}

int zlink::spot_reqrep_internal::publish_spot_routed_to_mesh (
  spot_node_t *node_,
  std::vector<zlink_msg_t> *combined_)
{
    if (!node_ || !combined_ || combined_->empty ()) {
        errno = EFAULT;
        return -1;
    }

    spot_runtime_t *runtime = spot_node_access_t::runtime (node_);
    if (!runtime || !runtime->mesh_pub) {
        errno = EFAULT;
        return -1;
    }

    if (connected_ready_peer_count (&runtime->execution.mesh_peer_state) == 0) {
        errno = ENOTCONN;
        return -1;
    }

    const uint64_t start_ns =
      spot_route_stats_enabled ()
        ? static_cast<uint64_t> (
            std::chrono::duration_cast<std::chrono::nanoseconds> (
              std::chrono::steady_clock::now ().time_since_epoch ())
              .count ())
        : 0;
    parsed_spot_envelope_t envelope;
    if (!parse_spot_routed_envelope (&(*combined_)[0], combined_->size (),
                                     &envelope)) {
        errno = EPROTO;
        return -1;
    }
    const std::string topic =
      spot_routed_mesh_topic_for_node (envelope.destination_node_rid);

    const int rc = logical_multipart_publish (
      runtime->mesh_pub, topic.c_str (), &(*combined_)[0], combined_->size (),
      0, true);
    if (spot_route_stats_enabled ()) {
        const uint64_t end_ns = static_cast<uint64_t> (
          std::chrono::duration_cast<std::chrono::nanoseconds> (
            std::chrono::steady_clock::now ().time_since_epoch ())
            .count ());
        g_spot_route_stats.publish_count.fetch_add (1, std::memory_order_relaxed);
        g_spot_route_stats.publish_ns.fetch_add (end_ns - start_ns,
                                                 std::memory_order_relaxed);
    }
    return rc;
}
