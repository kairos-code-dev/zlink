/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_TEST_LEGACY_API_COMPAT_HPP_INCLUDED__
#define __ZLINK_TEST_LEGACY_API_COMPAT_HPP_INCLUDED__

#include "../include/zlink.h"
#include "../src/services/spot/spot_node.hpp"
#include "../src/services/spot/spot_pub.hpp"
#include "../src/services/spot/spot_sub.hpp"
#include "../src/sockets/socket_base.hpp"

#include <map>
#include <string>
#include <string.h>

#ifndef ZLINK_AFFINITY
#define ZLINK_AFFINITY ZLINK_SOCKOPT_AFFINITY
#endif
#ifndef ZLINK_ROUTING_ID
#define ZLINK_ROUTING_ID ZLINK_SOCKOPT_ROUTING_ID
#endif
#ifndef ZLINK_SUBSCRIBE
#define ZLINK_SUBSCRIBE ZLINK_SOCKOPT_SUBSCRIBE
#endif
#ifndef ZLINK_UNSUBSCRIBE
#define ZLINK_UNSUBSCRIBE ZLINK_SOCKOPT_UNSUBSCRIBE
#endif
#ifndef ZLINK_RATE
#define ZLINK_RATE ZLINK_SOCKOPT_RATE
#endif
#ifndef ZLINK_RECOVERY_IVL
#define ZLINK_RECOVERY_IVL ZLINK_SOCKOPT_RECOVERY_IVL
#endif
#ifndef ZLINK_SNDBUF
#define ZLINK_SNDBUF ZLINK_SOCKOPT_SNDBUF
#endif
#ifndef ZLINK_RCVBUF
#define ZLINK_RCVBUF ZLINK_SOCKOPT_RCVBUF
#endif
#ifndef ZLINK_FD
#define ZLINK_FD ZLINK_SOCKOPT_FD
#endif
#ifndef ZLINK_EVENTS
#define ZLINK_EVENTS ZLINK_SOCKOPT_EVENTS
#endif
#ifndef ZLINK_TYPE
#define ZLINK_TYPE ZLINK_SOCKOPT_TYPE
#endif
#ifndef ZLINK_LINGER
#define ZLINK_LINGER ZLINK_SOCKOPT_LINGER
#endif
#ifndef ZLINK_RECONNECT_IVL
#define ZLINK_RECONNECT_IVL ZLINK_SOCKOPT_RECONNECT_IVL
#endif
#ifndef ZLINK_BACKLOG
#define ZLINK_BACKLOG ZLINK_SOCKOPT_BACKLOG
#endif
#ifndef ZLINK_RECONNECT_IVL_MAX
#define ZLINK_RECONNECT_IVL_MAX ZLINK_SOCKOPT_RECONNECT_IVL_MAX
#endif
#ifndef ZLINK_MAXMSGSIZE
#define ZLINK_MAXMSGSIZE ZLINK_SOCKOPT_MAXMSGSIZE
#endif
#ifndef ZLINK_SNDHWM
#define ZLINK_SNDHWM ZLINK_SOCKOPT_SNDHWM
#endif
#ifndef ZLINK_RCVHWM
#define ZLINK_RCVHWM ZLINK_SOCKOPT_RCVHWM
#endif
#ifndef ZLINK_MULTICAST_HOPS
#define ZLINK_MULTICAST_HOPS ZLINK_SOCKOPT_MULTICAST_HOPS
#endif
#ifndef ZLINK_RCVTIMEO
#define ZLINK_RCVTIMEO ZLINK_SOCKOPT_RCVTIMEO
#endif
#ifndef ZLINK_SNDTIMEO
#define ZLINK_SNDTIMEO ZLINK_SOCKOPT_SNDTIMEO
#endif
#ifndef ZLINK_LAST_ENDPOINT
#define ZLINK_LAST_ENDPOINT ZLINK_SOCKOPT_LAST_ENDPOINT
#endif
#ifndef ZLINK_ROUTER_MANDATORY
#define ZLINK_ROUTER_MANDATORY ZLINK_SOCKOPT_ROUTER_MANDATORY
#endif
#ifndef ZLINK_TCP_KEEPALIVE
#define ZLINK_TCP_KEEPALIVE ZLINK_SOCKOPT_TCP_KEEPALIVE
#endif
#ifndef ZLINK_TCP_KEEPALIVE_CNT
#define ZLINK_TCP_KEEPALIVE_CNT ZLINK_SOCKOPT_TCP_KEEPALIVE_CNT
#endif
#ifndef ZLINK_TCP_KEEPALIVE_IDLE
#define ZLINK_TCP_KEEPALIVE_IDLE ZLINK_SOCKOPT_TCP_KEEPALIVE_IDLE
#endif
#ifndef ZLINK_TCP_KEEPALIVE_INTVL
#define ZLINK_TCP_KEEPALIVE_INTVL ZLINK_SOCKOPT_TCP_KEEPALIVE_INTVL
#endif
#ifndef ZLINK_IMMEDIATE
#define ZLINK_IMMEDIATE ZLINK_SOCKOPT_IMMEDIATE
#endif
#ifndef ZLINK_XPUB_VERBOSE
#define ZLINK_XPUB_VERBOSE ZLINK_SOCKOPT_XPUB_VERBOSE
#endif
#ifndef ZLINK_IPV6
#define ZLINK_IPV6 ZLINK_SOCKOPT_IPV6
#endif
#ifndef ZLINK_PROBE_ROUTER
#define ZLINK_PROBE_ROUTER ZLINK_SOCKOPT_PROBE_ROUTER
#endif
#ifndef ZLINK_CONFLATE
#define ZLINK_CONFLATE ZLINK_SOCKOPT_CONFLATE
#endif
#ifndef ZLINK_ROUTER_HANDOVER
#define ZLINK_ROUTER_HANDOVER ZLINK_SOCKOPT_ROUTER_HANDOVER
#endif
#ifndef ZLINK_TOS
#define ZLINK_TOS ZLINK_SOCKOPT_TOS
#endif
#ifndef ZLINK_CONNECT_ROUTING_ID
#define ZLINK_CONNECT_ROUTING_ID ZLINK_SOCKOPT_CONNECT_ROUTING_ID
#endif
#ifndef ZLINK_HANDSHAKE_IVL
#define ZLINK_HANDSHAKE_IVL ZLINK_SOCKOPT_HANDSHAKE_IVL
#endif
#ifndef ZLINK_XPUB_NODROP
#define ZLINK_XPUB_NODROP ZLINK_SOCKOPT_XPUB_NODROP
#endif
#ifndef ZLINK_BLOCKY
#define ZLINK_BLOCKY ZLINK_SOCKOPT_BLOCKY
#endif
#ifndef ZLINK_XPUB_MANUAL
#define ZLINK_XPUB_MANUAL ZLINK_SOCKOPT_XPUB_MANUAL
#endif
#ifndef ZLINK_XPUB_WELCOME_MSG
#define ZLINK_XPUB_WELCOME_MSG ZLINK_SOCKOPT_XPUB_WELCOME_MSG
#endif
#ifndef ZLINK_STREAM_NOTIFY
#define ZLINK_STREAM_NOTIFY ZLINK_SOCKOPT_STREAM_NOTIFY
#endif
#ifndef ZLINK_INVERT_MATCHING
#define ZLINK_INVERT_MATCHING ZLINK_SOCKOPT_INVERT_MATCHING
#endif
#ifndef ZLINK_HEARTBEAT_IVL
#define ZLINK_HEARTBEAT_IVL ZLINK_SOCKOPT_HEARTBEAT_IVL
#endif
#ifndef ZLINK_HEARTBEAT_TTL
#define ZLINK_HEARTBEAT_TTL ZLINK_SOCKOPT_HEARTBEAT_TTL
#endif
#ifndef ZLINK_HEARTBEAT_TIMEOUT
#define ZLINK_HEARTBEAT_TIMEOUT ZLINK_SOCKOPT_HEARTBEAT_TIMEOUT
#endif
#ifndef ZLINK_XPUB_VERBOSER
#define ZLINK_XPUB_VERBOSER ZLINK_SOCKOPT_XPUB_VERBOSER
#endif
#ifndef ZLINK_CONNECT_TIMEOUT
#define ZLINK_CONNECT_TIMEOUT ZLINK_SOCKOPT_CONNECT_TIMEOUT
#endif
#ifndef ZLINK_TCP_MAXRT
#define ZLINK_TCP_MAXRT ZLINK_SOCKOPT_TCP_MAXRT
#endif
#ifndef ZLINK_MULTICAST_MAXTPDU
#define ZLINK_MULTICAST_MAXTPDU ZLINK_SOCKOPT_MULTICAST_MAXTPDU
#endif
#ifndef ZLINK_BINDTODEVICE
#define ZLINK_BINDTODEVICE ZLINK_SOCKOPT_BINDTODEVICE
#endif
#ifndef ZLINK_TLS_CERT
#define ZLINK_TLS_CERT ZLINK_SOCKOPT_TLS_CERT
#endif
#ifndef ZLINK_TLS_KEY
#define ZLINK_TLS_KEY ZLINK_SOCKOPT_TLS_KEY
#endif
#ifndef ZLINK_TLS_CA
#define ZLINK_TLS_CA ZLINK_SOCKOPT_TLS_CA
#endif
#ifndef ZLINK_TLS_VERIFY
#define ZLINK_TLS_VERIFY ZLINK_SOCKOPT_TLS_VERIFY
#endif
#ifndef ZLINK_XPUB_MANUAL_LAST_VALUE
#define ZLINK_XPUB_MANUAL_LAST_VALUE ZLINK_SOCKOPT_XPUB_MANUAL_LAST_VALUE
#endif
#ifndef ZLINK_TLS_REQUIRE_CLIENT_CERT
#define ZLINK_TLS_REQUIRE_CLIENT_CERT ZLINK_SOCKOPT_TLS_REQUIRE_CLIENT_CERT
#endif
#ifndef ZLINK_TLS_HOSTNAME
#define ZLINK_TLS_HOSTNAME ZLINK_SOCKOPT_TLS_HOSTNAME
#endif
#ifndef ZLINK_TLS_TRUST_SYSTEM
#define ZLINK_TLS_TRUST_SYSTEM ZLINK_SOCKOPT_TLS_TRUST_SYSTEM
#endif
#ifndef ZLINK_TLS_PASSWORD
#define ZLINK_TLS_PASSWORD ZLINK_SOCKOPT_TLS_PASSWORD
#endif
#ifndef ZLINK_ONLY_FIRST_SUBSCRIBE
#define ZLINK_ONLY_FIRST_SUBSCRIBE ZLINK_SOCKOPT_ONLY_FIRST_SUBSCRIBE
#endif
#ifndef ZLINK_TOPICS_COUNT
#define ZLINK_TOPICS_COUNT ZLINK_SOCKOPT_TOPICS_COUNT
#endif
#ifndef ZLINK_ZMP_METADATA
#define ZLINK_ZMP_METADATA ZLINK_SOCKOPT_ZMP_METADATA
#endif
#ifndef ZLINK_TCP_NODELAY
#define ZLINK_TCP_NODELAY ZLINK_SOCKOPT_TCP_NODELAY
#endif

