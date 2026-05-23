/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_CONTROL_PROTOCOL_HPP_INCLUDED__
#define __ZLINK_SPOT_CONTROL_PROTOCOL_HPP_INCLUDED__

#include "utils/stdint.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>

namespace zlink
{
namespace spot_control_protocol
{
static const int protocol_version = 2;
static const char ctrl_prefix[] = "__zlink.spot.ctrl.";
static const char bootstrap_prefix[] = "__zlink.spot.bootstrap.";
static const char ctrl_snapshot_topic[] = "__zlink.spot.ctrl.snapshot";
static const char ctrl_ready_ack_topic[] = "__zlink.spot.ctrl.ready_ack";
static const char bootstrap_ctrl_descriptor_topic[] =
  "__zlink.spot.bootstrap.ctrl_descriptor";
static const char peer_pub_route_topic[] = "__zlink.spot.peer_pub";

static const char cmd_terminate[] = "terminate";
static const char cmd_bind_pub[] = "bind_pub";
static const char cmd_connect_peer_pub[] = "connect_peer_pub";
static const char cmd_disconnect_peer_pub[] = "disconnect_peer_pub";
static const char cmd_connect_router_channel_peer[] =
  "connect_router_channel_peer";
static const char cmd_connect_router_channel_peer_rid[] =
  "connect_router_channel_peer_rid";
static const char cmd_disconnect_router_channel_peer[] =
  "disconnect_router_channel_peer";
static const char cmd_unbind_pub[] = "unbind_pub";
static const char cmd_replay_handle_state_subscriptions[] =
  "replay_handle_state.subscriptions";
static const char cmd_subscription_handle_state_subscribe[] =
  "subscription_handle_state.subscribe";
static const char cmd_subscription_unsubscribe[] =
  "subscription_unsubscribe";
static const char cmd_ready_ack_handle_state_subscribe[] =
  "ready_ack_handle_state.subscribe";
static const char cmd_ready_ack_unsubscribe[] = "ready_ack_unsubscribe";

static const char reply_ok[] = "ok";
static const char reply_error[] = "error";

inline bool starts_with (const std::string &value_, const char *prefix_)
{
    return prefix_
           && value_.compare (0, strlen (prefix_), prefix_) == 0;
}

inline bool command_is (const std::string &value_, const char *command_)
{
    return command_ && value_ == command_;
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

inline bool is_peer_pub_route_topic (const std::string &value_)
{
    return value_ == peer_pub_route_topic;
}

inline bool is_peer_pub_route_topic (const char *value_, size_t value_size_)
{
    return equals_literal (value_, value_size_, peer_pub_route_topic);
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

inline bool parse_endpoint_port_and_suffix (const std::string &endpoint_,
                                            size_t port_sep_,
                                            unsigned long *port_out_,
                                            std::string *suffix_out_)
{
    if (!port_out_ || !suffix_out_ || port_sep_ == std::string::npos
        || port_sep_ + 1 >= endpoint_.size ()) {
        return false;
    }

    const std::string port_and_suffix = endpoint_.substr (port_sep_ + 1);
    char *end = NULL;
    const unsigned long port = std::strtoul (port_and_suffix.c_str (), &end, 10);
    if (!end || end == port_and_suffix.c_str () || port < 1024UL
        || port > 65535UL) {
        return false;
    }

    const char *suffix = end;
    if (*suffix != '\0' && *suffix != '/')
        return false;

    *port_out_ = port;
    suffix_out_->assign (suffix);
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

}
}

#endif
