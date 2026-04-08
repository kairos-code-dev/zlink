/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_data_plane_internal.hpp"

#include "core/multipart_send_txn.hpp"
#include "services/spot/spot_control_protocol.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_runtime.hpp"
#include "sockets/socket_base.hpp"
#include "utils/clock.hpp"
#include "utils/err.hpp"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

namespace zlink
{
namespace
{
static const unsigned int ingress_forward_batch_limit = 2048;
static const size_t ingress_forward_batch_bytes_limit = 16 * 1024 * 1024;
static const uint32_t spot_batch_magic_v1 = 0x31544253u;
static const uint16_t spot_batch_version_v1 = 1;
static const uint32_t spot_batch_header_size_v1 = 12;
static const uint32_t spot_batch_metadata_size_v1 = 16;

void append_u16_le (std::string *out_, uint16_t value_)
{
    if (!out_)
        return;
    out_->push_back (static_cast<char> (value_ & 0xff));
    out_->push_back (static_cast<char> ((value_ >> 8) & 0xff));
}

void append_u32_le (std::string *out_, uint32_t value_)
{
    if (!out_)
        return;
    out_->push_back (static_cast<char> (value_ & 0xff));
    out_->push_back (static_cast<char> ((value_ >> 8) & 0xff));
    out_->push_back (static_cast<char> ((value_ >> 16) & 0xff));
    out_->push_back (static_cast<char> ((value_ >> 24) & 0xff));
}

int publish_owned_parts (socket_base_t *socket_,
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

int recv_logical_message (socket_base_t *src_,
                          bool dontwait_topic_,
                          std::string *topic_out_,
                          std::vector<std::string> *parts_out_,
                          size_t *wire_bytes_out_)
{
    if (!src_ || !topic_out_ || !parts_out_ || !wire_bytes_out_) {
        errno = EINVAL;
        return -1;
    }

    topic_out_->clear ();
    parts_out_->clear ();
    *wire_bytes_out_ = 0;

    msg_t topic_msg;
    if (topic_msg.init () != 0)
        return -1;

    if (src_->recv (&topic_msg, dontwait_topic_ ? ZLINK_DONTWAIT : 0) != 0) {
        const int err = errno;
        topic_msg.close ();
        errno = err;
        return -1;
    }

    *topic_out_ = std::string (static_cast<const char *> (topic_msg.data ()),
                               topic_msg.size ());
    *wire_bytes_out_ += topic_msg.size ();
    bool more = (topic_msg.flags () & msg_t::more) != 0;
    topic_msg.close ();

    while (more) {
        msg_t frame;
        if (frame.init () != 0)
            return -1;
        if (src_->recv (&frame, 0) != 0) {
            const int err = errno;
            frame.close ();
            errno = err;
            return -1;
        }

        parts_out_->push_back (
          std::string (static_cast<const char *> (frame.data ()), frame.size ()));
        *wire_bytes_out_ += frame.size ();
        more = (frame.flags () & msg_t::more) != 0;
        frame.close ();
    }

    return 0;
}

bool bucket_flush_due (const spot_topic_batch_bucket_t &bucket_,
                       const spot_node_batch_config_t &config_,
                       uint64_t now_ms_)
{
    if (bucket_.messages.empty ())
        return false;
    if (config_.delay_ms == 0)
        return true;
    return now_ms_ >= bucket_.first_enqueue_ms
                      + static_cast<uint64_t> (config_.delay_ms);
}

int flush_bucket (spot_data_plane_runtime_state_t *state_,
                  socket_base_t *mesh_pub_,
                  std::map<std::string, spot_topic_batch_bucket_t>::iterator it_)
{
    if (!state_ || !mesh_pub_ || it_ == state_->pending_peer_batches.end ()) {
        errno = EINVAL;
        return -1;
    }

    std::vector<std::string> batch_parts;
    if (spot_data_plane_forwarder_t::encode_batch_frames (it_->second,
                                                          &batch_parts)
        != 0) {
        return -1;
    }
    if (publish_owned_parts (mesh_pub_, it_->second.topic, batch_parts) != 0)
        return -1;

    state_->pending_peer_batches.erase (it_);
    return 0;
}
}

void spot_data_plane_forwarder_t::pump_socket_commands (socket_base_t *socket_)
{
    if (!socket_)
        return;

    uint32_t ignored = 0;
    const int rc = socket_->get_events_internal (0, &ignored);
    if (rc == 0)
        return;

    if (errno == EINTR || errno == ETERM)
        return;

    errno_assert (false);
}

int spot_data_plane_forwarder_t::resolve_internal_hwm_override (
  const char *env_name_, int default_value_)
{
    if (!env_name_ || env_name_[0] == '\0')
        return default_value_;

    const char *value = getenv (env_name_);
    if (!value || value[0] == '\0')
        return default_value_;

    char *end = NULL;
    errno = 0;
    const long parsed = strtol (value, &end, 10);
    if (errno != 0 || end == value)
        return default_value_;
    if (parsed < 0)
        return 0;
    if (parsed > INT_MAX)
        return INT_MAX;
    return static_cast<int> (parsed);
}

size_t spot_data_plane_forwarder_t::logical_message_payload_bytes (
  const std::vector<std::string> &parts_)
{
    size_t bytes = 0;
    for (size_t i = 0; i < parts_.size (); ++i)
        bytes += parts_[i].size ();
    return bytes;
}

uint32_t spot_data_plane_forwarder_t::logical_message_encoded_bytes (
  const std::vector<std::string> &parts_)
{
    size_t bytes = sizeof (uint16_t);
    for (size_t i = 0; i < parts_.size (); ++i)
        bytes += sizeof (uint32_t) + parts_[i].size ();
    return static_cast<uint32_t> (
      bytes > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t> (bytes));
}

bool spot_data_plane_forwarder_t::is_immediate_peer_topic (
  const std::string &topic_)
{
    if (topic_.empty ())
        return true;
    return spot_control_protocol::is_reserved_subject (topic_.c_str (),
                                                       topic_.size ());
}

uint64_t spot_data_plane_forwarder_t::next_flush_deadline_ms (
  const spot_data_plane_runtime_state_t *state_,
  const spot_node_batch_config_t &config_)
{
    if (!state_ || !config_.enabled || config_.delay_ms < 0)
        return 0;

    uint64_t next_deadline = 0;
    for (std::map<std::string, spot_topic_batch_bucket_t>::const_iterator it =
           state_->pending_peer_batches.begin ();
         it != state_->pending_peer_batches.end (); ++it) {
        if (it->second.messages.empty ())
            continue;
        const uint64_t deadline =
          it->second.first_enqueue_ms + static_cast<uint64_t> (config_.delay_ms);
        if (next_deadline == 0 || deadline < next_deadline)
            next_deadline = deadline;
    }
    return next_deadline;
}

int spot_data_plane_forwarder_t::flush_due_buckets (
  spot_runtime_t *runtime_,
  spot_data_plane_runtime_state_t *state_,
  socket_base_t *mesh_pub_,
  uint64_t now_ms_)
{
    if (!runtime_ || !state_ || !mesh_pub_)
        return 0;

    const spot_node_batch_config_t config = runtime_->peer_batch_config_snapshot ();
    if (!config.enabled)
        return 0;

    for (std::map<std::string, spot_topic_batch_bucket_t>::iterator it =
           state_->pending_peer_batches.begin ();
         it != state_->pending_peer_batches.end ();) {
        if (!bucket_flush_due (it->second, config, now_ms_)) {
            ++it;
            continue;
        }

        std::map<std::string, spot_topic_batch_bucket_t>::iterator current = it;
        ++it;
        if (flush_bucket (state_, mesh_pub_, current) != 0)
            return -1;
    }

    return 0;
}

int spot_data_plane_forwarder_t::flush_all_buckets (
  spot_runtime_t *runtime_,
  spot_data_plane_runtime_state_t *state_,
  socket_base_t *mesh_pub_)
{
    if (!runtime_ || !state_ || !mesh_pub_)
        return 0;

    while (!state_->pending_peer_batches.empty ()) {
        if (flush_bucket (state_, mesh_pub_,
                          state_->pending_peer_batches.begin ())
            != 0) {
            return -1;
        }
    }
    return 0;
}

int spot_data_plane_forwarder_t::encode_batch_frames (
  const spot_topic_batch_bucket_t &bucket_,
  std::vector<std::string> *parts_out_)
{
    if (!parts_out_ || bucket_.messages.empty ()) {
        errno = EINVAL;
        return -1;
    }

    parts_out_->clear ();

    std::string header;
    header.reserve (spot_batch_header_size_v1);
    append_u32_le (&header, spot_batch_magic_v1);
    append_u16_le (&header, spot_batch_version_v1);
    append_u16_le (&header, 0);
    append_u32_le (&header, spot_batch_header_size_v1);

    std::string body;
    body.reserve (bucket_.pending_encoded_bytes);
    uint32_t total_payload_bytes = 0;
    for (size_t i = 0; i < bucket_.messages.size (); ++i) {
        const spot_topic_batch_message_t &message = bucket_.messages[i];
        append_u32_le (&body, message.encoded_bytes);
        append_u16_le (&body,
                       static_cast<uint16_t> (message.parts.size ()));
        total_payload_bytes += static_cast<uint32_t> (message.payload_bytes);
        for (size_t j = 0; j < message.parts.size (); ++j) {
            append_u32_le (&body,
                           static_cast<uint32_t> (message.parts[j].size ()));
            body.append (message.parts[j]);
        }
    }

    std::string metadata;
    metadata.reserve (spot_batch_metadata_size_v1);
    append_u32_le (&metadata,
                   static_cast<uint32_t> (bucket_.messages.size ()));
    append_u32_le (&metadata, total_payload_bytes);
    append_u32_le (&metadata, static_cast<uint32_t> (body.size ()));
    append_u32_le (&metadata, 0);

    parts_out_->push_back (header);
    parts_out_->push_back (metadata);
    parts_out_->push_back (body);
    return 0;
}

int spot_data_plane_forwarder_t::recv_and_forward_ingress (
  socket_base_t *src_,
  socket_base_t *mesh_pub_,
  socket_base_t *fanout_,
  spot_runtime_t *runtime_,
  spot_data_plane_runtime_state_t *state_,
  const spot_node_t *node_)
{
    if (!src_ || !runtime_ || !state_) {
        errno = EINVAL;
        return -1;
    }

    unsigned int forwarded_messages = 0;
    size_t forwarded_bytes = 0;

    for (;;) {
        std::string topic;
        std::vector<std::string> parts;
        size_t wire_bytes = 0;
        if (recv_logical_message (src_, true, &topic, &parts, &wire_bytes) != 0) {
            if (errno == EAGAIN)
                return 0;
            return -1;
        }

        const bool forward_to_fanout =
          fanout_ && node_ && node_->has_local_filtered_subs ();
        if (forward_to_fanout && publish_owned_parts (fanout_, topic, parts) != 0)
            return -1;

        if (mesh_pub_) {
            const spot_node_batch_config_t config =
              runtime_->peer_batch_config_snapshot ();
            const uint32_t encoded_bytes =
              logical_message_encoded_bytes (parts);
            const bool oversized =
              encoded_bytes >= static_cast<uint32_t> (config.bypass_bytes)
              || encoded_bytes > static_cast<uint32_t> (config.max_bytes);

            if (!config.enabled || is_immediate_peer_topic (topic)) {
                if (publish_owned_parts (mesh_pub_, topic, parts) != 0)
                    return -1;
            } else if (oversized) {
                std::map<std::string, spot_topic_batch_bucket_t>::iterator it =
                  state_->pending_peer_batches.find (topic);
                if (it != state_->pending_peer_batches.end ()
                    && !it->second.messages.empty ()
                    && flush_bucket (state_, mesh_pub_, it) != 0) {
                    return -1;
                }
                if (publish_owned_parts (mesh_pub_, topic, parts) != 0)
                    return -1;
            } else {
                std::map<std::string, spot_topic_batch_bucket_t>::iterator it =
                  state_->pending_peer_batches.find (topic);
                if (it != state_->pending_peer_batches.end ()
                    && !it->second.messages.empty ()
                    && it->second.pending_encoded_bytes + encoded_bytes
                         > static_cast<size_t> (config.max_bytes)
                    && flush_bucket (state_, mesh_pub_, it) != 0) {
                    return -1;
                }

                const uint64_t now_ms = clock_t ().now_ms ();
                spot_topic_batch_bucket_t &bucket =
                  state_->pending_peer_batches[topic];
                if (bucket.topic.empty ())
                    bucket.topic = topic;
                if (bucket.messages.empty ())
                    bucket.first_enqueue_ms = now_ms;
                bucket.last_enqueue_ms = now_ms;

                spot_topic_batch_message_t message;
                message.payload_bytes = logical_message_payload_bytes (parts);
                message.encoded_bytes = encoded_bytes;
                message.parts.swap (parts);
                bucket.pending_message_count += 1;
                bucket.pending_payload_bytes += message.payload_bytes;
                bucket.pending_encoded_bytes += message.encoded_bytes + 4;
                bucket.messages.push_back (message);

                if (bucket.pending_message_count
                      >= static_cast<size_t> (config.max_messages)
                    || bucket.pending_encoded_bytes
                         >= static_cast<size_t> (config.max_bytes)
                    || config.delay_ms == 0) {
                    if (flush_bucket (state_, mesh_pub_,
                                      state_->pending_peer_batches.find (topic))
                        != 0) {
                        return -1;
                    }
                }
            }
        }

        ++forwarded_messages;
        forwarded_bytes += wire_bytes;
        if (forwarded_messages >= ingress_forward_batch_limit
            || forwarded_bytes >= ingress_forward_batch_bytes_limit)
            return 0;
    }
}
}
