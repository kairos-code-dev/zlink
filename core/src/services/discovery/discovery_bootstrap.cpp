/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "core/recv_internal.hpp"
#include "services/discovery/discovery.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/discovery/discovery_runtime_internal.hpp"
#include "services/control/service_control_runtime.hpp"
#include "services/discovery/routing_id_utils.hpp"

#include <cstdarg>
#include <cstdio>
#include <string.h>
#if !defined _WIN32
#include <unistd.h>
#endif

namespace zlink
{
namespace
{
static bool discovery_frame_has_more (const zlink_msg_t &frame_)
{
    return (reinterpret_cast<const msg_t *> (&frame_)->flags () & msg_t::more)
           != 0;
}

static void discovery_sleep_1ms ()
{
#if defined _WIN32
    Sleep (1);
#else
    usleep (1000);
#endif
}

static bool is_supported_registry_transport (const char *endpoint_)
{
    if (!endpoint_ || endpoint_[0] == '\0')
        return false;

    const char *scheme_end = strstr (endpoint_, "://");
    if (!scheme_end)
        return false;

    const size_t scheme_len = static_cast<size_t> (scheme_end - endpoint_);
    return (scheme_len == 3 && strncmp (endpoint_, "tcp", 3) == 0)
           || (scheme_len == 2 && strncmp (endpoint_, "ws", 2) == 0)
           || (scheme_len == 3 && strncmp (endpoint_, "wss", 3) == 0)
           || (scheme_len == 3 && strncmp (endpoint_, "tls", 3) == 0);
}

static void discovery_debugf (const char *fmt_, ...)
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

static void close_frames (std::vector<zlink_msg_t> *frames_)
{
    if (!frames_)
        return;
    for (size_t i = 0; i < frames_->size (); ++i)
        zlink_msg_close (&(*frames_)[i]);
    frames_->clear ();
}

static bool wait_socket_event (void *socket_, short events_, long timeout_ms_)
{
    return zlink::wait_socket_events_internal (socket_, events_, timeout_ms_) > 0;
}

static bool recv_dealer_frames (socket_base_t *socket_,
                                std::vector<zlink_msg_t> *frames_)
{
    if (!socket_ || !frames_)
        return false;
    frames_->clear ();
    while (true) {
        zlink_msg_t frame;
        zlink_msg_init (&frame);
        if (socket_->recv (reinterpret_cast<msg_t *> (&frame), 0) != 0) {
            zlink_msg_close (&frame);
            close_frames (frames_);
            return false;
        }
        frames_->push_back (frame);
        if (!discovery_frame_has_more (frame))
            break;
        if (!wait_socket_event (static_cast<void *> (socket_), ZLINK_POLLIN,
                                500)) {
            errno = EAGAIN;
            close_frames (frames_);
            return false;
        }
    }
    return !frames_->empty ();
}
}

discovery_bootstrap_runtime_t::bootstrap_state_t::bootstrap_state_t () :
    dealer (NULL),
    request_sent (false),
    request_started_ms (0)
{
}

discovery_bootstrap_socket_config_t::discovery_bootstrap_socket_config_t () :
    _routing_id_locked (false)
{
    memset (&_routing_id, 0, sizeof (_routing_id));
}

void discovery_bootstrap_socket_config_t::lock_routing_id (
  discovery_t *owner_)
{
    scoped_lock_t lock (owner_->_sync);
    _routing_id_locked = true;
}

int discovery_bootstrap_socket_config_t::set_routing_id (
  discovery_t *owner_, const void *data_, size_t size_)
{
    service_public_api_scope_t admission (owner_->_public_api);
    if (!admission.acquired ())
        return -1;
    if (!data_ || size_ == 0 || size_ > sizeof (_routing_id.data)) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (owner_->_sync);
    if (_routing_id_locked || owner_->_sub_socket
        || !owner_->_connected_endpoints.empty ()
        || owner_->_bootstrap_runtime->has_bootstrap_activity_locked ()) {
        errno = EFSM;
        return -1;
    }
    _routing_id_override.assign (static_cast<const char *> (data_), size_);
    _routing_id.size = static_cast<uint8_t> (size_);
    memcpy (_routing_id.data, data_, size_);
    return 0;
}

int discovery_bootstrap_socket_config_t::routing_id (
  discovery_t *owner_, zlink_routing_id_t *out_) const
{
    service_public_api_scope_t admission (owner_->_public_api);
    if (!admission.acquired ())
        return -1;
    if (!out_) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (owner_->_sync);
    *out_ = _routing_id;
    if (out_->size > 0)
        return 0;

    errno = EAGAIN;
    return -1;
}

void discovery_bootstrap_socket_config_t::upsert_socket_option (
  int option_, const void *optval_, size_t optvallen_)
{
    for (size_t i = 0; i < _socket_options.size (); ++i) {
        if (_socket_options[i].option == option_) {
            _socket_options[i].value.assign (
              static_cast<const unsigned char *> (optval_),
              static_cast<const unsigned char *> (optval_) + optvallen_);
            return;
        }
    }

    socket_opt_t opt;
    opt.option = option_;
    opt.value.assign (static_cast<const unsigned char *> (optval_),
                      static_cast<const unsigned char *> (optval_)
                        + optvallen_);
    _socket_options.push_back (opt);
}

int discovery_bootstrap_socket_config_t::set_option (
  discovery_t *owner_, int option_, const void *optval_, size_t optvallen_)
{
    service_public_api_scope_t admission (owner_->_public_api);
    if (!admission.acquired ())
        return -1;
    if (!optval_ || optvallen_ == 0) {
        errno = EINVAL;
        return -1;
    }

    {
        scoped_lock_t lock (owner_->_sync);
        upsert_socket_option (option_, optval_, optvallen_);
    }
    apply_socket_option_to_existing (owner_, option_, optval_, optvallen_);
    return 0;
}

int discovery_bootstrap_socket_config_t::set_tls_client (
  discovery_t *owner_,
  const char *ca_cert_,
  const char *hostname_,
  int trust_system_)
{
    service_public_api_scope_t admission (owner_->_public_api);
    if (!admission.acquired ())
        return -1;

    const char *tls_ca = ca_cert_ ? ca_cert_ : "";
    const char *tls_hostname = hostname_ ? hostname_ : "";
    const size_t tls_ca_len = strlen (tls_ca) + 1;
    const size_t tls_hostname_len = strlen (tls_hostname) + 1;

    const struct discovery_tls_option_t
    {
        int option;
        const void *value;
        size_t size;
    } options[] = {{ZLINK_INTERNAL_OPT_TLS_CA, tls_ca, tls_ca_len},
                   {ZLINK_INTERNAL_OPT_TLS_HOSTNAME, tls_hostname,
                    tls_hostname_len},
                   {ZLINK_INTERNAL_OPT_TLS_TRUST_SYSTEM, &trust_system_,
                    sizeof (trust_system_)}};

    {
        scoped_lock_t lock (owner_->_sync);
        for (size_t i = 0; i < sizeof (options) / sizeof (options[0]); ++i)
            upsert_socket_option (options[i].option, options[i].value,
                                  options[i].size);
    }

    for (size_t i = 0; i < sizeof (options) / sizeof (options[0]); ++i) {
        apply_socket_option_to_existing (
          owner_, options[i].option, options[i].value, options[i].size);
    }
    return 0;
}

void discovery_bootstrap_socket_config_t::apply_socket_options (
  socket_base_t *socket_) const
{
    if (!socket_)
        return;
    for (size_t i = 0; i < _socket_options.size (); ++i) {
        if (!_socket_options[i].value.empty ()) {
            socket_->setsockopt (_socket_options[i].option,
                                 &_socket_options[i].value[0],
                                 _socket_options[i].value.size ());
        }
    }
}

void discovery_bootstrap_socket_config_t::apply_socket_option_to_existing (
  discovery_t *owner_, int option_, const void *optval_, size_t optvallen_) const
{
    scoped_lock_t lock (owner_->_sync);
    if (owner_->_sub_socket) {
        static_cast<socket_base_t *> (owner_->_sub_socket)
          ->setsockopt (option_, optval_, optvallen_);
    }

    owner_->_bootstrap_runtime->apply_socket_option_to_bootstrap_dealers (
      owner_, option_, optval_, optvallen_);
    owner_->_uplink_runtime->apply_socket_option_to_existing (
      owner_, option_, optval_, optvallen_);
}

bool discovery_bootstrap_socket_config_t::ensure_socket_routing_id (
  socket_base_t *socket_)
{
    if (!socket_)
        return false;
    if (_routing_id.size > 0 && _routing_id_override.empty ()) {
        return socket_->setsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID,
                                    _routing_id.data, _routing_id.size)
               == 0;
    }
    if (!zlink::discovery::set_socket_routing_id (
          socket_, &_routing_id_override, &_routing_id)) {
        return false;
    }
    if (_routing_id_override.empty () && _routing_id.size > 0) {
        _routing_id_override.assign (reinterpret_cast<const char *> (
                                       _routing_id.data),
                                     _routing_id.size);
    }
    return true;
}

