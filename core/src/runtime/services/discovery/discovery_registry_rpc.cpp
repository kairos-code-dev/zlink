/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/discovery/discovery_registry_rpc.hpp"

#include "core/ctx.hpp"
#include "core/recv_internal.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/discovery/discovery_runtime_internal.hpp"
#include "services/discovery/routing_id_utils.hpp"
#include "sockets/common/socket_base.hpp"
#include "utils/clock.hpp"

#include <cstring>

namespace zlink
{
namespace discovery_registry_rpc
{
namespace
{
bool wait_socket_event (void *socket_, short events_, long timeout_ms_)
{
    return zlink::wait_socket_events_internal (socket_, events_, timeout_ms_) > 0;
}

int apply_dealer_timeouts (socket_base_t *dealer_,
                           int linger_ms_,
                           int sndtimeo_ms_,
                           int rcvtimeo_ms_)
{
    if (!dealer_) {
        errno = EINVAL;
        return -1;
    }
    dealer_->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger_ms_, sizeof (linger_ms_));
    dealer_->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &sndtimeo_ms_, sizeof (sndtimeo_ms_));
    dealer_->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO, &rcvtimeo_ms_, sizeof (rcvtimeo_ms_));
    return 0;
}

int wait_dealer_connected (socket_base_t *dealer_, long timeout_ms_)
{
    const uint64_t deadline_ms = clock_t ().now_ms () + timeout_ms_;
    while (clock_t ().now_ms () < deadline_ms) {
        if (wait_socket_event (static_cast<void *> (dealer_), ZLINK_POLLOUT, 50))
            break;
    }
    if (!wait_socket_event (static_cast<void *> (dealer_), ZLINK_POLLOUT, 0)) {
        errno = EAGAIN;
        return -1;
    }
    return 0;
}
}

int close_dealer (ctx_t *ctx_, socket_base_t *&dealer_)
{
    if (!dealer_)
        return 0;
    if (!ctx_) {
        dealer_->stop ();
        dealer_->close ();
        dealer_ = NULL;
        return 0;
    }
    return ctx_->close_socket_and_wait (dealer_, 1000);
}

int prepare_transient_dealer (ctx_t *ctx_,
                              discovery_bootstrap_runtime_t *bootstrap_runtime_,
                              const std::string &uplink_,
                              const zlink_routing_id_t *routing_id_,
                              socket_base_t **dealer_out_)
{
    if (!ctx_ || !bootstrap_runtime_ || !dealer_out_) {
        errno = EINVAL;
        return -1;
    }

    *dealer_out_ = NULL;
    socket_base_t *dealer = ctx_->create_socket (ZLINK_CORE_SOCKET_DEALER);
    if (!dealer)
        return -1;

    if (routing_id_ && routing_id_->size > 0) {
        if (dealer->setsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID, routing_id_->data, routing_id_->size)
            != 0) {
            (void) close_dealer (ctx_, dealer);
            return -1;
        }
    } else if (!zlink::discovery::set_socket_routing_id (dealer, NULL, NULL)) {
        (void) close_dealer (ctx_, dealer);
        return -1;
    }

    bootstrap_runtime_->apply_socket_options (dealer);
    (void) apply_dealer_timeouts (dealer, 200, 500, 500);
    if (dealer->connect (uplink_.c_str ()) != 0 || wait_dealer_connected (dealer, 500) != 0) {
        (void) close_dealer (ctx_, dealer);
        return -1;
    }

    *dealer_out_ = dealer;
    return 0;
}

int prepare_query_dealer (ctx_t *ctx_, const char *endpoint_, socket_base_t **dealer_out_)
{
    if (!ctx_ || !endpoint_ || !*endpoint_ || !dealer_out_) {
        errno = EINVAL;
        return -1;
    }

    *dealer_out_ = NULL;
    socket_base_t *dealer = ctx_->create_socket (ZLINK_CORE_SOCKET_DEALER);
    if (!dealer)
        return -1;
    if (!zlink::discovery::set_socket_routing_id (dealer, NULL, NULL)) {
        dealer->close ();
        return -1;
    }
    (void) apply_dealer_timeouts (dealer, 0, 1000, 1000);
    if (dealer->connect (endpoint_) != 0 || wait_dealer_connected (dealer, 1000) != 0) {
        dealer->close ();
        return -1;
    }
    *dealer_out_ = dealer;
    return 0;
}

