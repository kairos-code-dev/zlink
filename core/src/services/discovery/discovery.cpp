/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "core/recv_internal.hpp"
#include "services/discovery/discovery.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/control/service_control_runtime.hpp"
#include "services/gateway/routing_id_utils.hpp"

#include "utils/err.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string.h>
#if !defined _WIN32
#include <unistd.h>
#endif

namespace zlink
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

static const uint32_t discovery_tag_value = 0x1e6700d6;

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

static bool is_valid_service_type (uint16_t service_type_)
{
    return service_type_ == discovery_protocol::service_type_gateway_receiver
           || service_type_ == discovery_protocol::service_type_spot_node;
}

static void close_frames (std::vector<zlink_msg_t> *frames_)
{
    if (!frames_)
        return;
    for (size_t i = 0; i < frames_->size (); ++i)
        zlink_msg_close (&(*frames_)[i]);
    frames_->clear ();
}

static bool send_topology_report_frames (socket_base_t *dealer_,
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

static bool send_gateway_peer_report_frames (
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

static bool wait_socket_event (void *socket_, short events_, long timeout_ms_)
{
    return zlink::wait_socket_events_internal (socket_, events_, timeout_ms_) > 0;
}

static std::string topology_routing_key (const zlink_routing_id_t &rid_)
{
    if (rid_.size == 0)
        return std::string ();
    return std::string (reinterpret_cast<const char *> (rid_.data), rid_.size);
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

static int recv_status_ack (socket_base_t *socket_,
                            uint16_t expected_msg_id_,
                            int *status_out_,
                            std::string *resolved_out_,
                            std::string *error_out_)
{
    if (!socket_ || !status_out_) {
        errno = EINVAL;
        return -1;
    }

    *status_out_ = -1;
    if (resolved_out_)
        resolved_out_->clear ();
    if (error_out_)
        error_out_->clear ();

    const uint64_t deadline_ms = zlink::clock_t ().now_ms () + 2000;
    while (zlink::clock_t ().now_ms () < deadline_ms) {
        if (!wait_socket_event (static_cast<void *> (socket_), ZLINK_POLLIN, 50))
            continue;

        std::vector<zlink_msg_t> frames;
        if (!recv_dealer_frames (socket_, &frames)) {
            if (errno == EAGAIN)
                continue;
            return -1;
        }

        uint16_t msg_id = 0;
        if (frames.size () >= 2
            && discovery_protocol::read_u16 (frames[0], &msg_id)
            && msg_id == expected_msg_id_) {
            uint8_t status = 0xFF;
            if (zlink_msg_size (&frames[1]) == sizeof (uint8_t))
                memcpy (&status, zlink_msg_data (&frames[1]),
                        sizeof (uint8_t));
            *status_out_ = static_cast<int> (status);
            if (resolved_out_ && frames.size () >= 3
                && expected_msg_id_ == discovery_protocol::msg_register_ack) {
                *resolved_out_ = discovery_protocol::read_string (frames[2]);
            }
            if (error_out_) {
                if (expected_msg_id_ == discovery_protocol::msg_register_ack
                    && frames.size () >= 4) {
                    *error_out_ = discovery_protocol::read_string (frames[3]);
                } else if (
                  expected_msg_id_ == discovery_protocol::msg_unregister_ack
                  && frames.size () >= 3) {
                    *error_out_ = discovery_protocol::read_string (frames[2]);
                }
            }
            close_frames (&frames);
            return 0;
        }

        close_frames (&frames);
    }

    errno = EAGAIN;
    return -1;
}

static int close_transient_dealer (ctx_t *ctx_, socket_base_t *&dealer_)
{
    if (!dealer_)
        return 0;
    if (!ctx_) {
        dealer_->stop ();
        dealer_->close ();
        dealer_ = NULL;
        return 0;
    }
    return ctx_->close_socket_and_wait (dealer_, 1000);
}

static int prepare_transient_dealer (ctx_t *ctx_,
                                     const std::string &uplink_,
                                     const zlink_routing_id_t *routing_id_,
                                     socket_base_t **dealer_out_)
{
    if (!ctx_ || !dealer_out_) {
        errno = EINVAL;
        return -1;
    }

    *dealer_out_ = NULL;
    socket_base_t *dealer = ctx_->create_socket (ZLINK_CORE_SOCKET_DEALER);
    if (!dealer)
        return -1;

    if (routing_id_ && routing_id_->size > 0) {
        if (dealer->setsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID, routing_id_->data,
                                routing_id_->size)
            != 0) {
            (void) close_transient_dealer (ctx_, dealer);
            return -1;
        }
    } else if (!zlink::discovery::set_socket_routing_id (dealer, NULL, NULL)) {
        (void) close_transient_dealer (ctx_, dealer);
        return -1;
    }

    const int linger = 200;
    const int sndtimeo_ms = 500;
    const int rcvtimeo_ms = 500;
    dealer->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger, sizeof (linger));
    dealer->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &sndtimeo_ms, sizeof (sndtimeo_ms));
    dealer->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO, &rcvtimeo_ms, sizeof (rcvtimeo_ms));
    if (dealer->connect (uplink_.c_str ()) != 0) {
        (void) close_transient_dealer (ctx_, dealer);
        return -1;
    }

    const uint64_t deadline_ms = clock_t ().now_ms () + 500;
    while (clock_t ().now_ms () < deadline_ms) {
        if (wait_socket_event (static_cast<void *> (dealer), ZLINK_POLLOUT, 50))
            break;
    }
    if (!wait_socket_event (static_cast<void *> (dealer), ZLINK_POLLOUT, 0)) {
        errno = EAGAIN;
        (void) close_transient_dealer (ctx_, dealer);
        return -1;
    }

    *dealer_out_ = dealer;
    return 0;
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

void discovery_t::set_discovery_summary_enabled (bool enabled_)
{
    scoped_lock_t lock (_sync);
    _discovery_summary_enabled = enabled_;
}

