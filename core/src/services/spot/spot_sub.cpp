/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_sub.hpp"

#include "services/spot/spot_control_protocol.hpp"
#include "services/spot/spot_node.hpp"
#include "utils/err.hpp"
#include "utils/random.hpp"

#include <string.h>

namespace zlink
{
static const uint32_t spot_sub_tag_value = 0x1e6700da;

spot_sub_t::subject_descriptor_t::subject_descriptor_t () :
    subject_kind (ZLINK_SERVICE_EVENT_SUBJECT_NONE)
{
}

spot_sub_t::spot_sub_t (spot_node_t *node_,
                        socket_base_t *socket_,
                        uint64_t attachment_id_,
                        bool node_owned_default_) :
    _node (node_),
    _socket (socket_),
    _runtime (node_ ? node_->runtime () : NULL),
    _attachment_id (attachment_id_),
    _tag (spot_sub_tag_value),
    _node_owned_default (node_owned_default_),
    _routing_id_locked (false),
    _direct_handler_binding_index (0),
    _active_direct_handler (NULL),
    _handler_state (handler_none),
    _callback_inflight (0),
    _destroying (false),
    _monitor (node_ ? node_->ctx () : NULL),
    _monitor_event_queue (),
    _monitor_event_draining (false),
    _monitor_event_pending (0),
    _raw_monitor_socket (NULL),
    _monitor_stop (0),
    _monitor_thread_started (false)
{
    memset (&_routing_id, 0, sizeof (_routing_id));
    initialize_routing_id (&_routing_id);
}

spot_sub_t::~spot_sub_t ()
{
    _tag = 0xdeadbeef;
}

bool spot_sub_t::check_tag () const
{
    return _tag == spot_sub_tag_value;
}

bool spot_sub_t::is_node_owned_default () const
{
    return _node_owned_default;
}

void spot_sub_t::emit_monitor_event (const zlink_service_event_t &event_)
{
    if (_destroying.load (std::memory_order_acquire))
        return;
    _monitor.emit (event_);
}

socket_base_t *spot_sub_t::socket () const
{
    return _socket;
}

int spot_sub_t::initialize_routing_id (zlink_routing_id_t *out_)
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }

    const uint32_t value = generate_random ();
    out_->size = sizeof (value);
    memcpy (out_->data, &value, sizeof (value));
    return 0;
}

bool spot_sub_t::is_valid_topic (const char *topic_, std::string *out_)
{
    if (!topic_ || topic_[0] == '\0')
        return false;
    const size_t len = strlen (topic_);
    if (len == 0 || len > 255)
        return false;
    const std::string value (topic_, len);
    if (spot_control_protocol::is_reserved_subject (value))
        return false;
    if (out_)
        *out_ = value;
    return true;
}

bool spot_sub_t::is_valid_pattern (const char *pattern_,
                                   std::string *prefix_out_)
{
    if (!pattern_ || pattern_[0] == '\0')
        return false;
    const size_t len = strlen (pattern_);
    if (len < 2 || len > 255 || pattern_[len - 1] != '*')
        return false;
    const char *star = strchr (pattern_, '*');
    if (star != pattern_ + len - 1)
        return false;
    const std::string prefix (pattern_, len - 1);
    if (spot_control_protocol::is_reserved_subject (prefix))
        return false;
    if (prefix_out_)
        *prefix_out_ = prefix;
    return true;
}

void spot_sub_t::lock_routing_id ()
{
    _routing_id_locked = true;
}
}