int recv_status_ack (socket_base_t *socket_,
                     uint16_t expected_msg_id_,
                     int *status_out_,
                     std::string *resolved_out_,
                     uint32_t *source_registry_out_,
                     uint64_t *registration_id_out_,
                     std::string *error_out_)
{
    if (!socket_ || !status_out_) {
        errno = EINVAL;
        return -1;
    }

    *status_out_ = -1;
    if (resolved_out_)
        resolved_out_->clear ();
    if (source_registry_out_)
        *source_registry_out_ = 0;
    if (registration_id_out_)
        *registration_id_out_ = 0;
    if (error_out_)
        error_out_->clear ();

    const uint64_t deadline_ms = zlink::clock_t ().now_ms () + 2000;
    while (zlink::clock_t ().now_ms () < deadline_ms) {
        if (!wait_socket_event (static_cast<void *> (socket_), ZLINK_POLLIN, 50))
            continue;

        scoped_msg_frames_t frames;
        if (!recv_msg_sequence_socket_wait (socket_, &frames, 500)) {
            if (errno == EAGAIN)
                continue;
            return -1;
        }

        discovery_protocol::status_ack_t ack;
        if (discovery_protocol::decode_status_ack (frames, expected_msg_id_, &ack)) {
            *status_out_ = static_cast<int> (ack.status);
            if (resolved_out_)
                *resolved_out_ = ack.resolved_endpoint;
            if (source_registry_out_)
                *source_registry_out_ = ack.source_registry;
            if (registration_id_out_)
                *registration_id_out_ = ack.registration_id;
            if (error_out_)
                *error_out_ = ack.error;
            return 0;
        }

        const int saved_errno = errno;
        if (saved_errno != EPROTO) {
            errno = saved_errno;
            return -1;
        }
    }

    errno = EAGAIN;
    return -1;
}

int recv_topology_reply_entries (socket_base_t *socket_,
                                 std::vector<zlink_registry_topology_entry_t> *entries_out_)
{
    if (!socket_ || !entries_out_) {
        errno = EINVAL;
        return -1;
    }

    entries_out_->clear ();
    scoped_msg_frames_t frames;
    if (!recv_msg_sequence_socket_wait (socket_, &frames, 500))
        return -1;

    const bool ok = discovery_protocol::decode_topology_reply (frames, entries_out_);
    return ok ? 0 : -1;
}

int recv_member_peers_reply_entries (socket_base_t *socket_,
                                     std::vector<zlink_member_peer_entry_t> *entries_out_)
{
    if (!socket_ || !entries_out_) {
        errno = EINVAL;
        return -1;
    }

    entries_out_->clear ();
    scoped_msg_frames_t frames;
    if (!recv_msg_sequence_socket_wait (socket_, &frames, 500))
        return -1;

    const bool ok = discovery_protocol::decode_member_peers_reply (frames, entries_out_);
    return ok ? 0 : -1;
}

int recv_registry_status_reply (socket_base_t *socket_, zlink_registry_status_t *status_out_)
{
    if (!socket_ || !status_out_) {
        errno = EINVAL;
        return -1;
    }

    scoped_msg_frames_t frames;
    if (!recv_msg_sequence_socket_wait (socket_, &frames, 500))
        return -1;

    const bool ok = discovery_protocol::decode_registry_status_reply (frames, status_out_);
    return ok ? 0 : -1;
}

int recv_route_reply (socket_base_t *socket_,
                      zlink_routing_id_t *owner_rid_out_,
                      zlink_msg_t *value_out_)
{
    if (!socket_) {
        errno = EINVAL;
        return -1;
    }

    scoped_msg_frames_t frames;
    if (!recv_msg_sequence_socket_wait (socket_, &frames, 500))
        return -1;

    const bool ok = discovery_protocol::decode_route_reply (frames, owner_rid_out_, value_out_);
    return ok ? 0 : -1;
}