namespace zlink_test_compat
{
struct tls_state_t
{
    std::string cert;
    std::string key;
    std::string ca;
    std::string hostname;
    int require_client_cert;
    int trust_system;

    tls_state_t () : require_client_cert (0), trust_system (0) {}
};

inline std::map<void *, tls_state_t> &tls_states ()
{
    static std::map<void *, tls_state_t> states;
    return states;
}

inline int public_socket_type_to_legacy (int type_)
{
    switch (type_) {
        case ZLINK_SOCKET_PAIR:
            return ZLINK_PAIR;
        case ZLINK_SOCKET_PUB:
            return ZLINK_PUB;
        case ZLINK_SOCKET_SUB:
            return ZLINK_SUB;
        case ZLINK_SOCKET_DEALER:
            return ZLINK_DEALER;
        case ZLINK_SOCKET_ROUTER:
            return ZLINK_ROUTER;
        case ZLINK_SOCKET_XPUB:
            return ZLINK_XPUB;
        case ZLINK_SOCKET_XSUB:
            return ZLINK_XSUB;
        case ZLINK_SOCKET_STREAM:
            return ZLINK_STREAM;
        default:
            return type_;
    }
}

inline zlink::socket_base_t *as_raw_socket (void *handle_)
{
    if (!handle_)
        return NULL;
    zlink::socket_base_t *socket =
      static_cast<zlink::socket_base_t *> (handle_);
    return socket->check_tag () ? socket : NULL;
}

inline zlink::spot_node_t *as_spot_node (void *handle_)
{
    if (!handle_)
        return NULL;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
    return node->check_tag () ? node : NULL;
}

inline zlink::spot_pub_t *as_spot_pub (void *handle_)
{
    if (!handle_)
        return NULL;
    zlink::spot_pub_t *pub = static_cast<zlink::spot_pub_t *> (handle_);
    return pub->check_tag () ? pub : NULL;
}

inline zlink::spot_sub_t *as_spot_sub (void *handle_)
{
    if (!handle_)
        return NULL;
    zlink::spot_sub_t *sub = static_cast<zlink::spot_sub_t *> (handle_);
    return sub->check_tag () ? sub : NULL;
}

inline int legacy_socket_option_to_internal (int option_)
{
    switch (option_) {
        case ZLINK_AFFINITY: return ZLINK_INTERNAL_OPT_AFFINITY;
        case ZLINK_ROUTING_ID: return ZLINK_INTERNAL_OPT_ROUTING_ID;
        case ZLINK_SUBSCRIBE: return ZLINK_INTERNAL_OPT_SUBSCRIBE;
        case ZLINK_UNSUBSCRIBE: return ZLINK_INTERNAL_OPT_UNSUBSCRIBE;
        case ZLINK_RATE: return ZLINK_INTERNAL_OPT_RATE;
        case ZLINK_RECOVERY_IVL: return ZLINK_INTERNAL_OPT_RECOVERY_IVL;
        case ZLINK_SNDBUF: return ZLINK_INTERNAL_OPT_SNDBUF;
        case ZLINK_RCVBUF: return ZLINK_INTERNAL_OPT_RCVBUF;
        case ZLINK_FD: return ZLINK_INTERNAL_OPT_FD;
        case ZLINK_EVENTS: return ZLINK_INTERNAL_OPT_EVENTS;
        case ZLINK_TYPE: return ZLINK_INTERNAL_OPT_TYPE;
        case ZLINK_LINGER: return ZLINK_INTERNAL_OPT_LINGER;
        case ZLINK_RECONNECT_IVL: return ZLINK_INTERNAL_OPT_RECONNECT_IVL;
        case ZLINK_BACKLOG: return ZLINK_INTERNAL_OPT_BACKLOG;
        case ZLINK_RECONNECT_IVL_MAX: return ZLINK_INTERNAL_OPT_RECONNECT_IVL_MAX;
        case ZLINK_MAXMSGSIZE: return ZLINK_INTERNAL_OPT_MAXMSGSIZE;
        case ZLINK_SNDHWM: return ZLINK_INTERNAL_OPT_SNDHWM;
        case ZLINK_RCVHWM: return ZLINK_INTERNAL_OPT_RCVHWM;
        case ZLINK_MULTICAST_HOPS: return ZLINK_INTERNAL_OPT_MULTICAST_HOPS;
        case ZLINK_RCVTIMEO: return ZLINK_INTERNAL_OPT_RCVTIMEO;
        case ZLINK_SNDTIMEO: return ZLINK_INTERNAL_OPT_SNDTIMEO;
        case ZLINK_LAST_ENDPOINT: return ZLINK_INTERNAL_OPT_LAST_ENDPOINT;
        case ZLINK_ROUTER_MANDATORY: return ZLINK_INTERNAL_OPT_ROUTER_MANDATORY;
        case ZLINK_TCP_KEEPALIVE: return ZLINK_INTERNAL_OPT_TCP_KEEPALIVE;
        case ZLINK_TCP_KEEPALIVE_CNT: return ZLINK_INTERNAL_OPT_TCP_KEEPALIVE_CNT;
        case ZLINK_TCP_KEEPALIVE_IDLE: return ZLINK_INTERNAL_OPT_TCP_KEEPALIVE_IDLE;
        case ZLINK_TCP_KEEPALIVE_INTVL: return ZLINK_INTERNAL_OPT_TCP_KEEPALIVE_INTVL;
        case ZLINK_IMMEDIATE: return ZLINK_INTERNAL_OPT_IMMEDIATE;
        case ZLINK_XPUB_VERBOSE: return ZLINK_INTERNAL_OPT_XPUB_VERBOSE;
        case ZLINK_IPV6: return ZLINK_INTERNAL_OPT_IPV6;
        case ZLINK_PROBE_ROUTER: return ZLINK_INTERNAL_OPT_PROBE_ROUTER;
        case ZLINK_CONFLATE: return ZLINK_INTERNAL_OPT_CONFLATE;
        case ZLINK_ROUTER_HANDOVER: return ZLINK_INTERNAL_OPT_ROUTER_HANDOVER;
        case ZLINK_TOS: return ZLINK_INTERNAL_OPT_TOS;
        case ZLINK_CONNECT_ROUTING_ID: return ZLINK_INTERNAL_OPT_CONNECT_ROUTING_ID;
        case ZLINK_HANDSHAKE_IVL: return ZLINK_INTERNAL_OPT_HANDSHAKE_IVL;
        case ZLINK_XPUB_NODROP: return ZLINK_INTERNAL_OPT_XPUB_NODROP;
        case ZLINK_BLOCKY: return ZLINK_INTERNAL_OPT_BLOCKY;
        case ZLINK_XPUB_MANUAL: return ZLINK_INTERNAL_OPT_XPUB_MANUAL;
        case ZLINK_XPUB_WELCOME_MSG: return ZLINK_INTERNAL_OPT_XPUB_WELCOME_MSG;
        case ZLINK_STREAM_NOTIFY: return ZLINK_INTERNAL_OPT_STREAM_NOTIFY;
        case ZLINK_INVERT_MATCHING: return ZLINK_INTERNAL_OPT_INVERT_MATCHING;
        case ZLINK_HEARTBEAT_IVL: return ZLINK_INTERNAL_OPT_HEARTBEAT_IVL;
        case ZLINK_HEARTBEAT_TTL: return ZLINK_INTERNAL_OPT_HEARTBEAT_TTL;
        case ZLINK_HEARTBEAT_TIMEOUT: return ZLINK_INTERNAL_OPT_HEARTBEAT_TIMEOUT;
        case ZLINK_XPUB_VERBOSER: return ZLINK_INTERNAL_OPT_XPUB_VERBOSER;
        case ZLINK_CONNECT_TIMEOUT: return ZLINK_INTERNAL_OPT_CONNECT_TIMEOUT;
        case ZLINK_TCP_MAXRT: return ZLINK_INTERNAL_OPT_TCP_MAXRT;
        case ZLINK_MULTICAST_MAXTPDU: return ZLINK_INTERNAL_OPT_MULTICAST_MAXTPDU;
        case ZLINK_BINDTODEVICE: return ZLINK_INTERNAL_OPT_BINDTODEVICE;
        case ZLINK_TLS_CERT: return ZLINK_INTERNAL_OPT_TLS_CERT;
        case ZLINK_TLS_KEY: return ZLINK_INTERNAL_OPT_TLS_KEY;
        case ZLINK_TLS_CA: return ZLINK_INTERNAL_OPT_TLS_CA;
        case ZLINK_TLS_VERIFY: return ZLINK_INTERNAL_OPT_TLS_VERIFY;
        case ZLINK_XPUB_MANUAL_LAST_VALUE: return ZLINK_INTERNAL_OPT_XPUB_MANUAL_LAST_VALUE;
        case ZLINK_TLS_REQUIRE_CLIENT_CERT: return ZLINK_INTERNAL_OPT_TLS_REQUIRE_CLIENT_CERT;
        case ZLINK_TLS_HOSTNAME: return ZLINK_INTERNAL_OPT_TLS_HOSTNAME;
        case ZLINK_TLS_TRUST_SYSTEM: return ZLINK_INTERNAL_OPT_TLS_TRUST_SYSTEM;
        case ZLINK_TLS_PASSWORD: return ZLINK_INTERNAL_OPT_TLS_PASSWORD;
        case ZLINK_ONLY_FIRST_SUBSCRIBE: return ZLINK_INTERNAL_OPT_ONLY_FIRST_SUBSCRIBE;
        case ZLINK_TOPICS_COUNT: return ZLINK_INTERNAL_OPT_TOPICS_COUNT;
        case ZLINK_ZMP_METADATA: return ZLINK_INTERNAL_OPT_ZMP_METADATA;
        case ZLINK_TCP_NODELAY: return ZLINK_INTERNAL_OPT_TCP_NODELAY;
        default: return option_;
    }
}

inline int set_common_option (void *socket_, int option_, const void *optval_, size_t optvallen_)
{
    switch (option_) {
        case ZLINK_AFFINITY:
            return zlink_set_option (socket_, ZLINK_OPT_AFFINITY, optval_, optvallen_);
        case ZLINK_RATE:
            return zlink_set_option (socket_, ZLINK_OPT_RATE, optval_, optvallen_);
        case ZLINK_RECOVERY_IVL:
            return zlink_set_option (socket_, ZLINK_OPT_RECOVERY_IVL, optval_, optvallen_);
        case ZLINK_SNDBUF:
            return zlink_set_option (socket_, ZLINK_OPT_SNDBUF, optval_, optvallen_);
        case ZLINK_RCVBUF:
            return zlink_set_option (socket_, ZLINK_OPT_RCVBUF, optval_, optvallen_);
        case ZLINK_LINGER:
            return zlink_set_option (socket_, ZLINK_OPT_LINGER, optval_, optvallen_);
        case ZLINK_RECONNECT_IVL:
            return zlink_set_option (socket_, ZLINK_OPT_RECONNECT_IVL, optval_, optvallen_);
        case ZLINK_BACKLOG:
            return zlink_set_option (socket_, ZLINK_OPT_BACKLOG, optval_, optvallen_);
        case ZLINK_RECONNECT_IVL_MAX:
            return zlink_set_option (socket_, ZLINK_OPT_RECONNECT_IVL_MAX, optval_, optvallen_);
        case ZLINK_MAXMSGSIZE:
            return zlink_set_option (socket_, ZLINK_OPT_MAXMSGSIZE, optval_, optvallen_);
        case ZLINK_SNDHWM:
            return zlink_set_option (socket_, ZLINK_OPT_SNDHWM, optval_, optvallen_);
        case ZLINK_RCVHWM:
            return zlink_set_option (socket_, ZLINK_OPT_RCVHWM, optval_, optvallen_);
        case ZLINK_MULTICAST_HOPS:
            return zlink_set_option (socket_, ZLINK_OPT_MULTICAST_HOPS, optval_, optvallen_);
        case ZLINK_RCVTIMEO:
            return zlink_set_option (socket_, ZLINK_OPT_RCVTIMEO, optval_, optvallen_);
        case ZLINK_SNDTIMEO:
            return zlink_set_option (socket_, ZLINK_OPT_SNDTIMEO, optval_, optvallen_);
        case ZLINK_TCP_KEEPALIVE:
            return zlink_set_option (socket_, ZLINK_OPT_TCP_KEEPALIVE, optval_, optvallen_);
        case ZLINK_TCP_KEEPALIVE_CNT:
            return zlink_set_option (socket_, ZLINK_OPT_TCP_KEEPALIVE_CNT, optval_, optvallen_);
        case ZLINK_TCP_KEEPALIVE_IDLE:
            return zlink_set_option (socket_, ZLINK_OPT_TCP_KEEPALIVE_IDLE, optval_, optvallen_);
        case ZLINK_TCP_KEEPALIVE_INTVL:
            return zlink_set_option (socket_, ZLINK_OPT_TCP_KEEPALIVE_INTVL, optval_, optvallen_);
        case ZLINK_IMMEDIATE:
            return zlink_set_option (socket_, ZLINK_OPT_IMMEDIATE, optval_, optvallen_);
        case ZLINK_IPV6:
            return zlink_set_option (socket_, ZLINK_OPT_IPV6, optval_, optvallen_);
        case ZLINK_CONFLATE:
            return zlink_set_option (socket_, ZLINK_OPT_CONFLATE, optval_, optvallen_);
        case ZLINK_TOS:
            return zlink_set_option (socket_, ZLINK_OPT_TOS, optval_, optvallen_);
        case ZLINK_HANDSHAKE_IVL:
            return zlink_set_option (socket_, ZLINK_OPT_HANDSHAKE_IVL, optval_, optvallen_);
        case ZLINK_BLOCKY:
            return zlink_set_option (socket_, ZLINK_OPT_BLOCKY, optval_, optvallen_);
        case ZLINK_INVERT_MATCHING:
            return zlink_set_option (socket_, ZLINK_OPT_INVERT_MATCHING, optval_, optvallen_);
        case ZLINK_HEARTBEAT_IVL:
            return zlink_set_option (socket_, ZLINK_OPT_HEARTBEAT_IVL, optval_, optvallen_);
        case ZLINK_HEARTBEAT_TTL:
            return zlink_set_option (socket_, ZLINK_OPT_HEARTBEAT_TTL, optval_, optvallen_);
        case ZLINK_HEARTBEAT_TIMEOUT:
            return zlink_set_option (socket_, ZLINK_OPT_HEARTBEAT_TIMEOUT, optval_, optvallen_);
        case ZLINK_CONNECT_TIMEOUT:
            return zlink_set_option (socket_, ZLINK_OPT_CONNECT_TIMEOUT, optval_, optvallen_);
        case ZLINK_TCP_MAXRT:
            return zlink_set_option (socket_, ZLINK_OPT_TCP_MAXRT, optval_, optvallen_);
        case ZLINK_MULTICAST_MAXTPDU:
            return zlink_set_option (socket_, ZLINK_OPT_MULTICAST_MAXTPDU, optval_, optvallen_);
        case ZLINK_BINDTODEVICE:
            return zlink_set_option (socket_, ZLINK_OPT_BINDTODEVICE, optval_, optvallen_);
        case ZLINK_ZMP_METADATA:
            return zlink_set_option (socket_, ZLINK_OPT_ZMP_METADATA, optval_, optvallen_);
        case ZLINK_TCP_NODELAY:
            return zlink_set_option (socket_, ZLINK_OPT_TCP_NODELAY, optval_, optvallen_);
        default:
            errno = EINVAL;
            return -1;
    }
}

inline int get_common_option (void *socket_, int option_, void *optval_, size_t *optvallen_)
{
    switch (option_) {
        case ZLINK_FD:
            return zlink_get_option (socket_, ZLINK_OPT_FD, optval_, optvallen_);
        case ZLINK_EVENTS:
            return zlink_get_option (socket_, ZLINK_OPT_EVENTS, optval_, optvallen_);
        case ZLINK_LAST_ENDPOINT:
            return zlink_get_option (socket_, ZLINK_OPT_LAST_ENDPOINT, optval_, optvallen_);
        case ZLINK_TYPE: {
            int type = 0;
            size_t len = sizeof (type);
            const int rc =
              zlink_get_option (socket_, ZLINK_OPT_TYPE, &type, &len);
            if (rc != 0)
                return rc;
            if (!optval_ || !optvallen_ || *optvallen_ < sizeof (int)) {
                errno = EINVAL;
                return -1;
            }
            type = public_socket_type_to_legacy (type);
            memcpy (optval_, &type, sizeof (type));
            *optvallen_ = sizeof (type);
            return 0;
        }
        case ZLINK_ROUTING_ID: {
            zlink_routing_id_t rid;
            const int rc = zlink_get_routing_id (socket_, &rid);
            if (rc != 0)
                return rc;
            if (!optvallen_) {
                errno = EINVAL;
                return -1;
            }
            const size_t need = rid.size;
            if (!optval_ || *optvallen_ < need) {
                *optvallen_ = need;
                errno = EINVAL;
                return -1;
            }
            memcpy (optval_, rid.data, need);
            *optvallen_ = need;
            return 0;
        }
        case ZLINK_TOS:
            return zlink_get_option (socket_, ZLINK_OPT_TOS, optval_, optvallen_);
        case ZLINK_LINGER:
            return zlink_get_option (socket_, ZLINK_OPT_LINGER, optval_, optvallen_);
        case ZLINK_IPV6:
            return zlink_get_option (socket_, ZLINK_OPT_IPV6, optval_, optvallen_);
        default:
            errno = EINVAL;
            return -1;
    }
}

inline int apply_tls_server_if_ready (void *handle_)
{
    tls_state_t &state = tls_states ()[handle_];
    if (state.cert.empty () || state.key.empty ())
        return 0;
    return zlink_set_tls_server (
      handle_, state.cert.c_str (), state.key.c_str (), state.require_client_cert);
}

inline int apply_tls_client_if_ready (void *handle_)
{
    tls_state_t &state = tls_states ()[handle_];
    if (state.ca.empty () && !state.trust_system)
        return 0;
    return zlink_set_tls_client (
      handle_, state.ca.empty () ? NULL : state.ca.c_str (),
      state.hostname.empty () ? NULL : state.hostname.c_str (),
      state.trust_system);
}
} // namespace zlink_test_compat