int discovery_t::connect_registry (const char *registry_endpoint_)
{
    service_public_api_scope_t admission (_public_api);
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

    {
        scoped_lock_t lock (_sync);
        _routing_id_locked = true;
    }

    zlink::clock_t clock;
    const uint64_t deadline_ms = clock.now_ms () + 2000;
    while (clock.now_ms () < deadline_ms) {
        const int rc = bootstrap_registry (registry_endpoint_);
        if (rc == 0) {
            scoped_lock_t lock (_sync);
            _registry_bootstrap_endpoints.insert (registry_endpoint_);
            break;
        }
        if (rc == -1 && errno != EAGAIN)
            return -1;
        discovery_sleep_1ms ();
    }

    {
        scoped_lock_t lock (_sync);
        if (_bootstrapped_registry_endpoints.count (registry_endpoint_) == 0) {
            errno = EAGAIN;
            return -1;
        }
    }

    if (_task_id == 0) {
        service_control_runtime_t *runtime = _ctx->service_control_runtime ();
        if (!runtime) {
            errno = ENOTSUP;
            return -1;
        }
        _task_id = runtime->add_periodic_task (control_task, this, 1, true);
        if (_task_id == 0)
            return -1;
    } else {
        service_control_runtime_t *runtime = _ctx->service_control_runtime ();
        if (runtime)
            runtime->wakeup_task (_task_id);
    }

    return 0;
}

int discovery_t::set_routing_id (const void *data_, size_t size_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (!data_ || size_ == 0 || size_ > sizeof (_routing_id.data)) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (_routing_id_locked || _sub_socket || !_connected_endpoints.empty ()
        || !_registry_pub_endpoints.empty ()
        || !_registry_bootstrap_endpoints.empty ()) {
        errno = EFSM;
        return -1;
    }
    _routing_id_override.assign (static_cast<const char *> (data_), size_);
    _routing_id.size = static_cast<uint8_t> (size_);
    memcpy (_routing_id.data, data_, size_);
    return 0;
}

int discovery_t::routing_id (zlink_routing_id_t *out_) const
{
    service_public_api_scope_t admission (
      const_cast<service_public_api_guard_t &> (_public_api));
    if (!admission.acquired ())
        return -1;
    if (!out_) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    *out_ = _routing_id;
    return out_->size > 0 ? 0 : -1;
}

int discovery_t::set_option (int option_,
                             const void *optval_,
                             size_t optvallen_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (!optval_ || optvallen_ == 0) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    bool updated = false;
    for (size_t i = 0; i < _sub_opts.size (); ++i) {
        if (_sub_opts[i].option == option_) {
            _sub_opts[i].value.assign (
              static_cast<const unsigned char *> (optval_),
              static_cast<const unsigned char *> (optval_) + optvallen_);
            updated = true;
            break;
        }
    }
    if (!updated) {
        socket_opt_t opt;
        opt.option = option_;
        opt.value.assign (static_cast<const unsigned char *> (optval_),
                          static_cast<const unsigned char *> (optval_)
                            + optvallen_);
        _sub_opts.push_back (opt);
    }
    apply_socket_options_to_existing_locked (option_, optval_, optvallen_);
    return 0;
}

int discovery_t::set_tls_client (const char *ca_cert_,
                                 const char *hostname_,
                                 int trust_system_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    scoped_lock_t lock (_sync);
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

    for (size_t i = 0; i < sizeof (options) / sizeof (options[0]); ++i) {
        bool updated = false;
        for (size_t j = 0; j < _sub_opts.size (); ++j) {
            if (_sub_opts[j].option == options[i].option) {
                _sub_opts[j].value.assign (
                  static_cast<const unsigned char *> (options[i].value),
                  static_cast<const unsigned char *> (options[i].value)
                    + options[i].size);
                updated = true;
                break;
            }
        }
        if (!updated) {
            socket_opt_t opt;
            opt.option = options[i].option;
            opt.value.assign (
              static_cast<const unsigned char *> (options[i].value),
              static_cast<const unsigned char *> (options[i].value)
                + options[i].size);
            _sub_opts.push_back (opt);
        }
        apply_socket_options_to_existing_locked (
          options[i].option, options[i].value, options[i].size);
    }
    return 0;
}

void discovery_t::apply_socket_options_locked (socket_base_t *socket_)
{
    if (!socket_)
        return;
    for (size_t i = 0; i < _sub_opts.size (); ++i) {
        if (!_sub_opts[i].value.empty ())
            socket_->setsockopt (_sub_opts[i].option, &_sub_opts[i].value[0],
                                 _sub_opts[i].value.size ());
    }
}

void discovery_t::apply_socket_options_to_existing_locked (int option_,
                                                           const void *optval_,
                                                           size_t optvallen_)
{
    if (_sub_socket)
        static_cast<socket_base_t *> (_sub_socket)
          ->setsockopt (option_, optval_, optvallen_);

    for (std::map<std::string, bootstrap_state_t>::iterator it =
           _bootstrap_states.begin ();
         it != _bootstrap_states.end (); ++it) {
        if (it->second.dealer)
            it->second.dealer->setsockopt (option_, optval_, optvallen_);
    }

    for (std::map<std::string, socket_base_t *>::iterator it =
           _report_dealers.begin ();
         it != _report_dealers.end (); ++it) {
        if (it->second)
            it->second->setsockopt (option_, optval_, optvallen_);
    }

    for (std::map<std::string, socket_base_t *>::iterator it =
           _control_dealers.begin ();
         it != _control_dealers.end (); ++it) {
        if (it->second)
            it->second->setsockopt (option_, optval_, optvallen_);
    }
}

void *discovery_t::monitor_open (int events_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return NULL;
    return _monitor.open (events_);
}

