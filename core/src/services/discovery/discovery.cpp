/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/discovery/discovery.hpp"
#include "services/discovery/discovery_protocol.hpp"

#include "utils/err.hpp"

#include <algorithm>
#include <string.h>

namespace zlink
{
static const uint32_t discovery_tag_value = 0x1e6700d6;

static bool is_valid_service_type (uint16_t service_type_)
{
    return service_type_ == discovery_protocol::service_type_gateway_receiver
           || service_type_ == discovery_protocol::service_type_spot_node;
}

discovery_t::discovery_t (ctx_t *ctx_, uint16_t service_type_) :
    _ctx (ctx_),
    _tag (discovery_tag_value),
    _lifecycle (ctx_),
    _stop (0),
    _task_id (0),
    _sub_socket (NULL),
    _update_seq (0),
    _observer_callbacks_inflight (0),
    _destroying (false),
    _monitor_ready_count (0),
    _service_type (service_type_),
    _discovery_summary_enabled (true),
    _routing_id_locked (false),
    _heartbeat_interval_ms (5000),
    _monitor (ctx_)
{
    zlink_assert (_ctx);
    zlink_assert (is_valid_service_type (_service_type));
    _routing_id.size = 0;
}

discovery_t::~discovery_t ()
{
    _tag = 0xdeadbeef;
}

bool discovery_t::check_tag () const
{
    return _tag == discovery_tag_value;
}

void discovery_t::emit_ready_changed (uint32_t ready_count_)
{
    zlink_service_event_t event;
    bool emit = false;
    {
        scoped_lock_t lock (_sync);
        if (_monitor_ready_count == ready_count_)
            return;
        _monitor_ready_count = ready_count_;
        memset (&event, 0, sizeof (event));
        event.service_kind = ZLINK_SERVICE_KIND_DISCOVERY;
        event.event_type = ZLINK_DISCOVERY_MONITOR_EVENT_READY_CHANGED;
        event.value = ready_count_;
        if (_routing_id.size > 0) {
            event.detail_flags |= ZLINK_EVENT_DETAIL_SUBJECT_RID;
            event.routing_id = _routing_id;
        }
        emit = true;
    }
    if (emit)
        _monitor.emit (event);
}

void discovery_t::set_discovery_summary_enabled (bool enabled_)
{
    scoped_lock_t lock (_sync);
    _discovery_summary_enabled = enabled_;
}

}
