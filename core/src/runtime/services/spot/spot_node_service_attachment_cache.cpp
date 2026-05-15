/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_node.hpp"
#include "services/spot/spot_debug.hpp"

#include "sockets/common/socket_base.hpp"

namespace zlink
{
namespace
{
static void spot_shutdown_logf_local (bool always_, const char *fmt_, ...)
{
    if (!always_ && !spot_debug::shutdown_enabled ())
        return;

    va_list args;
    va_start (args, fmt_);
    debug_vfprintf (always_ ? NULL : "ZLINK_DEBUG_SPOT_SHUTDOWN",
                    "[spot-shutdown] ", fmt_, args);
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

    sub_cache->reserve (_service_attachment_state.attachments.size () * 2);
    readable_sub_cache->reserve (_service_attachment_state.attachments.size () * 2);
    for (std::map<std::string, service_attachment_t>::iterator it =
           _service_attachment_state.attachments.begin ();
         it != _service_attachment_state.attachments.end (); ++it) {
        service_attachment_t &attachment = it->second;
        attachment.router_cache.clear ();
        attachment.router_cache.reserve (attachment.manual.routers.size ()
                                         + attachment.discovered.routers.size ());
        attachment.router_cache.insert (attachment.router_cache.end (),
                                        attachment.manual.routers.begin (),
                                        attachment.manual.routers.end ());
        for (std::map<std::string, socket_base_t *>::const_iterator router_it =
               attachment.discovered.routers.begin ();
             router_it != attachment.discovered.routers.end (); ++router_it) {
            attachment.router_cache.push_back (router_it->second);
        }

        if (it->second.has_manual_pubsub ()) {
            service_attachment_state_t::service_sub_cache_entry_t entry;
            entry.channel_name = it->first;
            entry.socket = it->second.manual.sub;
            sub_cache->push_back (entry);
            readable_sub_cache->push_back (it->second.manual.sub);
            (void) readable_sub_poller->add (it->second.manual.sub, NULL,
                                             ZLINK_POLLIN);
        }
        if (it->second.has_auto_pubsub ()) {
            service_attachment_state_t::service_sub_cache_entry_t entry;
            entry.channel_name = it->first;
            entry.socket = it->second.discovered.sub;
            sub_cache->push_back (entry);
            readable_sub_cache->push_back (it->second.discovered.sub);
            (void) readable_sub_poller->add (it->second.discovered.sub, NULL,
                                             ZLINK_POLLIN);
        }
    }

    _service_attachment_state.sub_cache = sub_cache;
    _service_attachment_state.readable_sub_cache = readable_sub_cache;
    _service_attachment_state.readable_sub_poller = readable_sub_poller;
}

void spot_node_t::remove_attachment_monitors_by_owner_locked (
  const std::vector<socket_base_t *> &sockets_)
{
    if (sockets_.empty ())
        return;
    for (std::deque<attachment_monitor_handle_t>::iterator mit =
           _service_attachment_state.monitors.begin ();
         mit != _service_attachment_state.monitors.end ();) {
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
        mit = _service_attachment_state.monitors.erase (mit);
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
           _service_attachment_state.discoveries.begin ();
         it != _service_attachment_state.discoveries.end (); ++it) {
        if (it->second != discovery_)
            continue;
        const std::string channel_name = it->first;

        std::map<std::string, service_attachment_t>::iterator attach_it =
          _service_attachment_state.attachments.find (channel_name);
        if (attach_it != _service_attachment_state.attachments.end ()) {
            for (std::map<std::string, socket_base_t *>::iterator rit =
                   attach_it->second.discovered.routers.begin ();
                 rit != attach_it->second.discovered.routers.end (); ++rit) {
                if (rit->second) {
                    sockets_to_close_out_->push_back (rit->second);
                    _service_attachment_state.socket_index.erase (rit->second);
                }
            }
            if (attach_it->second.discovered.pub) {
                sockets_to_close_out_->push_back (attach_it->second.discovered.pub);
                _service_attachment_state.socket_index.erase (
                  attach_it->second.discovered.pub);
            }
            if (attach_it->second.discovered.sub) {
                sockets_to_close_out_->push_back (attach_it->second.discovered.sub);
                _service_attachment_state.socket_index.erase (
                  attach_it->second.discovered.sub);
            }

            attach_it->second.discovered.routers.clear ();
            attach_it->second.discovered.pub = NULL;
            attach_it->second.discovered.sub = NULL;
            attach_it->second.discovered.router_endpoints.clear ();
            attach_it->second.discovered.pub_endpoints.clear ();
            attach_it->second.discovered.sub_endpoints.clear ();
            attach_it->second.clear_auto_sub_replay ();
            if (attach_it->second.manual.routers.empty ()
                && !attach_it->second.has_manual_pubsub ())
                _service_attachment_state.attachments.erase (attach_it);
        }

        remove_attachment_monitors_by_owner_locked (*sockets_to_close_out_);
        _service_attachment_state.discoveries.erase (it);
        _service_attachment_state.pending_refresh_services.erase (channel_name);
        rebuild_service_attachment_caches_locked ();
        spot_shutdown_logf_local (
          false, "step=detach_discovered_service node=%p sockets=%zu",
          static_cast<void *> (this), sockets_to_close_out_->size ());
        return true;
    }
    return false;
}
}
