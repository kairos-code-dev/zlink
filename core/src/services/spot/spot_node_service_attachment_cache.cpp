/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_node.hpp"

#include "sockets/socket_base.hpp"

namespace zlink
{
namespace
{
static void spot_shutdown_logf_local (bool always_, const char *fmt_, ...)
{
    if (!always_ && !std::getenv ("ZLINK_DEBUG_SPOT_SHUTDOWN"))
        return;

    va_list args;
    va_start (args, fmt_);
    std::fprintf (stderr, "[spot-shutdown] ");
    std::vfprintf (stderr, fmt_, args);
    std::fprintf (stderr, "\n");
    std::fflush (stderr);
    va_end (args);
}
}

void spot_node_t::rebuild_service_attachment_caches_locked ()
{
    std::shared_ptr<service_attachment_state_t::service_sub_cache_t> sub_cache (
      new service_attachment_state_t::service_sub_cache_t ());
    std::shared_ptr<service_attachment_state_t::readable_sub_cache_t>
      readable_sub_cache (new service_attachment_state_t::readable_sub_cache_t ());
    std::shared_ptr<socket_poller_t> readable_sub_poller (
      new socket_poller_t ());
    std::shared_ptr<service_attachment_state_t::service_monitor_cache_t>
      monitor_cache (new service_attachment_state_t::service_monitor_cache_t ());

    sub_cache->reserve (_service_attachments.size () * 2);
    readable_sub_cache->reserve (_service_attachments.size () * 2);
    monitor_cache->reserve (_service_monitors.size ());
    for (std::map<std::string, service_attachment_t>::const_iterator it =
           _service_attachments.begin ();
         it != _service_attachments.end (); ++it) {
        update_service_stats_locked (it->first, it->second);
        if (it->second.has_manual_pubsub ()) {
            service_attachment_state_t::service_sub_cache_entry_t entry;
            entry.service_name = it->first;
            entry.socket = it->second.manual.sub;
            sub_cache->push_back (entry);
            readable_sub_cache->push_back (it->second.manual.sub);
            (void) readable_sub_poller->add (it->second.manual.sub, NULL,
                                             ZLINK_POLLIN);
        }
        if (it->second.has_auto_pubsub ()) {
            service_attachment_state_t::service_sub_cache_entry_t entry;
            entry.service_name = it->first;
            entry.socket = it->second.discovered.sub;
            sub_cache->push_back (entry);
            readable_sub_cache->push_back (it->second.discovered.sub);
            (void) readable_sub_poller->add (it->second.discovered.sub, NULL,
                                             ZLINK_POLLIN);
        }
    }
    for (std::deque<service_monitor_handle_t>::const_iterator it =
           _service_monitors.begin ();
         it != _service_monitors.end (); ++it)
        monitor_cache->push_back (*it);

    for (service_attachment_state_t::service_stats_cache_t::iterator it =
           _service_attachment_state.stats_cache.begin ();
         it != _service_attachment_state.stats_cache.end ();) {
        if (_service_attachments.count (it->first) == 0
            && _service_discoveries.count (it->first) == 0)
            it = _service_attachment_state.stats_cache.erase (it);
        else
            ++it;
    }

    _service_attachment_state.sub_cache = sub_cache;
    _service_attachment_state.readable_sub_cache = readable_sub_cache;
    _service_attachment_state.readable_sub_poller = readable_sub_poller;
    _service_attachment_state.monitor_cache = monitor_cache;
}

void spot_node_t::remove_service_monitors_by_owner_locked (
  const std::vector<socket_base_t *> &sockets_)
{
    if (sockets_.empty ())
        return;
    for (std::deque<service_monitor_handle_t>::iterator mit =
           _service_monitors.begin ();
         mit != _service_monitors.end ();) {
        bool matched = false;
        for (size_t i = 0; i < sockets_.size (); ++i) {
            if (mit->owner_socket == sockets_[i]) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            ++mit;
            continue;
        }
        if (mit->handle)
            (void) zlink_monitor_close (&mit->handle);
        mit = _service_monitors.erase (mit);
    }
}

bool spot_node_t::detach_discovered_service_locked (
  discovery_t *discovery_,
  std::vector<socket_base_t *> *sockets_to_close_out_)
{
    if (!sockets_to_close_out_)
        return false;
    sockets_to_close_out_->clear ();

    for (std::map<std::string, discovery_t *>::iterator it =
           _service_discoveries.begin ();
         it != _service_discoveries.end (); ++it) {
        if (it->second != discovery_)
            continue;
        const std::string service_name = it->first;

        std::map<std::string, service_attachment_t>::iterator attach_it =
          _service_attachments.find (service_name);
        if (attach_it != _service_attachments.end ()) {
            for (std::map<std::string, socket_base_t *>::iterator rit =
                   attach_it->second.discovered.routers.begin ();
                 rit != attach_it->second.discovered.routers.end (); ++rit) {
                if (rit->second) {
                    sockets_to_close_out_->push_back (rit->second);
                    _service_attachment_socket_index.erase (rit->second);
                }
            }
            if (attach_it->second.discovered.pub) {
                sockets_to_close_out_->push_back (attach_it->second.discovered.pub);
                _service_attachment_socket_index.erase (
                  attach_it->second.discovered.pub);
            }
            if (attach_it->second.discovered.sub) {
                sockets_to_close_out_->push_back (attach_it->second.discovered.sub);
                _service_attachment_socket_index.erase (
                  attach_it->second.discovered.sub);
            }

            attach_it->second.discovered.routers.clear ();
            attach_it->second.discovered.pub = NULL;
            attach_it->second.discovered.sub = NULL;
            attach_it->second.discovered.router_endpoints.clear ();
            attach_it->second.discovered.pub_endpoints.clear ();
            attach_it->second.discovered.sub_endpoints.clear ();
            attach_it->second.clear_auto_sub_replay ();
            update_service_stats_locked (service_name, attach_it->second);
            if (attach_it->second.manual.routers.empty ()
                && !attach_it->second.has_manual_pubsub ()) {
                _service_attachments.erase (attach_it);
                erase_service_stats_row_if_unused_locked (service_name);
            }
        }

        remove_service_monitors_by_owner_locked (*sockets_to_close_out_);
        _service_discoveries.erase (it);
        _service_attachment_state.pending_refresh_services.erase (service_name);
        erase_service_stats_row_if_unused_locked (service_name);
        rebuild_service_attachment_caches_locked ();
        spot_shutdown_logf_local (
          false, "step=detach_discovered_service node=%p sockets=%zu",
          static_cast<void *> (this), sockets_to_close_out_->size ());
        return true;
    }
    return false;
}
}

