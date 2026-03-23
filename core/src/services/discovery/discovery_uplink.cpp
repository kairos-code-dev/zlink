/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "core/recv_internal.hpp"
#include "services/discovery/discovery.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/gateway/routing_id_utils.hpp"

#include <cstdarg>
#include <cstdio>

namespace zlink
{
namespace
{
static void discovery_debugf_local (const char *fmt_, ...)
{
    if (!std::getenv ("ZLINK_DISCOVERY_DEBUG"))
        return;
    va_list args;
    va_start (args, fmt_);
    std::fprintf (stderr, "[discovery] ");
    std::vfprintf (stderr, fmt_, args);
    std::fprintf (stderr, "\n");
    va_end (args);
}

static bool send_topology_report_frames_local (
  socket_base_t *dealer_,
  const zlink_registry_topology_entry_t &entry_)
{
    if (!dealer_)
        return false;

    return zlink::discovery_protocol::send_u16 (
             dealer_, discovery_protocol::msg_topology_report, ZLINK_SNDMORE)
             >= 0
           && zlink::discovery_protocol::send_frame (
                dealer_, &entry_, sizeof (entry_), 0)
                >= 0;
}

static bool send_gateway_peer_report_frames_local (
  socket_base_t *dealer_,
  const zlink_registry_gateway_peer_entry_t &entry_)
{
    if (!dealer_)
        return false;

    return zlink::discovery_protocol::send_u16 (
             dealer_, discovery_protocol::msg_gateway_peer_report, ZLINK_SNDMORE)
             >= 0
           && zlink::discovery_protocol::send_frame (
                dealer_, &entry_, sizeof (entry_), 0)
                >= 0;
}

static bool wait_socket_event_local (void *socket_,
                                     short events_,
                                     long timeout_ms_)
{
    return zlink::wait_socket_events_internal (socket_, events_, timeout_ms_) > 0;
}
}

int discovery_t::ensure_topology_reporter_locked (
  const std::string &uplink_endpoint_,
  socket_base_t **dealer_out_)
{
    if (!dealer_out_) {
        errno = EINVAL;
        return -1;
    }
    *dealer_out_ = NULL;

    scoped_lock_t lock (_sync);
    std::map<std::string, socket_base_t *>::iterator it =
      _report_dealers.find (uplink_endpoint_);
    if (it == _report_dealers.end () || !it->second) {
        socket_base_t *dealer = _ctx->create_socket (ZLINK_CORE_SOCKET_DEALER);
        if (!dealer)
            return -1;
        _lifecycle.register_socket (dealer);
        if (!ensure_socket_routing_id (dealer)) {
            (void) _lifecycle.close_socket (dealer);
            return -1;
        }
        apply_socket_options_locked (dealer);
        const int linger = 200;
        const int sndtimeo_ms = 100;
        const int rcvtimeo_ms = 1000;
        dealer->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger, sizeof (linger));
        dealer->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &sndtimeo_ms,
                            sizeof (sndtimeo_ms));
        dealer->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO, &rcvtimeo_ms,
                            sizeof (rcvtimeo_ms));
        if (dealer->connect (uplink_endpoint_.c_str ()) != 0) {
            (void) _lifecycle.close_socket (dealer);
            return -1;
        }
        _report_dealers[uplink_endpoint_] = dealer;
        *dealer_out_ = dealer;
        return 0;
    }

    *dealer_out_ = it->second;
    return 0;
}

