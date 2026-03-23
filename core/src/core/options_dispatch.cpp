/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <limits.h>
#include <string.h>

#include "core/options.hpp"
#include "utils/err.hpp"

#ifndef ZLINK_HAVE_WINDOWS
#include <net/if.h>
#endif

#if defined IFNAMSIZ
#define BINDDEVSIZ IFNAMSIZ
#else
#define BINDDEVSIZ 16
#endif

namespace
{
static int sockopt_invalid ()
{
#if defined(ZLINK_ACT_MILITANT)
    zlink_assert (false);
#endif
    errno = EINVAL;
    return -1;
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

static int setsockopt_general (zlink::options_t *self_,
                               int option_,
                               const void *optval_,
                               size_t optvallen_,
                               bool is_int_,
                               int value_)
{
    switch (option_) {
        case ZLINK_INTERNAL_OPT_SNDHWM:
            if (is_int_ && value_ >= 0) {
                self_->sndhwm = value_;
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_RCVHWM:
            if (is_int_ && value_ >= 0) {
                self_->rcvhwm = value_;
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_AFFINITY:
            return do_setsockopt (optval_, optvallen_, &self_->affinity);

        case ZLINK_INTERNAL_OPT_ROUTING_ID:
            if (optvallen_ > 0 && optvallen_ <= UCHAR_MAX) {
                self_->routing_id_size = static_cast<unsigned char> (optvallen_);
                memcpy (self_->routing_id, optval_, self_->routing_id_size);
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_RATE:
            if (is_int_ && value_ > 0) {
                self_->rate = value_;
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_RECOVERY_IVL:
            if (is_int_ && value_ >= 0) {
                self_->recovery_ivl = value_;
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_MAXMSGSIZE:
            return do_setsockopt (optval_, optvallen_, &self_->maxmsgsize);

        case ZLINK_INTERNAL_OPT_MULTICAST_HOPS:
            if (is_int_ && value_ > 0) {
                self_->multicast_hops = value_;
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_MULTICAST_MAXTPDU:
            if (is_int_ && value_ > 0) {
                self_->multicast_maxtpdu = value_;
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_IPV6:
            return zlink::do_setsockopt_int_as_bool_strict (
              optval_, optvallen_, &self_->ipv6);

        case ZLINK_INTERNAL_OPT_IMMEDIATE:
            if (is_int_ && (value_ == 0 || value_ == 1)) {
                self_->immediate = value_;
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_CONFLATE:
            return zlink::do_setsockopt_int_as_bool_strict (
              optval_, optvallen_, &self_->conflate);

        case ZLINK_INTERNAL_OPT_HANDSHAKE_IVL:
            if (is_int_ && value_ >= 0) {
                self_->handshake_ivl = value_;
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_INVERT_MATCHING:
            return zlink::do_setsockopt_int_as_bool_relaxed (
              optval_, optvallen_, &self_->invert_matching);

        case ZLINK_INTERNAL_OPT_STREAM_NOTIFY:
            return zlink::do_setsockopt_int_as_bool_strict (
              optval_, optvallen_, &self_->stream_notify);

        case ZLINK_INTERNAL_OPT_ZMP_METADATA:
            return zlink::do_setsockopt_int_as_bool_strict (
              optval_, optvallen_, &self_->zmp_metadata);

        case ZLINK_INTERNAL_OPT_HEARTBEAT_IVL:
            if (is_int_ && value_ >= 0) {
                self_->heartbeat_interval = value_;
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_HEARTBEAT_TIMEOUT:
            if (is_int_ && value_ >= 0) {
                self_->heartbeat_timeout = value_;
                return 0;
            }
            break;

        default:
            break;
    }

    return -1;
}

static int setsockopt_transport (zlink::options_t *self_,
                                 int option_,
                                 const void *optval_,
                                 size_t optvallen_,
                                 bool is_int_,
                                 int value_)
{
    switch (option_) {
        case ZLINK_INTERNAL_OPT_SNDBUF:
            if (is_int_ && value_ >= -1) {
                self_->sndbuf = value_;
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_RCVBUF:
            if (is_int_ && value_ >= -1) {
                self_->rcvbuf = value_;
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_TOS:
            if (is_int_ && value_ >= 0) {
                self_->tos = value_;
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_LINGER:
            if (is_int_ && value_ >= -1) {
                self_->linger.store (value_);
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_CONNECT_TIMEOUT:
            if (is_int_ && value_ >= 0) {
                self_->connect_timeout = value_;
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_TCP_MAXRT:
            if (is_int_ && value_ >= 0) {
                self_->tcp_maxrt = value_;
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_RECONNECT_IVL:
            if (is_int_ && value_ >= -1) {
                self_->reconnect_ivl = value_;
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_RECONNECT_IVL_MAX:
            if (is_int_ && value_ >= 0) {
                self_->reconnect_ivl_max = value_;
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_BACKLOG:
            if (is_int_ && value_ >= 0) {
                self_->backlog = value_;
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_RCVTIMEO:
            if (is_int_ && value_ >= -1) {
                self_->rcvtimeo = value_;
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_SNDTIMEO:
            if (is_int_ && value_ >= -1) {
                self_->sndtimeo = value_;
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_TCP_KEEPALIVE:
            if (is_int_ && (value_ == -1 || value_ == 0 || value_ == 1)) {
                self_->tcp_keepalive = value_;
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_TCP_KEEPALIVE_CNT:
            if (is_int_ && (value_ == -1 || value_ >= 0)) {
                self_->tcp_keepalive_cnt = value_;
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_TCP_KEEPALIVE_IDLE:
            if (is_int_ && (value_ == -1 || value_ >= 0)) {
                self_->tcp_keepalive_idle = value_;
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_TCP_KEEPALIVE_INTVL:
            if (is_int_ && (value_ == -1 || value_ >= 0)) {
                self_->tcp_keepalive_intvl = value_;
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_TCP_NODELAY:
            if (is_int_ && (value_ == -1 || value_ == 0 || value_ == 1)) {
                self_->tcp_nodelay = value_;
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_BINDTODEVICE:
            return do_setsockopt_string_allow_empty_strict (
              optval_, optvallen_, &self_->bound_device, BINDDEVSIZ);

        default:
            break;
    }

    return -1;
}

static int setsockopt_security (zlink::options_t *self_,
                                int option_,
                                const void *optval_,
                                size_t optvallen_,
                                bool is_int_,
                                int value_)
{
    switch (option_) {
        case ZLINK_INTERNAL_OPT_HEARTBEAT_TTL:
            value_ = value_ / 100;
            if (is_int_ && value_ >= 0 && value_ <= UINT16_MAX) {
                self_->heartbeat_ttl = static_cast<uint16_t> (value_);
                return 0;
            }
            break;

#ifdef ZLINK_HAVE_TLS
        case ZLINK_INTERNAL_OPT_TLS_CERT:
            return do_setsockopt_string_allow_empty_strict (
              optval_, optvallen_, &self_->tls_cert, 256);
        case ZLINK_INTERNAL_OPT_TLS_KEY:
            return do_setsockopt_string_allow_empty_strict (
              optval_, optvallen_, &self_->tls_key, 256);
        case ZLINK_INTERNAL_OPT_TLS_CA:
            return do_setsockopt_string_allow_empty_strict (
              optval_, optvallen_, &self_->tls_ca, 256);
        case ZLINK_INTERNAL_OPT_TLS_VERIFY:
            if (is_int_ && (value_ == 0 || value_ == 1)) {
                self_->tls_verify = value_;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_TLS_REQUIRE_CLIENT_CERT:
            if (is_int_ && (value_ == 0 || value_ == 1)) {
                self_->tls_require_client_cert = value_;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_TLS_HOSTNAME:
            return do_setsockopt_string_allow_empty_strict (
              optval_, optvallen_, &self_->tls_hostname, 256);
        case ZLINK_INTERNAL_OPT_TLS_TRUST_SYSTEM:
            if (is_int_ && (value_ == 0 || value_ == 1)) {
                self_->tls_trust_system = value_;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_TLS_PASSWORD:
            return do_setsockopt_string_allow_empty_strict (
              optval_, optvallen_, &self_->tls_password, 256);
#endif

        default:
            break;
    }

    return -1;
}

static int getsockopt_general (const zlink::options_t *self_,
                               int option_,
                               void *optval_,
                               size_t *optvallen_,
                               bool is_int_,
                               int *value_)
{
    switch (option_) {
        case ZLINK_INTERNAL_OPT_SNDHWM:
            if (is_int_) {
                *value_ = self_->sndhwm;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_RCVHWM:
            if (is_int_) {
                *value_ = self_->rcvhwm;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_AFFINITY:
            if (*optvallen_ == sizeof (uint64_t)) {
                *(static_cast<uint64_t *> (optval_)) = self_->affinity;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_ROUTING_ID:
            return zlink::do_getsockopt (optval_, optvallen_, self_->routing_id,
                                         self_->routing_id_size);
        case ZLINK_INTERNAL_OPT_RATE:
            if (is_int_) {
                *value_ = self_->rate;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_RECOVERY_IVL:
            if (is_int_) {
                *value_ = self_->recovery_ivl;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_TYPE:
            if (is_int_) {
                *value_ = self_->type;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_MAXMSGSIZE:
            if (*optvallen_ == sizeof (int64_t)) {
                *(static_cast<int64_t *> (optval_)) = self_->maxmsgsize;
                *optvallen_ = sizeof (int64_t);
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_MULTICAST_HOPS:
            if (is_int_) {
                *value_ = self_->multicast_hops;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_MULTICAST_MAXTPDU:
            if (is_int_) {
                *value_ = self_->multicast_maxtpdu;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_IPV6:
            if (is_int_) {
                *value_ = self_->ipv6;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_IMMEDIATE:
            if (is_int_) {
                *value_ = self_->immediate;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_CONFLATE:
            if (is_int_) {
                *value_ = self_->conflate;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_HANDSHAKE_IVL:
            if (is_int_) {
                *value_ = self_->handshake_ivl;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_INVERT_MATCHING:
            if (is_int_) {
                *value_ = self_->invert_matching;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_STREAM_NOTIFY:
            if (is_int_) {
                *value_ = self_->stream_notify ? 1 : 0;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_ZMP_METADATA:
            if (is_int_) {
                *value_ = self_->zmp_metadata ? 1 : 0;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_HEARTBEAT_IVL:
            if (is_int_) {
                *value_ = self_->heartbeat_interval;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_HEARTBEAT_TIMEOUT:
            if (is_int_) {
                *value_ = self_->heartbeat_timeout;
                return 0;
            }
            break;
        default:
            break;
    }

    return -1;
}

static int getsockopt_transport (const zlink::options_t *self_,
                                 int option_,
                                 void *optval_,
                                 size_t *optvallen_,
                                 bool is_int_,
                                 int *value_)
{
    switch (option_) {
        case ZLINK_INTERNAL_OPT_SNDBUF:
            if (is_int_) {
                *value_ = self_->sndbuf;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_RCVBUF:
            if (is_int_) {
                *value_ = self_->rcvbuf;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_TOS:
            if (is_int_) {
                *value_ = self_->tos;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_LINGER:
            if (is_int_) {
                *value_ = self_->linger.load ();
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_CONNECT_TIMEOUT:
            if (is_int_) {
                *value_ = self_->connect_timeout;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_TCP_MAXRT:
            if (is_int_) {
                *value_ = self_->tcp_maxrt;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_RECONNECT_IVL:
            if (is_int_) {
                *value_ = self_->reconnect_ivl;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_RECONNECT_IVL_MAX:
            if (is_int_) {
                *value_ = self_->reconnect_ivl_max;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_BACKLOG:
            if (is_int_) {
                *value_ = self_->backlog;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_RCVTIMEO:
            if (is_int_) {
                *value_ = self_->rcvtimeo;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_SNDTIMEO:
            if (is_int_) {
                *value_ = self_->sndtimeo;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_TCP_KEEPALIVE:
            if (is_int_) {
                *value_ = self_->tcp_keepalive;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_TCP_KEEPALIVE_CNT:
            if (is_int_) {
                *value_ = self_->tcp_keepalive_cnt;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_TCP_KEEPALIVE_IDLE:
            if (is_int_) {
                *value_ = self_->tcp_keepalive_idle;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_TCP_KEEPALIVE_INTVL:
            if (is_int_) {
                *value_ = self_->tcp_keepalive_intvl;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_TCP_NODELAY:
            if (is_int_) {
                *value_ = self_->tcp_nodelay;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_BINDTODEVICE:
            return zlink::do_getsockopt (optval_, optvallen_,
                                         self_->bound_device);
        default:
            break;
    }

    return -1;
}

static int getsockopt_security (const zlink::options_t *self_,
                                int option_,
                                void *optval_,
                                size_t *optvallen_,
                                bool is_int_,
                                int *value_)
{
    switch (option_) {
        case ZLINK_INTERNAL_OPT_HEARTBEAT_TTL:
            if (is_int_) {
                *value_ = self_->heartbeat_ttl * 100;
                return 0;
            }
            break;

#ifdef ZLINK_HAVE_TLS
        case ZLINK_INTERNAL_OPT_TLS_CERT:
            return zlink::do_getsockopt (optval_, optvallen_, self_->tls_cert);
        case ZLINK_INTERNAL_OPT_TLS_KEY:
            return zlink::do_getsockopt (optval_, optvallen_, self_->tls_key);
        case ZLINK_INTERNAL_OPT_TLS_CA:
            return zlink::do_getsockopt (optval_, optvallen_, self_->tls_ca);
        case ZLINK_INTERNAL_OPT_TLS_VERIFY:
            if (is_int_) {
                *value_ = self_->tls_verify;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_TLS_REQUIRE_CLIENT_CERT:
            if (is_int_) {
                *value_ = self_->tls_require_client_cert;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_TLS_HOSTNAME:
            return zlink::do_getsockopt (optval_, optvallen_,
                                         self_->tls_hostname);
        case ZLINK_INTERNAL_OPT_TLS_TRUST_SYSTEM:
            if (is_int_) {
                *value_ = self_->tls_trust_system;
                return 0;
            }
            break;
        case ZLINK_INTERNAL_OPT_TLS_PASSWORD:
            return zlink::do_getsockopt (optval_, optvallen_,
                                         self_->tls_password);
#endif

        default:
            break;
    }

    return -1;
}
}

int zlink::options_t::setsockopt (int option_,
                                  const void *optval_,
                                  size_t optvallen_)
{
    const bool is_int = (optvallen_ == sizeof (int));
    int value = 0;
    if (is_int)
        memcpy (&value, optval_, sizeof (int));

    if (setsockopt_general (this, option_, optval_, optvallen_, is_int, value)
        == 0)
        return 0;
    if (setsockopt_transport (
          this, option_, optval_, optvallen_, is_int, value)
        == 0)
        return 0;
    if (setsockopt_security (this, option_, optval_, optvallen_, is_int, value)
        == 0)
        return 0;

    errno = EINVAL;
    return -1;
}

int zlink::options_t::getsockopt (int option_,
                                  void *optval_,
                                  size_t *optvallen_) const
{
    const bool is_int = (*optvallen_ == sizeof (int));
    int *value = static_cast<int *> (optval_);

    if (getsockopt_general (this, option_, optval_, optvallen_, is_int, value)
        == 0)
        return 0;
    if (getsockopt_transport (
          this, option_, optval_, optvallen_, is_int, value)
        == 0)
        return 0;
    if (getsockopt_security (
          this, option_, optval_, optvallen_, is_int, value)
        == 0)
        return 0;

    errno = EINVAL;
    return -1;
}
