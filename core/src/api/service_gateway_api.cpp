/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/monitor_api_internal.hpp"
#include "api/service_api_internal.hpp"

#include "core/msg.hpp"
#include "services/common/service_public_api.hpp"
#include "services/gateway/gateway.hpp"
#include "services/gateway/gateway_access.hpp"

#include <new>
#include <string.h>
#include <vector>

namespace
{
static void close_gateway_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

static bool frame_has_more (const zlink_msg_t &msg_)
{
    return (reinterpret_cast<const zlink::msg_t *> (&msg_)->flags ()
            & zlink::msg_t::more)
           != 0;
}

static int copy_routing_id_frame (const zlink_msg_t &frame_,
                                  zlink_routing_id_t *source_rid_out_)
{
    if (!source_rid_out_)
        return 0;

    const size_t routing_id_size = zlink_msg_size (&frame_);
    const size_t routing_id_copy =
      routing_id_size > sizeof (source_rid_out_->data)
        ? sizeof (source_rid_out_->data)
        : routing_id_size;
    source_rid_out_->size = static_cast<uint8_t> (routing_id_copy);
    if (routing_id_copy > 0) {
        memcpy (
          source_rid_out_->data,
          zlink_msg_data (&const_cast<zlink_msg_t &> (frame_)),
          routing_id_copy);
    }
    return 0;
}

static int relocate_msg_to_output (zlink_msg_t *src_, zlink_msg_t *dst_)
{
    if (!src_ || !dst_) {
        errno = EFAULT;
        return -1;
    }

    zlink::msg_t *src = reinterpret_cast<zlink::msg_t *> (src_);
    if (!src->check ()) {
        errno = EFAULT;
        return -1;
    }

    *reinterpret_cast<zlink::msg_t *> (dst_) = *src;
    if (src->init () != 0) {
        zlink_msg_close (dst_);
        errno = EFAULT;
        return -1;
    }

    return 0;
}

static int move_single_part_to_output (zlink_msg_t *src_,
                                       zlink_msg_t **parts_out_,
                                       size_t *part_count_out_)
{
    if (!src_ || !parts_out_ || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }

    zlink_msg_t *parts =
      static_cast<zlink_msg_t *> (malloc (sizeof (zlink_msg_t)));
    if (!parts) {
        errno = ENOMEM;
        return -1;
    }
    if (relocate_msg_to_output (src_, parts) != 0) {
        free (parts);
        return -1;
    }

    *parts_out_ = parts;
    *part_count_out_ = 1;
    errno = 0;
    return 0;
}

static int recv_gateway_parts (zlink::socket_base_t *socket_,
                               zlink_routing_id_t *source_rid_out_,
                               zlink_msg_t **parts_out_,
                               size_t *part_count_out_,
                               int flags_)
{
    if (!socket_ || !parts_out_ || !part_count_out_) {
        errno = EINVAL;
        return -1;
    }

    *parts_out_ = NULL;
    *part_count_out_ = 0;
    if (source_rid_out_)
        memset (source_rid_out_, 0, sizeof (*source_rid_out_));

    zlink_msg_t rid_frame;
    zlink_msg_init (&rid_frame);
    if (socket_->recv (reinterpret_cast<zlink::msg_t *> (&rid_frame), flags_)
        < 0) {
        zlink_msg_close (&rid_frame);
        return -1;
    }

    const size_t routing_id_size = zlink_msg_size (&rid_frame);
    if (routing_id_size == 0 || routing_id_size > 255) {
        zlink_msg_close (&rid_frame);
        errno = EPROTO;
        return -1;
    }

    copy_routing_id_frame (rid_frame, source_rid_out_);

    if (!frame_has_more (rid_frame)) {
        zlink_msg_close (&rid_frame);
        return 0;
    }

    zlink_msg_t first_payload;
    zlink_msg_init (&first_payload);
    if (socket_->recv (reinterpret_cast<zlink::msg_t *> (&first_payload), 0)
        < 0) {
        zlink_msg_close (&first_payload);
        zlink_msg_close (&rid_frame);
        return -1;
    }

    if (!frame_has_more (first_payload)) {
        zlink_msg_close (&rid_frame);
        return move_single_part_to_output (
          &first_payload, parts_out_, part_count_out_);
    }

    std::vector<zlink_msg_t> frames;
    frames.push_back (rid_frame);
    frames.push_back (first_payload);
    while (frame_has_more (frames.back ())) {
        zlink_msg_t frame;
        zlink_msg_init (&frame);
        if (socket_->recv (reinterpret_cast<zlink::msg_t *> (&frame), 0) < 0) {
            zlink_msg_close (&frame);
            close_gateway_parts (frames.data (), frames.size ());
            return -1;
        }
        frames.push_back (frame);
    }

    const size_t payload_count = frames.size () - 1;
    if (payload_count == 0) {
        zlink_msg_close (&frames[0]);
        return 0;
    }

    zlink_msg_t *payload = static_cast<zlink_msg_t *> (
      malloc (payload_count * sizeof (zlink_msg_t)));
    if (!payload) {
        close_gateway_parts (frames.data (), frames.size ());
        errno = ENOMEM;
        return -1;
    }
    for (size_t i = 0; i < payload_count; ++i) {
        if (relocate_msg_to_output (&frames[i + 1], &payload[i]) != 0) {
            for (size_t j = 0; j < i; ++j)
                zlink_msg_close (&payload[j]);
            free (payload);
            close_gateway_parts (frames.data (), frames.size ());
            errno = EFAULT;
            return -1;
        }
    }

    zlink_msg_close (&frames[0]);
    *parts_out_ = payload;
    *part_count_out_ = payload_count;
    return 0;
}
}