bool discovery_t::ensure_socket_routing_id (socket_base_t *socket_)
{
    if (!socket_)
        return false;
    if (_routing_id.size > 0 && _routing_id_override.empty ()) {
        return socket_->setsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID, _routing_id.data,
                                    _routing_id.size)
               == 0;
    }
    if (!zlink::discovery::set_socket_routing_id (
          socket_, &_routing_id_override, &_routing_id))
        return false;
    if (_routing_id_override.empty () && _routing_id.size > 0)
        _routing_id_override.assign (
          reinterpret_cast<const char *> (_routing_id.data), _routing_id.size);
    return true;
}

int discovery_t::bootstrap_registry (const char *registry_endpoint_)
{
    socket_base_t *dealer = NULL;
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    if (ensure_bootstrap_dealer (registry_endpoint_, &dealer) != 0)
        return -1;
    bootstrap_state_t *state = NULL;
    {
        scoped_lock_t lock (_sync);
        state = &_bootstrap_states[registry_endpoint_];
        rid = _routing_id;
        discovery_debugf ("bootstrap state endpoint=%s sent=%d started=%llu dealer=%p",
                          registry_endpoint_, state->request_sent ? 1 : 0,
                          static_cast<unsigned long long> (state->request_started_ms),
                          static_cast<void *> (state->dealer));
    }
    if (!state || !dealer) {
        errno = ENOTCONN;
        return -1;
    }

    if (!state->request_sent) {
        if (!wait_socket_event (static_cast<void *> (dealer), ZLINK_POLLOUT,
                                0))
            return 1;
        discovery_protocol::bootstrap_req_t req;
        memset (&req, 0, sizeof (req));
        req.msg_id = discovery_protocol::msg_bootstrap_req;
        req.service_type = _service_type;
        req.routing_id = rid;
        if (discovery_protocol::send_frame (dealer, &req, sizeof (req), 0)
            < 0) {
            if (errno == EAGAIN)
                return 1;
            discovery_debugf ("bootstrap send failed errno=%d", errno);
            return -1;
        }

        scoped_lock_t lock (_sync);
        bootstrap_state_t &stored = _bootstrap_states[registry_endpoint_];
        stored.request_sent = true;
        stored.request_started_ms = zlink::clock_t ().now_ms ();
        discovery_debugf ("bootstrap request sent endpoint=%s dealer=%p",
                          registry_endpoint_, static_cast<void *> (dealer));
        return 1;
    }

    if (!wait_socket_event (static_cast<void *> (dealer), ZLINK_POLLIN, 0)) {
        const uint64_t now_ms = zlink::clock_t ().now_ms ();
        if (state->request_started_ms != 0
            && now_ms - state->request_started_ms > 2000) {
            discovery_debugf ("bootstrap timeout endpoint=%s", registry_endpoint_);
            scoped_lock_t lock (_sync);
            bootstrap_state_t &stored = _bootstrap_states[registry_endpoint_];
            (void) _lifecycle.close_socket_and_wait (stored.dealer, 1000);
            stored.request_sent = false;
            stored.request_started_ms = 0;
        }
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

    {
        scoped_lock_t lock (_sync);
        _registry_pub_endpoints.insert (pub_endpoint);
        _registry_uplink_endpoints.insert (uplink_endpoint);
        _latest_registry_uplink_endpoint = uplink_endpoint;
        _bootstrapped_registry_endpoints.insert (registry_endpoint_);
        if (heartbeat_interval_ms > 0)
            _heartbeat_interval_ms = heartbeat_interval_ms;
        std::map<std::string, bootstrap_state_t>::iterator it =
          _bootstrap_states.find (registry_endpoint_);
        if (it != _bootstrap_states.end ()) {
            if (it->second.dealer
                && uplink_endpoint == registry_endpoint_
                && _report_dealers.find (uplink_endpoint)
                     == _report_dealers.end ()) {
                _report_dealers[uplink_endpoint] = it->second.dealer;
                it->second.dealer = NULL;
            }
            if (it->second.dealer) {
                it->second.dealer->close ();
                it->second.dealer = NULL;
            }
            _bootstrap_states.erase (it);
        }
    }

    discovery_debugf ("bootstrap ok endpoint=%s pub=%s uplink=%s",
                      registry_endpoint_, pub_endpoint.c_str (),
                      uplink_endpoint.c_str ());

    (void) ensure_topology_reporters ();
    return 0;
}

int discovery_t::ensure_bootstrap_dealer (const std::string &registry_endpoint_,
                                          socket_base_t **dealer_out_)
{
    if (!dealer_out_) {
        errno = EINVAL;
        return -1;
    }
    *dealer_out_ = NULL;

    scoped_lock_t lock (_sync);
    bootstrap_state_t &state = _bootstrap_states[registry_endpoint_];
    if (!state.dealer) {
        state.dealer = _ctx->create_socket (ZLINK_CORE_SOCKET_DEALER);
        if (!state.dealer)
            return -1;
        _lifecycle.register_socket (state.dealer);
        if (!ensure_socket_routing_id (state.dealer)) {
            (void) _lifecycle.close_socket (state.dealer);
            state.dealer = NULL;
            return -1;
        }
        apply_socket_options_locked (state.dealer);
        const int linger = 0;
        const int sndtimeo_ms = 0;
        const int rcvtimeo_ms = 0;
        state.dealer->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger, sizeof (linger));
        state.dealer->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &sndtimeo_ms,
                                  sizeof (sndtimeo_ms));
        state.dealer->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO, &rcvtimeo_ms,
                                  sizeof (rcvtimeo_ms));
        if (state.dealer->connect (registry_endpoint_.c_str ()) != 0) {
            (void) _lifecycle.close_socket (state.dealer);
            state.dealer = NULL;
            return -1;
        }
    }

    *dealer_out_ = state.dealer;
    return 0;
}

