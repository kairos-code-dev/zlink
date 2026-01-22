/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#include <string.h>
#include <limits.h>
#include <set>

#include "core/options.hpp"
#include "utils/err.hpp"
#include "utils/macros.hpp"

#ifndef ZMQ_HAVE_WINDOWS
#include <net/if.h>
#endif

#if defined IFNAMSIZ
#define BINDDEVSIZ IFNAMSIZ
#else
#define BINDDEVSIZ 16
#endif

static int sockopt_invalid ()
{
#if defined(ZMQ_ACT_MILITANT)
    zmq_assert (false);
#endif
    errno = EINVAL;
    return -1;
}

int zmq::do_getsockopt (void *const optval_,
                        size_t *const optvallen_,
                        const std::string &value_)
{
    return do_getsockopt (optval_, optvallen_, value_.c_str (),
                          value_.size () + 1);
}

int zmq::do_getsockopt (void *const optval_,
                        size_t *const optvallen_,
                        const void *value_,
                        const size_t value_len_)
{
    if (*optvallen_ < value_len_) {
        return sockopt_invalid ();
    }
    memcpy (optval_, value_, value_len_);
    memset (static_cast<char *> (optval_) + value_len_, 0,
            *optvallen_ - value_len_);
    *optvallen_ = value_len_;
    return 0;
}

template <typename T>
static int do_setsockopt (const void *const optval_,
                          const size_t optvallen_,
                          T *const out_value_)
{
    if (optvallen_ == sizeof (T)) {
        memcpy (out_value_, optval_, sizeof (T));
        return 0;
    }
    return sockopt_invalid ();
}

int zmq::do_setsockopt_int_as_bool_strict (const void *const optval_,
                                           const size_t optvallen_,
                                           bool *const out_value_)
{
    int value = -1;
    if (do_setsockopt (optval_, optvallen_, &value) == -1)
        return -1;
    if (value == 0 || value == 1) {
        *out_value_ = (value != 0);
        return 0;
    }
    return sockopt_invalid ();
}

int zmq::do_setsockopt_int_as_bool_relaxed (const void *const optval_,
                                            const size_t optvallen_,
                                            bool *const out_value_)
{
    int value = -1;
    if (do_setsockopt (optval_, optvallen_, &value) == -1)
        return -1;
    *out_value_ = (value != 0);
    return 0;
}

static int
do_setsockopt_string_allow_empty_strict (const void *const optval_,
                                         const size_t optvallen_,
                                         std::string *const out_value_,
                                         const size_t max_len_)
{
    if (optval_ == NULL && optvallen_ == 0) {
        out_value_->clear ();
        return 0;
    }
    if (optval_ != NULL && optvallen_ > 0 && optvallen_ <= max_len_) {
        out_value_->assign (static_cast<const char *> (optval_), optvallen_);
        return 0;
    }
    return sockopt_invalid ();
}

static int
do_setsockopt_string_allow_empty_relaxed (const void *const optval_,
                                          const size_t optvallen_,
                                          std::string *const out_value_,
                                          const size_t max_len_)
{
    if (optvallen_ > 0 && optvallen_ <= max_len_) {
        out_value_->assign (static_cast<const char *> (optval_), optvallen_);
        return 0;
    }
    return sockopt_invalid ();
}

template <typename T>
static int do_setsockopt_set (const void *const optval_,
                              const size_t optvallen_,
                              std::set<T> *const set_)
{
    if (optvallen_ == 0 && optval_ == NULL) {
        set_->clear ();
        return 0;
    }
    if (optvallen_ == sizeof (T) && optval_ != NULL) {
        set_->insert (*(static_cast<const T *> (optval_)));
        return 0;
    }
    return sockopt_invalid ();
}

const int default_hwm = 1000;

zmq::options_t::options_t () :
    sndhwm (default_hwm),
    rcvhwm (default_hwm),
    affinity (0),
    routing_id_size (0),
    rate (100),
    recovery_ivl (10000),
    multicast_hops (1),
    multicast_maxtpdu (1500),
    sndbuf (-1),
    rcvbuf (-1),
    tos (0),
    priority (0),
    type (-1),
    linger (-1),
    connect_timeout (0),
    tcp_maxrt (0),
    reconnect_ivl (100),
    reconnect_ivl_max (0),
    backlog (100),
    maxmsgsize (-1),
    rcvtimeo (-1),
    sndtimeo (-1),
    ipv6 (false),
    immediate (0),
    filter (false),
    invert_matching (false),
    recv_routing_id (false),
    tcp_keepalive (-1),
    tcp_keepalive_cnt (-1),
    tcp_keepalive_idle (-1),
    tcp_keepalive_intvl (-1),
    socket_id (0),
    conflate (false),
    handshake_ivl (30000),
    connected (false),
    heartbeat_ttl (0),
    heartbeat_interval (0),
    heartbeat_timeout (-1),
    use_fd (-1),
    in_batch_size (8192),
    out_batch_size (8192),
    zero_copy (true),
    monitor_event_version (1),
    busy_poll (0),
    zmp_metadata (false)
