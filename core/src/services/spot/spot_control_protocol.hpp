/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_CONTROL_PROTOCOL_HPP_INCLUDED__
#define __ZLINK_SPOT_CONTROL_PROTOCOL_HPP_INCLUDED__

#include "utils/stdint.hpp"

#include <stdio.h>
#include <string>
#include <string.h>

namespace zlink
{
namespace spot_control_protocol
{
static const int protocol_version = 1;
static const char ctrl_prefix[] = "__zlink.spot.ctrl.";
static const char bootstrap_prefix[] = "__zlink.spot.bootstrap.";
static const char ctrl_snapshot_topic[] = "__zlink.spot.ctrl.snapshot";
static const char ctrl_ready_ack_topic[] = "__zlink.spot.ctrl.ready_ack";
static const char bootstrap_ctrl_descriptor_topic[] =
  "__zlink.spot.bootstrap.ctrl_descriptor";

inline bool starts_with (const std::string &value_, const char *prefix_)
{
    return prefix_
           && value_.compare (0, strlen (prefix_), prefix_) == 0;
}

inline bool starts_with (const char *value_,
                         size_t value_size_,
                         const char *prefix_)
{
    if (!value_ || !prefix_)
        return false;

    const size_t prefix_size = strlen (prefix_);
    return value_size_ >= prefix_size && memcmp (value_, prefix_, prefix_size) == 0;
}

inline bool equals_literal (const char *value_,
                            size_t value_size_,
                            const char *literal_)
{
    if (!value_ || !literal_)
        return false;

    const size_t literal_size = strlen (literal_);
    return value_size_ == literal_size
           && memcmp (value_, literal_, literal_size) == 0;
}

inline bool is_reserved_subject (const std::string &value_)
{
    return starts_with (value_, ctrl_prefix)
           || starts_with (value_, bootstrap_prefix);
}

inline bool is_reserved_subject (const char *value_, size_t value_size_)
{
    return starts_with (value_, value_size_, ctrl_prefix)
           || starts_with (value_, value_size_, bootstrap_prefix);
}

inline bool is_ctrl_snapshot_topic (const std::string &value_)
{
    return value_ == ctrl_snapshot_topic;
}

inline bool is_ctrl_snapshot_topic (const char *value_, size_t value_size_)
{
    return equals_literal (value_, value_size_, ctrl_snapshot_topic);
}

inline bool is_ctrl_ready_ack_topic (const std::string &value_)
{
    return value_ == ctrl_ready_ack_topic;
}

inline bool is_ctrl_ready_ack_topic (const char *value_, size_t value_size_)
{
    return equals_literal (value_, value_size_, ctrl_ready_ack_topic);
}

inline bool is_bootstrap_ctrl_descriptor_topic (const std::string &value_)
{
    return value_ == bootstrap_ctrl_descriptor_topic;
}

inline bool is_bootstrap_ctrl_descriptor_topic (const char *value_,
                                                size_t value_size_)
{
    return equals_literal (value_, value_size_,
                           bootstrap_ctrl_descriptor_topic);
}

inline std::string node_id_string (uint32_t node_id_)
{
    char buf[32];
    snprintf (buf, sizeof (buf), "%u", static_cast<unsigned int> (node_id_));
    return std::string (buf);
}

inline bool derive_fixed_port_endpoint (const std::string &endpoint_,
                                        const char *scheme_,
                                        std::string *out_)
{
    if (!out_ || !starts_with (endpoint_, scheme_))
        return false;

    const size_t scheme_len = strlen (scheme_);
    const size_t port_sep = endpoint_.rfind (':');
    if (port_sep == std::string::npos || port_sep <= scheme_len)
        return false;

    const int port = atoi (endpoint_.c_str () + port_sep + 1);
    if (port <= 0 || port > 64535)
        return false;

    char buf[32];
    snprintf (buf, sizeof (buf), "%d", port + 1000);
    *out_ = endpoint_.substr (0, port_sep + 1) + buf;
    return true;
}

inline bool derive_peer_ctrl_bind_endpoint (const std::string &data_endpoint_,
                                            uint32_t node_id_,
                                            std::string *out_)
{
    if (!out_ || data_endpoint_.empty ())
        return false;

    if (starts_with (data_endpoint_, "inproc://")) {
        *out_ = std::string ("inproc://zlink.spot.peer-ctrl.")
                + node_id_string (node_id_);
        return true;
    }

    if (starts_with (data_endpoint_, "ipc://")) {
        *out_ = data_endpoint_ + ".zlink-spot-ctrl." + node_id_string (node_id_);
        return true;
    }

    if (derive_fixed_port_endpoint (data_endpoint_, "tcp://", out_))
        return true;
    if (derive_fixed_port_endpoint (data_endpoint_, "tls://", out_))
        return true;
    if (derive_fixed_port_endpoint (data_endpoint_, "ws://", out_))
        return true;
    if (derive_fixed_port_endpoint (data_endpoint_, "wss://", out_))
        return true;

    return false;
}

inline bool derive_peer_route_bind_endpoint (const std::string &data_endpoint_,
                                             uint32_t node_id_,
                                             std::string *out_)
{
    if (!out_ || data_endpoint_.empty ())
        return false;

    if (starts_with (data_endpoint_, "inproc://")) {
        *out_ = std::string ("inproc://zlink.spot.peer-route.")
                + node_id_string (node_id_);
        return true;
    }

    if (starts_with (data_endpoint_, "ipc://")) {
        *out_ = data_endpoint_ + ".zlink-spot-route." + node_id_string (node_id_);
        return true;
    }

    const auto derive_offset_port_endpoint =
      [out_] (const std::string &endpoint_, const char *prefix_,
              int offset_) -> bool {
        if (!starts_with (endpoint_, prefix_))
            return false;

        const size_t host_offset = strlen (prefix_);
        const size_t port_sep = endpoint_.rfind (':');
        if (port_sep == std::string::npos || port_sep <= host_offset
            || port_sep + 1 >= endpoint_.size ()) {
            return false;
        }

        const std::string port_text = endpoint_.substr (port_sep + 1);
        char *end = NULL;
        const unsigned long port = std::strtoul (port_text.c_str (), &end, 10);
        if (!end || *end != '\0' || port < 1024UL || port > 65535UL)
            return false;

        const unsigned long min_port = 1024UL;
        const unsigned long port_space = 65535UL - min_port + 1UL;
        const unsigned long mapped_port =
          ((port - min_port + static_cast<unsigned long> (offset_)) % port_space)
          + min_port;

        char buf[32];
        snprintf (buf, sizeof (buf), "%lu", mapped_port);
        *out_ = endpoint_.substr (0, port_sep + 1) + buf;
        return true;
      };

    if (derive_offset_port_endpoint (data_endpoint_, "tcp://", 20000))
        return true;
    if (derive_offset_port_endpoint (data_endpoint_, "tls://", 20000))
        return true;
    if (derive_offset_port_endpoint (data_endpoint_, "ws://", 20000))
        return true;
    if (derive_offset_port_endpoint (data_endpoint_, "wss://", 20000))
        return true;

    return false;
}

}
}

#endif