static inline int zlink_setsockopt (void *socket_,
                                    int option_,
                                    const void *optval_,
                                    size_t optvallen_)
{
    if (zlink::socket_base_t *socket =
          zlink_test_compat::as_raw_socket (socket_)) {
        return socket->setsockopt (
          zlink_test_compat::legacy_socket_option_to_internal (option_),
          optval_, optvallen_);
    }

    switch (option_) {
        case ZLINK_ROUTING_ID:
            return zlink_set_routing_id (socket_, optval_, optvallen_);
        case ZLINK_SUBSCRIBE:
            return zlink_set_subscription (socket_, static_cast<const char *> (optval_));
        case ZLINK_UNSUBSCRIBE:
            return zlink_unset_subscription (socket_, static_cast<const char *> (optval_));
        case ZLINK_ROUTER_MANDATORY:
            return zlink_set_router_option (
              socket_, ZLINK_ROUTER_OPT_MANDATORY, optval_, optvallen_);
        case ZLINK_ROUTER_HANDOVER:
            return zlink_set_router_option (
              socket_, ZLINK_ROUTER_OPT_HANDOVER, optval_, optvallen_);
        case ZLINK_PROBE_ROUTER:
            return zlink_set_router_option (
              socket_, ZLINK_ROUTER_OPT_PROBE, optval_, optvallen_);
        case ZLINK_CONNECT_ROUTING_ID:
            return zlink_set_router_option (
              socket_, ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID, optval_, optvallen_);
        case ZLINK_STREAM_NOTIFY:
            return zlink_set_stream_option (
              socket_, ZLINK_STREAM_OPT_NOTIFY, optval_, optvallen_);
        case ZLINK_XPUB_VERBOSE:
            return zlink_set_pub_option (
              socket_, ZLINK_PUB_OPT_VERBOSE, optval_, optvallen_);
        case ZLINK_XPUB_VERBOSER:
            return zlink_set_pub_option (
              socket_, ZLINK_PUB_OPT_VERBOSER, optval_, optvallen_);
        case ZLINK_XPUB_MANUAL:
            return zlink_set_pub_option (
              socket_, ZLINK_PUB_OPT_MANUAL, optval_, optvallen_);
        case ZLINK_XPUB_MANUAL_LAST_VALUE:
            return zlink_set_pub_option (
              socket_, ZLINK_PUB_OPT_MANUAL_LAST_VALUE, optval_, optvallen_);
        case ZLINK_XPUB_NODROP:
            return zlink_set_pub_option (
              socket_, ZLINK_PUB_OPT_NODROP, optval_, optvallen_);
        case ZLINK_XPUB_WELCOME_MSG:
            return zlink_set_pub_option (
              socket_, ZLINK_PUB_OPT_WELCOME_MSG, optval_, optvallen_);
        case ZLINK_TLS_CERT: {
            zlink_test_compat::tls_state_t &state =
              zlink_test_compat::tls_states ()[socket_];
            state.cert.assign (static_cast<const char *> (optval_),
                               optvallen_ ? optvallen_ : strlen (static_cast<const char *> (optval_)));
            return zlink_test_compat::apply_tls_server_if_ready (socket_);
        }
        case ZLINK_TLS_KEY: {
            zlink_test_compat::tls_state_t &state =
              zlink_test_compat::tls_states ()[socket_];
            state.key.assign (static_cast<const char *> (optval_),
                              optvallen_ ? optvallen_ : strlen (static_cast<const char *> (optval_)));
            return zlink_test_compat::apply_tls_server_if_ready (socket_);
        }
        case ZLINK_TLS_REQUIRE_CLIENT_CERT: {
            zlink_test_compat::tls_state_t &state =
              zlink_test_compat::tls_states ()[socket_];
            state.require_client_cert =
              optval_ && optvallen_ == sizeof (int)
                ? *static_cast<const int *> (optval_)
                : 0;
            return zlink_test_compat::apply_tls_server_if_ready (socket_);
        }
        case ZLINK_TLS_CA: {
            zlink_test_compat::tls_state_t &state =
              zlink_test_compat::tls_states ()[socket_];
            state.ca.assign (static_cast<const char *> (optval_),
                             optvallen_ ? optvallen_ : strlen (static_cast<const char *> (optval_)));
            return zlink_test_compat::apply_tls_client_if_ready (socket_);
        }
        case ZLINK_TLS_HOSTNAME: {
            zlink_test_compat::tls_state_t &state =
              zlink_test_compat::tls_states ()[socket_];
            state.hostname.assign (static_cast<const char *> (optval_),
                                   optvallen_ ? optvallen_ : strlen (static_cast<const char *> (optval_)));
            return zlink_test_compat::apply_tls_client_if_ready (socket_);
        }
        case ZLINK_TLS_TRUST_SYSTEM: {
            zlink_test_compat::tls_state_t &state =
              zlink_test_compat::tls_states ()[socket_];
            state.trust_system =
              optval_ && optvallen_ == sizeof (int)
                ? *static_cast<const int *> (optval_)
                : 0;
            return zlink_test_compat::apply_tls_client_if_ready (socket_);
        }
        default:
            return zlink_test_compat::set_common_option (
              socket_, option_, optval_, optvallen_);
    }
}

