/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "core/recv_internal.hpp"
#include "services/discovery/discovery.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/discovery/routing_id_utils.hpp"
#include "services/discovery/discovery_runtime_internal.hpp"

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

static bool wait_socket_event_local (void *socket_,
                                     short events_,
                                     long timeout_ms_)
{
    return zlink::wait_socket_events_internal (socket_, events_, timeout_ms_) > 0;
}

}

discovery_uplink_runtime_t::discovery_uplink_runtime_t ()
{
}

socket_base_t *discovery_uplink_runtime_t::create_uplink_dealer (
  discovery_t *owner_,
  const std::string &uplink_endpoint_,
  int linger_,
  int sndtimeo_ms_,
  int rcvtimeo_ms_,
  bool use_bootstrap_routing_id_)
{
    socket_base_t *dealer = owner_->create_tracked_socket (ZLINK_CORE_SOCKET_DEALER);
    if (!dealer)
        return NULL;

    const bool routing_ok =
      use_bootstrap_routing_id_
        ? owner_->_bootstrap_runtime->ensure_socket_routing_id (dealer)
        : zlink::discovery::set_socket_routing_id (dealer, NULL, NULL);
    if (!routing_ok) {
        (void) owner_->close_tracked_socket (dealer, 10000);
        return NULL;
    }

    owner_->_bootstrap_runtime->apply_socket_options (dealer);
    dealer->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger_, sizeof (linger_));
    dealer->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &sndtimeo_ms_,
                        sizeof (sndtimeo_ms_));
    dealer->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO, &rcvtimeo_ms_,
                        sizeof (rcvtimeo_ms_));
    if (dealer->connect (uplink_endpoint_.c_str ()) != 0) {
        (void) owner_->close_tracked_socket (dealer, 10000);
        return NULL;
    }

    return dealer;
}

void discovery_uplink_runtime_t::apply_socket_option_to_existing (
  discovery_t *owner_, int option_, const void *optval_, size_t optvallen_) const
{
    scoped_lock_t lock (owner_->_sync);
    for (std::map<std::string, socket_base_t *>::const_iterator it =
           _report_dealers.begin ();
         it != _report_dealers.end (); ++it) {
        if (it->second)
            it->second->setsockopt (option_, optval_, optvallen_);
    }

    for (std::map<std::string, socket_base_t *>::const_iterator it =
           _control_dealers.begin ();
         it != _control_dealers.end (); ++it) {
        if (it->second)
            it->second->setsockopt (option_, optval_, optvallen_);
    }
}

int discovery_uplink_runtime_t::ensure_topology_reporter (
  discovery_t *owner_,
  const std::string &uplink_endpoint_,
  socket_base_t **dealer_out_)
{
    if (!dealer_out_) {
        errno = EINVAL;
        return -1;
    }
    *dealer_out_ = NULL;

    scoped_lock_t lock (owner_->_sync);
    std::map<std::string, socket_base_t *>::iterator it =
      _report_dealers.find (uplink_endpoint_);
    if (it == _report_dealers.end () || !it->second) {
        socket_base_t *dealer =
          create_uplink_dealer (owner_, uplink_endpoint_, 200, 100, 1000, true);
        if (!dealer)
            return -1;
        _report_dealers[uplink_endpoint_] = dealer;
        *dealer_out_ = dealer;
        return 0;
    }

    *dealer_out_ = it->second;
    return 0;
}

int discovery_uplink_runtime_t::ensure_control_dealer (
  discovery_t *owner_,
  const std::string &uplink_endpoint_,
  socket_base_t **dealer_out_)
{
    if (!dealer_out_) {
        errno = EINVAL;
        return -1;
    }
    *dealer_out_ = NULL;

    scoped_lock_t lock (owner_->_sync);
    std::map<std::string, socket_base_t *>::iterator it =
      _control_dealers.find (uplink_endpoint_);
    if (it == _control_dealers.end () || !it->second) {
        socket_base_t *dealer = create_uplink_dealer (owner_, uplink_endpoint_,
                                                      200, 500, 500, false);
        if (!dealer)
            return -1;
        _control_dealers[uplink_endpoint_] = dealer;
        *dealer_out_ = dealer;
        return 0;
    }

    *dealer_out_ = it->second;
    return 0;
}