discovery_bootstrap_runtime_t::discovery_bootstrap_runtime_t () :
    _registry_bootstrap_endpoints (_request_state.registry_bootstrap_endpoints),
    _bootstrapped_registry_endpoints (
      _request_state.bootstrapped_registry_endpoints),
    _bootstrap_states (_request_state.bootstrap_states),
    _registry_pub_endpoints (_request_state.registry_pub_endpoints),
    _heartbeat_interval_ms (_request_state.heartbeat_interval_ms)
{
}

int discovery_bootstrap_runtime_t::connect_registry (
  discovery_t *owner_, const char *registry_endpoint_)
{
    service_public_api_scope_t admission (owner_->_public_api);
    if (!admission.acquired ())
        return -1;
    if (!registry_endpoint_) {
        errno = EINVAL;
        return -1;
    }
    if (!is_supported_registry_transport (registry_endpoint_)) {
        errno = EPROTONOSUPPORT;
        return -1;
    }

    _socket_config.lock_routing_id (owner_);

    zlink::clock_t clock;
    const uint64_t deadline_ms = clock.now_ms () + 2000;
    while (clock.now_ms () < deadline_ms) {
        const int rc = bootstrap_registry (owner_, registry_endpoint_);
        if (rc == 0) {
            remember_bootstrap_endpoint (owner_, registry_endpoint_);
            break;
        }
        if (rc == -1 && errno != EAGAIN)
            return -1;
        discovery_sleep_1ms ();
    }

    if (!bootstrap_completed (owner_, registry_endpoint_)) {
        errno = EAGAIN;
        return -1;
    }

    if (owner_->ensure_control_task_active () != 0)
        return -1;

    owner_->emit_ready_changed (1);
    return 0;
}

