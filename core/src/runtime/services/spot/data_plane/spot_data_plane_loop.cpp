/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/data_plane/spot_data_plane_loop.hpp"

#include "services/spot/data_plane/spot_data_plane_internal.hpp"
#include "services/spot/data_plane/spot_mesh_pub_hwm.hpp"
#include "services/spot/node/spot_node.hpp"
#include "services/spot/runtime/spot_runtime.hpp"

#include "api/service/service_api_internal.hpp"
#include "api/spot/dispatch/service_spot_dispatch_surface_internal.hpp"
#include "api/spot/request_reply/service_spot_request_reply_internal.hpp"

#include "services/common/monitor_decode.hpp"
#include "core/socket_poller.hpp"
#include "sockets/common/socket_base.hpp"
#include "utils/clock.hpp"

#include <algorithm>
#include <climits>
#include <mutex>
#include <thread>
#include <vector>

namespace zlink
{
namespace
{
const int data_plane_idle_tick_ms = 100;
const int data_plane_min_event_capacity = 8;
const unsigned int data_plane_ready_yield_loop_limit = 64;

enum data_plane_dispatch_pass_t
{
    data_plane_dispatch_control_pass = 0,
    data_plane_dispatch_ingress_pass,
    data_plane_dispatch_pass_count
};

void pump_data_plane_socket_commands (spot_data_plane_runtime_state_t *state_)
{
    spot_data_plane_forwarder_t::pump_socket_commands (state_->ctrl);
    spot_data_plane_forwarder_t::pump_socket_commands (state_->mesh_pub);
    spot_data_plane_forwarder_t::pump_socket_commands (state_->mesh_xsub);
    spot_data_plane_forwarder_t::pump_socket_commands (state_->pub_ingress_sub);
    spot_data_plane_forwarder_t::pump_socket_commands (
      state_->mesh_peer_observer.pub_monitor);
    spot_data_plane_forwarder_t::pump_socket_commands (
      state_->mesh_peer_observer.xsub_monitor);
    spot_data_plane_forwarder_t::pump_socket_commands (state_->peer_ctrl_pub);
    spot_data_plane_forwarder_t::pump_socket_commands (state_->peer_ctrl_sub);
    spot_data_plane_forwarder_t::pump_socket_commands (
      state_->external_router);
    spot_data_plane_forwarder_t::pump_socket_commands (state_->fanout);
}

void sync_data_plane_attachment_targets (
  spot_runtime_t *runtime_,
  spot_data_plane_runtime_state_t *state_,
  const spot_data_plane_protocol_state_t *protocol_state_)
{
    const uint64_t attachment_version = runtime_->attachment_state_version ();
    if (state_->last_attachment_version != attachment_version) {
        spot_data_plane_forwarder_t::sync_local_fanout_targets (runtime_, state_);
        spot_data_plane_forwarder_t::sync_remote_mesh_targets (runtime_, state_,
                                                               protocol_state_);
        state_->last_attachment_version = attachment_version;
    }
}

void apply_data_plane_socket_policy_once (
  spot_data_plane_runtime_state_t *state_)
{
    if (!state_->runtime_sockets_nodelay_applied) {
        if (state_->mesh_pub)
            state_->mesh_pub->set_all_pipes_nodelay ();
        if (state_->pub_ingress_sub)
            state_->pub_ingress_sub->set_all_pipes_nodelay ();
        if (state_->peer_ctrl_pub)
            state_->peer_ctrl_pub->set_all_pipes_nodelay ();
        if (state_->peer_ctrl_sub)
            state_->peer_ctrl_sub->set_all_pipes_nodelay ();
        if (state_->external_router)
            state_->external_router->set_all_pipes_nodelay ();
        if (state_->fanout)
            state_->fanout->set_all_pipes_nodelay ();
        state_->runtime_sockets_nodelay_applied = true;
    }
}

void refresh_data_plane_limits_and_hwm (
  spot_runtime_t *runtime_,
  spot_data_plane_runtime_state_t *state_)
{
    spot_mesh_pub_hwm_t::refresh_live_socket (
      runtime_, state_->mesh_pub, &state_->mesh_pub_hwm.current_sndhwm,
      &state_->mesh_pub_hwm.last_hwm_version,
      &state_->mesh_pub_hwm.last_bound_endpoint);
    spot_data_plane_forwarder_t::update_pending_queue_limits (runtime_, state_);
}

void drain_data_plane_queued_ingress (
  spot_runtime_t *runtime_,
  spot_data_plane_runtime_state_t *state_)
{
    (void) spot_reqrep_internal::drain_runtime_external_router_ingress_queue (
      runtime_);
    (void) spot_data_plane_forwarder_t::drain_pub_ingress_socket (runtime_,
                                                                  state_);
    (void) spot_data_plane_forwarder_t::drain_publish_ingress_queue (runtime_,
                                                                     state_);
    (void) spot_reqrep_internal::drain_runtime_routed_send_queue (runtime_);
}

void flush_data_plane_pending_output (
  spot_runtime_t *runtime_,
  spot_data_plane_runtime_state_t *state_)
{
    (void) spot_data_plane_forwarder_t::flush_mesh_pub_pending (runtime_, state_);
    (void) spot_data_plane_forwarder_t::flush_local_fanout_pending (runtime_,
                                                                    state_);
    (void) spot_data_plane_forwarder_t::flush_staged_messages (runtime_, state_);
}

void refresh_data_plane_poller_interest (spot_data_plane_runtime_state_t *state_)
{
    spot_data_plane_forwarder_t::refresh_poller_interest (state_);
}

void service_runtime_sockets (spot_runtime_t *runtime_,
                              spot_data_plane_runtime_state_t *state_,
                              const spot_data_plane_protocol_state_t *protocol_state_)
{
    if (!runtime_ || !state_)
        return;

    pump_data_plane_socket_commands (state_);
    sync_data_plane_attachment_targets (runtime_, state_, protocol_state_);
    apply_data_plane_socket_policy_once (state_);
    refresh_data_plane_limits_and_hwm (runtime_, state_);
    drain_data_plane_queued_ingress (runtime_, state_);
    flush_data_plane_pending_output (runtime_, state_);
    refresh_data_plane_poller_interest (state_);
}

int drain_peer_ctrl_messages (spot_node_t *node_,
                              spot_data_plane_runtime_state_t *state_,
                              spot_data_plane_protocol_state_t *protocol_state_)
{
    if (!state_->peer_ctrl_sub)
        return 0;
    return spot_data_plane_protocol_t::recv_and_process_ctrl_messages (
      state_->peer_ctrl_sub, node_, protocol_state_);
}

int drain_direct_route_messages (spot_node_t *node_,
                                 spot_data_plane_runtime_state_t *state_)
{
    if (state_->external_router
        && !state_->external_router->socket_msg_dispatch_active ()
        && zlink_spot_process_external_router (
             node_, state_->external_router)
             != 0)
        return -1;

    return 0;
}

bool is_ctrl_event (socket_base_t *socket_,
                    const spot_data_plane_runtime_state_t &state_)
{
    return socket_ == state_.ctrl || socket_ == state_.peer_ctrl_sub
           || socket_ == state_.external_router
           || state_.mesh_peer_observer.owns (socket_);
}

bool should_handle_pollin_event_in_pass (
  data_plane_dispatch_pass_t pass_,
  socket_base_t *socket_,
  const spot_data_plane_runtime_state_t &state_)
{
    if (pass_ == data_plane_dispatch_control_pass)
        return is_ctrl_event (socket_, state_);
    if (pass_ == data_plane_dispatch_ingress_pass)
        return socket_ == state_.mesh_xsub || socket_ == state_.pub_ingress_sub;
    return false;
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
                 state_->ctrl, node_, runtime_, state_->poller, state_->mesh_pub,
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

    if (socket_ == state_->external_router) {
        if (!socket_->socket_msg_dispatch_active ()
            && zlink_spot_process_external_router (node_, socket_) != 0) {
            *fatal_errno_out_ = errno;
            *running_out_ = false;
        }
        return true;
    }

    if (!state_->mesh_peer_observer.owns (socket_))
        return false;

    socket_base_t *monitor = socket_;
    while (*running_out_) {
        zlink_monitor_event_t raw;
        if (recv_socket_monitor_event (monitor, &raw, ZLINK_DONTWAIT)
            != 0) {
            if (errno == EAGAIN || errno == EINTR)
                break;
            *fatal_errno_out_ = errno;
            *running_out_ = false;
            break;
        }

        spot_data_plane_protocol_t::sync_mesh_connected_endpoint (runtime_, raw);
    }
    return true;
}

bool handle_mesh_event (socket_base_t *socket_,
                        spot_node_t *node_,
                        spot_runtime_t *runtime_,
                        spot_data_plane_runtime_state_t *state_,
                        spot_data_plane_protocol_state_t *protocol_state_,
                        bool *running_out_,
                        int *fatal_errno_out_)
{
    if (!state_->mesh_xsub || socket_ != state_->mesh_xsub)
        return false;

    if (spot_data_plane_protocol_t::recv_and_dispatch_mesh_xsub (
          state_->mesh_xsub, state_->peer_ctrl_pub, runtime_, state_, node_,
          protocol_state_)
        != 0) {
        *fatal_errno_out_ = errno;
        *running_out_ = false;
    }
    return true;
}

bool handle_pub_ingress_event (socket_base_t *socket_,
                               spot_runtime_t *runtime_,
                               spot_data_plane_runtime_state_t *state_,
                               bool *running_out_,
                               int *fatal_errno_out_)
{
    if (!state_->pub_ingress_sub || socket_ != state_->pub_ingress_sub)
        return false;

    if (spot_data_plane_forwarder_t::drain_pub_ingress_socket (runtime_, state_)
        != 0) {
        *fatal_errno_out_ = errno;
        *running_out_ = false;
    }
    return true;
}

bool handle_pollout_event (socket_base_t *socket_,
                           spot_node_t *node_,
                           spot_runtime_t *runtime_,
                           spot_data_plane_runtime_state_t *state_,
                           bool *running_out_,
                           int *fatal_errno_out_)
{
    (void) node_;

    if (spot_data_plane_forwarder_t::flush_local_fanout_pending (runtime_, state_,
                                                                 socket_)
        != 0) {
        *fatal_errno_out_ = errno;
        *running_out_ = false;
        return true;
    }

    if (spot_data_plane_forwarder_t::flush_mesh_pub_pending (runtime_, state_,
                                                             socket_)
        != 0) {
        *fatal_errno_out_ = errno;
        *running_out_ = false;
        return true;
    }

    if (spot_data_plane_forwarder_t::flush_staged_messages (runtime_, state_)
        != 0) {
        *fatal_errno_out_ = errno;
        *running_out_ = false;
        return true;
    }

    return false;
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

    for (int i = 0; i < event_count_ && *running_out_; ++i) {
        if ((events_[i].events & ZLINK_POLLOUT) == 0)
            continue;
        socket_base_t *socket =
          static_cast<socket_base_t *> (events_[i].socket);
        if (handle_pollout_event (socket, node_, runtime_, state_, running_out_,
                                  &fatal_errno)
            && !*running_out_) {
            break;
        }
    }

    for (int pass = 0; pass < data_plane_dispatch_pass_count && *running_out_;
         ++pass) {
        const data_plane_dispatch_pass_t dispatch_pass =
          static_cast<data_plane_dispatch_pass_t> (pass);
        for (int i = 0; i < event_count_; ++i) {
            if ((events_[i].events & ZLINK_POLLIN) == 0)
                continue;
            if (!events_[i].socket
                && events_[i].fd == state_->publish_ingress.signaler.get_fd ()) {
                (void) state_->publish_ingress.signaler.recv_failable ();
                {
                    std::lock_guard<std::mutex> lock (
                      state_->publish_ingress.mutex);
                    state_->publish_ingress.signal_armed = false;
                }
                continue;
            }
            if (!events_[i].socket
                && events_[i].fd == state_->routed_send.signaler.get_fd ()) {
                (void) state_->routed_send.signaler.recv_failable ();
                {
                    std::lock_guard<std::mutex> lock (
                      state_->routed_send.mutex);
                    state_->routed_send.signal_armed = false;
                }
                continue;
            }
            if (!events_[i].socket
                && events_[i].fd
                     == state_->external_router_ingress.signaler.get_fd ()) {
                (void) state_->external_router_ingress.signaler.recv_failable ();
                {
                    std::lock_guard<std::mutex> lock (
                      state_->external_router_ingress.mutex);
                    state_->external_router_ingress.signal_armed = false;
                }
                continue;
            }

            socket_base_t *socket =
              static_cast<socket_base_t *> (events_[i].socket);
            if (!should_handle_pollin_event_in_pass (dispatch_pass, socket,
                                                     *state_)) {
                continue;
            }

            if (handle_ctrl_event (socket, node_, runtime_, state_,
                                   protocol_state_, running_out_, &fatal_errno)
                || handle_mesh_event (socket, node_, runtime_, state_,
                                      protocol_state_, running_out_,
                                      &fatal_errno)
                || handle_pub_ingress_event (socket, runtime_, state_,
                                             running_out_, &fatal_errno)) {
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
    if (now_ms < *next_bootstrap_ms_
        && !(runtime_ && runtime_->missing_external_routes_for_ready_peer ()))
        return 0;

    const bool bootstrap_ready = !protocol_state_->peer_ready_filters.empty ();
    if (state_->mesh_pub
        && spot_data_plane_protocol_t::should_publish_bootstrap_descriptor (
          runtime_, bootstrap_ready, *last_bootstrap_peer_version_out_)) {
        if (spot_data_plane_protocol_t::publish_bootstrap_descriptor (
              state_->mesh_pub, node_, runtime_)
            != 0) {
            return errno;
        }

        *last_bootstrap_peer_version_out_ =
          mesh_peer_version (&runtime_->execution.mesh_peer_state);
    }

    *next_bootstrap_ms_ =
      now_ms
      + spot_data_plane_protocol_t::resolve_bootstrap_broadcast_interval_ms (
          runtime_, bootstrap_ready);
    return 0;
}

int resolve_data_plane_poll_timeout_ms (
  uint64_t next_bootstrap_ms_,
  spot_data_plane_runtime_state_t *state_)
{
    uint64_t next_due_ms = next_bootstrap_ms_;
    if (state_) {
        std::lock_guard<std::mutex> lock (state_->routed_send.mutex);
        if (state_->routed_send.retry_after_ms != 0
            && (!next_due_ms || state_->routed_send.retry_after_ms < next_due_ms))
            next_due_ms = state_->routed_send.retry_after_ms;
    }

    if (next_due_ms == 0)
        return data_plane_idle_tick_ms;

    const uint64_t now_ms = clock_t ().now_ms ();
    if (next_due_ms <= now_ms)
        return 0;

    const uint64_t remaining_ms = next_due_ms - now_ms;
    const int timeout = remaining_ms > static_cast<uint64_t> (INT_MAX)
                          ? INT_MAX
                          : static_cast<int> (remaining_ms);
    return std::min (timeout, data_plane_idle_tick_ms);
}

}

int spot_data_plane_loop_t::run_until_shutdown (
  spot_node_t *node_,
  spot_runtime_t *runtime_,
  spot_data_plane_runtime_state_t *state_,
  spot_data_plane_protocol_state_t *protocol_state_out_)
{
    bool running = true;
    int fatal_errno = 0;
    uint64_t next_bootstrap_ms = 0;
    uint64_t last_bootstrap_peer_version = UINT64_MAX;
    spot_data_plane_protocol_state_t protocol_state;
    if (protocol_state_out_)
        *protocol_state_out_ = protocol_state;
    spot_data_plane_protocol_state_t *protocol_state_ptr =
      protocol_state_out_ ? protocol_state_out_ : &protocol_state;
    unsigned int consecutive_ready_loops = 0;

    while (running) {
        if (runtime_ && runtime_->stop.get () != 0)
            break;
        service_runtime_sockets (runtime_, state_, protocol_state_ptr);

        if (drain_peer_ctrl_messages (node_, state_, protocol_state_ptr) != 0) {
            fatal_errno = errno;
            break;
        }
        if (drain_direct_route_messages (node_, state_) != 0) {
            fatal_errno = errno;
            break;
        }
        const size_t event_capacity =
          static_cast<size_t> (
            std::max (state_->poller->size (), data_plane_min_event_capacity));
        if (state_->poll_events.size () < event_capacity)
            state_->poll_events.resize (event_capacity);
        const int rc = state_->poller->wait (
          state_->poll_events.empty () ? NULL : &state_->poll_events[0],
          static_cast<int> (state_->poll_events.size ()),
          resolve_data_plane_poll_timeout_ms (next_bootstrap_ms, state_));
        if (rc < 0) {
            consecutive_ready_loops = 0;
            if (errno == EAGAIN || errno == EINTR)
                continue;
            fatal_errno = errno;
            break;
        }
        if (rc > 0
            && ++consecutive_ready_loops
                 >= data_plane_ready_yield_loop_limit) {
            std::this_thread::yield ();
            consecutive_ready_loops = 0;
        }

        fatal_errno = dispatch_ready_events (
          state_->poll_events.empty () ? NULL : &state_->poll_events[0], rc,
          node_, runtime_, state_, protocol_state_ptr, &running);
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

int spot_data_plane_loop_t::run_once (
  spot_node_t *node_,
  spot_runtime_t *runtime_,
  spot_data_plane_runtime_state_t *state_,
  spot_data_plane_protocol_state_t *protocol_state_,
  uint64_t *next_bootstrap_ms_,
  uint64_t *last_bootstrap_peer_version_,
  bool *running_out_)
{
    if (!node_ || !runtime_ || !state_ || !protocol_state_ || !next_bootstrap_ms_
        || !last_bootstrap_peer_version_ || !running_out_) {
        errno = EINVAL;
        return EINVAL;
    }

    *running_out_ = true;
    if (!state_->poller)
        return EFAULT;
    if (runtime_->stop.get () != 0) {
        *running_out_ = false;
        return 0;
    }
    service_runtime_sockets (runtime_, state_, protocol_state_);

    if (drain_peer_ctrl_messages (node_, state_, protocol_state_) != 0)
        return errno;
    if (drain_direct_route_messages (node_, state_) != 0)
        return errno;
    const size_t event_capacity =
      static_cast<size_t> (
        std::max (state_->poller->size (), data_plane_min_event_capacity));
    if (state_->poll_events.size () < event_capacity)
        state_->poll_events.resize (event_capacity);
    const int rc = state_->poller->wait (
      state_->poll_events.empty () ? NULL : &state_->poll_events[0],
      static_cast<int> (state_->poll_events.size ()), 0);
    if (rc < 0) {
        if (errno == EAGAIN || errno == EINTR)
            return 0;
        return errno;
    }

    int fatal_errno =
      dispatch_ready_events (
        state_->poll_events.empty () ? NULL : &state_->poll_events[0], rc,
        node_, runtime_, state_, protocol_state_, running_out_);
    if (!*running_out_ || fatal_errno != 0)
        return fatal_errno;

    fatal_errno = publish_bootstrap_if_due (
      node_, runtime_, state_, protocol_state_, next_bootstrap_ms_,
      last_bootstrap_peer_version_);
    if (fatal_errno != 0)
        *running_out_ = false;
    return fatal_errno;
}
}