#ifdef ZMQ_HAVE_TLS
    ,
    tls_verify (1),
    tls_require_client_cert (0),
    tls_trust_system (1)
#endif
{
}

const int deciseconds_per_millisecond = 100;

int zmq::options_t::setsockopt (int option_,
                                const void *optval_,
                                size_t optvallen_)
{
    const bool is_int = (optvallen_ == sizeof (int));
    int value = 0;
    if (is_int)
        memcpy (&value, optval_, sizeof (int));

    switch (option_) {
        case ZMQ_SNDHWM:
            if (is_int && value >= 0) {
                sndhwm = value;
                return 0;
            }
            break;

        case ZMQ_RCVHWM:
            if (is_int && value >= 0) {
                rcvhwm = value;
                return 0;
            }
            break;

        case ZMQ_AFFINITY:
            return do_setsockopt (optval_, optvallen_, &affinity);

        case ZMQ_ROUTING_ID:
            if (optvallen_ > 0 && optvallen_ <= UCHAR_MAX) {
                routing_id_size = static_cast<unsigned char> (optvallen_);
                memcpy (routing_id, optval_, routing_id_size);
                return 0;
            }
            break;

        case ZMQ_RATE:
            if (is_int && value > 0) {
                rate = value;
                return 0;
            }
            break;

        case ZMQ_RECOVERY_IVL:
            if (is_int && value >= 0) {
                recovery_ivl = value;
                return 0;
            }
            break;

        case ZMQ_SNDBUF:
            if (is_int && value >= -1) {
                sndbuf = value;
                return 0;
            }
            break;

        case ZMQ_RCVBUF:
            if (is_int && value >= -1) {
                rcvbuf = value;
                return 0;
            }
            break;

        case ZMQ_TOS:
            if (is_int && value >= 0) {
                tos = value;
                return 0;
            }
            break;

        case ZMQ_LINGER:
            if (is_int && value >= -1) {
                linger.store (value);
                return 0;
            }
            break;

        case ZMQ_CONNECT_TIMEOUT:
            if (is_int && value >= 0) {
                connect_timeout = value;
                return 0;
            }
            break;

        case ZMQ_TCP_MAXRT:
            if (is_int && value >= 0) {
                tcp_maxrt = value;
                return 0;
            }
            break;

        case ZMQ_RECONNECT_IVL:
            if (is_int && value >= -1) {
                reconnect_ivl = value;
                return 0;
            }
            break;

        case ZMQ_RECONNECT_IVL_MAX:
            if (is_int && value >= 0) {
                reconnect_ivl_max = value;
                return 0;
            }
            break;

        case ZMQ_BACKLOG:
            if (is_int && value >= 0) {
                backlog = value;
                return 0;
            }
            break;

        case ZMQ_MAXMSGSIZE:
            return do_setsockopt (optval_, optvallen_, &maxmsgsize);

        case ZMQ_MULTICAST_HOPS:
            if (is_int && value > 0) {
                multicast_hops = value;
                return 0;
            }
            break;

        case ZMQ_MULTICAST_MAXTPDU:
            if (is_int && value > 0) {
                multicast_maxtpdu = value;
                return 0;
            }
            break;

        case ZMQ_RCVTIMEO:
            if (is_int && value >= -1) {
                rcvtimeo = value;
                return 0;
            }
            break;

        case ZMQ_SNDTIMEO:
            if (is_int && value >= -1) {
                sndtimeo = value;
                return 0;
            }
            break;

        case ZMQ_IPV6:
            return do_setsockopt_int_as_bool_strict (optval_, optvallen_,
                                                     &ipv6);

        case ZMQ_TCP_KEEPALIVE:
            if (is_int && (value == -1 || value == 0 || value == 1)) {
                tcp_keepalive = value;
                return 0;
            }
            break;

        case ZMQ_TCP_KEEPALIVE_CNT:
            if (is_int && (value == -1 || value >= 0)) {
                tcp_keepalive_cnt = value;
                return 0;
            }
            break;

        case ZMQ_TCP_KEEPALIVE_IDLE:
            if (is_int && (value == -1 || value >= 0)) {
                tcp_keepalive_idle = value;
                return 0;
            }
            break;

        case ZMQ_TCP_KEEPALIVE_INTVL:
            if (is_int && (value == -1 || value >= 0)) {
                tcp_keepalive_intvl = value;
                return 0;
            }
            break;

        case ZMQ_IMMEDIATE:
            if (is_int && (value == 0 || value == 1)) {
                immediate = value;
                return 0;
            }
            break;

        case ZMQ_CONFLATE:
            return do_setsockopt_int_as_bool_strict (optval_, optvallen_,
                                                     &conflate);

        case ZMQ_HANDSHAKE_IVL:
            if (is_int && value >= 0) {
                handshake_ivl = value;
                return 0;
            }
            break;

        case ZMQ_INVERT_MATCHING:
            return do_setsockopt_int_as_bool_relaxed (optval_, optvallen_,
                                                      &invert_matching);

        case ZMQ_ZMP_METADATA:
            return do_setsockopt_int_as_bool_strict (optval_, optvallen_,
                                                     &zmp_metadata);

        case ZMQ_HEARTBEAT_IVL:
            if (is_int && value >= 0) {
                heartbeat_interval = value;
                return 0;
            }
            break;

        case ZMQ_HEARTBEAT_TTL:
            value = value / deciseconds_per_millisecond;
            if (is_int && value >= 0 && value <= UINT16_MAX) {
                heartbeat_ttl = static_cast<uint16_t> (value);
                return 0;
            }
            break;

        case ZMQ_HEARTBEAT_TIMEOUT:
            if (is_int && value >= 0) {
                heartbeat_timeout = value;
                return 0;
            }
            break;

        case ZMQ_USE_FD:
            if (is_int && value >= -1) {
                use_fd = value;
                return 0;
            }
            break;

        case ZMQ_BINDTODEVICE:
            return do_setsockopt_string_allow_empty_strict (
              optval_, optvallen_, &bound_device, BINDDEVSIZ);

#ifdef ZMQ_HAVE_TLS
        case ZMQ_TLS_CERT:
            return do_setsockopt_string_allow_empty_strict (
              optval_, optvallen_, &tls_cert, 256);

        case ZMQ_TLS_KEY:
            return do_setsockopt_string_allow_empty_strict (
              optval_, optvallen_, &tls_key, 256);

        case ZMQ_TLS_CA:
            return do_setsockopt_string_allow_empty_strict (
              optval_, optvallen_, &tls_ca, 256);

        case ZMQ_TLS_VERIFY:
            if (is_int && (value == 0 || value == 1)) {
                tls_verify = value;
                return 0;
            }
            break;

        case ZMQ_TLS_REQUIRE_CLIENT_CERT:
            if (is_int && (value == 0 || value == 1)) {
                tls_require_client_cert = value;
                return 0;
            }
            break;

        case ZMQ_TLS_HOSTNAME:
            return do_setsockopt_string_allow_empty_strict (
              optval_, optvallen_, &tls_hostname, 256);

        case ZMQ_TLS_TRUST_SYSTEM:
            if (is_int && (value == 0 || value == 1)) {
                tls_trust_system = value;
                return 0;
            }
            break;

        case ZMQ_TLS_PASSWORD:
            return do_setsockopt_string_allow_empty_strict (
              optval_, optvallen_, &tls_password, 256);
#endif

        default:
            break;
    }

    errno = EINVAL;
    return -1;
}