int discovery_bootstrap_runtime_t::set_routing_id (discovery_t *owner_,
                                                   const void *data_,
                                                   size_t size_)
{
    return _socket_config.set_routing_id (owner_, data_, size_);
}

int discovery_bootstrap_runtime_t::routing_id (discovery_t *owner_,
                                               zlink_routing_id_t *out_) const
{
    return _socket_config.routing_id (owner_, out_);
}

int discovery_bootstrap_runtime_t::set_option (discovery_t *owner_,
                                               int option_,
                                               const void *optval_,
                                               size_t optvallen_)
{
    return _socket_config.set_option (owner_, option_, optval_, optvallen_);
}

int discovery_bootstrap_runtime_t::set_tls_client (discovery_t *owner_,
                                                   const char *ca_cert_,
                                                   const char *hostname_,
                                                   int trust_system_)
{
    return _socket_config.set_tls_client (owner_, ca_cert_, hostname_,
                                          trust_system_);
}

void discovery_bootstrap_runtime_t::apply_socket_options (
  socket_base_t *socket_) const
{
    _socket_config.apply_socket_options (socket_);
}

void discovery_bootstrap_runtime_t::apply_socket_option_to_existing (
  discovery_t *owner_, int option_, const void *optval_, size_t optvallen_) const
{
    _socket_config.apply_socket_option_to_existing (owner_, option_, optval_,
                                                    optvallen_);
}

bool discovery_bootstrap_runtime_t::ensure_socket_routing_id (
  socket_base_t *socket_)
{
    return _socket_config.ensure_socket_routing_id (socket_);
}

