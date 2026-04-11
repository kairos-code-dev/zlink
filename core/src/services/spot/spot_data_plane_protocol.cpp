/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_message_parts_internal.hpp"
#include "services/spot/spot_mesh_pub_budget.hpp"

#include "services/spot/spot_control_protocol.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_runtime.hpp"

#include "core/multipart_send_txn.hpp"
#include "services/common/monitor_decode.hpp"
#include "sockets/socket_base.hpp"
#include "utils/clock.hpp"
#include "utils/err.hpp"

#include <errno.h>
#include <map>
#include <set>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zlink
{
namespace
{
static const unsigned int mesh_xsub_forward_batch_limit = 16384;
// Bound fanout bursts by bytes so large SPOT payloads cannot hold the client
// data-plane thread long enough to inflate delivery tail latency.
static const size_t mesh_xsub_forward_batch_bytes_limit = 16 * 1024 * 1024;
static const uint32_t spot_batch_magic_v1 = 0x31544253u;
static const uint16_t spot_batch_version_v1 = 1;
static const uint32_t spot_batch_header_size_v1 = 12;
static const uint32_t spot_batch_metadata_size_v1 = 16;

static void spot_ctrl_debugf (const char *fmt_, ...)
{
    if (!getenv ("ZLINK_SPOT_CTRL_DEBUG"))
        return;

    va_list args;
    va_start (args, fmt_);
    fprintf (stderr, "[spot-ctrl] ");
    vfprintf (stderr, fmt_, args);
    fprintf (stderr, "\n");
    fflush (stderr);
    FILE *fp = fopen ("/tmp/zlink_spot_ctrl.log", "a");
    if (fp) {
        va_list file_args;
        va_start (file_args, fmt_);
        vfprintf (fp, fmt_, file_args);
        fprintf (fp, "\n");
        va_end (file_args);
        fclose (fp);
    }
    va_end (args);
}

static void spot_ready_ack_ctrl_debugf (const char *fmt_, ...)
{
    if (!getenv ("ZLINK_DEBUG_SPOT_READY_ACK"))
        return;

    va_list args;
    va_start (args, fmt_);
    fprintf (stderr, "[spot-ready-ack-ctrl] ");
    vfprintf (stderr, fmt_, args);
    fprintf (stderr, "\n");
    fflush (stderr);
    FILE *fp = fopen ("/tmp/zlink_spot_ready_ack.log", "a");
    if (fp) {
        va_list file_args;
        va_start (file_args, fmt_);
        vfprintf (fp, fmt_, file_args);
        fprintf (fp, "\n");
        va_end (file_args);
        fclose (fp);
    }
    va_end (args);
}

static int send_ascii_frame (socket_base_t *socket_,
                             const std::string &value_,
                             int flags_)
{
    msg_t msg;
    if (msg.init_size (value_.size ()) != 0)
        return -1;
    if (!value_.empty ())
        memcpy (msg.data (), value_.data (), value_.size ());
    const int rc = socket_->send (&msg, flags_);
    msg.close ();
    return rc;
}

static uint16_t read_u16_le (const unsigned char *data_)
{
    return static_cast<uint16_t> (data_[0])
           | (static_cast<uint16_t> (data_[1]) << 8);
}

static uint32_t read_u32_le (const unsigned char *data_)
{
    return static_cast<uint32_t> (data_[0])
           | (static_cast<uint32_t> (data_[1]) << 8)
           | (static_cast<uint32_t> (data_[2]) << 16)
           | (static_cast<uint32_t> (data_[3]) << 24);
}

static void warn_malformed_batch (const std::string &topic_,
                                  const char *reason_)
{
    std::fprintf (stderr, "[spot-batch] warning: drop malformed batch topic=%s reason=%s\n",
                  topic_.c_str (), reason_ ? reason_ : "unknown");
    std::fflush (stderr);
}

static int publish_owned_parts (socket_base_t *socket_,
                                const std::string &topic_,
                                const std::vector<std::string> &parts_)
{
    if (!socket_ || topic_.empty ()) {
        errno = EINVAL;
        return -1;
    }

    std::vector<zlink_msg_t> frames (parts_.size ());
    if (!frames.empty ())
        memset (&frames[0], 0, frames.size () * sizeof (zlink_msg_t));
    for (size_t i = 0; i < parts_.size (); ++i) {
        if (zlink_msg_init_size (&frames[i], parts_[i].size ()) != 0) {
            for (size_t j = 0; j < i; ++j)
                (void) zlink_msg_close (&frames[j]);
            return -1;
        }
        if (!parts_[i].empty ())
            memcpy (zlink_msg_data (&frames[i]), parts_[i].data (),
                    parts_[i].size ());
    }

    const int rc = logical_multipart_publish (
      socket_, topic_.c_str (), frames.empty () ? NULL : &frames[0],
      frames.size (), 0, true);
    const int saved_errno = rc == 0 ? 0 : errno;
    for (size_t i = 0; i < frames.size (); ++i)
        (void) zlink_msg_close (&frames[i]);
    if (saved_errno != 0) {
        errno = saved_errno;
        return -1;
    }
    return 0;
}

static int send_control_snapshot (socket_base_t *socket_,
                                  const char *topic_,
                                  const std::string &target_endpoint_,
                                  const std::string &source_node_id_,
                                  const std::set<std::string> &filters_)
{
    if (!socket_ || !topic_ || target_endpoint_.empty ()
        || source_node_id_.empty ()) {
        errno = EINVAL;
        return -1;
    }

    const std::string version =
      spot_control_protocol::node_id_string (
        static_cast<uint32_t> (spot_control_protocol::protocol_version));

    if (send_ascii_frame (socket_, topic_, ZLINK_SNDMORE)
        != 0
        || send_ascii_frame (socket_, target_endpoint_, ZLINK_SNDMORE) != 0
        || send_ascii_frame (socket_, source_node_id_, ZLINK_SNDMORE) != 0) {
        return -1;
    }

    const bool has_filters = !filters_.empty ();
    if (send_ascii_frame (socket_, version, has_filters ? ZLINK_SNDMORE : 0)
        != 0)
        return -1;

    std::set<std::string>::const_iterator it = filters_.begin ();
    while (it != filters_.end ()) {
        std::set<std::string>::const_iterator next = it;
        ++next;
        if (send_ascii_frame (socket_, *it,
                              next != filters_.end () ? ZLINK_SNDMORE : 0)
            != 0) {
            return -1;
        }
        it = next;
    }

    spot_ctrl_debugf ("send snapshot target=%s source=%s filters=%zu",
                      target_endpoint_.c_str (), source_node_id_.c_str (),
                      static_cast<size_t> (filters_.size ()));

    return 0;
}

static int send_snapshot_to_target (socket_base_t *socket_,
                                    spot_node_t *node_,
                                    const std::string &target_endpoint_)
{
    if (!socket_ || !node_ || target_endpoint_.empty ()) {
        errno = EINVAL;
        return -1;
    }

    std::set<std::string> filters;
    node_->snapshot_raw_subscription_filters (&filters);
    return send_control_snapshot (
      socket_, spot_control_protocol::ctrl_snapshot_topic, target_endpoint_,
      spot_control_protocol::node_id_string (
        spot_node_access_t::runtime (node_)->node_id),
      filters);
}

static int send_snapshot_to_peers (
  socket_base_t *socket_,
  spot_node_t *node_,
  const std::map<std::string, std::string> &peer_ctrl_endpoints_)
{
    if (!socket_ || !node_)
        return 0;

    std::set<std::string> filters;
    node_->snapshot_raw_subscription_filters (&filters);
    const std::string source_node_id =
      spot_control_protocol::node_id_string (
        spot_node_access_t::runtime (node_)->node_id);

    for (std::map<std::string, std::string>::const_iterator it =
           peer_ctrl_endpoints_.begin ();
         it != peer_ctrl_endpoints_.end (); ++it) {
        if (it->first.empty ())
            continue;
        if (send_control_snapshot (socket_,
                                   spot_control_protocol::ctrl_snapshot_topic,
                                   it->first, source_node_id, filters)
            != 0) {
            return -1;
        }
    }

    return 0;
}

static int send_ready_ack_snapshots_to_target (
  socket_base_t *socket_,
  const std::string &target_endpoint_,
  const std::map<std::string, std::map<std::string, std::set<std::string> > > &
    outbound_ready_filters_)
{
    if (!socket_ || target_endpoint_.empty ())
        return 0;

    const std::map<std::string,
                   std::map<std::string, std::set<std::string> > >::const_iterator
      target_it = outbound_ready_filters_.find (target_endpoint_);
    if (target_it == outbound_ready_filters_.end ())
        return 0;

    for (std::map<std::string, std::set<std::string> >::const_iterator it =
           target_it->second.begin ();
         it != target_it->second.end (); ++it) {
        if (it->first.empty ())
            continue;
        if (send_control_snapshot (socket_,
                                   spot_control_protocol::ctrl_ready_ack_topic,
                                   target_endpoint_, it->first,
                                   it->second)
            != 0)
            return -1;
    }

    return 0;
}

static bool parse_ready_ack_arg (const std::string &arg_,
                                 std::string *target_endpoint_out_,
                                 std::string *raw_filter_out_,
                                 std::string *ack_source_id_out_)
{
    if (!target_endpoint_out_ || !raw_filter_out_ || !ack_source_id_out_)
        return false;

    const size_t first = arg_.find ('\n');
    if (first == std::string::npos)
        return false;
    const size_t second = arg_.find ('\n', first + 1);
    if (second == std::string::npos)
        return false;

    *target_endpoint_out_ = arg_.substr (0, first);
    *raw_filter_out_ = arg_.substr (first + 1, second - first - 1);
    *ack_source_id_out_ = arg_.substr (second + 1);
    return !target_endpoint_out_->empty () && !raw_filter_out_->empty ()
           && !ack_source_id_out_->empty ();
}

static int recv_remaining_frame_strings (socket_base_t *socket_,
                                         std::vector<std::string> *out_)
{
    if (!socket_ || !out_) {
        errno = EINVAL;
        return -1;
    }

    out_->clear ();
    while (true) {
        msg_t frame;
        if (frame.init () != 0)
            return -1;
        if (socket_->recv (&frame, 0) != 0) {
            frame.close ();
            return -1;
        }
        out_->push_back (std::string (
          static_cast<const char *> (frame.data ()), frame.size ()));
        const bool more = (frame.flags () & msg_t::more) != 0;
        frame.close ();
        if (!more)
            break;
    }
    return 0;
}

static uint64_t default_bootstrap_broadcast_interval_ms (
  const spot_runtime_t *runtime_)
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
}

int spot_data_plane_protocol_t::recv_ascii_command (
  socket_base_t *socket_, std::vector<std::string> *frames_)
{
    if (!frames_)
        return -1;
    frames_->clear ();
    while (true) {
        msg_t frame;
        if (frame.init () != 0)
            return -1;
        if (socket_->recv (&frame, 0) != 0) {
            frame.close ();
            return -1;
        }
        frames_->push_back (std::string (
          static_cast<const char *> (frame.data ()), frame.size ()));
        const bool more = (frame.flags () & msg_t::more) != 0;
        frame.close ();
        if (!more)
            break;
    }
    return frames_->empty () ? -1 : 0;
}

int spot_data_plane_protocol_t::send_subscription_update (
  socket_base_t *socket_, const std::string &raw_filter_, bool subscribe_)
{
    if (!socket_) {
        errno = EFAULT;
        return -1;
    }

    msg_t msg;
    if (msg.init_size (raw_filter_.size () + 1) != 0)
        return -1;

    unsigned char *data = static_cast<unsigned char *> (msg.data ());
    data[0] = subscribe_ ? 1 : 0;
    if (!raw_filter_.empty ())
        memcpy (data + 1, raw_filter_.data (), raw_filter_.size ());

    const int rc = socket_->send (&msg, 0);
    msg.close ();
    return rc;
}

int spot_data_plane_protocol_t::send_errno_reply (socket_base_t *socket_,
                                                  int error_)
{
    char buf[32];
    snprintf (buf, sizeof (buf), "%d", error_);
    if (send_ascii_frame (socket_, "error", ZLINK_SNDMORE) != 0)
        return -1;
    return send_ascii_frame (socket_, buf, 0);
}

int spot_data_plane_protocol_t::send_ok_reply (socket_base_t *socket_)
{
    return send_ascii_frame (socket_, "ok", 0);
}

uint64_t spot_data_plane_protocol_t::resolve_bootstrap_broadcast_interval_ms (
  const spot_runtime_t *runtime_, bool bootstrap_ready_)
{
    static uint64_t env_cached = 0;
    static bool env_checked = false;
    if (env_checked)
        return env_cached != 0 ? env_cached
                               : (bootstrap_ready_
                                    ? default_bootstrap_broadcast_interval_ms (
                                        runtime_)
                                    : 1000);

    uint64_t value = 0;
    const char *env = getenv ("ZLINK_SPOT_BOOTSTRAP_INTERVAL_MS");
    if (env && *env) {
        char *end = NULL;
        const unsigned long parsed = strtoul (env, &end, 10);
        if (end != env && parsed > 0)
            value = static_cast<uint64_t> (parsed);
    }

    env_cached = value;
    env_checked = true;
    return env_cached != 0 ? env_cached
                           : (bootstrap_ready_
                                ? default_bootstrap_broadcast_interval_ms (
                                    runtime_)
                                : 1000);
}

int spot_data_plane_protocol_t::publish_bootstrap_descriptor (
  socket_base_t *mesh_pub_, spot_node_t *node_, spot_runtime_t *runtime_)
{
    if (!mesh_pub_ || !node_ || !runtime_ || runtime_->peer_ctrl_endpoint.empty ())
        return 0;

    const std::string public_data_endpoint = node_->public_endpoint ();
    if (public_data_endpoint.empty ())
        return 0;

    const std::string source_node_id =
      spot_control_protocol::node_id_string (runtime_->node_id);
    const std::string version =
      spot_control_protocol::node_id_string (
        static_cast<uint32_t> (spot_control_protocol::protocol_version));

    if (send_ascii_frame (mesh_pub_,
                          spot_control_protocol::bootstrap_ctrl_descriptor_topic,
                          ZLINK_SNDMORE)
          != 0
        || send_ascii_frame (mesh_pub_, public_data_endpoint, ZLINK_SNDMORE)
             != 0
        || send_ascii_frame (mesh_pub_, runtime_->peer_ctrl_endpoint,
                             ZLINK_SNDMORE)
             != 0
        || send_ascii_frame (mesh_pub_, source_node_id, ZLINK_SNDMORE) != 0
        || send_ascii_frame (mesh_pub_, version, 0) != 0) {
        return -1;
    }

    spot_ctrl_debugf ("broadcast bootstrap data=%s ctrl=%s",
                      public_data_endpoint.c_str (),
                      runtime_->peer_ctrl_endpoint.c_str ());

    return 0;
}

bool spot_data_plane_protocol_t::should_publish_bootstrap_descriptor (
  const spot_runtime_t *runtime_,
  bool bootstrap_ready_,
  uint64_t last_published_peer_version_)
{
    if (!runtime_ || !bootstrap_ready_)
        return true;

    const uint32_t ready_peer_count =
      connected_ready_peer_count (&runtime_->execution.mesh_peer_state);
    if (ready_peer_count == 0)
        return true;

    return mesh_peer_version (&runtime_->execution.mesh_peer_state)
           != last_published_peer_version_;
}

void spot_data_plane_protocol_t::sync_mesh_xsub_connected_endpoint (
  spot_runtime_t *runtime_, const zlink_monitor_event_t &raw_)
{
    if (!runtime_ || raw_.remote_addr[0] == '\0')
        return;

    if (std::getenv ("ZLINK_DEBUG_SPOT_CONTROL")) {
        std::fprintf (stderr,
                      "[spot-control] mesh-monitor node=%p event=%llu remote=%s\n",
                      runtime_->owner,
                      static_cast<unsigned long long> (raw_.event),
                      raw_.remote_addr);
        std::fflush (stderr);
    }

    const bool changed =
      sync_mesh_peer_monitor_state (&runtime_->execution.mesh_peer_state, raw_);
    if (std::getenv ("ZLINK_DEBUG_SPOT_CONTROL")) {
        std::fprintf (
          stderr,
          "[spot-control] mesh-monitor node=%p changed=%d version=%llu\n",
          runtime_->owner,
          changed ? 1 : 0,
          static_cast<unsigned long long> (
            mesh_peer_version (&runtime_->execution.mesh_peer_state)));
        std::fflush (stderr);
    }
    if (!changed)
        return;
    if (runtime_->owner)
        spot_node_access_t::wake_control_task (runtime_->owner);
}

void spot_data_plane_protocol_t::clear_mesh_xsub_connected_endpoints (
  spot_runtime_t *runtime_)
{
    if (!runtime_)
        return;

    const bool changed =
      clear_mesh_peer_monitor_state (&runtime_->execution.mesh_peer_state);
    if (changed && runtime_->owner)
        spot_node_access_t::wake_control_task (runtime_->owner);
}

void spot_data_plane_protocol_t::clear_snapshot_sources (
  spot_node_t *node_, spot_data_plane_protocol_state_t *state_)
{
    if (!node_ || !state_)
        return;

    const std::string self_endpoint = node_->public_endpoint ();
    for (std::map<std::string, std::set<std::string> >::iterator it =
           state_->peer_ready_filters.begin ();
         it != state_->peer_ready_filters.end (); ++it) {
        if (self_endpoint.empty ())
            continue;
        for (std::set<std::string>::const_iterator filter_it =
               it->second.begin ();
             filter_it != it->second.end (); ++filter_it) {
            node_->notify_pub_delivery_ready_ack (self_endpoint, *filter_it,
                                                  it->first, false);
        }
    }
    state_->peer_ready_filters.clear ();
}

int spot_data_plane_protocol_t::decode_batch_frame (
  const std::string &topic_,
  const std::vector<std::string> &frames_,
  spot_data_plane_protocol_state_t *state_,
  bool *is_batch_out_)
{
    if (is_batch_out_)
        *is_batch_out_ = false;
    if (!state_) {
        errno = EINVAL;
        return -1;
    }

    if (frames_.size () != 3)
        return 0;
    if (frames_[0].size () != spot_batch_header_size_v1)
        return 0;

    const unsigned char *header =
      reinterpret_cast<const unsigned char *> (frames_[0].data ());
    const uint32_t magic = read_u32_le (header);
    const uint16_t version = read_u16_le (header + 4);
    const uint32_t header_size = read_u32_le (header + 8);
    if (magic != spot_batch_magic_v1 || version != spot_batch_version_v1
        || header_size != spot_batch_header_size_v1) {
        return 0;
    }

    if (is_batch_out_)
        *is_batch_out_ = true;
    if (frames_[1].size () != spot_batch_metadata_size_v1) {
        warn_malformed_batch (topic_, "metadata-size");
        errno = EBADMSG;
        return -1;
    }

    const unsigned char *metadata =
      reinterpret_cast<const unsigned char *> (frames_[1].data ());
    const uint32_t message_count = read_u32_le (metadata);
    const uint32_t encoded_bytes = read_u32_le (metadata + 8);
    if (message_count == 0) {
        warn_malformed_batch (topic_, "message-count-zero");
        errno = EBADMSG;
        return -1;
    }
    if (encoded_bytes != frames_[2].size ()) {
        warn_malformed_batch (topic_, "encoded-bytes-mismatch");
        errno = EBADMSG;
        return -1;
    }

    state_->pending_unbatch.active = true;
    state_->pending_unbatch.topic = topic_;
    state_->pending_unbatch.body = frames_[2];
    state_->pending_unbatch.total_message_count = message_count;
    state_->pending_unbatch.decoded_message_index = 0;
    state_->pending_unbatch.decode_offset = 0;
    return 0;
}

int spot_data_plane_protocol_t::resume_pending_unbatch (
  socket_base_t *fanout_,
  const spot_runtime_t *runtime_,
  spot_data_plane_protocol_state_t *state_)
{
    if (!fanout_ || !runtime_ || !state_) {
        errno = EINVAL;
        return -1;
    }
    if (!state_->pending_unbatch.active)
        return 0;

    const spot_node_batch_config_t config = runtime_->peer_batch_config_snapshot ();
    const size_t max_messages =
      static_cast<size_t> (config.unbatch_max_messages_per_turn);
    const size_t max_bytes =
      static_cast<size_t> (config.unbatch_max_bytes_per_turn);
    size_t processed_messages = 0;
    size_t processed_bytes = 0;

    while (state_->pending_unbatch.active && processed_messages < max_messages
           && processed_bytes < max_bytes) {
        const std::string &body = state_->pending_unbatch.body;
        const size_t offset = state_->pending_unbatch.decode_offset;
        if (offset + 4 > body.size ()) {
            warn_malformed_batch (state_->pending_unbatch.topic,
                                  "message-length-overflow");
            state_->pending_unbatch.clear ();
            return 0;
        }

        const unsigned char *cursor =
          reinterpret_cast<const unsigned char *> (body.data ()) + offset;
        const uint32_t message_encoded_bytes = read_u32_le (cursor);
        const size_t message_begin = offset + 4;
        const size_t message_end = message_begin + message_encoded_bytes;
        if (message_end > body.size () || message_encoded_bytes < 2) {
            warn_malformed_batch (state_->pending_unbatch.topic,
                                  "message-body-overflow");
            state_->pending_unbatch.clear ();
            return 0;
        }

        size_t message_offset = message_begin;
        const uint16_t part_count = read_u16_le (
          reinterpret_cast<const unsigned char *> (body.data ()) + message_offset);
        message_offset += 2;
        std::vector<std::string> parts;
        parts.reserve (part_count);

        for (uint16_t i = 0; i < part_count; ++i) {
            if (message_offset + 4 > message_end) {
                warn_malformed_batch (state_->pending_unbatch.topic,
                                      "part-size-overflow");
                state_->pending_unbatch.clear ();
                return 0;
            }
            const uint32_t part_size = read_u32_le (
              reinterpret_cast<const unsigned char *> (body.data ())
              + message_offset);
            message_offset += 4;
            if (message_offset + part_size > message_end) {
                warn_malformed_batch (state_->pending_unbatch.topic,
                                      "part-payload-overflow");
                state_->pending_unbatch.clear ();
                return 0;
            }
            parts.push_back (body.substr (message_offset, part_size));
            message_offset += part_size;
        }

        if (message_offset != message_end) {
            warn_malformed_batch (state_->pending_unbatch.topic,
                                  "message-size-mismatch");
            state_->pending_unbatch.clear ();
            return 0;
        }

        if (publish_owned_parts (fanout_, state_->pending_unbatch.topic, parts)
            != 0) {
            return -1;
        }

        processed_messages += 1;
        processed_bytes += 4 + message_encoded_bytes;
        state_->pending_unbatch.decoded_message_index += 1;
        state_->pending_unbatch.decode_offset = message_end;

        if (state_->pending_unbatch.decoded_message_index
            == state_->pending_unbatch.total_message_count) {
            if (state_->pending_unbatch.decode_offset != body.size ()) {
                warn_malformed_batch (state_->pending_unbatch.topic,
                                      "tail-bytes-mismatch");
            }
            state_->pending_unbatch.clear ();
            break;
        }
    }

    return 0;
}

int spot_data_plane_protocol_t::recv_and_process_ctrl_messages (
  socket_base_t *ctrl_sub_,
  spot_node_t *node_,
  spot_data_plane_protocol_state_t *state_)
{
    if (!ctrl_sub_ || !node_ || !state_) {
        errno = EFAULT;
        return -1;
    }

    static const unsigned int ctrl_poll_batch_limit = 64;

    unsigned int processed = 0;
    while (processed < ctrl_poll_batch_limit) {
        msg_t topic_msg;
        if (topic_msg.init () != 0)
            return -1;
        if (ctrl_sub_->recv (&topic_msg, ZLINK_DONTWAIT) != 0) {
            const int err = errno;
            topic_msg.close ();
            if (err == EAGAIN)
                return 0;
            return -1;
        }

        const std::string topic (
          static_cast<const char *> (topic_msg.data ()), topic_msg.size ());
        std::vector<std::string> frames;
        if (recv_remaining_frame_strings (ctrl_sub_, &frames) != 0) {
            topic_msg.close ();
            return -1;
        }
        topic_msg.close ();
        ++processed;

        const bool is_subscription_snapshot =
          spot_control_protocol::is_ctrl_snapshot_topic (topic);
        const bool is_ready_ack_snapshot =
          spot_control_protocol::is_ctrl_ready_ack_topic (topic);
        if ((!is_subscription_snapshot && !is_ready_ack_snapshot)
            || frames.size () < 3) {
            continue;
        }

        const std::string &target_endpoint = frames[0];
        const std::string &source_key = frames[1];
        if (target_endpoint.empty () || source_key.empty ())
            continue;

        std::set<std::string> new_filters;
        for (size_t i = 3; i < frames.size (); ++i) {
            if (!frames[i].empty ())
                new_filters.insert (frames[i]);
        }

        if (!is_ready_ack_snapshot)
            continue;

        std::set<std::string> &previous_filters =
          state_->peer_ready_filters[source_key];

        for (std::set<std::string>::const_iterator it =
               previous_filters.begin ();
             it != previous_filters.end (); ++it) {
            if (new_filters.count (*it) != 0)
                continue;
            node_->notify_pub_delivery_ready_ack (target_endpoint, *it,
                                                  source_key, false);
        }

        for (std::set<std::string>::const_iterator it = new_filters.begin ();
             it != new_filters.end (); ++it) {
            if (previous_filters.count (*it) != 0)
                continue;
            node_->notify_pub_delivery_ready_ack (target_endpoint, *it,
                                                  source_key, true);
        }

        spot_ctrl_debugf ("recv snapshot target=%s source=%s filters=%zu",
                          target_endpoint.c_str (), source_key.c_str (),
                          static_cast<size_t> (new_filters.size ()));

        previous_filters.swap (new_filters);
        if (previous_filters.empty ())
            state_->peer_ready_filters.erase (source_key);
    }

    return 0;
}

int spot_data_plane_protocol_t::recv_and_dispatch_mesh_xsub (
  socket_base_t *mesh_xsub_,
  socket_base_t *fanout_,
  socket_base_t *peer_ctrl_pub_,
  spot_runtime_t *runtime_,
  spot_node_t *node_,
  spot_data_plane_protocol_state_t *state_)
{
    if (!mesh_xsub_ || !fanout_ || !peer_ctrl_pub_ || !runtime_ || !node_
        || !state_) {
        errno = EFAULT;
        return -1;
    }
    if (state_->pending_unbatch.active)
        return 0;

    unsigned int processed = 0;
    size_t processed_bytes = 0;
    for (;;) {
        std::string topic;
        spot_owned_msg_parts_t frames;
        if (spot_recv_logical_message_parts (
              mesh_xsub_, true, &topic, &frames, &processed_bytes)
            != 0) {
            if (errno == EAGAIN)
                return 0;
            return -1;
        }

        const char *topic_data = topic.data ();
        const size_t topic_size = topic.size ();

        if (!spot_control_protocol::is_bootstrap_ctrl_descriptor_topic (
              topic_data, topic_size)) {
            std::vector<std::string> frame_strings;
            spot_copy_msg_parts_to_strings (frames, &frame_strings);
            bool is_batch = false;
            const int decode_rc =
              decode_batch_frame (topic, frame_strings, state_, &is_batch);
            if (decode_rc != 0) {
                spot_clear_msg_parts (&frames);
                if (errno == EBADMSG)
                    errno = 0;
                else
                    return -1;
            } else if (!is_batch) {
                if (spot_publish_msg_parts_consume (fanout_, topic, &frames)
                    != 0) {
                    return -1;
                }
            } else if (resume_pending_unbatch (fanout_, runtime_, state_) != 0) {
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

        if (frames.size () < 4) {
            spot_clear_msg_parts (&frames);
            continue;
        }

        const std::string peer_data_endpoint =
          spot_msg_frame_to_string (frames[0]);
        const std::string peer_ctrl_endpoint =
          spot_msg_frame_to_string (frames[1]);
        spot_clear_msg_parts (&frames);
        if (peer_data_endpoint.empty () || peer_ctrl_endpoint.empty ())
            continue;

        const std::map<std::string, std::string>::iterator existing =
          state_->peer_ctrl_endpoints.find (peer_data_endpoint);
        const bool changed = existing == state_->peer_ctrl_endpoints.end ()
                             || existing->second != peer_ctrl_endpoint;
        if (!changed)
            continue;

        if (existing != state_->peer_ctrl_endpoints.end ()
            && !existing->second.empty ()) {
            (void) peer_ctrl_pub_->term_endpoint (existing->second.c_str ());
        }

        if (peer_ctrl_pub_->connect (peer_ctrl_endpoint.c_str ()) != 0)
            return -1;

        state_->peer_ctrl_endpoints[peer_data_endpoint] = peer_ctrl_endpoint;

        spot_ctrl_debugf ("connect peer ctrl data=%s ctrl=%s",
                          peer_data_endpoint.c_str (),
                          peer_ctrl_endpoint.c_str ());
        if (send_snapshot_to_target (peer_ctrl_pub_, node_, peer_data_endpoint)
            != 0) {
            return -1;
        }
        if (send_ready_ack_snapshots_to_target (peer_ctrl_pub_,
                                                peer_data_endpoint,
                                                state_->outbound_ready_filters)
            != 0) {
            return -1;
        }
        ++processed;
        if (processed >= mesh_xsub_forward_batch_limit
            || processed_bytes >= mesh_xsub_forward_batch_bytes_limit)
            return 0;
    }
}

int spot_data_plane_protocol_t::handle_ctrl_command (
  socket_base_t *ctrl_,
  spot_node_t *node_,
  spot_runtime_t *runtime_,
  socket_base_t *mesh_pub_,
  socket_base_t *mesh_xsub_,
  socket_base_t *peer_ctrl_pub_,
  socket_base_t *peer_ctrl_sub_,
  const std::vector<std::string> &frames_,
  spot_data_plane_protocol_state_t *state_,
  bool *running_out_)
{
    if (!ctrl_ || !node_ || !runtime_ || !mesh_pub_ || !mesh_xsub_
        || !peer_ctrl_pub_ || !peer_ctrl_sub_ || !state_ || !running_out_) {
        errno = EFAULT;
        return -1;
    }

    const std::string verb = frames_.empty () ? std::string () : frames_[0];
    const std::string arg = frames_.size () > 1 ? frames_[1] : std::string ();

    if (verb == "terminate") {
        if (send_ok_reply (ctrl_) != 0)
            return -1;
        *running_out_ = false;
        return 0;
    }

    if (verb == "bind_pub") {
        std::string cert;
        std::string key;
        {
            scoped_lock_t lock (node_->_sync);
            cert = node_->_tls_cert;
            key = node_->_tls_key;
        }

        const int mesh_pub_sndhwm =
          spot_mesh_pub_budget_t::resolve_initial_bind_sndhwm (runtime_, arg);

        // Step 1: bind mesh_pub first (supports port 0 / ephemeral).
        if (mesh_pub_->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM,
                                   &mesh_pub_sndhwm,
                                   sizeof (mesh_pub_sndhwm))
              != 0
            || spot_node_t::apply_tls_server (mesh_pub_, cert, key) != 0
            || spot_node_t::apply_tls_server (peer_ctrl_sub_, cert, key) != 0
            || mesh_pub_->bind (arg.c_str ()) != 0) {
            if (send_errno_reply (ctrl_, errno != 0 ? errno : EIO) != 0)
                return -1;
            return 0;
        }

        // Step 2: resolve the actual bound endpoint.
        std::string resolved_endpoint = arg;
        {
            char resolved[256] = {0};
            size_t resolved_size = sizeof (resolved);
            if (mesh_pub_->getsockopt (ZLINK_INTERNAL_OPT_LAST_ENDPOINT,
                                       resolved, &resolved_size)
                == 0) {
                const size_t len =
                  resolved_size > 0 ? strnlen (resolved, resolved_size) : 0;
                if (len > 0)
                    resolved_endpoint.assign (resolved, len);
            }
        }

        // Step 3: derive ctrl endpoint from the resolved endpoint.
        std::string ctrl_bind_endpoint;
        if (!spot_control_protocol::derive_peer_ctrl_bind_endpoint (
              resolved_endpoint, runtime_->node_id, &ctrl_bind_endpoint)) {
            if (send_errno_reply (ctrl_, EINVAL) != 0)
                return -1;
            return 0;
        }

        if (peer_ctrl_sub_->bind (ctrl_bind_endpoint.c_str ()) != 0) {
            if (send_errno_reply (ctrl_, errno != 0 ? errno : EIO) != 0)
                return -1;
            return 0;
        }

        runtime_->peer_ctrl_endpoint = ctrl_bind_endpoint;
        runtime_->bound_endpoint = resolved_endpoint;
        {
            scoped_lock_t lock (node_->_sync);
            node_->_bound_endpoint = resolved_endpoint;
            node_->_server_tls_locked = true;
        }
        return send_ok_reply (ctrl_);
    }

    if (verb == "connect_peer_pub") {
        std::string ca;
        std::string host;
        int trust = 0;
        {
            scoped_lock_t lock (node_->_sync);
            ca = node_->_tls_ca;
            host = node_->_tls_hostname;
            trust = node_->_tls_trust_system;
        }
        if (spot_node_t::apply_tls_client (mesh_xsub_, ca, host, trust) != 0
            || spot_node_t::apply_tls_client (peer_ctrl_pub_, ca, host, trust)
                 != 0
            || mesh_xsub_->connect (arg.c_str ()) != 0
            || send_subscription_update (mesh_xsub_, "", true) != 0) {
            (void) mesh_xsub_->term_endpoint (arg.c_str ());
            if (send_errno_reply (ctrl_, errno) != 0)
                return -1;
            return 0;
        }

        std::string peer_ctrl_endpoint;
        if (spot_control_protocol::derive_peer_ctrl_bind_endpoint (
              arg, runtime_->node_id, &peer_ctrl_endpoint)
            && peer_ctrl_endpoint.compare (0, 9, "inproc://") != 0) {
            const std::map<std::string, std::string>::iterator it =
              state_->peer_ctrl_endpoints.find (arg);
            if (it == state_->peer_ctrl_endpoints.end ()
                || it->second != peer_ctrl_endpoint) {
                if (it != state_->peer_ctrl_endpoints.end ()
                    && !it->second.empty ()) {
                    (void) peer_ctrl_pub_->term_endpoint (it->second.c_str ());
                }
                if (peer_ctrl_pub_->connect (peer_ctrl_endpoint.c_str ()) != 0
                    || send_snapshot_to_target (peer_ctrl_pub_, node_, arg) != 0
                    || send_ready_ack_snapshots_to_target (
                         peer_ctrl_pub_, arg,
                         state_->outbound_ready_filters)
                         != 0) {
                    (void) mesh_xsub_->term_endpoint (arg.c_str ());
                    if (send_errno_reply (ctrl_,
                                          errno != 0 ? errno : EIO)
                        != 0) {
                        return -1;
                    }
                    return 0;
                }
                state_->peer_ctrl_endpoints[arg] = peer_ctrl_endpoint;
            }
        }
        {
            scoped_lock_t lock (node_->_sync);
            node_->_mesh_client_tls_locked = true;
        }
        return send_ok_reply (ctrl_);
    }

    if (verb == "replay_subscriptions" || verb == "subscription_subscribe"
        || verb == "subscription_unsubscribe") {
        if (send_snapshot_to_peers (peer_ctrl_pub_, node_,
                                    state_->peer_ctrl_endpoints)
            != 0) {
            if (send_errno_reply (ctrl_, errno) != 0)
                return -1;
            return 0;
        }
        return send_ok_reply (ctrl_);
    }

    if (verb == "ready_ack_subscribe" || verb == "ready_ack_unsubscribe") {
        std::string target_endpoint;
        std::string raw_filter;
        std::string ack_source_id;
        if (!parse_ready_ack_arg (arg, &target_endpoint, &raw_filter,
                                  &ack_source_id)) {
            if (send_errno_reply (ctrl_, EINVAL) != 0)
                return -1;
            return 0;
        }

        spot_ready_ack_ctrl_debugf (
          "command verb=%s target=%s filter=%s source=%s", verb.c_str (),
          target_endpoint.c_str (), raw_filter.c_str (),
          ack_source_id.c_str ());

        std::set<std::string> filters;
        {
            std::set<std::string> &source_filters =
              state_->outbound_ready_filters[target_endpoint][ack_source_id];
            if (verb == "ready_ack_subscribe")
                source_filters.insert (raw_filter);
            else
                source_filters.erase (raw_filter);

            if (source_filters.empty ())
                state_->outbound_ready_filters[target_endpoint].erase (
                  ack_source_id);
            if (state_->outbound_ready_filters[target_endpoint].empty ())
                state_->outbound_ready_filters.erase (target_endpoint);
            else
                filters = source_filters;
        }

        if (send_control_snapshot (peer_ctrl_pub_,
                                   spot_control_protocol::ctrl_ready_ack_topic,
                                   target_endpoint, ack_source_id, filters)
            != 0) {
            if (send_errno_reply (ctrl_, errno) != 0)
                return -1;
            return 0;
        }
        return send_ok_reply (ctrl_);
    }

    if (verb == "unbind_pub") {
        clear_snapshot_sources (node_, state_);
        state_->outbound_ready_filters.clear ();
        for (std::map<std::string, std::string>::iterator it =
               state_->peer_ctrl_endpoints.begin ();
             it != state_->peer_ctrl_endpoints.end (); ++it) {
            if (!it->second.empty ())
                (void) peer_ctrl_pub_->term_endpoint (it->second.c_str ());
        }
        state_->peer_ctrl_endpoints.clear ();
        if (!runtime_->peer_ctrl_endpoint.empty ())
            (void) peer_ctrl_sub_->term_endpoint (
              runtime_->peer_ctrl_endpoint.c_str ());
        runtime_->peer_ctrl_endpoint.clear ();
        runtime_->bound_endpoint.clear ();
        spot_mesh_pub_budget_t::reset_runtime_state (runtime_);
        if (mesh_pub_->term_endpoint (arg.c_str ()) != 0) {
            if (send_errno_reply (ctrl_, errno) != 0)
                return -1;
            return 0;
        }
        return send_ok_reply (ctrl_);
    }

    if (verb == "disconnect_peer_pub") {
        const std::map<std::string, std::string>::iterator it =
          state_->peer_ctrl_endpoints.find (arg);
        if (it != state_->peer_ctrl_endpoints.end ()) {
            const std::map<std::string,
                           std::map<std::string, std::set<std::string> > >::iterator
              ready_it = state_->outbound_ready_filters.find (arg);
            if (ready_it != state_->outbound_ready_filters.end ()) {
                std::set<std::string> empty_filters;
                for (std::map<std::string,
                              std::set<std::string> >::const_iterator
                       source_it = ready_it->second.begin ();
                     source_it != ready_it->second.end (); ++source_it) {
                    (void) send_control_snapshot (
                      peer_ctrl_pub_, spot_control_protocol::ctrl_ready_ack_topic,
                      arg, source_it->first, empty_filters);
                }
            }
            std::set<std::string> empty_filters;
            (void) send_control_snapshot (
              peer_ctrl_pub_, spot_control_protocol::ctrl_snapshot_topic, arg,
              spot_control_protocol::node_id_string (runtime_->node_id),
              empty_filters);
            state_->outbound_ready_filters.erase (arg);
            (void) peer_ctrl_pub_->term_endpoint (it->second.c_str ());
            state_->peer_ctrl_endpoints.erase (it);
        }
        if (mesh_xsub_->term_endpoint (arg.c_str ()) != 0) {
            if (send_errno_reply (ctrl_, errno) != 0)
                return -1;
            return 0;
        }

        if (remove_connected_mesh_peer_endpoint (
              &runtime_->execution.mesh_peer_state, arg)
            && runtime_->owner) {
            spot_node_access_t::wake_control_task (runtime_->owner);
        }
        return send_ok_reply (ctrl_);
    }

    if (send_errno_reply (ctrl_, EINVAL) != 0)
        return -1;
    return 0;
}
}