int zmq::options_t::getsockopt (int option_,
                                void *optval_,
                                size_t *optvallen_) const
{
    const bool is_int = (*optvallen_ == sizeof (int));
    int *value = static_cast<int *> (optval_);

    switch (option_) {
        case ZMQ_SNDHWM:
            if (is_int) {
                *value = sndhwm;
                return 0;
            }
            break;

        case ZMQ_RCVHWM:
            if (is_int) {
                *value = rcvhwm;
                return 0;
            }
            break;

        case ZMQ_AFFINITY:
            if (*optvallen_ == sizeof (uint64_t)) {
                *(static_cast<uint64_t *> (optval_)) = affinity;
                return 0;
            }
            break;

        case ZMQ_ROUTING_ID:
            return do_getsockopt (optval_, optvallen_, routing_id,
                                  routing_id_size);

        case ZMQ_RATE:
            if (is_int) {
                *value = rate;
                return 0;
            }
            break;

        case ZMQ_RECOVERY_IVL:
            if (is_int) {
                *value = recovery_ivl;
                return 0;
            }
            break;

        case ZMQ_SNDBUF:
            if (is_int) {
                *value = sndbuf;
                return 0;
            }
            break;

        case ZMQ_RCVBUF:
            if (is_int) {
                *value = rcvbuf;
                return 0;
            }
            break;

        case ZMQ_TOS:
            if (is_int) {
                *value = tos;
                return 0;
            }
            break;

        case ZMQ_TYPE:
            if (is_int) {
                *value = type;
                return 0;
            }
            break;

        case ZMQ_LINGER:
            if (is_int) {
                *value = linger.load ();
                return 0;
            }
            break;

        case ZMQ_CONNECT_TIMEOUT:
            if (is_int) {
                *value = connect_timeout;
                return 0;
            }
            break;

        case ZMQ_TCP_MAXRT:
            if (is_int) {
                *value = tcp_maxrt;
                return 0;
            }
            break;

        case ZMQ_RECONNECT_IVL:
            if (is_int) {
                *value = reconnect_ivl;
                return 0;
            }
            break;

        case ZMQ_RECONNECT_IVL_MAX:
            if (is_int) {
                *value = reconnect_ivl_max;
                return 0;
            }
            break;

        case ZMQ_BACKLOG:
            if (is_int) {
                *value = backlog;
                return 0;
            }
            break;

        case ZMQ_MAXMSGSIZE:
            if (*optvallen_ == sizeof (int64_t)) {
                *(static_cast<int64_t *> (optval_)) = maxmsgsize;
                *optvallen_ = sizeof (int64_t);
                return 0;
            }
            break;

        case ZMQ_MULTICAST_HOPS:
            if (is_int) {
                *value = multicast_hops;
                return 0;
            }
            break;

        case ZMQ_MULTICAST_MAXTPDU:
            if (is_int) {
                *value = multicast_maxtpdu;
                return 0;
            }
            break;

        case ZMQ_RCVTIMEO:
            if (is_int) {
                *value = rcvtimeo;
                return 0;
            }
            break;

        case ZMQ_SNDTIMEO:
            if (is_int) {
                *value = sndtimeo;
                return 0;
            }
            break;

        case ZMQ_IPV6:
            if (is_int) {
                *value = ipv6;
                return 0;
            }
            break;

        case ZMQ_IMMEDIATE:
            if (is_int) {
                *value = immediate;
                return 0;
            }
            break;

        case ZMQ_TCP_KEEPALIVE:
            if (is_int) {
                *value = tcp_keepalive;
                return 0;
            }
            break;

        case ZMQ_TCP_KEEPALIVE_CNT:
            if (is_int) {
                *value = tcp_keepalive_cnt;
                return 0;
            }
            break;

        case ZMQ_TCP_KEEPALIVE_IDLE:
            if (is_int) {
                *value = tcp_keepalive_idle;
                return 0;
            }
            break;

        case ZMQ_TCP_KEEPALIVE_INTVL:
            if (is_int) {
                *value = tcp_keepalive_intvl;
                return 0;
            }
            break;

        case ZMQ_CONFLATE:
            if (is_int) {
                *value = conflate;
                return 0;
            }
            break;

        case ZMQ_HANDSHAKE_IVL:
            if (is_int) {
                *value = handshake_ivl;
                return 0;
            }
            break;

        case ZMQ_INVERT_MATCHING:
            if (is_int) {
                *value = invert_matching;
                return 0;
            }
            break;

        case ZMQ_ZMP_METADATA:
            if (is_int) {
                *value = zmp_metadata ? 1 : 0;
                return 0;
            }
            break;

        case ZMQ_HEARTBEAT_IVL:
            if (is_int) {
                *value = heartbeat_interval;
                return 0;
            }
            break;

        case ZMQ_HEARTBEAT_TTL:
            if (is_int) {
                *value = heartbeat_ttl * 100;
                return 0;
            }
            break;

        case ZMQ_HEARTBEAT_TIMEOUT:
            if (is_int) {
                *value = heartbeat_timeout;
                return 0;
            }
            break;

        case ZMQ_USE_FD:
            if (is_int) {
                *value = use_fd;
                return 0;
            }
            break;

        case ZMQ_BINDTODEVICE:
            return do_getsockopt (optval_, optvallen_, bound_device);

#ifdef ZMQ_HAVE_TLS
        case ZMQ_TLS_CERT:
            return do_getsockopt (optval_, optvallen_, tls_cert);

        case ZMQ_TLS_KEY:
            return do_getsockopt (optval_, optvallen_, tls_key);

        case ZMQ_TLS_CA:
            return do_getsockopt (optval_, optvallen_, tls_ca);

        case ZMQ_TLS_VERIFY:
            if (is_int) {
                *value = tls_verify;
                return 0;
            }
            break;

        case ZMQ_TLS_REQUIRE_CLIENT_CERT:
            if (is_int) {
                *value = tls_require_client_cert;
                return 0;
            }
            break;

        case ZMQ_TLS_HOSTNAME:
            return do_getsockopt (optval_, optvallen_, tls_hostname);

        case ZMQ_TLS_TRUST_SYSTEM:
            if (is_int) {
                *value = tls_trust_system;
                return 0;
            }
            break;

        case ZMQ_TLS_PASSWORD:
            return do_getsockopt (optval_, optvallen_, tls_password);
#endif

        default:
            break;
    }
    errno = EINVAL;
    return -1;
}