void discovery_t::snapshot_providers (const std::string &service_name_,
                                      std::vector<provider_info_t> *out_)
{
    if (!out_)
        return;
    out_->clear ();
    scoped_lock_t lock (_sync);
    std::map<std::string, service_state_t>::iterator it =
      _services.find (service_name_);
    if (it == _services.end ())
        return;
    *out_ = it->second.providers;
}

bool discovery_t::latest_registry_uplink (std::string *out_)
{
    if (!out_)
        return false;

    scoped_lock_t lock (_sync);
    if (_latest_registry_uplink_endpoint.empty ())
        return false;
    *out_ = _latest_registry_uplink_endpoint;
    return true;
}

uint64_t discovery_t::update_seq ()
{
    scoped_lock_t lock (_sync);
    return _update_seq;
}

uint64_t discovery_t::service_update_seq (const std::string &service_name_)
{
    scoped_lock_t lock (_sync);
    std::map<std::string, uint64_t>::iterator it =
      _service_seq.find (service_name_);
    if (it == _service_seq.end ())
        return 0;
    return it->second;
}

void discovery_t::add_observer (discovery_observer_t *observer_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return;
    if (!observer_)
        return;
    scoped_lock_t lock (_sync);
    _observers.insert (observer_);
}

int discovery_t::remove_observer (discovery_observer_t *observer_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (!observer_)
        return 0;
    scoped_lock_t lock (_sync);
    if (_observer_callbacks_inflight > 0) {
        errno = EBUSY;
        return -1;
    }
    _observers.erase (observer_);
    return 0;
}

void discovery_t::upsert_service_summary (
  const zlink_registry_topology_entry_t &entry_)
{
    if (entry_.routing_id.size == 0 || entry_.service_name[0] == '\0')
        return;

    topology_key_t key;
    key.service_kind = entry_.service_kind;
    key.routing_id_key = topology_routing_key (entry_.routing_id);
    key.service_name = entry_.service_name;

    {
        scoped_lock_t lock (_sync);
        topology_summary_t &summary = _summary_store[key];
        summary.entry = entry_;
        summary.dirty = true;
        summary.tombstone = entry_.state == ZLINK_TOPOLOGY_STATE_STOPPED;
    }

    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (runtime && _task_id != 0)
        runtime->wakeup_task (_task_id);
}

void discovery_t::upsert_gateway_peer_summary (
  const zlink_registry_gateway_peer_entry_t &entry_)
{
    if (entry_.gateway_routing_id.size == 0 || entry_.peer_routing_id.size == 0
        || entry_.service_name[0] == '\0') {
        return;
    }

    gateway_peer_key_t key;
    key.gateway_routing_id_key = topology_routing_key (entry_.gateway_routing_id);
    key.service_name = entry_.service_name;
    key.peer_routing_id_key = topology_routing_key (entry_.peer_routing_id);

    {
        scoped_lock_t lock (_sync);
        gateway_peer_summary_t &summary = _gateway_peer_summary_store[key];
        summary.entry = entry_;
        summary.dirty = true;
    }

    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (runtime && _task_id != 0)
        runtime->wakeup_task (_task_id);
}

void discovery_t::erase_service_summary (uint16_t service_kind_,
                                         const zlink_routing_id_t &routing_id_,
                                         const std::string &service_name_,
                                         bool stopped_)
{
    if (routing_id_.size == 0 || service_name_.empty ())
        return;

    topology_key_t key;
    key.service_kind = service_kind_;
    key.routing_id_key = topology_routing_key (routing_id_);
    key.service_name = service_name_;

    {
        scoped_lock_t lock (_sync);
        if (!stopped_) {
            _summary_store.erase (key);
        } else {
            topology_summary_t &summary = _summary_store[key];
            memset (&summary.entry, 0, sizeof (summary.entry));
            summary.entry.service_kind =
              static_cast<zlink_service_kind_t> (service_kind_);
            summary.entry.routing_id = routing_id_;
            summary.entry.state = ZLINK_TOPOLOGY_STATE_STOPPED;
            summary.entry.source = ZLINK_TOPOLOGY_SOURCE_DISCOVERY;
            strncpy (summary.entry.service_name, service_name_.c_str (),
                     sizeof (summary.entry.service_name) - 1);
            summary.dirty = true;
            summary.tombstone = true;
        }
    }

    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (runtime && _task_id != 0)
        runtime->wakeup_task (_task_id);
}