bool discovery_bootstrap_runtime_t::has_bootstrap_activity_locked () const
{
    return !_registry_pub_endpoints.empty ()
           || !_registry_bootstrap_endpoints.empty ();
}

void discovery_bootstrap_runtime_t::apply_socket_option_to_bootstrap_dealers (
  discovery_t *owner_, int option_, const void *optval_, size_t optvallen_) const
{
    (void) owner_;
    for (std::map<std::string, bootstrap_state_t>::const_iterator it =
           _bootstrap_states.begin ();
         it != _bootstrap_states.end (); ++it) {
        if (it->second.dealer)
            it->second.dealer->setsockopt (option_, optval_, optvallen_);
    }
}

void discovery_bootstrap_runtime_t::remember_bootstrap_endpoint (
  discovery_t *owner_, const char *registry_endpoint_)
{
    scoped_lock_t lock (owner_->_sync);
    _registry_bootstrap_endpoints.insert (registry_endpoint_);
}

bool discovery_bootstrap_runtime_t::bootstrap_completed (
  discovery_t *owner_, const char *registry_endpoint_) const
{
    scoped_lock_t lock (owner_->_sync);
    return _bootstrapped_registry_endpoints.count (registry_endpoint_) != 0;
}

void discovery_bootstrap_runtime_t::begin_bootstrap_request (
  discovery_t *owner_, const std::string &registry_endpoint_)
{
    scoped_lock_t lock (owner_->_sync);
    bootstrap_state_t &stored = _bootstrap_states[registry_endpoint_];
    stored.request_sent = true;
    stored.request_started_ms = zlink::clock_t ().now_ms ();
    discovery_debugf ("bootstrap request sent endpoint=%s dealer=%p",
                      registry_endpoint_.c_str (),
                      static_cast<void *> (stored.dealer));
}

bool discovery_bootstrap_runtime_t::reset_bootstrap_request_if_timed_out (
  discovery_t *owner_, const std::string &registry_endpoint_, uint64_t now_ms_)
{
    scoped_lock_t lock (owner_->_sync);
    std::map<std::string, bootstrap_state_t>::iterator it =
      _bootstrap_states.find (registry_endpoint_);
    if (it == _bootstrap_states.end () || it->second.request_started_ms == 0
        || now_ms_ - it->second.request_started_ms <= 2000) {
        return false;
    }

    discovery_debugf ("bootstrap timeout endpoint=%s",
                      registry_endpoint_.c_str ());
    (void) owner_->close_tracked_socket_and_wait (it->second.dealer, 1000);
    it->second.request_sent = false;
    it->second.request_started_ms = 0;
    return true;
}

void discovery_bootstrap_runtime_t::commit_bootstrap_success (
  discovery_t *owner_,
  const std::string &registry_endpoint_,
  const std::string &pub_endpoint_,
  const std::string &uplink_endpoint_,
  uint32_t heartbeat_interval_ms_,
  socket_base_t **adopted_report_dealer_out_)
{
    if (adopted_report_dealer_out_)
        *adopted_report_dealer_out_ = NULL;

    scoped_lock_t lock (owner_->_sync);
    _registry_pub_endpoints.insert (pub_endpoint_);
    _bootstrapped_registry_endpoints.insert (registry_endpoint_);
    if (heartbeat_interval_ms_ > 0)
        _heartbeat_interval_ms = heartbeat_interval_ms_;

    std::map<std::string, bootstrap_state_t>::iterator it =
      _bootstrap_states.find (registry_endpoint_);
    if (it == _bootstrap_states.end ())
        return;

    if (adopted_report_dealer_out_ && it->second.dealer
        && uplink_endpoint_ == registry_endpoint_) {
        *adopted_report_dealer_out_ = it->second.dealer;
        it->second.dealer = NULL;
    }
    if (it->second.dealer) {
        it->second.dealer->close ();
        it->second.dealer = NULL;
    }
    _bootstrap_states.erase (it);
}