static inline int zlink_getsockopt (void *socket_,
                                    int option_,
                                    void *optval_,
                                    size_t *optvallen_)
{
    if (zlink::socket_base_t *socket =
          zlink_test_compat::as_raw_socket (socket_)) {
        const int internal_option =
          zlink_test_compat::legacy_socket_option_to_internal (option_);
        const int rc = socket->getsockopt (internal_option, optval_, optvallen_);
        if (rc != 0) {
            if (option_ == ZLINK_ROUTING_ID && optvallen_) {
                zlink_routing_id_t rid;
                const int rid_rc = zlink_get_routing_id (socket_, &rid);
                if (rid_rc == 0) {
                    const size_t need = rid.size;
                    if (!optval_ || *optvallen_ < need) {
                        *optvallen_ = need;
                        errno = EINVAL;
                        return -1;
                    }
                    if (need > 0)
                        memcpy (optval_, rid.data, need);
                    *optvallen_ = need;
                    return 0;
                }
            }
            return rc;
        }
        if ((option_ == ZLINK_TYPE || option_ == ZLINK_SOCKOPT_TYPE) && optval_
            && optvallen_ && *optvallen_ >= sizeof (int)) {
            int type = 0;
            memcpy (&type, optval_, sizeof (type));
            type = zlink_test_compat::public_socket_type_to_legacy (type);
            memcpy (optval_, &type, sizeof (type));
        }
        return 0;
    }

    return zlink_test_compat::get_common_option (
      socket_, option_, optval_, optvallen_);
}