int discovery_t::destroy ()
{
    if (!_public_api.begin_close_or_fail_busy ())
        return -1;
    {
        scoped_lock_t lock (_sync);
        if (_observer_callbacks_inflight > 0) {
            _public_api.cancel_close ();
            errno = EBUSY;
            return -1;
        }
        _destroying = true;
    }
    _stop.set (1);
    zlink_service_event_t terminal;
    memset (&terminal, 0, sizeof (terminal));
    terminal.service_kind = ZLINK_SERVICE_KIND_DISCOVERY;
    terminal.event_type = ZLINK_MONITOR_EVENT_CLOSED;
    terminal.detail_flags = ZLINK_EVENT_DETAIL_SUBJECT_RID;
    terminal.routing_id = _routing_id;
    _monitor.close_all (&terminal);
    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (runtime && _task_id != 0)
        runtime->remove_task (_task_id);
    _task_id = 0;
    void *sub_socket = NULL;
    std::set<std::string> connected_endpoints;
    std::map<std::string, bootstrap_state_t> bootstrap_states;
    std::map<std::string, socket_base_t *> report_dealers;
    std::map<std::string, socket_base_t *> control_dealers;
    std::vector<discovery_observer_t *> observers;
    {
        scoped_lock_t lock (_sync);
        sub_socket = _sub_socket;
        connected_endpoints = _connected_endpoints;
        bootstrap_states = _bootstrap_states;
        report_dealers = _report_dealers;
        control_dealers = _control_dealers;
        _sub_socket = NULL;
        _connected_endpoints.clear ();
        observers.assign (_observers.begin (), _observers.end ());
        _observers.clear ();
        _observer_callbacks_inflight = 0;
        _report_dealers.clear ();
        _control_dealers.clear ();
        _bootstrap_states.clear ();
        _registry_uplink_endpoints.clear ();
        _latest_registry_uplink_endpoint.clear ();
        _registry_bootstrap_endpoints.clear ();
        _bootstrapped_registry_endpoints.clear ();
        _registry_pub_endpoints.clear ();
        _registered_services.clear ();
        _summary_store.clear ();
    }

    if (sub_socket) {
        for (std::set<std::string>::const_iterator it = connected_endpoints.begin ();
             it != connected_endpoints.end (); ++it)
            zlink_disconnect (sub_socket, it->c_str ());
        socket_base_t *sub = static_cast<socket_base_t *> (sub_socket);
        sub->set_all_pipes_nodelay ();
        (void) _lifecycle.close_socket_and_wait (sub, 1000);
    }

    for (std::map<std::string, bootstrap_state_t>::iterator it =
           bootstrap_states.begin ();
         it != bootstrap_states.end (); ++it) {
        if (!it->second.dealer)
            continue;
        if (!it->first.empty ())
            zlink_disconnect (it->second.dealer, it->first.c_str ());
        it->second.dealer->set_all_pipes_nodelay ();
        (void) _lifecycle.close_socket_and_wait (it->second.dealer, 1000);
        it->second.dealer = NULL;
    }
    for (std::map<std::string, socket_base_t *>::iterator it =
           report_dealers.begin ();
         it != report_dealers.end (); ++it) {
        if (!it->second)
            continue;
        if (!it->first.empty ())
            zlink_disconnect (it->second, it->first.c_str ());
        it->second->set_all_pipes_nodelay ();
        (void) _lifecycle.close_socket_and_wait (it->second, 1000);
    }
    for (std::map<std::string, socket_base_t *>::iterator it =
           control_dealers.begin ();
         it != control_dealers.end (); ++it) {
        if (!it->second)
            continue;
        if (!it->first.empty ())
            zlink_disconnect (it->second, it->first.c_str ());
        it->second->set_all_pipes_nodelay ();
        (void) _lifecycle.close_socket_and_wait (it->second, 1000);
    }
    (void) _lifecycle.wait_drained (10000);

    for (size_t i = 0; i < observers.size (); ++i) {
        if (observers[i])
            observers[i]->on_discovery_destroyed (this);
    }
    return 0;
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

void discovery_t::control_task (void *arg_)
{
    discovery_t *self = static_cast<discovery_t *> (arg_);
    self->tick ();
}

int discovery_t::ensure_sub_socket ()
{
    scoped_lock_t lock (_sync);
    if (_sub_socket)
        return 0;

    void *sub = static_cast<void *> (_ctx->create_socket (ZLINK_CORE_SOCKET_SUB));
    if (!sub)
        return -1;
    _lifecycle.register_socket (static_cast<socket_base_t *> (sub));

    apply_socket_options_locked (static_cast<socket_base_t *> (sub));
    if (!ensure_socket_routing_id (static_cast<socket_base_t *> (sub))) {
        socket_base_t *sub_socket = static_cast<socket_base_t *> (sub);
        (void) _lifecycle.close_socket (sub_socket);
        return -1;
    }
    static_cast<socket_base_t *> (sub)->setsockopt (ZLINK_INTERNAL_OPT_SUBSCRIBE, "", 0);
    _sub_socket = sub;
    _connected_endpoints.clear ();
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
            discovery_debugf ("uplink connect failed endpoint=%s errno=%d",
                              endpoints[i].c_str (), errno);
            continue;
        }

        if (dealer
            && wait_socket_event (static_cast<void *> (dealer), ZLINK_POLLOUT,
                                  10))
            ++ready_count;
    }
    return ready_count == 0 ? -1 : 0;
}