int discovery_bootstrap_runtime_t::bootstrap_registry (
  discovery_t *owner_, const char *registry_endpoint_)
{
    socket_base_t *dealer = NULL;
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    if (ensure_bootstrap_dealer (owner_, registry_endpoint_, &dealer) != 0)
        return -1;
    bootstrap_state_t *state = NULL;
    {
        scoped_lock_t lock (owner_->_sync);
        state = &_bootstrap_states[registry_endpoint_];
        rid = _socket_config.routing_id_value ();
        discovery_debugf ("bootstrap state endpoint=%s sent=%d started=%llu dealer=%p",
                          registry_endpoint_, state->request_sent ? 1 : 0,
                          static_cast<unsigned long long> (
                            state->request_started_ms),
                          static_cast<void *> (state->dealer));
    }
    if (!state || !dealer) {
        errno = ENOTCONN;
        return -1;
    }

    if (!state->request_sent) {
        if (!wait_socket_event (static_cast<void *> (dealer), ZLINK_POLLOUT, 0))
            return 1;
        discovery_protocol::bootstrap_req_t req;
        memset (&req, 0, sizeof (req));
        req.msg_id = discovery_protocol::msg_bootstrap_req;
        req.service_type = owner_->_service_type;
        req.routing_id = rid;
        if (discovery_protocol::send_frame (dealer, &req, sizeof (req), 0)
            < 0) {
            if (errno == EAGAIN)
                return 1;
            discovery_debugf ("bootstrap send failed errno=%d", errno);
            return -1;
        }

        begin_bootstrap_request (owner_, registry_endpoint_);
        return 1;
    }

    if (!wait_socket_event (static_cast<void *> (dealer), ZLINK_POLLIN, 0)) {
        const uint64_t now_ms = zlink::clock_t ().now_ms ();
        (void) reset_bootstrap_request_if_timed_out (owner_, registry_endpoint_,
                                                     now_ms);
        return 1;
    }

    std::vector<zlink_msg_t> frames;
    if (!recv_dealer_frames (dealer, &frames)) {
        if (errno == EAGAIN)
            return 1;
        discovery_debugf ("bootstrap recv failed errno=%d", errno);
        return -1;
    }

    uint16_t msg_id = 0;
    uint32_t heartbeat_interval_ms = 0;
    uint32_t registry_id = 0;
    uint32_t feature_flags = 0;
    std::string pub_endpoint;
    std::string uplink_endpoint;
    bool ok = false;
    if (frames.size () == 1
        && zlink_msg_size (&frames[0])
             == sizeof (discovery_protocol::bootstrap_rep_t)) {
        discovery_protocol::bootstrap_rep_t rep;
        memcpy (&rep, zlink_msg_data (&frames[0]), sizeof (rep));
        msg_id = rep.msg_id;
        heartbeat_interval_ms = rep.heartbeat_interval_ms;
        registry_id = rep.registry_id;
        feature_flags = rep.feature_flags;
        pub_endpoint = rep.pub_endpoint;
        uplink_endpoint = rep.uplink_endpoint;
        ok = msg_id == discovery_protocol::msg_bootstrap_rep;
    }
    close_frames (&frames);
    (void) registry_id;
    (void) feature_flags;

    if (!ok || pub_endpoint.empty () || uplink_endpoint.empty ()) {
        errno = EPROTO;
        discovery_debugf ("bootstrap bad reply errno=%d", errno);
        return -1;
    }

    socket_base_t *adopted_report_dealer = NULL;
    commit_bootstrap_success (owner_, registry_endpoint_, pub_endpoint,
                              uplink_endpoint, heartbeat_interval_ms,
                              &adopted_report_dealer);
    owner_->_uplink_runtime->remember_registry_uplink (owner_, uplink_endpoint);
    owner_->_uplink_runtime->adopt_report_dealer (owner_, uplink_endpoint,
                                                  adopted_report_dealer);

    discovery_debugf ("bootstrap ok endpoint=%s pub=%s uplink=%s",
                      registry_endpoint_, pub_endpoint.c_str (),
                      uplink_endpoint.c_str ());

    (void) owner_->ensure_topology_reporters ();
    return 0;
}