void *zlink_gateway_new (void *ctx_, const char *service_name_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    if (!service_name_ || service_name_[0] == '\0') {
        errno = EINVAL;
        return NULL;
    }
    zlink::gateway_t *gateway =
      new (std::nothrow) zlink::gateway_t (static_cast<zlink::ctx_t *> (ctx_),
                                           service_name_, NULL);
    if (!gateway) {
        errno = ENOMEM;
        return NULL;
    }
    register_gateway_mode_state (gateway);
    return static_cast<void *> (gateway);
}

int zlink_gateway_attach_discovery (void *gateway_, void *discovery_)
{
    if (!gateway_ || !discovery_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    zlink::discovery_t *disc = static_cast<zlink::discovery_t *> (discovery_);
    if (!disc->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->attach_discovery (disc);
}

int zlink_gateway_bind (void *gateway_, const char *bind_endpoint_)
{
    if (!gateway_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->bind (bind_endpoint_);
}

int zlink_gateway_connect (void *gateway_,
                           const char *endpoint_,
                           const zlink_routing_id_t *routing_id_)
{
    if (!gateway_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->connect (endpoint_, routing_id_);
}

int zlink_gateway_disconnect (void *gateway_, const char *endpoint_)
{
    if (!gateway_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->disconnect (endpoint_);
}

int zlink_gateway_status_snapshot (void *gateway_,
                                   zlink_gateway_status_t *out_)
{
    if (!gateway_) {
        errno = EFAULT;
        return -1;
    }
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    zlink::service_public_api_scope_t admission (gateway->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    return gateway->snapshot_status (out_);
}

int zlink_gateway_set_lb_strategy (void *gateway_,
                                   zlink_gateway_lb_strategy_t strategy_)
{
    if (!gateway_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->set_lb_strategy (strategy_);
}

int zlink_gateway_update_peer_weight (void *gateway_,
                                      const zlink_routing_id_t *routing_id_,
                                      uint32_t weight_)
{
    if (!gateway_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->update_peer_weight (routing_id_, weight_);
}

int zlink_gateway_destroy (void **gateway_p_)
{
    if (!gateway_p_ || !*gateway_p_) {
        errno = EFAULT;
        return -1;
    }
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (*gateway_p_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    if (!gateway->public_api_guard ().begin_close_or_fail_busy ())
        return -1;
    if (has_open_service_monitor_for_subject (gateway)) {
        gateway->public_api_guard ().cancel_close ();
        errno = EBUSY;
        return -1;
    }
    if (gateway->destroy () != 0) {
        gateway->public_api_guard ().cancel_close ();
        return -1;
    }
    erase_gateway_mode_state (gateway);
    delete gateway;
    *gateway_p_ = NULL;
    return 0;
}

int gateway_send_parts (void *gateway_,
                        zlink_msg_t *parts_,
                        size_t part_count_,
                        zlink_send_flags_t flags_)
{
    if (!gateway_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->send (parts_, part_count_, flags_);
}

int gateway_send_parts_rid (void *gateway_,
                            const zlink_routing_id_t *routing_id_,
                            zlink_msg_t *parts_,
                            size_t part_count_,
                            zlink_send_flags_t flags_)
{
    if (!gateway_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->send_rid (routing_id_, parts_, part_count_, flags_);
}

int zlink_service_msg_recv_handler_internal (
  void *handle_,
  zlink_socket_msg_handler_fn handler_,
  void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    if (is_registered_gateway_handle (handle_)) {
        return gateway_install_recv_handler (
          static_cast<zlink::gateway_t *> (handle_), handler_, userdata_);
    }

    errno = EFAULT;
    return -1;
}

int zlink_service_send_internal (void *handle_,
                                 zlink_msg_t *parts_,
                                 size_t part_count_,
                                 zlink_send_flags_t flags_)
{
    if (is_registered_gateway_handle (handle_))
        return gateway_send_parts (handle_, parts_, part_count_, flags_);

    errno = EFAULT;
    return -1;
}

int zlink_service_send_rid_internal (void *handle_,
                                     const zlink_routing_id_t *target_rid_,
                                     zlink_msg_t *parts_,
                                     size_t part_count_,
                                     zlink_send_flags_t flags_)
{
    if (is_registered_gateway_handle (handle_))
        return gateway_send_parts_rid (handle_, target_rid_, parts_,
                                       part_count_, flags_);

    errno = EFAULT;
    return -1;
}

int zlink_service_recv_internal (void *handle_,
                                 zlink_routing_id_t *source_rid_out_,
                                 zlink_msg_t **parts_out_,
                                 size_t *part_count_out_,
                                 zlink_send_flags_t flags_)
{
    if (is_registered_gateway_handle (handle_)) {
        zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (handle_);
        if (!gateway || !gateway->check_tag ()) {
            errno = EFAULT;
            return -1;
        }
        if (validate_recv_flags (flags_) != 0)
            return -1;
        zlink::service_public_api_scope_t admission (
          gateway->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        if (gateway_require_recv_model (gateway) != 0)
            return -1;
        zlink::socket_base_t *router =
          zlink::gateway_access_t::router_socket (gateway);
        if (!router) {
            errno = ENOTSUP;
            return -1;
        }
        return recv_gateway_parts (router, source_rid_out_, parts_out_,
                                   part_count_out_, flags_);
    }

    errno = EFAULT;
    return -1;
}
