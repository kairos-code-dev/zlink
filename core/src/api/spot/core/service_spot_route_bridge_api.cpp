/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/core/config_result_internal.hpp"
#include "api/message/submit_result_internal.hpp"
#include "api/socket/socket_api_internal.hpp"
#include "api/socket/socket_message_api_internal.hpp"
#include "api/socket/socket_request_reply_internal.hpp"
#include "api/spot/core/service_spot_route_bridge_channel_reply_internal.hpp"
#include "api/spot/core/service_spot_route_bridge_codec_internal.hpp"
#include "api/spot/request_reply/service_spot_request_reply_internal.hpp"
#include "services/spot/node/spot_node.hpp"
#include "services/spot/node/spot_node_access.hpp"
#include "services/spot/runtime/spot_handle.hpp"
#include "services/spot/runtime/spot_runtime.hpp"
#include "runtime/core/internal_defs.hpp"
#include "sockets/common/socket_base.hpp"
#include "utils/routing_id.hpp"

#include <map>
#include <new>
#include <string>
#include <vector>

namespace
{
namespace channel_reply = zlink::spot_route_bridge_channel_reply_internal;
namespace codec = zlink::spot_route_bridge_codec_internal;

const uint32_t spot_route_bridge_tag = 0x5b720001u;

struct bridge_endpoint_t
{
    std::string channel_name;
    zlink::socket_base_t *socket;
    int socket_type;
    uint32_t capabilities;
    int inbound_relay_policy;
    zlink_routing_id_t target_node_rid;
    bool has_target_node;
    zlink_routing_id_t bridge_source_rid;
    std::string bridge_source_key;
    std::shared_ptr<zlink::spot_reqrep_internal::router_spot_request_reply_state_t>
      reply_adapter_state;
};

struct spot_route_bridge_t
{
    uint32_t tag;
    zlink::spot_node_t *node;
    zlink_spot_route_bridge_options_t options;
    std::map<std::string, bridge_endpoint_t> endpoints;
    uint64_t rejected_inbound_count;
    uint64_t malformed_inbound_count;
    uint64_t routed_send_failure_count;
    bool closed;
};

uint32_t request_timeout_ms (const spot_route_bridge_t *bridge_, uint32_t timeout_ms_);

bool valid_channel_name (const char *channel_name_)
{
    return channel_name_ && *channel_name_;
}

bool has_valid_routing_id (const zlink_routing_id_t *rid_)
{
    return rid_ && rid_->size > 0 && rid_->size <= sizeof (rid_->data);
}

int drain_endpoint_reply_progress (spot_route_bridge_t *bridge_, bridge_endpoint_t &endpoint_)
{
    int drained = 0;

    if (endpoint_.socket) {
        endpoint_.socket->socket_msg_dispatch_drain_pending ();
        std::shared_ptr<zlink::socket_reqrep_internal::socket_request_reply_state_t> socket_state =
          zlink::socket_reqrep_internal::find_request_reply_state (
            make_socket_handle (endpoint_.socket));
        if (socket_state) {
            const int socket_drained =
              zlink::socket_reqrep_internal::drain_reply_completions (socket_state,
                                                                      endpoint_.socket);
            if (socket_drained < 0)
                return -1;
            drained += socket_drained;
        }
    }

    std::vector<std::shared_ptr<spot_logical_state_t>> spots;
    if (bridge_->node)
        bridge_->node->snapshot_spot_states (&spots);
    for (std::vector<std::shared_ptr<spot_logical_state_t>>::iterator it = spots.begin ();
         it != spots.end (); ++it) {
        if (!*it || !(*it)->request_reply_state)
            continue;
        void *owner = (*it)->request_reply_state->owner
                        ? (*it)->request_reply_state->owner
                        : static_cast<void *> ((*it).get ());
        const int spot_drained =
          zlink::spot_reqrep_internal::drain_spot_channel_reply_completions_from (
            (*it)->request_reply_state, owner, endpoint_.socket);
        if (spot_drained < 0) {
            if (errno == ENOENT) {
                errno = 0;
                continue;
            }
            return -1;
        }
        drained += spot_drained;
    }

    if (endpoint_.reply_adapter_state) {
        const int router_drained = zlink::spot_reqrep_internal::drain_router_reply_completions (
          endpoint_.reply_adapter_state, endpoint_.socket);
        if (router_drained < 0)
            return -1;
        drained += router_drained;
    }

    errno = 0;
    return drained;
}

bool build_endpoint_source_rid (void *socket_,
                                const char *channel_name_,
                                zlink_routing_id_t *out_)
{
    if (!out_ || !valid_channel_name (channel_name_)) {
        errno = EFAULT;
        return false;
    }
    memset (out_, 0, sizeof (*out_));
    if (socket_ && zlink_get_routing_id (socket_, out_) == 0 && out_->size > 0)
        return true;

    const size_t channel_name_len = strlen (channel_name_);
    const char prefix[] = "bridge:";
    const size_t prefix_len = sizeof (prefix) - 1;
    const size_t max_payload = sizeof (out_->data);
    const size_t copied_channel_len =
      channel_name_len > max_payload - prefix_len ? max_payload - prefix_len : channel_name_len;
    out_->size = static_cast<uint8_t> (prefix_len + copied_channel_len);
    memcpy (out_->data, prefix, prefix_len);
    memcpy (out_->data + prefix_len, channel_name_, copied_channel_len);
    return true;
}

int deliver_relay_to_local_spot (spot_route_bridge_t *bridge_,
                                 const zlink_routing_id_t *source_node_rid_,
                                 codec::decoded_relay_t *relay_,
                                 uint64_t request_seq_)
{
    if (!bridge_ || !relay_) {
        errno = EFAULT;
        return -1;
    }

    zlink_routing_id_t local_node_rid;
    memset (&local_node_rid, 0, sizeof (local_node_rid));
    if (bridge_->node->node_routing_id (&local_node_rid) != 0 || local_node_rid.size == 0)
        return -1;

    std::shared_ptr<zlink::spot_reqrep_internal::spot_request_reply_state_t> target_state =
      zlink::spot_reqrep_internal::find_spot_state_by_identity (
        zlink::routing_id_key (local_node_rid), zlink::routing_id_key (relay_->target_spot_rid));
    if (!target_state) {
        errno = ENOENT;
        return -1;
    }

    zlink_routing_id_t empty_source_node_rid;
    memset (&empty_source_node_rid, 0, sizeof (empty_source_node_rid));
    const zlink_routing_id_t *queued_source_node_rid =
      has_valid_routing_id (source_node_rid_) ? source_node_rid_ : &empty_source_node_rid;
    const int rc = zlink::spot_reqrep_internal::queue_spot_message (
      target_state.get (), queued_source_node_rid, &relay_->target_spot_rid, request_seq_,
      relay_->payload_parts, relay_->payload_part_count);
    if (rc == 0)
        codec::close_decoded_relay_envelope (relay_);
    return rc;
}

int deliver_request_relay_to_local_spot (spot_route_bridge_t *bridge_,
                                         bridge_endpoint_t *endpoint_,
                                         const zlink_routing_id_t *source_node_rid_,
                                         uint64_t channel_request_seq_,
                                         codec::decoded_relay_t *relay_)
{
    if (!bridge_ || !endpoint_ || !relay_) {
        errno = EFAULT;
        return -1;
    }
    if (!has_valid_routing_id (source_node_rid_) || channel_request_seq_ == 0) {
        errno = EPROTO;
        return -1;
    }

    if (relay_->kind != codec::packet_kind_request) {
        errno = EPROTO;
        return -1;
    }

    zlink_routing_id_t local_node_rid;
    memset (&local_node_rid, 0, sizeof (local_node_rid));
    if (bridge_->node->node_routing_id (&local_node_rid) != 0 || local_node_rid.size == 0)
        return -1;

    std::shared_ptr<zlink::spot_reqrep_internal::spot_request_reply_state_t> target_state =
      zlink::spot_reqrep_internal::find_spot_state_by_identity (
        zlink::routing_id_key (local_node_rid), zlink::routing_id_key (relay_->target_spot_rid));
    if (!target_state) {
        errno = ENOENT;
        return -1;
    }
    if (!endpoint_->reply_adapter_state) {
        errno = EFAULT;
        return -1;
    }

    const uint64_t local_request_seq = channel_reply::register_pending_reply (
      endpoint_->reply_adapter_state, endpoint_->socket, endpoint_->socket_type, source_node_rid_,
      channel_request_seq_, request_timeout_ms (bridge_, 0));
    if (local_request_seq == 0)
        return -1;

    const int rc = zlink::spot_reqrep_internal::queue_spot_message (
      target_state.get (), &endpoint_->bridge_source_rid, NULL, local_request_seq,
      relay_->payload_parts, relay_->payload_part_count);
    if (rc != 0) {
        channel_reply::erase_pending_reply (endpoint_->reply_adapter_state, local_request_seq);
        return -1;
    }

    codec::close_decoded_relay_envelope (relay_);
    return 0;
}

uint32_t endpoint_capabilities (const zlink_spot_route_bridge_endpoint_options_t *options_)
{
    if (!options_ || options_->capabilities == 0)
        return ZLINK_SPOT_ROUTE_BRIDGE_ROUTE_ONLY;
    return options_->capabilities;
}

uint32_t request_timeout_ms (const spot_route_bridge_t *bridge_, uint32_t timeout_ms_)
{
    if (timeout_ms_ != 0 || !bridge_)
        return timeout_ms_;
    return bridge_->options.default_request_timeout_ms > 0
             ? static_cast<uint32_t> (bridge_->options.default_request_timeout_ms)
             : 0;
}

spot_route_bridge_t *as_bridge (void *bridge_)
{
    spot_route_bridge_t *bridge = static_cast<spot_route_bridge_t *> (bridge_);
    if (!bridge || bridge->tag != spot_route_bridge_tag) {
        errno = EFAULT;
        return NULL;
    }
    if (bridge->closed) {
        errno = ESHUTDOWN;
        return NULL;
    }
    return bridge;
}

zlink::socket_base_t *as_socket_of_type (void *socket_, int expected_type_)
{
    zlink::socket_base_t *socket = static_cast<zlink::socket_base_t *> (socket_);
    if (!socket || !socket->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    if (socket->socket_type () != expected_type_) {
        errno = EINVAL;
        return NULL;
    }
    return socket;
}

int attach_channel (spot_route_bridge_t *bridge_,
                    const char *channel_name_,
                    void *socket_,
                    int expected_socket_type_,
                    const zlink_spot_route_bridge_endpoint_options_t *options_)
{
    if (!bridge_ || !valid_channel_name (channel_name_)) {
        errno = EINVAL;
        return -1;
    }
    zlink::socket_base_t *socket = as_socket_of_type (socket_, expected_socket_type_);
    if (!socket)
        return -1;

    const std::string channel_name (channel_name_);
    if (bridge_->endpoints.count (channel_name) != 0) {
        errno = EBUSY;
        return -1;
    }

    bridge_endpoint_t endpoint;
    endpoint.channel_name = channel_name;
    endpoint.socket = socket;
    endpoint.socket_type = expected_socket_type_;
    endpoint.capabilities = endpoint_capabilities (options_);
    endpoint.inbound_relay_policy = options_ ? options_->inbound_relay_policy : 0;
    memset (&endpoint.target_node_rid, 0, sizeof (endpoint.target_node_rid));
    endpoint.has_target_node = false;
    if (!build_endpoint_source_rid (socket_, channel_name_, &endpoint.bridge_source_rid))
        return -1;
    endpoint.bridge_source_key = zlink::routing_id_key (endpoint.bridge_source_rid);
    endpoint.reply_adapter_state = zlink::spot_reqrep_internal::find_or_create_router_state (socket_);
    if (!endpoint.reply_adapter_state) {
        errno = EFAULT;
        return -1;
    }
    zlink::spot_reqrep_internal::bind_router_state_rid (socket_, endpoint.bridge_source_key,
                                                       endpoint.reply_adapter_state);
    bridge_->endpoints[channel_name] = endpoint;
    return 0;
}

int require_endpoint (spot_route_bridge_t *bridge_, const char *channel_name_)
{
    if (!bridge_ || !valid_channel_name (channel_name_)) {
        errno = EINVAL;
        return -1;
    }
    if (bridge_->endpoints.count (channel_name_) == 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int handle_received_common (spot_route_bridge_t *bridge_,
                            const char *channel_name_,
                            const zlink_routing_id_t *source_node_rid_,
                            uint8_t dealer_message_type_,
                            uint64_t request_seq_,
                            zlink_msg_t *parts_,
                            size_t part_count_,
                            bool *handled_)
{
    if (!handled_) {
        errno = EINVAL;
        return -1;
    }
    *handled_ = false;
    if (require_endpoint (bridge_, channel_name_) != 0)
        return -1;
    if (!parts_ || part_count_ == 0)
        return 0;
    bridge_endpoint_t &endpoint = bridge_->endpoints[channel_name_];
    const codec::packet_kind_t packet_kind = codec::decode_relay_kind (parts_, part_count_);
    if (packet_kind == codec::packet_kind_none) {
        if ((endpoint.capabilities & ZLINK_SPOT_ROUTE_BRIDGE_CAP_CHANNEL_INBOUND) == 0)
            ++bridge_->rejected_inbound_count;
        return 0;
    }

    *handled_ = true;
    if ((endpoint.capabilities & ZLINK_SPOT_ROUTE_BRIDGE_CAP_SPOT_ROUTE) == 0) {
        zlink_multipart_close (parts_, part_count_);
        ++bridge_->rejected_inbound_count;
        errno = EPERM;
        return -1;
    }
    codec::decoded_relay_t relay;
    if (!codec::decode_relay_parts (parts_, part_count_, &relay)) {
        ++bridge_->malformed_inbound_count;
        zlink_multipart_close (parts_, part_count_);
        return -1;
    }

    if (relay.kind == codec::packet_kind_request) {
        const bool request_metadata_present =
          endpoint.socket_type == ZLINK_CORE_SOCKET_ROUTER
            ? request_seq_ != 0 && has_valid_routing_id (source_node_rid_)
            : dealer_message_type_ == ZLINK_DEALER_MESSAGE_REQUEST && request_seq_ != 0;
        if (!request_metadata_present) {
            zlink_multipart_close (parts_, part_count_);
            ++bridge_->rejected_inbound_count;
            errno = ENOTSUP;
            return -1;
        }

        zlink_routing_id_t dealer_peer_rid;
        memset (&dealer_peer_rid, 0, sizeof (dealer_peer_rid));
        const zlink_routing_id_t *reply_peer_rid = source_node_rid_;
        if (endpoint.socket_type == ZLINK_CORE_SOCKET_DEALER) {
            dealer_peer_rid = endpoint.bridge_source_rid;
            reply_peer_rid = &dealer_peer_rid;
        }

        const int rc = deliver_request_relay_to_local_spot (
          bridge_, &endpoint, reply_peer_rid, request_seq_, &relay);
        if (rc != 0) {
            if (errno == EPROTO)
                ++bridge_->malformed_inbound_count;
            codec::close_decoded_relay_parts (&relay);
        }
        return rc;
    }

    const zlink_routing_id_t *relay_source_node_rid = source_node_rid_;
    zlink_routing_id_t dealer_source_node_rid;
    if (endpoint.socket_type == ZLINK_CORE_SOCKET_DEALER
        && !has_valid_routing_id (relay_source_node_rid)) {
        dealer_source_node_rid = endpoint.bridge_source_rid;
        relay_source_node_rid = &dealer_source_node_rid;
    }
    const int rc = deliver_relay_to_local_spot (bridge_, relay_source_node_rid, &relay, 0);
    if (rc != 0) {
        if (errno == EPROTO)
            ++bridge_->malformed_inbound_count;
        codec::close_decoded_relay_parts (&relay);
    }
    return rc;
}
}

void *zlink_spot_route_bridge_new (void *ctx_,
                                   void *spot_node_,
                                   const zlink_spot_route_bridge_options_t *options_)
{
    (void) ctx_;
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (spot_node_);
    if (!node) {
        errno = EFAULT;
        return NULL;
    }
    if (!zlink::spot_node_access_t::routed_enabled (node)) {
        errno = ENOTSUP;
        return NULL;
    }

    spot_route_bridge_t *bridge = new (std::nothrow) spot_route_bridge_t ();
    if (!bridge) {
        errno = ENOMEM;
        return NULL;
    }
    bridge->tag = spot_route_bridge_tag;
    bridge->node = node;
    memset (&bridge->options, 0, sizeof (bridge->options));
    if (options_)
        bridge->options = *options_;
    bridge->rejected_inbound_count = 0;
    bridge->malformed_inbound_count = 0;
    bridge->routed_send_failure_count = 0;
    bridge->closed = false;
    return bridge;
}

int zlink_spot_route_bridge_attach_dealer_channel (
  void *bridge_,
  const char *channel_name_,
  void *dealer_socket_,
  const zlink_spot_route_bridge_endpoint_options_t *options_)
{
    spot_route_bridge_t *bridge = as_bridge (bridge_);
    if (!bridge)
        return -1;
    return attach_channel (bridge, channel_name_, dealer_socket_, ZLINK_CORE_SOCKET_DEALER,
                           options_);
}

int zlink_spot_route_bridge_attach_router_channel (
  void *bridge_,
  const char *channel_name_,
  void *router_socket_,
  const zlink_spot_route_bridge_endpoint_options_t *options_)
{
    spot_route_bridge_t *bridge = as_bridge (bridge_);
    if (!bridge)
        return -1;
    return attach_channel (bridge, channel_name_, router_socket_, ZLINK_CORE_SOCKET_ROUTER,
                           options_);
}

int zlink_spot_route_bridge_set_target_node (void *bridge_,
                                             const char *channel_name_,
                                             const zlink_routing_id_t *target_node_rid_)
{
    spot_route_bridge_t *bridge = as_bridge (bridge_);
    if (!bridge)
        return -1;
    if (!target_node_rid_ || target_node_rid_->size == 0) {
        errno = EINVAL;
        return -1;
    }
    if (require_endpoint (bridge, channel_name_) != 0)
        return -1;
    bridge_endpoint_t &endpoint = bridge->endpoints[channel_name_];
    endpoint.target_node_rid = *target_node_rid_;
    endpoint.has_target_node = true;
    return 0;
}

int zlink_spot_route_bridge_send (void *bridge_,
                                  const char *channel_name_,
                                  const zlink_routing_id_t *target_spot_rid_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_,
                                  zlink_send_flags_t flags_)
{
    spot_route_bridge_t *bridge = as_bridge (bridge_);
    if (!bridge)
        return -1;
    if (require_endpoint (bridge, channel_name_) != 0)
        return -1;
    bridge_endpoint_t &endpoint = bridge->endpoints[channel_name_];
    if ((endpoint.capabilities & ZLINK_SPOT_ROUTE_BRIDGE_CAP_SPOT_ROUTE) == 0) {
        ++bridge->routed_send_failure_count;
        errno = EPERM;
        return -1;
    }

    std::vector<zlink_msg_t> relay_parts;
    if (!codec::build_send_relay_parts (target_spot_rid_, parts_, part_count_, &relay_parts))
        return -1;

    int rc = -1;
    if (endpoint.socket_type == ZLINK_CORE_SOCKET_DEALER) {
        rc = zlink_socket_send_internal (endpoint.socket, relay_parts.data (), relay_parts.size (),
                                         flags_);
    } else if (endpoint.socket_type == ZLINK_CORE_SOCKET_ROUTER) {
        if (!endpoint.has_target_node) {
            errno = ENOENT;
            rc = -1;
            codec::close_relay_parts (&relay_parts);
        } else {
            rc = zlink_socket_send_rid_internal (endpoint.socket, &endpoint.target_node_rid,
                                                relay_parts.data (), relay_parts.size (), flags_);
        }
    } else {
        errno = EINVAL;
        rc = -1;
        codec::close_relay_parts (&relay_parts);
    }
    if (rc != 0)
        ++bridge->routed_send_failure_count;
    return rc;
}

int zlink_spot_route_bridge_request (void *bridge_,
                                     const char *channel_name_,
                                     const zlink_routing_id_t *target_spot_rid_,
                                     zlink_msg_t *parts_,
                                     size_t part_count_,
                                     zlink_reply_handler_fn callback_,
                                     void *user_data_,
                                     zlink_send_flags_t flags_,
                                     uint32_t timeout_ms_)
{
    spot_route_bridge_t *bridge = as_bridge (bridge_);
    if (!bridge)
        return -1;
    if (require_endpoint (bridge, channel_name_) != 0)
        return -1;
    if (!callback_) {
        errno = EINVAL;
        return -1;
    }
    bridge_endpoint_t &endpoint = bridge->endpoints[channel_name_];
    if ((endpoint.capabilities & ZLINK_SPOT_ROUTE_BRIDGE_CAP_SPOT_ROUTE) == 0) {
        ++bridge->routed_send_failure_count;
        errno = EPERM;
        return -1;
    }

    std::vector<zlink_msg_t> relay_parts;
    if (!codec::build_request_relay_parts (target_spot_rid_, parts_, part_count_, &relay_parts))
        return -1;

    int rc = -1;
    const uint32_t resolved_timeout_ms = request_timeout_ms (bridge, timeout_ms_);
    if (endpoint.socket_type == ZLINK_CORE_SOCKET_DEALER) {
        rc = zlink::socket_reqrep_internal::start_request (
          make_socket_handle (endpoint.socket), NULL, relay_parts.data (), relay_parts.size (),
          flags_, resolved_timeout_ms, callback_, user_data_);
    } else if (endpoint.socket_type == ZLINK_CORE_SOCKET_ROUTER) {
        if (!endpoint.has_target_node) {
            errno = ENOENT;
            rc = -1;
            codec::close_relay_parts (&relay_parts);
        } else {
            rc = zlink::socket_reqrep_internal::start_request (
              make_socket_handle (endpoint.socket), &endpoint.target_node_rid, relay_parts.data (),
              relay_parts.size (), flags_, resolved_timeout_ms, callback_, user_data_);
        }
    } else {
        errno = EINVAL;
        rc = -1;
        codec::close_relay_parts (&relay_parts);
    }
    if (rc != 0)
        ++bridge->routed_send_failure_count;
    return rc;
}

int zlink_spot_route_bridge_handle_router_received (void *bridge_,
                                                    const char *channel_name_,
                                                    const zlink_routing_id_t *source_node_rid_,
                                                    zlink_msg_t *parts_,
                                                    size_t part_count_,
                                                    bool *handled_)
{
    spot_route_bridge_t *bridge = as_bridge (bridge_);
    if (!bridge)
        return -1;
    return handle_received_common (bridge, channel_name_, source_node_rid_,
                                   ZLINK_DEALER_MESSAGE_RAW, 0, parts_, part_count_, handled_);
}

int zlink_spot_route_bridge_handle_router_received_with_metadata (
  void *bridge_,
  const char *channel_name_,
  const zlink_routing_id_t *source_node_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_,
  bool *handled_)
{
    spot_route_bridge_t *bridge = as_bridge (bridge_);
    if (!bridge)
        return -1;
    return handle_received_common (bridge, channel_name_, source_node_rid_,
                                   ZLINK_DEALER_MESSAGE_RAW, request_seq_, parts_, part_count_,
                                   handled_);
}

int zlink_spot_route_bridge_handle_dealer_received (void *bridge_,
                                                    const char *channel_name_,
                                                    zlink_msg_t *parts_,
                                                    size_t part_count_,
                                                    bool *handled_)
{
    spot_route_bridge_t *bridge = as_bridge (bridge_);
    if (!bridge)
        return -1;
    return handle_received_common (bridge, channel_name_, NULL, ZLINK_DEALER_MESSAGE_RAW, 0,
                                   parts_, part_count_, handled_);
}

int zlink_spot_route_bridge_handle_dealer_received_with_metadata (
  void *bridge_,
  const char *channel_name_,
  uint8_t message_type_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_,
  bool *handled_)
{
    spot_route_bridge_t *bridge = as_bridge (bridge_);
    if (!bridge)
        return -1;
    return handle_received_common (bridge, channel_name_, NULL, message_type_, request_seq_, parts_,
                                   part_count_, handled_);
}

int zlink_spot_route_bridge_drain (void *bridge_)
{
    spot_route_bridge_t *bridge = as_bridge (bridge_);
    if (!bridge)
        return -1;

    int rc = 0;
    for (std::map<std::string, bridge_endpoint_t>::iterator it = bridge->endpoints.begin ();
         it != bridge->endpoints.end (); ++it) {
        bridge_endpoint_t &endpoint = it->second;
        const int drained = drain_endpoint_reply_progress (bridge, endpoint);
        if (drained < 0) {
            rc = -1;
        } else {
            rc += drained;
        }
    }
    return rc;
}

int zlink_spot_route_bridge_summary (void *bridge_, zlink_spot_route_bridge_summary_t *out_)
{
    spot_route_bridge_t *bridge = as_bridge (bridge_);
    if (!bridge)
        return -1;
    if (!out_) {
        errno = EFAULT;
        return -1;
    }

    const uint32_t requested_size = out_->struct_size;
    memset (out_, 0, sizeof (*out_));
    out_->struct_size = requested_size != 0 ? requested_size : sizeof (*out_);
    out_->attached_channel_count = static_cast<uint32_t> (bridge->endpoints.size ());
    out_->rejected_inbound_count = bridge->rejected_inbound_count;
    out_->malformed_inbound_count = bridge->malformed_inbound_count;
    out_->routed_send_failure_count = bridge->routed_send_failure_count;

    uint64_t pending_count = 0;
    for (std::map<std::string, bridge_endpoint_t>::iterator it = bridge->endpoints.begin ();
         it != bridge->endpoints.end (); ++it) {
        bridge_endpoint_t &endpoint = it->second;
        pending_count += channel_reply::pending_reply_count (endpoint.reply_adapter_state);

        std::shared_ptr<zlink::socket_reqrep_internal::socket_request_reply_state_t> socket_state =
          zlink::socket_reqrep_internal::find_request_reply_state (
            make_socket_handle (endpoint.socket));
        if (socket_state) {
            std::lock_guard<std::mutex> lock (socket_state->mutex);
            pending_count += socket_state->pending_requests.size ();
        }
    }
    out_->pending_request_count = pending_count;
    return 0;
}

int zlink_spot_route_bridge_close (void *bridge_)
{
    spot_route_bridge_t *bridge = static_cast<spot_route_bridge_t *> (bridge_);
    if (!bridge || bridge->tag != spot_route_bridge_tag) {
        errno = EFAULT;
        return -1;
    }
    bridge->closed = true;
    bridge->tag = 0;
    delete bridge;
    return 0;
}