int discovery_uplink_runtime_t::ensure_topology_reporters (discovery_t *owner_)
{
    scoped_lock_t uplink_lock (owner_->_uplink_sync);
    std::vector<std::string> endpoints;
    collect_uplink_endpoints (owner_, &endpoints);
    size_t ready_count = 0;
    for (size_t i = 0; i < endpoints.size (); ++i) {
        socket_base_t *dealer = NULL;
        if (ensure_topology_reporter (owner_, endpoints[i], &dealer) != 0) {
            discovery_debugf_local ("uplink connect failed endpoint=%s errno=%d",
                                    endpoints[i].c_str (), errno);
            continue;
        }

        if (dealer
            && wait_socket_event_local (static_cast<void *> (dealer),
                                        ZLINK_POLLOUT, 10)) {
            ++ready_count;
        }
    }
    return ready_count == 0 ? -1 : 0;
}

void discovery_uplink_runtime_t::flush_topology_reports (discovery_t *owner_)
{
    if (ensure_topology_reporters (owner_) != 0)
        return;

    std::vector<discovery_t::topology_key_t> keys;
    std::vector<zlink_registry_topology_entry_t> entries;
    {
        scoped_lock_t lock (owner_->_sync);
        for (std::map<discovery_t::topology_key_t,
                      discovery_t::topology_summary_t>::iterator it =
               owner_->_summary_store.begin ();
             it != owner_->_summary_store.end (); ++it) {
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
        scoped_lock_t lock (owner_->_sync);
        for (std::map<std::string, socket_base_t *>::const_iterator it =
               _report_dealers.begin ();
             it != _report_dealers.end (); ++it) {
            if (it->second)
                dealers.push_back (it->second);
        }
    }
    if (dealers.empty ())
        return;

    scoped_lock_t uplink_lock (owner_->_uplink_sync);
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
        scoped_lock_t lock (owner_->_sync);
        for (size_t i = 0; i < keys.size (); ++i) {
            std::map<discovery_t::topology_key_t,
                     discovery_t::topology_summary_t>::iterator it =
              owner_->_summary_store.find (keys[i]);
            if (it == owner_->_summary_store.end () || !sent[i])
                continue;
            it->second.dirty = false;
        }
    }
}

void discovery_uplink_runtime_t::refresh_registered_service_heartbeats (
  discovery_t *owner_, uint64_t now_ms_)
{
    std::vector<discovery_t::registered_service_t> services;
    {
        scoped_lock_t lock (owner_->_sync);
        for (std::map<discovery_t::registered_service_key_t,
                      discovery_t::registered_service_t>::const_iterator it =
               owner_->_registered_services.begin ();
             it != owner_->_registered_services.end (); ++it) {
            if (it->second.uplink_endpoint.empty ())
                continue;
            if (it->second.last_heartbeat_ms != 0
                && now_ms_ - it->second.last_heartbeat_ms
                     < owner_->_bootstrap_runtime->heartbeat_interval_ms ()) {
                continue;
            }
            services.push_back (it->second);
        }
    }

    if (services.empty ())
        return;

    scoped_lock_t uplink_lock (owner_->_uplink_sync);
    for (size_t i = 0; i < services.size (); ++i) {
        socket_base_t *dealer = NULL;
        if (ensure_control_dealer (owner_, services[i].uplink_endpoint, &dealer)
            != 0) {
            continue;
        }
        if (!wait_socket_event_local (static_cast<void *> (dealer),
                                      ZLINK_POLLOUT, 0)) {
            continue;
        }
        if (discovery_protocol::send_u16 (
              dealer, discovery_protocol::msg_heartbeat, ZLINK_SNDMORE)
              < 0
            || discovery_protocol::send_u16 (dealer, services[i].service_type,
                                             ZLINK_SNDMORE)
                 < 0
            || discovery_protocol::send_u16 (dealer, services[i].service_role,
                                             ZLINK_SNDMORE)
                 < 0
            || discovery_protocol::send_string (dealer, services[i].service_name,
                                                ZLINK_SNDMORE)
                 < 0
            || discovery_protocol::send_string (dealer, services[i].endpoint, 0)
                 < 0) {
            continue;
        }

        scoped_lock_t lock (owner_->_sync);
        discovery_t::registered_service_key_t key;
        key.service_type = services[i].service_type;
        key.service_role = services[i].service_role;
        key.service_name = services[i].service_name;
        key.endpoint = services[i].endpoint;
        std::map<discovery_t::registered_service_key_t,
                 discovery_t::registered_service_t>::iterator it =
          owner_->_registered_services.find (key);
        if (it != owner_->_registered_services.end ())
            it->second.last_heartbeat_ms = now_ms_;
    }
}

