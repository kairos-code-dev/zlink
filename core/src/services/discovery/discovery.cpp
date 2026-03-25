/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/discovery/discovery.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/discovery/discovery_runtime_internal.hpp"

#include "utils/err.hpp"

#include <algorithm>
#include <string.h>

namespace zlink
{
static const uint32_t discovery_tag_value = 0x1e6700d6;

discovery_t::discovery_t (ctx_t *ctx_,
                          uint16_t service_type_,
                          const std::string &service_name_) :
    _ctx (ctx_),
    _tag (discovery_tag_value),
    _lifecycle (ctx_),
    _stop (0),
    _task_id (0),
    _sub_socket (NULL),
    _bootstrap_runtime (new discovery_bootstrap_runtime_t ()),
    _uplink_runtime (new discovery_uplink_runtime_t ()),
    _update_seq (0),
    _observer_callbacks_inflight (0),
    _destroying (false),
    _monitor_ready_count (0),
    _service_seq (0),
    _service_type (service_type_),
    _discovery_summary_enabled (true),
    _service_name (service_name_),
    _monitor (ctx_)
{
    zlink_assert (_ctx);
    zlink_assert (discovery_protocol::is_valid_service_type (_service_type));
    zlink_assert (_bootstrap_runtime);
    zlink_assert (_uplink_runtime);
    if (_service_name.empty ())
        _tag = 0xdeadbeef;
}

discovery_t::~discovery_t ()
{
    delete _uplink_runtime;
    _uplink_runtime = NULL;
    delete _bootstrap_runtime;
    _bootstrap_runtime = NULL;
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
        const zlink_routing_id_t &routing_id =
          _bootstrap_runtime->routing_id_value ();
        if (routing_id.size > 0) {
            event.detail_flags |= ZLINK_EVENT_DETAIL_SUBJECT_RID;
            event.routing_id = routing_id;
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
