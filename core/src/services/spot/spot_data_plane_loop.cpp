/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_data_plane_loop.hpp"

#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_mesh_pub_budget.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_runtime.hpp"

#include "services/common/monitor_decode.hpp"
#include "core/socket_poller.hpp"
#include "sockets/socket_base.hpp"
#include "utils/clock.hpp"

#include <climits>

namespace zlink
{
namespace
{
void initialize_poller (socket_poller_t *poller_,
                        const spot_data_plane_runtime_state_t &state_)
{
    poller_->add (state_.ctrl, NULL, ZLINK_POLLIN);
    poller_->add (state_.ingress, NULL, ZLINK_POLLIN);
    poller_->add (state_.mesh_xsub, NULL, ZLINK_POLLIN);
    poller_->add (state_.peer_ctrl_sub, NULL, ZLINK_POLLIN);
    poller_->add (state_.mesh_xsub_monitor, NULL, ZLINK_POLLIN);
}

void service_runtime_sockets (spot_runtime_t *runtime_,
                              spot_data_plane_runtime_state_t *state_)
{
    spot_data_plane_forwarder_t::pump_socket_commands (state_->mesh_pub);
    spot_data_plane_forwarder_t::pump_socket_commands (state_->mesh_xsub);
    spot_data_plane_forwarder_t::pump_socket_commands (state_->peer_ctrl_pub);
    spot_data_plane_forwarder_t::pump_socket_commands (state_->peer_ctrl_sub);
    spot_data_plane_forwarder_t::pump_socket_commands (state_->ingress);
    spot_data_plane_forwarder_t::pump_socket_commands (state_->fanout);

    state_->mesh_pub->set_all_pipes_nodelay ();
    state_->peer_ctrl_pub->set_all_pipes_nodelay ();
    state_->peer_ctrl_sub->set_all_pipes_nodelay ();
    state_->ingress->set_all_pipes_nodelay ();
    state_->fanout->set_all_pipes_nodelay ();
    spot_mesh_pub_budget_t::refresh_live_socket (
      runtime_, state_->mesh_pub, &state_->current_mesh_pub_sndhwm,
      &state_->last_mesh_pub_budget_version,
      &state_->last_mesh_pub_bound_endpoint);
}

int drain_peer_ctrl_messages (spot_node_t *node_,
                              spot_data_plane_runtime_state_t *state_,
                              spot_data_plane_protocol_state_t *protocol_state_)
{
    return spot_data_plane_protocol_t::recv_and_process_ctrl_messages (
      state_->peer_ctrl_sub, node_, protocol_state_);
}

bool is_ctrl_event (socket_base_t *socket_,
                    const spot_data_plane_runtime_state_t &state_)
{
    return socket_ == state_.ctrl || socket_ == state_.peer_ctrl_sub
           || socket_ == state_.mesh_xsub_monitor;
}

bool handle_ctrl_event (socket_base_t *socket_,
                        spot_node_t *node_,
                        spot_runtime_t *runtime_,
                        spot_data_plane_runtime_state_t *state_,
                        spot_data_plane_protocol_state_t *protocol_state_,
                        bool *running_out_,
                        int *fatal_errno_out_)
{
    if (socket_ == state_->ctrl) {
        std::vector<std::string> frames;
        if (spot_data_plane_protocol_t::recv_ascii_command (state_->ctrl,
                                                            &frames)
              != 0
            || spot_data_plane_protocol_t::handle_ctrl_command (
                 state_->ctrl, node_, runtime_, state_->mesh_pub,
                 state_->mesh_xsub, state_->peer_ctrl_pub,
                 state_->peer_ctrl_sub, frames, protocol_state_, running_out_)
                 != 0) {
            *fatal_errno_out_ = errno != 0 ? errno : EIO;
            *running_out_ = false;
        }
        return true;
    }

    if (socket_ == state_->peer_ctrl_sub) {
        if (drain_peer_ctrl_messages (node_, state_, protocol_state_) != 0) {
            *fatal_errno_out_ = errno;
            *running_out_ = false;
        }
        return true;
    }

    if (socket_ != state_->mesh_xsub_monitor)
        return false;

    while (*running_out_) {
        zlink_monitor_event_t raw;
        if (recv_socket_monitor_event (state_->mesh_xsub_monitor, &raw,
                                       ZLINK_DONTWAIT)
            != 0) {
            if (errno == EAGAIN || errno == EINTR)
                break;
            *fatal_errno_out_ = errno;
            *running_out_ = false;
            break;
        }

        spot_data_plane_protocol_t::sync_mesh_xsub_connected_endpoint (
          runtime_, raw);
    }
    return true;
}

bool handle_mesh_event (socket_base_t *socket_,
                        spot_node_t *node_,
                        spot_data_plane_runtime_state_t *state_,
                        spot_data_plane_protocol_state_t *protocol_state_,
                        bool *running_out_,
                        int *fatal_errno_out_)
{
    if (socket_ != state_->mesh_xsub)
        return false;

    if (spot_data_plane_protocol_t::recv_and_dispatch_mesh_xsub (
          state_->mesh_xsub, state_->fanout, state_->peer_ctrl_pub, node_,
          protocol_state_)
        != 0) {
        *fatal_errno_out_ = errno;
        *running_out_ = false;
    }
    return true;
}

bool handle_ingress_event (socket_base_t *socket_,
                           spot_node_t *node_,
                           spot_data_plane_runtime_state_t *state_,
                           bool *running_out_,
                           int *fatal_errno_out_)
{
    if (socket_ != state_->ingress)
        return false;

    if (spot_data_plane_forwarder_t::recv_and_forward_ingress (
          state_->ingress, state_->mesh_pub, state_->fanout, node_)
        != 0) {
        *fatal_errno_out_ = errno;
        *running_out_ = false;
    }
    return true;
}

int dispatch_ready_events (const socket_poller_t::event_t *events_,
                           int event_count_,
                           spot_node_t *node_,
                           spot_runtime_t *runtime_,
                           spot_data_plane_runtime_state_t *state_,
                           spot_data_plane_protocol_state_t *protocol_state_,
                           bool *running_out_)
{
    int fatal_errno = 0;

    for (int pass = 0; pass < 3 && *running_out_; ++pass) {
        for (int i = 0; i < event_count_; ++i) {
            if ((events_[i].events & ZLINK_POLLIN) == 0)
                continue;

            socket_base_t *socket =
              static_cast<socket_base_t *> (events_[i].socket);
            if ((pass == 0 && !is_ctrl_event (socket, *state_))
                || (pass == 1 && socket != state_->mesh_xsub)
                || (pass == 2 && socket != state_->ingress)) {
                continue;
            }

            if (handle_ctrl_event (socket, node_, runtime_, state_,
                                   protocol_state_, running_out_,
                                   &fatal_errno)
                || handle_mesh_event (socket, node_, state_, protocol_state_,
                                      running_out_, &fatal_errno)
                || handle_ingress_event (socket, node_, state_, running_out_,
                                         &fatal_errno)) {
                if (!*running_out_)
                    break;
            }
        }
    }

    return fatal_errno;
}

int publish_bootstrap_if_due (spot_node_t *node_,
                              spot_runtime_t *runtime_,
                              spot_data_plane_runtime_state_t *state_,
                              spot_data_plane_protocol_state_t *protocol_state_,
                              uint64_t *next_bootstrap_ms_,
                              uint64_t *last_bootstrap_peer_version_out_)
{
    const uint64_t now_ms = clock_t ().now_ms ();
    if (now_ms < *next_bootstrap_ms_)
        return 0;

    const bool bootstrap_ready = !protocol_state_->peer_ready_filters.empty ();
    if (spot_data_plane_protocol_t::should_publish_bootstrap_descriptor (
          runtime_, bootstrap_ready, *last_bootstrap_peer_version_out_)) {
        if (spot_data_plane_protocol_t::publish_bootstrap_descriptor (
              state_->mesh_pub, node_, runtime_)
            != 0) {
            return errno;
        }

        *last_bootstrap_peer_version_out_ =
          mesh_peer_version (&runtime_->mesh_peer_state);
    }

    *next_bootstrap_ms_ =
      now_ms
      + spot_data_plane_protocol_t::resolve_bootstrap_broadcast_interval_ms (
          runtime_, bootstrap_ready);
    return 0;
}

int resolve_data_plane_poll_timeout_ms (uint64_t next_bootstrap_ms_)
{
    if (next_bootstrap_ms_ == 0)
        return 0;

    const uint64_t now_ms = clock_t ().now_ms ();
    if (next_bootstrap_ms_ <= now_ms)
        return 0;

    const uint64_t remaining_ms = next_bootstrap_ms_ - now_ms;
    return remaining_ms > static_cast<uint64_t> (INT_MAX)
             ? INT_MAX
             : static_cast<int> (remaining_ms);
}
}

int spot_data_plane_loop_t::run_until_shutdown (
  spot_node_t *node_,
  spot_runtime_t *runtime_,
  spot_data_plane_runtime_state_t *state_,
  spot_data_plane_protocol_state_t *protocol_state_out_)
{
    socket_poller_t poller;
    initialize_poller (&poller, *state_);

    bool running = true;
    int fatal_errno = 0;
    uint64_t next_bootstrap_ms = 0;
    uint64_t last_bootstrap_peer_version = UINT64_MAX;
    spot_data_plane_protocol_state_t protocol_state;
    if (protocol_state_out_)
        *protocol_state_out_ = protocol_state;
    spot_data_plane_protocol_state_t *protocol_state_ptr =
      protocol_state_out_ ? protocol_state_out_ : &protocol_state;

    while (running) {
        service_runtime_sockets (runtime_, state_);

        if (drain_peer_ctrl_messages (node_, state_, protocol_state_ptr) != 0) {
            fatal_errno = errno;
            break;
        }

        socket_poller_t::event_t events[5];
        const int rc =
          poller.wait (events, 5,
                       resolve_data_plane_poll_timeout_ms (next_bootstrap_ms));
        if (rc < 0) {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            fatal_errno = errno;
            break;
        }

        fatal_errno = dispatch_ready_events (events, rc, node_, runtime_, state_,
                                             protocol_state_ptr, &running);
        if (!running)
            break;

        fatal_errno = publish_bootstrap_if_due (node_, runtime_, state_,
                                                protocol_state_ptr,
                                                &next_bootstrap_ms,
                                                &last_bootstrap_peer_version);
        if (fatal_errno != 0) {
            running = false;
            break;
        }
    }

    return fatal_errno;
}
}