int discovery_t::register_service (uint16_t service_type_,
                                   const char *service_name_,
                                   const char *endpoint_,
                                   uint32_t weight_,
                                   std::string *resolved_endpoint_out_,
                                   const zlink_routing_id_t *routing_id_)
{
    if (!service_name_ || service_name_[0] == '\0' || !endpoint_
        || endpoint_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    std::string uplink;
    {
        scoped_lock_t lock (_sync);
        uplink = _latest_registry_uplink_endpoint;
    }
    if (uplink.empty ()) {
        errno = EAGAIN;
        return -1;
    }

    socket_base_t *dealer = NULL;
    if (prepare_transient_dealer (_ctx, uplink, routing_id_, &dealer) != 0) {
        discovery_debugf ("register_service pollout timeout uplink=%s",
                          uplink.c_str ());
        return -1;
    }

    if (discovery_protocol::send_u16 (
          dealer, discovery_protocol::msg_register, ZLINK_SNDMORE)
          < 0
        || discovery_protocol::send_u16 (dealer, service_type_, ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_string (dealer, service_name_,
                                            ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_string (dealer, endpoint_, ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_u32 (dealer, weight_, 0)
             < 0)
        {
            (void) close_transient_dealer (_ctx, dealer);
        return -1;
        }

    int status = -1;
    std::string resolved;
    std::string error;
    if (recv_status_ack (dealer, discovery_protocol::msg_register_ack, &status,
                         &resolved, &error)
        != 0) {
        discovery_debugf ("register_service ack recv failed errno=%d", errno);
        (void) close_transient_dealer (_ctx, dealer);
        return -1;
    }
    (void) close_transient_dealer (_ctx, dealer);
    if (status != 0) {
        if (status == -1) {
            discovery_debugf ("register_service ack timeout uplink=%s",
                              uplink.c_str ());
            errno = EAGAIN;
            return -1;
        }
        discovery_debugf ("register_service rejected status=%d error=%s", status,
                          error.c_str ());
        errno = EINVAL;
        return -1;
    }

    registered_service_key_t key;
    key.service_type = service_type_;
    key.service_name = service_name_;
    key.endpoint = resolved.empty () ? endpoint_ : resolved;

    {
        scoped_lock_t lock (_sync);
        registered_service_t &service = _registered_services[key];
        service.service_type = service_type_;
        service.service_name = service_name_;
        service.endpoint = key.endpoint;
        service.uplink_endpoint = uplink;
        service.weight = weight_;
        service.last_heartbeat_ms = clock_t ().now_ms ();
    }

    if (resolved_endpoint_out_)
        *resolved_endpoint_out_ = key.endpoint;
    return 0;
}

int discovery_t::update_service_weight (uint16_t service_type_,
                                        const char *service_name_,
                                        const char *endpoint_,
                                        uint32_t weight_)
{
    if (!service_name_ || service_name_[0] == '\0' || !endpoint_
        || endpoint_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    std::string uplink;
    {
        scoped_lock_t lock (_sync);
        registered_service_key_t key;
        key.service_type = service_type_;
        key.service_name = service_name_;
        key.endpoint = endpoint_;
        std::map<registered_service_key_t, registered_service_t>::const_iterator
          it = _registered_services.find (key);
        if (it != _registered_services.end ())
            uplink = it->second.uplink_endpoint;
        else
            uplink = _latest_registry_uplink_endpoint;
    }
    if (uplink.empty ()) {
        errno = EAGAIN;
        return -1;
    }

    socket_base_t *dealer = NULL;
    if (prepare_transient_dealer (_ctx, uplink, NULL, &dealer) != 0) {
        discovery_debugf ("update_service_weight pollout timeout uplink=%s",
                          uplink.c_str ());
        return -1;
    }

    const uint32_t value = weight_;
    if (discovery_protocol::send_u16 (
          dealer, discovery_protocol::msg_update_weight, ZLINK_SNDMORE)
          < 0
        || discovery_protocol::send_u16 (dealer, service_type_, ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_string (dealer, service_name_,
                                            ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_string (dealer, endpoint_, ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_u32 (dealer, value, 0) < 0) {
        (void) close_transient_dealer (_ctx, dealer);
        return -1;
    }

    int status = -1;
    std::string resolved;
    std::string error;
    if (recv_status_ack (dealer, discovery_protocol::msg_register_ack, &status,
                         &resolved, &error)
        != 0) {
        discovery_debugf ("update_service_weight ack recv failed errno=%d",
                          errno);
        (void) close_transient_dealer (_ctx, dealer);
        return -1;
    }
    (void) close_transient_dealer (_ctx, dealer);
    if (status != 0) {
        if (status == -1) {
            discovery_debugf ("update_service_weight ack timeout uplink=%s",
                              uplink.c_str ());
            errno = EAGAIN;
            return -1;
        }
        discovery_debugf ("update_service_weight rejected status=%d error=%s",
                          status, error.c_str ());
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    registered_service_key_t key;
    key.service_type = service_type_;
    key.service_name = service_name_;
    key.endpoint = endpoint_;
    std::map<registered_service_key_t, registered_service_t>::iterator it =
      _registered_services.find (key);
    if (it != _registered_services.end ())
        it->second.weight = value;
    return 0;
}

int discovery_t::unregister_service (uint16_t service_type_,
                                     const char *service_name_,
                                     const char *endpoint_)
{
    if (!service_name_ || service_name_[0] == '\0' || !endpoint_
        || endpoint_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    std::string uplink;
    {
        scoped_lock_t lock (_sync);
        registered_service_key_t key;
        key.service_type = service_type_;
        key.service_name = service_name_;
        key.endpoint = endpoint_;
        std::map<registered_service_key_t, registered_service_t>::const_iterator
          it = _registered_services.find (key);
        if (it != _registered_services.end ())
            uplink = it->second.uplink_endpoint;
        else
            uplink = _latest_registry_uplink_endpoint;
    }
    if (uplink.empty ()) {
        errno = EAGAIN;
        return -1;
    }

    socket_base_t *dealer = NULL;
    if (prepare_transient_dealer (_ctx, uplink, NULL, &dealer) != 0) {
        discovery_debugf ("unregister_service pollout timeout uplink=%s",
                          uplink.c_str ());
        return -1;
    }

    if (discovery_protocol::send_u16 (
          dealer, discovery_protocol::msg_unregister, ZLINK_SNDMORE)
          < 0
        || discovery_protocol::send_u16 (dealer, service_type_, ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_string (dealer, service_name_,
                                            ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_string (dealer, endpoint_, 0) < 0)
        {
            (void) close_transient_dealer (_ctx, dealer);
        return -1;
        }

    int status = -1;
    std::string error;
    if (recv_status_ack (dealer, discovery_protocol::msg_unregister_ack,
                         &status, NULL, &error)
        != 0) {
        discovery_debugf ("unregister_service ack recv failed errno=%d",
                          errno);
        (void) close_transient_dealer (_ctx, dealer);
        return -1;
    }
    (void) close_transient_dealer (_ctx, dealer);
    if (status != 0) {
        if (status == -1) {
            discovery_debugf ("unregister_service ack timeout uplink=%s",
                              uplink.c_str ());
            errno = EAGAIN;
            return -1;
        }
        discovery_debugf ("unregister_service rejected status=%d error=%s",
                          status, error.c_str ());
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    registered_service_key_t key;
    key.service_type = service_type_;
    key.service_name = service_name_;
    key.endpoint = endpoint_;
    _registered_services.erase (key);
    return 0;
}

void discovery_t::close_sub_socket ()
{
    void *sub_socket = NULL;
    std::set<std::string> connected_endpoints;
    {
        scoped_lock_t lock (_sync);
        sub_socket = _sub_socket;
        connected_endpoints = _connected_endpoints;
        _sub_socket = NULL;
        _connected_endpoints.clear ();
    }

    if (!sub_socket)
        return;

    for (std::set<std::string>::const_iterator it = connected_endpoints.begin ();
         it != connected_endpoints.end (); ++it)
        zlink_disconnect (sub_socket, it->c_str ());
    socket_base_t *sub = static_cast<socket_base_t *> (sub_socket);
    (void) _lifecycle.close_socket (sub);
}

void discovery_t::tick ()
{
    if (_stop.get () != 0)
        return;

    std::vector<std::string> bootstrap_endpoints;
    {
        scoped_lock_t lock (_sync);
        for (std::set<std::string>::const_iterator it =
               _registry_bootstrap_endpoints.begin ();
             it != _registry_bootstrap_endpoints.end (); ++it) {
            if (_bootstrapped_registry_endpoints.count (*it) == 0)
                bootstrap_endpoints.push_back (*it);
        }
    }

    for (size_t i = 0; i < bootstrap_endpoints.size (); ++i) {
        if (bootstrap_registry (bootstrap_endpoints[i].c_str ()) == 0) {
            scoped_lock_t lock (_sync);
            _bootstrapped_registry_endpoints.insert (bootstrap_endpoints[i]);
        }
    }

    if (ensure_sub_socket () != 0)
        return;

    void *sub = NULL;
    std::set<std::string> endpoints;
    {
        scoped_lock_t lock (_sync);
        sub = _sub_socket;
        endpoints = _registry_pub_endpoints;
    }
    if (!sub)
        return;

    for (std::set<std::string>::const_iterator it = endpoints.begin ();
         it != endpoints.end (); ++it) {
        scoped_lock_t lock (_sync);
        if (_connected_endpoints.find (*it) == _connected_endpoints.end ()
            && _sub_socket == sub) {
            zlink_connect (sub, it->c_str ());
            _connected_endpoints.insert (*it);
        }
    }

    while (true) {
        if (!wait_socket_event (sub, ZLINK_POLLIN, 0))
            break;

        std::vector<zlink_msg_t> frames;
        while (true) {
            zlink_msg_t frame;
            zlink_msg_init (&frame);
            if (recv_msg_internal (sub, &frame, ZLINK_DONTWAIT) == -1) {
                zlink_msg_close (&frame);
                break;
            }
            frames.push_back (frame);
            if (!discovery_frame_has_more (frame))
                break;
        }
        if (!frames.empty ())
            handle_service_list (frames);
        close_frames (&frames);
    }

    refresh_registered_service_heartbeats (clock_t ().now_ms ());
    flush_topology_reports ();
    flush_gateway_peer_reports ();
}

void discovery_t::notify_observers (const std::set<std::string> &services_)
{
    if (services_.empty ())
        return;
    std::vector<discovery_observer_t *> observers;
    {
        scoped_lock_t lock (_sync);
        observers.assign (_observers.begin (), _observers.end ());
    }
    if (observers.empty ())
        return;
    for (std::set<std::string>::const_iterator sit = services_.begin ();
         sit != services_.end (); ++sit) {
        for (size_t i = 0; i < observers.size (); ++i) {
            if (!observers[i])
                continue;
            {
                scoped_lock_t lock (_sync);
                if (_observers.find (observers[i]) == _observers.end ())
                    continue;
                ++_observer_callbacks_inflight;
            }
            observers[i]->on_service_update (*sit);
            {
                scoped_lock_t lock (_sync);
                if (_observer_callbacks_inflight > 0)
                    --_observer_callbacks_inflight;
                _observer_cv.broadcast ();
            }
        }
    }
}

void discovery_t::handle_service_list (const std::vector<zlink_msg_t> &frames_)
{
    if (frames_.size () < 4)
        return;

    uint16_t msg_id = 0;
    if (!discovery_protocol::read_u16 (frames_[0], &msg_id))
        return;
    if (msg_id != discovery_protocol::msg_service_list)
        return;

    uint32_t registry_id = 0;
    uint64_t list_seq = 0;
    uint32_t service_count = 0;

    if (!discovery_protocol::read_u32 (frames_[1], &registry_id)
        || !discovery_protocol::read_u64 (frames_[2], &list_seq)
        || !discovery_protocol::read_u32 (frames_[3], &service_count)) {
        return;
    }

    std::map<std::string, service_state_t> updated;

    size_t index = 4;
    for (uint32_t i = 0; i < service_count && index < frames_.size (); ++i) {
        if (index + 2 >= frames_.size ())
            break;
        uint16_t service_type = 0;
        if (!discovery_protocol::read_u16 (frames_[index++], &service_type))
            break;
        const std::string service_name =
          discovery_protocol::read_string (frames_[index++]);
        uint32_t receiver_count = 0;
        if (!discovery_protocol::read_u32 (frames_[index++], &receiver_count))
            break;

        service_state_t state;
        for (uint32_t p = 0; p < receiver_count && index + 2 < frames_.size ();
             ++p) {
            provider_info_t info;
            info.service_name = service_name;
            info.endpoint = discovery_protocol::read_string (frames_[index++]);
            discovery_protocol::read_routing_id (frames_[index++],
                                                 &info.routing_id);
            discovery_protocol::read_u32 (frames_[index++], &info.weight);
            info.registered_at = 0;
            if (service_type == _service_type)
                state.providers.push_back (info);
        }

        if (service_type != _service_type)
            continue;

        std::map<std::string, service_state_t>::iterator it =
          updated.find (service_name);
        if (it == updated.end ())
            updated[service_name] = state;
        else
            it->second.providers.insert (it->second.providers.end (),
                                         state.providers.begin (),
                                         state.providers.end ());
    }

    std::set<std::string> changed;
    std::vector<zlink_service_event_t> events;
    {
        scoped_lock_t lock (_sync);
        std::map<uint32_t, uint64_t>::iterator sit =
          _registry_seq.find (registry_id);
        if (sit != _registry_seq.end () && list_seq <= sit->second)
            return;
        _registry_seq[registry_id] = list_seq;

        const auto provider_equal =
          [] (const provider_info_t &a_, const provider_info_t &b_) {
              if (a_.endpoint != b_.endpoint)
                  return false;
              if (a_.routing_id.size != b_.routing_id.size)
                  return false;
              if (a_.routing_id.size > 0
                  && memcmp (a_.routing_id.data, b_.routing_id.data,
                             a_.routing_id.size)
                       != 0)
                  return false;
              return a_.weight == b_.weight;
          };
        const auto providers_equal =
          [&] (const service_state_t &a_, const service_state_t &b_) {
              if (a_.providers.size () != b_.providers.size ())
                  return false;
              for (size_t i = 0; i < a_.providers.size (); ++i) {
                  if (!provider_equal (a_.providers[i], b_.providers[i]))
                      return false;
              }
              return true;
          };

        for (std::map<std::string, service_state_t>::iterator uit =
               updated.begin ();
             uit != updated.end (); ++uit) {
            std::map<std::string, service_state_t>::iterator oit =
              _services.find (uit->first);
            if (oit == _services.end ()
                || !providers_equal (oit->second, uit->second)) {
                _service_seq[uit->first] = _update_seq + 1;
                changed.insert (uit->first);

                zlink_service_event_t ev;
                memset (&ev, 0, sizeof (ev));
                ev.service_kind = ZLINK_SERVICE_KIND_DISCOVERY;
                ev.detail_flags = ZLINK_EVENT_DETAIL_SERVICE_NAME
                                  | ZLINK_EVENT_DETAIL_SUBJECT_RID;
                ev.routing_id = _routing_id;
                strncpy (ev.service_name, uit->first.c_str (),
                         sizeof (ev.service_name) - 1);
                ev.value = static_cast<uint32_t> (uit->second.providers.size ());
                if (oit == _services.end () || oit->second.providers.empty ())
                    ev.event_type = ZLINK_DISCOVERY_SERVICE_UP;
                else if (uit->second.providers.empty ())
                    ev.event_type = ZLINK_DISCOVERY_SERVICE_DOWN;
                else
                    ev.event_type = ZLINK_DISCOVERY_PROVIDERS_CHANGED;
                events.push_back (ev);
            }
        }
        for (std::map<std::string, service_state_t>::iterator oit =
               _services.begin ();
             oit != _services.end (); ++oit) {
            if (updated.find (oit->first) == updated.end ()) {
                _service_seq[oit->first] = _update_seq + 1;
                changed.insert (oit->first);

                zlink_service_event_t ev;
                memset (&ev, 0, sizeof (ev));
                ev.service_kind = ZLINK_SERVICE_KIND_DISCOVERY;
                ev.event_type = ZLINK_DISCOVERY_SERVICE_DOWN;
                ev.detail_flags = ZLINK_EVENT_DETAIL_SERVICE_NAME
                                  | ZLINK_EVENT_DETAIL_SUBJECT_RID;
                ev.routing_id = _routing_id;
                strncpy (ev.service_name, oit->first.c_str (),
                         sizeof (ev.service_name) - 1);
                events.push_back (ev);
            }
        }
        _services.swap (updated);
        _update_seq++;
    }

    if (!changed.empty ())
        notify_observers (changed);
    for (size_t i = 0; i < events.size (); ++i) {
        discovery_debugf ("service event type=%u name=%s value=%u",
                          static_cast<unsigned int> (events[i].event_type),
                          events[i].service_name,
                          static_cast<unsigned int> (events[i].value));
        uint16_t state = ZLINK_TOPOLOGY_STATE_DISCOVERED;
        if (events[i].event_type == ZLINK_DISCOVERY_SERVICE_UP
            || events[i].event_type == ZLINK_DISCOVERY_PROVIDERS_CHANGED)
            state = events[i].value > 0 ? ZLINK_TOPOLOGY_STATE_READY
                                        : ZLINK_TOPOLOGY_STATE_DISCOVERED;
        else if (events[i].event_type == ZLINK_DISCOVERY_SERVICE_DOWN)
            state = ZLINK_TOPOLOGY_STATE_LOST;
        if (_discovery_summary_enabled) {
            zlink_registry_topology_entry_t entry;
            memset (&entry, 0, sizeof (entry));
            entry.routing_id = _routing_id;
            entry.service_kind = ZLINK_SERVICE_KIND_DISCOVERY;
            strncpy (entry.service_name, events[i].service_name,
                     sizeof (entry.service_name) - 1);
            entry.source = ZLINK_TOPOLOGY_SOURCE_REGISTRY;
            entry.state = static_cast<zlink_topology_state_t> (state);
            entry.desired_count = 1;
            entry.ready_count = events[i].value;
            entry.last_reported_ms = zlink::clock_t ().now_ms ();
            upsert_service_summary (entry);
        }
        _monitor.emit (events[i]);
    }
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
            if (!wait_socket_event (static_cast<void *> (dealers[d]),
                                    ZLINK_POLLOUT, 0)) {
                all_sent = false;
                continue;
            }
            const bool dealer_sent =
              send_topology_report_frames (dealers[d], entries[i]);
            if (!dealer_sent)
                all_sent = false;
        }
        sent[i] = all_sent;
        if (!all_sent) {
            discovery_debugf (
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
            if (!wait_socket_event (static_cast<void *> (dealers[d]),
                                    ZLINK_POLLOUT, 0)) {
                all_sent = false;
                continue;
            }
            if (!send_gateway_peer_report_frames (dealers[d], entries[i]))
                all_sent = false;
        }
        sent[i] = all_sent;
        if (!all_sent) {
            discovery_debugf ("gateway peer report send failed service=%s errno=%d",
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
        if (!wait_socket_event (static_cast<void *> (dealer), ZLINK_POLLOUT, 0))
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
