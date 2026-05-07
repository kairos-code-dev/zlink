/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_data_plane_message_io_internal.hpp"

#include "api/request_reply_protocol_internal.hpp"
#include "services/spot/spot_control_protocol.hpp"
#include "services/spot/spot_message_parts_internal.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_runtime.hpp"
#include "sockets/socket_base.hpp"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace
{
void spot_ctrl_debugf (const char *fmt_, ...)
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
}

int zlink::spot_data_plane_message_io::send_ascii_frame (
  socket_base_t *socket_,
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

int zlink::spot_data_plane_message_io::send_control_snapshot (
  socket_base_t *socket_,
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

    if (send_ascii_frame (socket_, topic_, ZLINK_SNDMORE) != 0
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

int zlink::spot_data_plane_message_io::send_snapshot_to_target (
  socket_base_t *socket_,
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

int zlink::spot_data_plane_message_io::send_snapshot_to_peers (
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

int zlink::spot_data_plane_message_io::send_ready_ack_snapshots_to_target (
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
            != 0) {
            return -1;
        }
    }

    return 0;
}

bool zlink::spot_data_plane_message_io::parse_ready_ack_arg (
  const std::string &arg_,
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

int zlink::spot_data_plane_message_io::recv_remaining_frame_strings (
  socket_base_t *socket_,
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
        if (socket_->recv (&frame, ZLINK_DONTWAIT) != 0) {
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

int zlink::spot_data_plane_message_io::recv_remaining_frames_to_vector (
  socket_base_t *socket_,
  std::vector<zlink_msg_t> *out_,
  size_t *wire_bytes_out_)
{
    if (!socket_ || !out_ || !wire_bytes_out_) {
        errno = EINVAL;
        return -1;
    }

    while (out_->empty ()
           || (reinterpret_cast<msg_t *> (&out_->back ())->flags ()
                & msg_t::more)
                != 0) {
        msg_t frame;
        if (frame.init () != 0)
            return -1;

        if (socket_->recv (&frame, ZLINK_DONTWAIT) != 0) {
            const int err = errno;
            frame.close ();
            zlink::request_reply::close_built_parts (out_);
            out_->clear ();
            errno = err;
            return -1;
        }

        *wire_bytes_out_ += frame.size ();
        out_->push_back (zlink_msg_t ());
        zlink_msg_t &stored = out_->back ();
        zlink_msg_init (&stored);
        if (zlink_msg_move (&stored, reinterpret_cast<zlink_msg_t *> (&frame))
            != 0) {
            const int err = errno;
            frame.close ();
            zlink_msg_close (&stored);
            out_->pop_back ();
            zlink::request_reply::close_built_parts (out_);
            out_->clear ();
            errno = err;
            return -1;
        }
    }

    return 0;
}

int zlink::spot_data_plane_message_io::recv_remaining_frames_to_parts (
  socket_base_t *socket_,
  spot_owned_msg_parts_t *parts_out_,
  size_t *wire_bytes_out_)
{
    if (!socket_ || !parts_out_ || !wire_bytes_out_) {
        errno = EINVAL;
        return -1;
    }

    spot_clear_msg_parts (parts_out_);
    while (parts_out_->empty ()
           || (reinterpret_cast<msg_t *> (&parts_out_->back ())->flags ()
                & msg_t::more)
                != 0) {
        msg_t frame;
        if (frame.init () != 0) {
            spot_clear_msg_parts (parts_out_);
            return -1;
        }

        if (socket_->recv (&frame, ZLINK_DONTWAIT) != 0) {
            const int err = errno;
            frame.close ();
            spot_clear_msg_parts (parts_out_);
            errno = err;
            return -1;
        }

        parts_out_->push_back (zlink_msg_t ());
        zlink_msg_t &stored = parts_out_->back ();
        spot_init_msg_frame (&stored);
        if (zlink_msg_move (&stored, reinterpret_cast<zlink_msg_t *> (&frame))
            != 0) {
            const int err = errno;
            frame.close ();
            spot_close_msg_frame (&stored);
            parts_out_->pop_back ();
            spot_clear_msg_parts (parts_out_);
            errno = err;
            return -1;
        }

        *wire_bytes_out_ += zlink_msg_size (&stored);
    }

    return 0;
}