void discovery_uplink_runtime_t::remember_bootstrap_success (
  discovery_t *owner_,
  const std::string &registry_endpoint_,
  const std::string &uplink_endpoint_,
  socket_base_t *bootstrap_dealer_)
{
    socket_base_t *orphaned_dealer = NULL;
    (void) registry_endpoint_;

    {
        scoped_lock_t lock (owner_->_sync);
        _registry_uplink_endpoints.insert (uplink_endpoint_);
        _latest_registry_uplink_endpoint = uplink_endpoint_;
        if (bootstrap_dealer_) {
            std::map<std::string, socket_base_t *>::iterator it =
              _report_dealers.find (uplink_endpoint_);
            if (it == _report_dealers.end () || !it->second)
                _report_dealers[uplink_endpoint_] = bootstrap_dealer_;
            else
                orphaned_dealer = bootstrap_dealer_;
        }
    }

    if (orphaned_dealer)
        (void) owner_->close_tracked_socket_and_wait (orphaned_dealer, 1000);
}

void discovery_uplink_runtime_t::collect_uplink_endpoints (
  discovery_t *owner_, std::vector<std::string> *out_) const
{
    if (!out_)
        return;
    out_->clear ();
    scoped_lock_t lock (owner_->_sync);
    for (std::set<std::string>::const_iterator it =
           _registry_uplink_endpoints.begin ();
         it != _registry_uplink_endpoints.end (); ++it) {
        out_->push_back (*it);
    }
}

bool discovery_uplink_runtime_t::latest_registry_uplink (
  discovery_t *owner_, std::string *out_) const
{
    if (!out_)
        return false;

    scoped_lock_t lock (owner_->_sync);
    if (_latest_registry_uplink_endpoint.empty ())
        return false;
    *out_ = _latest_registry_uplink_endpoint;
    return true;
}

void discovery_uplink_runtime_t::take_shutdown_state (
  discovery_t *owner_,
  std::vector<std::pair<std::string, socket_base_t *> > *report_dealers,
  std::vector<std::pair<std::string, socket_base_t *> > *control_dealers)
{
    if (!report_dealers || !control_dealers)
        return;
    report_dealers->clear ();
    control_dealers->clear ();

    scoped_lock_t lock (owner_->_sync);
    for (std::map<std::string, socket_base_t *>::iterator it =
           _report_dealers.begin ();
         it != _report_dealers.end (); ++it) {
        if (it->second) {
            report_dealers->push_back (std::make_pair (it->first, it->second));
            it->second = NULL;
        }
    }
    for (std::map<std::string, socket_base_t *>::iterator it =
           _control_dealers.begin ();
         it != _control_dealers.end (); ++it) {
        if (it->second) {
            control_dealers->push_back (std::make_pair (it->first, it->second));
            it->second = NULL;
        }
    }
    _report_dealers.clear ();
    _control_dealers.clear ();
    _registry_uplink_endpoints.clear ();
    _latest_registry_uplink_endpoint.clear ();
}

int discovery_t::ensure_topology_reporters ()
{
    return _uplink_runtime->ensure_topology_reporters (this);
}

void discovery_t::flush_topology_reports ()
{
    _uplink_runtime->flush_topology_reports (this);
}

void discovery_t::refresh_registered_service_heartbeats (uint64_t now_ms_)
{
    _uplink_runtime->refresh_registered_service_heartbeats (this, now_ms_);
}
}