int discovery_t::ensure_control_dealer_locked (
  const std::string &uplink_endpoint_,
  socket_base_t **dealer_out_)
{
    if (!dealer_out_) {
        errno = EINVAL;
        return -1;
    }
    *dealer_out_ = NULL;

    scoped_lock_t lock (_sync);
    std::map<std::string, socket_base_t *>::iterator it =
      _control_dealers.find (uplink_endpoint_);
    if (it == _control_dealers.end () || !it->second) {
        socket_base_t *dealer = _ctx->create_socket (ZLINK_CORE_SOCKET_DEALER);
        if (!dealer)
            return -1;
        _lifecycle.register_socket (dealer);
        if (!zlink::discovery::set_socket_routing_id (dealer, NULL, NULL)) {
            (void) _lifecycle.close_socket (dealer);
            return -1;
        }
        apply_socket_options_locked (dealer);
        const int linger = 200;
        const int sndtimeo_ms = 500;
        const int rcvtimeo_ms = 500;
        dealer->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger, sizeof (linger));
        dealer->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &sndtimeo_ms,
                            sizeof (sndtimeo_ms));
        dealer->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO, &rcvtimeo_ms,
                            sizeof (rcvtimeo_ms));
        if (dealer->connect (uplink_endpoint_.c_str ()) != 0) {
            (void) _lifecycle.close_socket (dealer);
            return -1;
        }
        _control_dealers[uplink_endpoint_] = dealer;
        *dealer_out_ = dealer;
        return 0;
    }

    *dealer_out_ = it->second;
    return 0;
}

int discovery_t::ensure_topology_reporters ()
{
    scoped_lock_t uplink_lock (_uplink_sync);
    std::vector<std::string> endpoints;
    {
        scoped_lock_t lock (_sync);
        for (std::set<std::string>::const_iterator it =
               _registry_uplink_endpoints.begin ();
             it != _registry_uplink_endpoints.end (); ++it)
            endpoints.push_back (*it);
    }
    size_t ready_count = 0;
    for (size_t i = 0; i < endpoints.size (); ++i) {
        socket_base_t *dealer = NULL;
        if (ensure_topology_reporter_locked (endpoints[i], &dealer) != 0) {
            discovery_debugf_local ("uplink connect failed endpoint=%s errno=%d",
                                    endpoints[i].c_str (), errno);
            continue;
        }

        if (dealer
            && wait_socket_event_local (static_cast<void *> (dealer),
                                        ZLINK_POLLOUT, 10))
            ++ready_count;
    }
    return ready_count == 0 ? -1 : 0;
}

void discovery_t::flush_topology_reports ()
{
    if (ensure_topology_reporters () != 0)
        return;

    std::vector<topology_key_t> keys;
    std::vector<zlink_registry_topology_entry_t> entries;
    {
        scoped_lock_t lock (_sync);
        for (std::map<topology_key_t, topology_summary_t>::iterator it =
               _summary_store.begin ();
             it != _summary_store.end (); ++it) {
            if (!it->second.dirty)
                continue;
            keys.push_back (it->first);
            entries.push_back (it->second.entry);
        }
    }

    if (entries.empty ())
        return;

    std::vector<bool> sent (entries.size (), false);
    std::vector<socket_base_t *> dealers;
    {
        scoped_lock_t lock (_sync);
        for (std::map<std::string, socket_base_t *>::const_iterator it =
               _report_dealers.begin ();
             it != _report_dealers.end (); ++it) {
            if (it->second)
                dealers.push_back (it->second);
        }
    }
    if (dealers.empty ())
        return;

    scoped_lock_t uplink_lock (_uplink_sync);
    for (size_t i = 0; i < entries.size (); ++i) {
        bool all_sent = true;
        for (size_t d = 0; d < dealers.size (); ++d) {
            if (!wait_socket_event_local (static_cast<void *> (dealers[d]),
                                          ZLINK_POLLOUT, 0)) {
                all_sent = false;
                continue;
            }
            const bool dealer_sent =
              send_topology_report_frames_local (dealers[d], entries[i]);
            if (!dealer_sent)
                all_sent = false;
        }
        sent[i] = all_sent;
        if (!all_sent) {
            discovery_debugf_local (
              "topology report send failed kind=%u service=%s errno=%d",
              static_cast<unsigned int> (entries[i].service_kind),
              entries[i].service_name, errno);
        }
    }

    {
        scoped_lock_t lock (_sync);
        for (size_t i = 0; i < keys.size (); ++i) {
            std::map<topology_key_t, topology_summary_t>::iterator it =
              _summary_store.find (keys[i]);
            if (it == _summary_store.end () || !sent[i])
                continue;
            it->second.dirty = false;
        }
    }
}

