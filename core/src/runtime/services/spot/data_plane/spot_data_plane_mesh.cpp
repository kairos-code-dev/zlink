/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/data_plane/spot_data_plane_internal.hpp"
#include "services/spot/data_plane/spot_data_plane_message_io_internal.hpp"
#include "services/spot/common/spot_message_parts_internal.hpp"

#include "api/socket/request_reply_protocol_internal.hpp"
#include "api/spot/request_reply/service_spot_request_reply_internal.hpp"
#include "services/spot/common/spot_control_protocol.hpp"
#include "services/spot/common/spot_debug.hpp"
#include "services/spot/node/spot_node.hpp"
#include "services/spot/node/spot_node_access.hpp"
#include "services/spot/runtime/spot_runtime.hpp"

#include "services/common/monitor_decode.hpp"

#include <cstring>

namespace zlink
{
namespace
{
namespace spot_io = zlink::spot_data_plane_message_io;

static const unsigned int mesh_xsub_forward_batch_limit = 16384;
static const size_t mesh_xsub_forward_batch_bytes_limit = 16 * 1024 * 1024;

void spot_ctrl_debugf (const char *fmt_, ...)
{
    va_list args;
    va_start (args, fmt_);
    debug_vfprintf_with_file ("ZLINK_SPOT_CTRL_DEBUG", "[spot-ctrl] ", spot_debug::ctrl_log_path (),
                              fmt_, args);
    va_end (args);
}

uint64_t default_bootstrap_broadcast_interval_ms (const spot_runtime_t *runtime_)
{
    if (runtime_) {
        const std::string &bound_endpoint = runtime_->bound_endpoint;
        if (bound_endpoint.compare (0, 6, "tcp://") == 0
            || bound_endpoint.compare (0, 6, "tls://") == 0) {
            return 5000;
        }
    }

    return 1000;
}

int connect_external_router_peer (spot_node_t *node_,
                                  spot_runtime_t *runtime_,
                                  const std::string &peer_data_endpoint_,
                                  const std::string &peer_route_endpoint_)
{
    if (!node_ || !runtime_ || peer_data_endpoint_.empty () || peer_route_endpoint_.empty ()) {
        errno = EFAULT;
        return -1;
    }
    if (!runtime_->external_router)
        return 0;

    std::string route_id;
    if (!node_->external_route_id_for_peer_endpoint (peer_data_endpoint_, &route_id))
        route_id = peer_route_endpoint_;

    if (runtime_->external_router->setsockopt (ZLINK_INTERNAL_OPT_CONNECT_ROUTING_ID,
                                               route_id.data (), route_id.size ())
          != 0
        || runtime_->external_router->connect (peer_route_endpoint_.c_str ()) != 0) {
        const int saved_errno = errno != 0 ? errno : EIO;
        if (spot_debug::enabled ("ZLINK_DEBUG_SPOT_DIRECT_ROUTE")) {
            std::fprintf (stderr,
                          "[spot-direct] connect external router failed data=%s route=%s "
                          "endpoint=%s errno=%d\n",
                          peer_data_endpoint_.c_str (), route_id.c_str (),
                          peer_route_endpoint_.c_str (), saved_errno);
        }
        (void) runtime_->external_router->term_endpoint (peer_route_endpoint_.c_str ());
        errno = saved_errno;
        return -1;
    }

    runtime_->set_external_route_id (peer_data_endpoint_, route_id, peer_route_endpoint_);
    if (spot_debug::enabled ("ZLINK_DEBUG_SPOT_DIRECT_ROUTE")) {
        std::fprintf (
          stderr, "[spot-direct] connect external router data=%s route=%s endpoint=%s\n",
          peer_data_endpoint_.c_str (), route_id.c_str (), peer_route_endpoint_.c_str ());
    }
    return 0;
}
}

int spot_data_plane_protocol_t::publish_bootstrap_descriptor (socket_base_t *mesh_pub_,
                                                              spot_node_t *node_,
                                                              spot_runtime_t *runtime_)
{
    if (!mesh_pub_ || !node_ || !runtime_ || runtime_->peer_ctrl_endpoint.empty ())
        return 0;

    const std::string public_data_endpoint = node_->public_endpoint ();
    if (public_data_endpoint.empty ())
        return 0;

    const std::string source_node_id = spot_control_protocol::node_id_string (runtime_->node_id);
    const std::string version = spot_control_protocol::node_id_string (
      static_cast<uint32_t> (spot_control_protocol::protocol_version));
    if (spot_io::send_ascii_frame (
          mesh_pub_, spot_control_protocol::bootstrap_ctrl_descriptor_topic, ZLINK_SNDMORE)
          != 0
        || spot_io::send_ascii_frame (mesh_pub_, public_data_endpoint, ZLINK_SNDMORE) != 0
        || spot_io::send_ascii_frame (mesh_pub_, runtime_->peer_ctrl_endpoint, ZLINK_SNDMORE) != 0
        || spot_io::send_ascii_frame (mesh_pub_, source_node_id, ZLINK_SNDMORE) != 0
        || spot_io::send_ascii_frame (mesh_pub_, version, ZLINK_SNDMORE) != 0
        || spot_io::send_ascii_frame (mesh_pub_, runtime_->external_router_bind_endpoint, 0) != 0) {
        return -1;
    }

    spot_ctrl_debugf ("broadcast bootstrap data=%s ctrl=%s route=%s", public_data_endpoint.c_str (),
                      runtime_->peer_ctrl_endpoint.c_str (),
                      runtime_->external_router_bind_endpoint.c_str ());

    return 0;
}

bool spot_data_plane_protocol_t::should_publish_bootstrap_descriptor (
  const spot_runtime_t *runtime_, bool bootstrap_ready_, uint64_t last_published_peer_version_)
{
    if (!runtime_ || !bootstrap_ready_)
        return true;
    if (runtime_->missing_external_routes_for_ready_peer ())
        return true;

    const uint32_t ready_peer_count =
      connected_ready_peer_count (&runtime_->execution.mesh_peer_state);
    if (ready_peer_count == 0)
        return true;

    return mesh_peer_version (&runtime_->execution.mesh_peer_state) != last_published_peer_version_;
}

void spot_data_plane_protocol_t::sync_mesh_connected_endpoint (spot_runtime_t *runtime_,
                                                               const zlink_monitor_event_t &raw_)
{
    if (!runtime_ || raw_.remote_addr[0] == '\0')
        return;

    if (spot_debug::enabled ("ZLINK_DEBUG_SPOT_CONTROL")) {
        std::fprintf (stderr, "[spot-control] mesh-monitor node=%p event=%llu remote=%s\n",
                      runtime_->owner, static_cast<unsigned long long> (raw_.event),
                      raw_.remote_addr);
        std::fflush (stderr);
    }

    const bool changed = sync_mesh_peer_monitor_state (&runtime_->execution.mesh_peer_state, raw_);
    if (raw_.event == ZLINK_EVENT_DISCONNECTED) {
        runtime_->execution.data_plane_protocol_state.peer_ctrl_endpoints.clear ();
        runtime_->execution.data_plane_protocol_state.peer_ready_filters.clear ();
        runtime_->execution.data_plane_protocol_state.outbound_ready_filters.clear ();
    }
    if (spot_debug::enabled ("ZLINK_DEBUG_SPOT_CONTROL")) {
        std::fprintf (stderr, "[spot-control] mesh-monitor node=%p changed=%d version=%llu\n",
                      runtime_->owner, changed ? 1 : 0,
                      static_cast<unsigned long long> (
                        mesh_peer_version (&runtime_->execution.mesh_peer_state)));
        std::fflush (stderr);
    }
    if (!changed)
        return;
    if (runtime_->owner)
        spot_node_access_t::wake_control_task (runtime_->owner);
}

void spot_data_plane_protocol_t::clear_mesh_connected_endpoints (spot_runtime_t *runtime_)
{
    if (!runtime_)
        return;

    const bool changed = clear_mesh_peer_monitor_state (&runtime_->execution.mesh_peer_state);
    if (changed && runtime_->owner)
        spot_node_access_t::wake_control_task (runtime_->owner);
}

void spot_data_plane_protocol_t::clear_snapshot_sources (spot_node_t *node_,
                                                         spot_data_plane_protocol_state_t *state_)
{
    if (!node_ || !state_)
        return;

    const std::string self_endpoint = node_->public_endpoint ();
    for (std::map<std::string, std::set<std::string>>::iterator it =
           state_->peer_ready_filters.begin ();
         it != state_->peer_ready_filters.end (); ++it) {
        if (self_endpoint.empty ())
            continue;
        for (std::set<std::string>::const_iterator filter_it = it->second.begin ();
             filter_it != it->second.end (); ++filter_it) {
            node_->notify_pub_delivery_ready_ack (self_endpoint, *filter_it, it->first, false);
        }
    }
    state_->peer_ready_filters.clear ();
}

int spot_data_plane_protocol_t::recv_and_dispatch_mesh_xsub (
  socket_base_t *mesh_xsub_,
  socket_base_t *peer_ctrl_pub_,
  spot_runtime_t *runtime_,
  spot_data_plane_runtime_state_t *runtime_state_,
  spot_node_t *node_,
  spot_data_plane_protocol_state_t *state_)
{
    if (!mesh_xsub_ || !peer_ctrl_pub_ || !runtime_ || !runtime_state_ || !node_ || !state_) {
        errno = EFAULT;
        return -1;
    }

    unsigned int processed = 0;
    size_t processed_bytes = 0;
    for (;;) {
        msg_t topic_msg;
        if (topic_msg.init () != 0)
            return -1;
        if (mesh_xsub_->recv (&topic_msg, ZLINK_DONTWAIT) != 0) {
            const int err = errno;
            topic_msg.close ();
            if (err == EAGAIN)
                return 0;
            return -1;
        }

        processed_bytes += topic_msg.size ();
        const bool has_payload = (topic_msg.flags () & msg_t::more) != 0;
        std::string topic (static_cast<const char *> (topic_msg.data ()), topic_msg.size ());
        topic_msg.close ();

        if (!has_payload) {
            ++processed;
            if (processed >= mesh_xsub_forward_batch_limit
                || processed_bytes >= mesh_xsub_forward_batch_bytes_limit)
                return 0;
            continue;
        }

        spot_owned_msg_parts_t frames;
        if (spot_io::recv_remaining_frames_to_parts (mesh_xsub_, &frames, &processed_bytes) != 0) {
            if (errno == EAGAIN || errno == EINTR)
                return 0;
            return -1;
        }

        const char *topic_data = topic.data ();
        const size_t topic_size = topic.size ();

        if (!spot_control_protocol::is_bootstrap_ctrl_descriptor_topic (topic_data, topic_size)) {
            if (!runtime_state_->local_fanout.targets.empty ()
                && spot_data_plane_forwarder_t::forward_local_fanout (runtime_, runtime_state_,
                                                                      topic, frames)
                     != 0) {
                if (errno == EAGAIN
                    && spot_data_plane_forwarder_t::stage_message (runtime_state_, topic, frames,
                                                                   true, true, false)
                         == 0) {
                    spot_clear_msg_parts (&frames);
                    return 0;
                }
                spot_clear_msg_parts (&frames);
                return -1;
            }
            spot_clear_msg_parts (&frames);

            ++processed;
            if (processed >= mesh_xsub_forward_batch_limit
                || processed_bytes >= mesh_xsub_forward_batch_bytes_limit)
                return 0;
            continue;
        }

        if (frames.size () < 5) {
            spot_clear_msg_parts (&frames);
            continue;
        }

        const std::string peer_data_endpoint = spot_msg_frame_to_string (frames[0]);
        const std::string peer_ctrl_endpoint = spot_msg_frame_to_string (frames[1]);
        const std::string peer_route_endpoint = spot_msg_frame_to_string (frames[4]);
        if (peer_data_endpoint.empty () || peer_ctrl_endpoint.empty ()) {
            spot_clear_msg_parts (&frames);
            continue;
        }

        if (!peer_route_endpoint.empty ()
            && connect_external_router_peer (node_, runtime_, peer_data_endpoint,
                                             peer_route_endpoint)
                 != 0) {
            spot_clear_msg_parts (&frames);
            continue;
        }
        spot_clear_msg_parts (&frames);

        const std::map<std::string, std::string>::iterator existing =
          state_->peer_ctrl_endpoints.find (peer_data_endpoint);
        const bool changed =
          existing == state_->peer_ctrl_endpoints.end () || existing->second != peer_ctrl_endpoint;
        if (!changed)
            continue;

        if (existing != state_->peer_ctrl_endpoints.end () && !existing->second.empty ()) {
            (void) peer_ctrl_pub_->term_endpoint (existing->second.c_str ());
        }

        if (peer_ctrl_pub_->connect (peer_ctrl_endpoint.c_str ()) != 0)
            continue;
        state_->peer_ctrl_endpoints[peer_data_endpoint] = peer_ctrl_endpoint;

        ++processed;
        if (processed >= mesh_xsub_forward_batch_limit
            || processed_bytes >= mesh_xsub_forward_batch_bytes_limit)
            return 0;
    }
}
}