int discovery_bootstrap_runtime_t::ensure_bootstrap_dealer (
  discovery_t *owner_,
  const std::string &registry_endpoint_,
  socket_base_t **dealer_out_)
{
    if (!dealer_out_) {
        errno = EINVAL;
        return -1;
    }
    *dealer_out_ = NULL;

    scoped_lock_t lock (owner_->_sync);
    bootstrap_state_t &state = _bootstrap_states[registry_endpoint_];
    if (!state.dealer) {
        state.dealer = owner_->create_tracked_socket (ZLINK_CORE_SOCKET_DEALER);
        if (!state.dealer)
            return -1;
        if (!ensure_socket_routing_id (state.dealer)) {
            (void) owner_->close_tracked_socket (state.dealer, 10000);
            state.dealer = NULL;
            return -1;
        }
        apply_socket_options (state.dealer);
        const int linger = 0;
        const int sndtimeo_ms = 0;
        const int rcvtimeo_ms = 0;
        state.dealer->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger,
                                  sizeof (linger));
        state.dealer->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &sndtimeo_ms,
                                  sizeof (sndtimeo_ms));
        state.dealer->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO, &rcvtimeo_ms,
                                  sizeof (rcvtimeo_ms));
        if (state.dealer->connect (registry_endpoint_.c_str ()) != 0) {
            (void) owner_->close_tracked_socket (state.dealer, 10000);
            state.dealer = NULL;
            return -1;
        }
    }

    *dealer_out_ = state.dealer;
    return 0;
}

void discovery_bootstrap_runtime_t::collect_pending_bootstrap_endpoints (
  discovery_t *owner_, std::vector<std::string> *out_) const
{
    if (!out_)
        return;
    out_->clear ();
    scoped_lock_t lock (owner_->_sync);
    for (std::set<std::string>::const_iterator it =
           _registry_bootstrap_endpoints.begin ();
         it != _registry_bootstrap_endpoints.end (); ++it) {
        if (_bootstrapped_registry_endpoints.count (*it) == 0)
            out_->push_back (*it);
    }
}

void discovery_bootstrap_runtime_t::collect_registry_pub_endpoints (
  discovery_t *owner_, std::set<std::string> *out_) const
{
    if (!out_)
        return;
    scoped_lock_t lock (owner_->_sync);
    *out_ = _registry_pub_endpoints;
}

void discovery_bootstrap_runtime_t::take_shutdown_state (
  discovery_t *owner_,
  std::vector<std::pair<std::string, socket_base_t *> > *bootstrap_dealers)
{
    if (!bootstrap_dealers)
        return;
    bootstrap_dealers->clear ();
    scoped_lock_t lock (owner_->_sync);
    for (std::map<std::string, bootstrap_state_t>::iterator it =
           _bootstrap_states.begin ();
         it != _bootstrap_states.end (); ++it) {
        if (it->second.dealer) {
            bootstrap_dealers->push_back (
              std::make_pair (it->first, it->second.dealer));
            it->second.dealer = NULL;
        }
    }
    _bootstrap_states.clear ();
    _registry_bootstrap_endpoints.clear ();
    _bootstrapped_registry_endpoints.clear ();
    _registry_pub_endpoints.clear ();
}

int discovery_t::connect_registry (const char *registry_endpoint_)
{
    return _bootstrap_runtime->connect_registry (this, registry_endpoint_);
}

int discovery_t::set_routing_id (const void *data_, size_t size_)
{
    return _bootstrap_runtime->set_routing_id (this, data_, size_);
}

int discovery_t::routing_id (zlink_routing_id_t *out_) const
{
    return _bootstrap_runtime->routing_id (const_cast<discovery_t *> (this),
                                           out_);
}

int discovery_t::set_tls_client (const char *ca_cert_,
                                 const char *hostname_,
                                 int trust_system_)
{
    return _bootstrap_runtime->set_tls_client (this, ca_cert_, hostname_,
                                               trust_system_);
}

void *discovery_t::monitor_open (int events_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return NULL;
    return _monitor.open (events_);
}

int discovery_t::bootstrap_registry (const char *registry_endpoint_)
{
    return _bootstrap_runtime->bootstrap_registry (this, registry_endpoint_);
}

namespace
{
}
} // namespace zlink