void discovery_t::flush_gateway_peer_reports ()
{
    if (ensure_topology_reporters () != 0)
        return;

    std::vector<gateway_peer_key_t> keys;
    std::vector<zlink_registry_gateway_peer_entry_t> entries;
    {
        scoped_lock_t lock (_sync);
        for (std::map<gateway_peer_key_t, gateway_peer_summary_t>::iterator it =
               _gateway_peer_summary_store.begin ();
             it != _gateway_peer_summary_store.end (); ++it) {
            if (!it->second.dirty)
                continue;
            keys.push_back (it->first);
            entries.push_back (it->second.entry);
        }
    }

    if (entries.empty ())
        return;

    std::vector<bool> sent (entries.size (), false);
    std::vector<socket_base_t *> dealers;
    {
        scoped_lock_t lock (_sync);
        for (std::map<std::string, socket_base_t *>::const_iterator it =
               _report_dealers.begin ();
             it != _report_dealers.end (); ++it) {
            if (it->second)
                dealers.push_back (it->second);
        }
    }
    if (dealers.empty ())
        return;

    scoped_lock_t uplink_lock (_uplink_sync);
    for (size_t i = 0; i < entries.size (); ++i) {
        bool all_sent = true;
        for (size_t d = 0; d < dealers.size (); ++d) {
            if (!wait_socket_event_local (static_cast<void *> (dealers[d]),
                                          ZLINK_POLLOUT, 0)) {
                all_sent = false;
                continue;
            }
            if (!send_gateway_peer_report_frames_local (dealers[d], entries[i]))
                all_sent = false;
        }
        sent[i] = all_sent;
        if (!all_sent) {
            discovery_debugf_local (
              "gateway peer report send failed service=%s errno=%d",
              entries[i].service_name, errno);
        }
    }

    {
        scoped_lock_t lock (_sync);
        for (size_t i = 0; i < keys.size (); ++i) {
            std::map<gateway_peer_key_t, gateway_peer_summary_t>::iterator it =
              _gateway_peer_summary_store.find (keys[i]);
            if (it == _gateway_peer_summary_store.end () || !sent[i])
                continue;
            it->second.dirty = false;
        }
    }
}

void discovery_t::refresh_registered_service_heartbeats (uint64_t now_ms_)
{
    std::vector<registered_service_t> services;
    {
        scoped_lock_t lock (_sync);
        for (std::map<registered_service_key_t, registered_service_t>::const_iterator
               it = _registered_services.begin ();
             it != _registered_services.end (); ++it) {
            if (it->second.uplink_endpoint.empty ())
                continue;
            if (it->second.last_heartbeat_ms != 0
                && now_ms_ - it->second.last_heartbeat_ms
                     < _heartbeat_interval_ms)
                continue;
            services.push_back (it->second);
        }
    }

    if (services.empty ())
        return;

    scoped_lock_t uplink_lock (_uplink_sync);
    for (size_t i = 0; i < services.size (); ++i) {
        socket_base_t *dealer = NULL;
        if (ensure_control_dealer_locked (services[i].uplink_endpoint, &dealer)
            != 0)
            continue;
        if (!wait_socket_event_local (static_cast<void *> (dealer),
                                      ZLINK_POLLOUT, 0))
            continue;
        if (discovery_protocol::send_u16 (
              dealer, discovery_protocol::msg_heartbeat, ZLINK_SNDMORE)
              < 0
            || discovery_protocol::send_u16 (dealer, services[i].service_type,
                                             ZLINK_SNDMORE)
                 < 0
            || discovery_protocol::send_string (dealer, services[i].service_name,
                                                ZLINK_SNDMORE)
                 < 0
            || discovery_protocol::send_string (dealer, services[i].endpoint, 0)
                 < 0)
            continue;

        scoped_lock_t lock (_sync);
        registered_service_key_t key;
        key.service_type = services[i].service_type;
        key.service_name = services[i].service_name;
        key.endpoint = services[i].endpoint;
        std::map<registered_service_key_t, registered_service_t>::iterator it =
          _registered_services.find (key);
        if (it != _registered_services.end ())
            it->second.last_heartbeat_ms = now_ms_;
    }
}
}