static inline int zlink_registry_setsockopt (void *,
                                             int,
                                             int,
                                             const void *,
                                             size_t)
{
    errno = ENOTSUP;
    return -1;
}

static inline int zlink_unsubscribe (void *handle_, const char *filter_)
{
    return zlink_unset_subscription (handle_, filter_);
}

static inline void *zlink_spot_pub_new (void *node_)
{
    zlink::spot_node_t *node = zlink_test_compat::as_spot_node (node_);
    if (!node) {
        errno = EFAULT;
        return NULL;
    }
    return node->create_spot_pub ();
}
static inline int zlink_spot_pub_destroy (void **spot_pub_p)
{
    if (!spot_pub_p) {
        errno = EFAULT;
        return -1;
    }
    if (!*spot_pub_p) {
        errno = EFAULT;
        return -1;
    }
    zlink::spot_pub_t *pub = zlink_test_compat::as_spot_pub (*spot_pub_p);
    if (!pub) {
        errno = EFAULT;
        return -1;
    }
    int rc = pub->destroy ();
    if (rc != 0)
        rc = pub->destroy_from_node ();
    if (rc != 0)
        return -1;
    delete pub;
    *spot_pub_p = NULL;
    return 0;
}
static inline void *zlink_spot_sub_new (void *node_)
{
    zlink::spot_node_t *node = zlink_test_compat::as_spot_node (node_);
    if (!node) {
        errno = EFAULT;
        return NULL;
    }
    return node->create_spot_sub ();
}
static inline int zlink_spot_sub_destroy (void **spot_sub_p)
{
    if (!spot_sub_p) {
        errno = EFAULT;
        return -1;
    }
    if (!*spot_sub_p) {
        errno = EFAULT;
        return -1;
    }
    zlink::spot_sub_t *sub = zlink_test_compat::as_spot_sub (*spot_sub_p);
    if (!sub) {
        errno = EFAULT;
        return -1;
    }
    int rc = sub->destroy ();
    if (rc != 0)
        rc = sub->destroy_from_node ();
    if (rc != 0)
        return -1;
    delete sub;
    *spot_sub_p = NULL;
    return 0;
}
static inline void *zlink_spot_node_default_pub (void *node_)
{
    zlink::spot_node_t *node = zlink_test_compat::as_spot_node (node_);
    if (!node) {
        errno = EFAULT;
        return NULL;
    }
    return node->ensure_default_pub ();
}
static inline void *zlink_spot_node_default_sub (void *node_)
{
    zlink::spot_node_t *node = zlink_test_compat::as_spot_node (node_);
    if (!node) {
        errno = EFAULT;
        return NULL;
    }
    return node->ensure_default_sub ();
}
static inline int zlink_spot_pub_send_ready_handler (
  void *handle_,
  zlink_send_ready_handler_fn handler_,
  void *userdata_)
{
    if (zlink::spot_pub_t *pub = zlink_test_compat::as_spot_pub (handle_)) {
        if (pub->node ()) {
            zlink::service_public_api_scope_t admission (
              pub->node ()->public_api_guard ());
            if (!admission.acquired ())
                return -1;
        }
        void *subject = handle_;
        if (pub->is_node_owned_default () && pub->node ())
            subject = pub->node ();
        return pub->set_send_ready_handler (handler_, subject, userdata_);
    }
    return zlink_send_ready_handler (handle_, handler_, userdata_);
}
static inline void *zlink_spot_pub_monitor_open (
  void *handle_,
  zlink_spot_monitor_event_mask_t events_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_)
{
    if (zlink_test_compat::as_spot_pub (handle_))
        return zlink_spot_monitor_open (
          handle_, ZLINK_SPOT_ROLE_PUB, events_, handler_, userdata_);
    return zlink_spot_node_monitor_open (
      handle_, ZLINK_SPOT_ROLE_PUB, events_, handler_, userdata_);
}
static inline void *zlink_spot_sub_monitor_open (
  void *handle_,
  zlink_spot_monitor_event_mask_t events_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_)
{
    if (zlink_test_compat::as_spot_sub (handle_))
        return zlink_spot_monitor_open (
          handle_, ZLINK_SPOT_ROLE_SUB, events_, handler_, userdata_);
    return zlink_spot_node_monitor_open (
      handle_, ZLINK_SPOT_ROLE_SUB, events_, handler_, userdata_);
}

#endif