int bind_route (ctx_t *ctx_,
                discovery_bootstrap_runtime_t *bootstrap_runtime_,
                const std::string &uplink_,
                zlink_route_kind_t kind_,
                const void *key_,
                size_t key_size_,
                const void *value_,
                size_t value_size_,
                const std::string &channel_name_,
                uint16_t owner_service_role_,
                const std::string &owner_endpoint_,
                uint64_t owner_registration_id_)
{
    socket_base_t *dealer = NULL;
    if (prepare_transient_dealer (ctx_, bootstrap_runtime_, uplink_, NULL, &dealer) != 0)
        return -1;

    const uint32_t raw_kind = kind_;
    const int send_rc =
      discovery_protocol::send_u16 (dealer, discovery_protocol::msg_bind_route, ZLINK_SNDMORE) < 0
      || discovery_protocol::send_u32 (dealer, raw_kind, ZLINK_SNDMORE) < 0
      || discovery_protocol::send_frame (dealer, key_, key_size_, ZLINK_SNDMORE) < 0
      || discovery_protocol::send_frame (dealer, value_, value_size_, ZLINK_SNDMORE) < 0
      || discovery_protocol::send_string (dealer, channel_name_, ZLINK_SNDMORE) < 0
      || discovery_protocol::send_u16 (dealer, owner_service_role_, ZLINK_SNDMORE) < 0
      || discovery_protocol::send_string (dealer, owner_endpoint_, ZLINK_SNDMORE) < 0
      || discovery_protocol::send_u64 (dealer, owner_registration_id_, 0) < 0;
    if (send_rc) {
        (void) close_dealer (ctx_, dealer);
        return -1;
    }

    const int rc = recv_route_reply (dealer, NULL, NULL);
    const int saved_errno = errno;
    (void) close_dealer (ctx_, dealer);
    if (rc != 0)
        errno = saved_errno;
    return rc;
}

int unbind_route (ctx_t *ctx_,
                  discovery_bootstrap_runtime_t *bootstrap_runtime_,
                  const std::string &uplink_,
                  zlink_route_kind_t kind_,
                  const void *key_,
                  size_t key_size_,
                  const std::string &channel_name_,
                  uint16_t owner_service_role_,
                  const std::string &owner_endpoint_,
                  uint64_t owner_registration_id_)
{
    socket_base_t *dealer = NULL;
    if (prepare_transient_dealer (ctx_, bootstrap_runtime_, uplink_, NULL, &dealer) != 0)
        return -1;

    const uint32_t raw_kind = kind_;
    const int send_rc =
      discovery_protocol::send_u16 (dealer, discovery_protocol::msg_unbind_route, ZLINK_SNDMORE) < 0
      || discovery_protocol::send_u32 (dealer, raw_kind, ZLINK_SNDMORE) < 0
      || discovery_protocol::send_frame (dealer, key_, key_size_, ZLINK_SNDMORE) < 0
      || discovery_protocol::send_string (dealer, channel_name_, ZLINK_SNDMORE) < 0
      || discovery_protocol::send_u16 (dealer, owner_service_role_, ZLINK_SNDMORE) < 0
      || discovery_protocol::send_string (dealer, owner_endpoint_, ZLINK_SNDMORE) < 0
      || discovery_protocol::send_u64 (dealer, owner_registration_id_, 0) < 0;
    if (send_rc) {
        (void) close_dealer (ctx_, dealer);
        return -1;
    }

    const int rc = recv_route_reply (dealer, NULL, NULL);
    const int saved_errno = errno;
    (void) close_dealer (ctx_, dealer);
    if (rc != 0)
        errno = saved_errno;
    return rc;
}

int resolve_route (ctx_t *ctx_,
                   discovery_bootstrap_runtime_t *bootstrap_runtime_,
                   const std::string &uplink_,
                   zlink_route_kind_t kind_,
                   const void *key_,
                   size_t key_size_,
                   const std::string &channel_name_,
                   zlink_routing_id_t *owner_rid_out_,
                   zlink_msg_t *value_out_)
{
    socket_base_t *dealer = NULL;
    if (prepare_transient_dealer (ctx_, bootstrap_runtime_, uplink_, NULL, &dealer) != 0)
        return -1;

    const uint32_t raw_kind = kind_;
    const int send_rc =
      discovery_protocol::send_u16 (dealer, discovery_protocol::msg_resolve_route, ZLINK_SNDMORE)
        < 0
      || discovery_protocol::send_u32 (dealer, raw_kind, ZLINK_SNDMORE) < 0
      || discovery_protocol::send_frame (dealer, key_, key_size_, ZLINK_SNDMORE) < 0
      || discovery_protocol::send_string (dealer, channel_name_, 0) < 0;
    if (send_rc) {
        (void) close_dealer (ctx_, dealer);
        return -1;
    }

    const int rc = recv_route_reply (dealer, owner_rid_out_, value_out_);
    const int saved_errno = errno;
    (void) close_dealer (ctx_, dealer);
    if (rc != 0)
        errno = saved_errno;
    return rc;
}
}
}
