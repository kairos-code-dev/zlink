/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/socket/request_reply_protocol_internal.hpp"
#include "api/spot/request_reply/service_spot_routed_protocol_internal.hpp"
#include "api/spot/request_reply/service_spot_request_reply_internal.hpp"
#include "runtime/services/spot/request_reply/spot_request_reply_local_dispatch_internal.hpp"

namespace
{
namespace routed_protocol = zlink::spot_routed_protocol;

using zlink::spot_reqrep_internal::parsed_spot_envelope_t;

bool parse_combined_local_message (
  std::vector<zlink_msg_t> *combined_,
  parsed_spot_envelope_t *spot_envelope_out_,
  zlink::request_reply::parsed_envelope_t *request_reply_envelope_out_)
{
    if (!combined_ || !spot_envelope_out_ || !request_reply_envelope_out_) {
        errno = EFAULT;
        return false;
    }

    if (!zlink::spot_reqrep_internal::parse_spot_routed_envelope (
          &(*combined_)[0], combined_->size (), spot_envelope_out_)) {
        errno = EPROTO;
        return false;
    }

    if (!zlink::request_reply::parse_envelope (spot_envelope_out_->payload_parts,
                                               spot_envelope_out_->payload_part_count,
                                               request_reply_envelope_out_)) {
        errno = EPROTO;
        return false;
    }

    return true;
}

}

int zlink::spot_reqrep_internal::dispatch_local_reply_impl (std::vector<zlink_msg_t> *combined_)
{
    if (!combined_) {
        errno = EFAULT;
        return -1;
    }

    parsed_spot_envelope_t spot_envelope;
    zlink::request_reply::parsed_envelope_t rr_envelope;
    if (!parse_combined_local_message (combined_, &spot_envelope, &rr_envelope))
        return -1;

    if (spot_envelope.destination_class == routed_protocol::spot_endpoint_class)
        return deliver_reply_to_spot (spot_envelope, rr_envelope);

    return deliver_reply_to_router (spot_envelope.destination_endpoint_rid, rr_envelope);
}

int zlink::spot_reqrep_internal::dispatch_local_request_impl (const std::string &router_rid_,
                                                              std::vector<zlink_msg_t> *combined_)
{
    parsed_spot_envelope_t spot_envelope;
    zlink::request_reply::parsed_envelope_t rr_envelope;
    if (!parse_combined_local_message (combined_, &spot_envelope, &rr_envelope))
        return -1;

    if (spot_envelope.destination_class == routed_protocol::spot_endpoint_class)
        return dispatch_spot_request_to_spot (spot_envelope, rr_envelope);

    return dispatch_spot_request_to_router (router_rid_, spot_envelope, rr_envelope);
}

int zlink::spot_reqrep_internal::dispatch_local_built_message (
  uint8_t source_class_,
  const std::string &source_node_rid_,
  const std::string &source_endpoint_rid_,
  uint8_t destination_class_,
  const std::string &destination_node_rid_,
  const std::string &destination_endpoint_rid_,
  uint8_t message_type_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (source_class_, source_node_rid_, source_endpoint_rid_,
                                          destination_class_, destination_node_rid_,
                                          destination_endpoint_rid_, message_type_, request_seq_,
                                          parts_, part_count_, &combined)
        != 0)
        return -1;

    const int rc = dispatch_local_reply (&combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return rc;
}

int zlink::spot_reqrep_internal::process_parsed_route_combined_for_local_delivery_impl (
  std::vector<zlink_msg_t> *combined_, const parsed_spot_envelope_t &spot_envelope_)
{
    if (!combined_) {
        errno = EFAULT;
        return -1;
    }

    zlink::request_reply::parsed_envelope_t rr_envelope;
    if (zlink::request_reply::parse_envelope (spot_envelope_.payload_parts,
                                              spot_envelope_.payload_part_count, &rr_envelope)) {
        if (rr_envelope.message_type == zlink::request_reply::request_type) {
            if (spot_envelope_.destination_class == routed_protocol::spot_endpoint_class)
                return dispatch_spot_request_to_spot (spot_envelope_, rr_envelope);
            return dispatch_spot_request_to_router (spot_envelope_.destination_endpoint_rid,
                                                    spot_envelope_, rr_envelope);
        }

        if (spot_envelope_.destination_class == routed_protocol::spot_endpoint_class)
            return deliver_reply_to_spot (spot_envelope_, rr_envelope);
        return deliver_reply_to_router (spot_envelope_.destination_endpoint_rid, rr_envelope);
    }

    if (spot_envelope_.destination_class == routed_protocol::spot_endpoint_class) {
        return dispatch_local_direct_to_spot (
          spot_envelope_.source_class, spot_envelope_.source_node_rid,
          spot_envelope_.source_endpoint_rid, &spot_envelope_.source_node_rid_value,
          &spot_envelope_.source_endpoint_rid_value, spot_envelope_.destination_node_rid,
          spot_envelope_.destination_endpoint_rid, spot_envelope_.payload_parts,
          spot_envelope_.payload_part_count);
    }

    return dispatch_local_direct_to_router (
      spot_envelope_.destination_endpoint_rid, spot_envelope_.source_node_rid,
      spot_envelope_.source_endpoint_rid, &spot_envelope_.source_node_rid_value,
      &spot_envelope_.source_endpoint_rid_value, spot_envelope_.payload_parts,
      spot_envelope_.payload_part_count);
}

int zlink::spot_reqrep_internal::process_route_combined_for_local_delivery_impl (
  std::vector<zlink_msg_t> *combined_)
{
    if (!combined_) {
        errno = EFAULT;
        return -1;
    }

    parsed_spot_envelope_t spot_envelope;
    if (!zlink::spot_reqrep_internal::parse_spot_routed_envelope (
          &(*combined_)[0], combined_->size (), &spot_envelope)) {
        errno = EPROTO;
        return -1;
    }

    return process_parsed_route_combined_for_local_delivery (combined_, spot_envelope);
}
