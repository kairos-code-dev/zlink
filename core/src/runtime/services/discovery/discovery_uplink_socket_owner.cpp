/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/discovery/discovery_access.hpp"
#include "services/discovery/discovery.hpp"
#include "services/discovery/discovery_runtime_internal.hpp"
#include "services/discovery/routing_id_utils.hpp"

namespace zlink
{
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
    socket_base_t *dealer = discovery_access_t::create_tracked_socket (
      owner_, ZLINK_CORE_SOCKET_DEALER);
    if (!dealer)
        return NULL;

    const bool routing_ok =
      use_bootstrap_routing_id_
        ? discovery_access_t::bootstrap_runtime (owner_)->ensure_socket_routing_id (dealer)
        : zlink::discovery::set_socket_routing_id (dealer, NULL, NULL);
    if (!routing_ok) {
        (void) discovery_access_t::close_tracked_socket (owner_, dealer,
                                                         10000);
        return NULL;
    }

    discovery_access_t::bootstrap_runtime (owner_)->apply_socket_options (dealer);
    dealer->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger_, sizeof (linger_));
    dealer->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &sndtimeo_ms_,
                        sizeof (sndtimeo_ms_));
    dealer->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO, &rcvtimeo_ms_,
                        sizeof (rcvtimeo_ms_));
    if (dealer->connect (uplink_endpoint_.c_str ()) != 0) {
        (void) discovery_access_t::close_tracked_socket (owner_, dealer,
                                                         10000);
        return NULL;
    }

    return dealer;
}

void discovery_uplink_runtime_t::apply_socket_option_to_existing (
  discovery_t *owner_, int option_, const void *optval_, size_t optvallen_) const
{
    scoped_lock_t lock (discovery_access_t::sync (owner_));
    for (std::map<std::string, socket_base_t *>::const_iterator it =
           _socket_owner_state.report_dealers.begin ();
         it != _socket_owner_state.report_dealers.end (); ++it) {
        if (it->second)
            it->second->setsockopt (option_, optval_, optvallen_);
    }

    for (std::map<std::string, socket_base_t *>::const_iterator it =
           _socket_owner_state.control_dealers.begin ();
         it != _socket_owner_state.control_dealers.end (); ++it) {
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

    scoped_lock_t lock (discovery_access_t::sync (owner_));
    std::map<std::string, socket_base_t *>::iterator it =
      _socket_owner_state.report_dealers.find (uplink_endpoint_);
    if (it == _socket_owner_state.report_dealers.end () || !it->second) {
        socket_base_t *dealer =
          create_uplink_dealer (owner_, uplink_endpoint_, 200, 100, 1000, true);
        if (!dealer)
            return -1;
        _socket_owner_state.report_dealers[uplink_endpoint_] = dealer;
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

    scoped_lock_t lock (discovery_access_t::sync (owner_));
    std::map<std::string, socket_base_t *>::iterator it =
      _socket_owner_state.control_dealers.find (uplink_endpoint_);
    if (it == _socket_owner_state.control_dealers.end () || !it->second) {
        socket_base_t *dealer = create_uplink_dealer (owner_, uplink_endpoint_,
                                                      200, 500, 500, false);
        if (!dealer)
            return -1;
        _socket_owner_state.control_dealers[uplink_endpoint_] = dealer;
        *dealer_out_ = dealer;
        return 0;
    }

    *dealer_out_ = it->second;
    return 0;
}

void discovery_uplink_runtime_t::remember_registry_uplink (
  discovery_t *owner_, const std::string &uplink_endpoint_)
{
    scoped_lock_t lock (discovery_access_t::sync (owner_));
    _socket_owner_state.registry_uplink_endpoints.insert (uplink_endpoint_);
    _socket_owner_state.latest_registry_uplink_endpoint = uplink_endpoint_;
}

void discovery_uplink_runtime_t::adopt_report_dealer (
  discovery_t *owner_,
  const std::string &uplink_endpoint_,
  socket_base_t *bootstrap_dealer_)
{
    if (!bootstrap_dealer_)
        return;

    socket_base_t *orphaned_dealer = NULL;
    {
        scoped_lock_t lock (discovery_access_t::sync (owner_));
        std::map<std::string, socket_base_t *>::iterator it =
          _socket_owner_state.report_dealers.find (uplink_endpoint_);
        if (it == _socket_owner_state.report_dealers.end () || !it->second)
            _socket_owner_state.report_dealers[uplink_endpoint_] = bootstrap_dealer_;
        else
            orphaned_dealer = bootstrap_dealer_;
    }

    if (orphaned_dealer)
        (void) discovery_access_t::close_tracked_socket_and_wait (
          owner_, orphaned_dealer, 1000);
}

void discovery_uplink_runtime_t::collect_uplink_endpoints (
  discovery_t *owner_, std::vector<std::string> *out_) const
{
    if (!out_)
        return;
    out_->clear ();
    scoped_lock_t lock (discovery_access_t::sync (owner_));
    for (std::set<std::string>::const_iterator it =
           _socket_owner_state.registry_uplink_endpoints.begin ();
         it != _socket_owner_state.registry_uplink_endpoints.end (); ++it) {
        out_->push_back (*it);
    }
}

bool discovery_uplink_runtime_t::latest_registry_uplink (
  discovery_t *owner_, std::string *out_) const
{
    if (!out_)
        return false;

    scoped_lock_t lock (discovery_access_t::sync (owner_));
    if (_socket_owner_state.latest_registry_uplink_endpoint.empty ())
        return false;
    *out_ = _socket_owner_state.latest_registry_uplink_endpoint;
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

    scoped_lock_t lock (discovery_access_t::sync (owner_));
    for (std::map<std::string, socket_base_t *>::iterator it =
           _socket_owner_state.report_dealers.begin ();
         it != _socket_owner_state.report_dealers.end (); ++it) {
        if (it->second) {
            report_dealers->push_back (std::make_pair (it->first, it->second));
            it->second = NULL;
        }
    }
    for (std::map<std::string, socket_base_t *>::iterator it =
           _socket_owner_state.control_dealers.begin ();
         it != _socket_owner_state.control_dealers.end (); ++it) {
        if (it->second) {
            control_dealers->push_back (std::make_pair (it->first, it->second));
            it->second = NULL;
        }
    }
    _socket_owner_state.report_dealers.clear ();
    _socket_owner_state.control_dealers.clear ();
    _socket_owner_state.registry_uplink_endpoints.clear ();
    _socket_owner_state.latest_registry_uplink_endpoint.clear ();
}
}
