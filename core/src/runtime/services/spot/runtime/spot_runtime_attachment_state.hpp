/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_RUNTIME_ATTACHMENT_STATE_HPP_INCLUDED__
#define __ZLINK_SPOT_RUNTIME_ATTACHMENT_STATE_HPP_INCLUDED__

#include "utils/mutex.hpp"
#include "utils/stdint.hpp"

#include <deque>
#include <map>
#include <string>

namespace zlink
{
class socket_base_t;

enum spot_attachment_kind_t
{
    spot_attachment_pub = 1,
    spot_attachment_sub = 2
};

struct spot_attachment_t
{
    spot_attachment_t () : id (0), kind (0), socket (NULL), relay_socket (NULL) {}

    uint64_t id;
    int kind;
    socket_base_t *socket;
    socket_base_t *relay_socket;
    std::string endpoint;
    std::string relay_endpoint;
};

struct spot_runtime_attachment_state_t
{
    spot_runtime_attachment_state_t () : next_id (0), version (0) {}

    mutable mutex_t sync;
    uint64_t next_id;
    uint64_t version;
    std::map<uint64_t, spot_attachment_t> items;
    std::deque<socket_base_t *> retired_relay_sockets;
};
}

#endif
