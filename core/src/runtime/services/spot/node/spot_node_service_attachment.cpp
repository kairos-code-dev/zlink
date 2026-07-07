/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/node/spot_node.hpp"
#include "services/spot/common/spot_auto_hwm_internal.hpp"
#include "services/spot/common/spot_debug.hpp"
#include "services/spot/runtime/spot_runtime.hpp"

#include "core/recv_internal.hpp"
#include "sockets/common/socket_base.hpp"

namespace zlink
{
namespace
{
void snapshot_service_auto_hwm_inputs_local (spot_runtime_t *runtime_,
                                             size_t *connected_peer_count_out_,
                                             size_t *active_peer_count_out_)
{
    size_t local_pub_count = 0;
    size_t local_sub_count = 0;
    size_t connected_peer_count = 0;
    size_t active_peer_count = 0;
    if (runtime_) {
        runtime_->snapshot_auto_hwm_inputs (&local_pub_count, &local_sub_count,
                                            &connected_peer_count, &active_peer_count);
    }
    if (connected_peer_count_out_)
        *connected_peer_count_out_ = connected_peer_count;
    if (active_peer_count_out_)
        *active_peer_count_out_ = active_peer_count;
}

static void spot_shutdown_logf_local (bool always_, const char *fmt_, ...)
{
    if (!always_ && !spot_debug::shutdown_enabled ())
        return;

    va_list args;
    va_start (args, fmt_);
    debug_vfprintf (always_ ? NULL : "ZLINK_DEBUG_SPOT_SHUTDOWN", "[spot-shutdown] ", fmt_, args);
    va_end (args);
}

}

socket_base_t *spot_node_t::select_service_router (const std::string &channel_name_)
{
    scoped_lock_t lock (_sync);
    std::map<std::string, service_attachment_t>::iterator it =
      service_attachments ().attachments.find (channel_name_);
    if (it == service_attachments ().attachments.end ()) {
        errno = ENOENT;
        return NULL;
    }
    service_attachment_t &attachment = it->second;
    const size_t candidate_count = attachment.router_cache.size ();
    if (candidate_count == 0) {
        errno = ENOTCONN;
        return NULL;
    }
    for (size_t attempt = 0; attempt < candidate_count; ++attempt) {
        if (attachment.next_router_index >= candidate_count)
            attachment.next_router_index = 0;
        socket_base_t *router = attachment.router_cache[attachment.next_router_index];
        attachment.next_router_index = (attachment.next_router_index + 1) % candidate_count;
        if (router && zlink::wait_socket_events_internal (router, ZLINK_POLLOUT, 0) > 0)
            return router;
    }
    errno = ENOTCONN;
    return NULL;
}

socket_base_t *spot_node_t::service_pub_socket (const std::string &channel_name_) const
{
    scoped_lock_t lock (_sync);
    std::map<std::string, service_attachment_t>::const_iterator it =
      service_attachments ().attachments.find (channel_name_);
    if (it == service_attachments ().attachments.end ()) {
        errno = ENOENT;
        return NULL;
    }
    if (it->second.has_manual_pubsub ())
        return it->second.manual.pub;
    errno = ENOTCONN;
    return NULL;
}

int spot_node_t::apply_service_subscription_filters ()
{
    std::set<std::string> filters;
    snapshot_raw_subscription_filters (&filters);

    std::vector<std::pair<socket_base_t *, std::set<std::string>>> work;
    const auto collect_filter_work =
      [&] (std::vector<std::pair<socket_base_t *, std::set<std::string>>> *out_) {
          if (!out_)
              return;
          scoped_lock_t lock (_sync);
          for (std::map<std::string, service_attachment_t>::iterator it =
                 service_attachments ().attachments.begin ();
               it != service_attachments ().attachments.end (); ++it) {
              if (it->second.applied_filters == filters)
                  continue;
              if (it->second.manual.sub)
                  out_->push_back (
                    std::make_pair (it->second.manual.sub, it->second.applied_filters));
          }
      };
    const auto commit_applied_filters = [&] () {
        scoped_lock_t lock (_sync);
        for (std::map<std::string, service_attachment_t>::iterator it =
               service_attachments ().attachments.begin ();
             it != service_attachments ().attachments.end (); ++it) {
            if (it->second.manual.sub)
                it->second.applied_filters = filters;
        }
    };

    collect_filter_work (&work);

    for (size_t i = 0; i < work.size (); ++i) {
        socket_base_t *sub = work[i].first;
        const std::set<std::string> &applied = work[i].second;
        for (std::set<std::string>::const_iterator it = applied.begin (); it != applied.end ();
             ++it) {
            if (filters.count (*it) == 0 && zlink_unset_subscription (sub, it->c_str ()) != 0)
                return -1;
        }
        for (std::set<std::string>::const_iterator it = filters.begin (); it != filters.end ();
             ++it) {
            if (applied.count (*it) == 0 && zlink_set_subscription (sub, it->c_str ()) != 0)
                return -1;
        }
    }

    commit_applied_filters ();
    return 0;
}

}
