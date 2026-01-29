/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_H_INCLUDED__
#define __ZMQ_H_INCLUDED__

/*  Version macros for compile-time API version detection                     */
#define ZMQ_VERSION_MAJOR 4
#define ZMQ_VERSION_MINOR 3
#define ZMQ_VERSION_PATCH 5

#define ZMQ_MAKE_VERSION(major, minor, patch)                                  \
    ((major) *10000 + (minor) *100 + (patch))
#define ZMQ_VERSION                                                            \
    ZMQ_MAKE_VERSION (ZMQ_VERSION_MAJOR, ZMQ_VERSION_MINOR, ZMQ_VERSION_PATCH)

#ifdef __cplusplus
extern "C" {
#endif

#if !defined _WIN32_WCE
#include <errno.h>
#endif
#include <stddef.h>
#include <stdio.h>

/*  Handle DSO symbol visibility                                             */
#if defined ZMQ_NO_EXPORT
#define ZMQ_EXPORT
#else
#if defined _WIN32
#if defined ZMQ_STATIC
#define ZMQ_EXPORT
#elif defined DLL_EXPORT
#define ZMQ_EXPORT __declspec(dllexport)
#else
#define ZMQ_EXPORT __declspec(dllimport)
#endif
#else
#if defined __SUNPRO_C || defined __SUNPRO_CC
#define ZMQ_EXPORT __global
#elif (defined __GNUC__ && __GNUC__ >= 4) || defined __INTEL_COMPILER
#define ZMQ_EXPORT __attribute__ ((visibility ("default")))
#else
#define ZMQ_EXPORT
#endif
#endif
#endif

/*  Define integer types needed for event interface                          */
#define ZMQ_DEFINED_STDINT 1
#if defined ZMQ_HAVE_SOLARIS || defined ZMQ_HAVE_OPENVMS
#include <inttypes.h>
#elif defined _MSC_VER && _MSC_VER < 1600
#ifndef uint64_t
typedef unsigned __int64 uint64_t;
#endif
#ifndef int32_t
typedef __int32 int32_t;
#endif
#ifndef uint32_t
typedef unsigned __int32 uint32_t;
#endif
#ifndef uint16_t
typedef unsigned __int16 uint16_t;
#endif
#ifndef uint8_t
typedef unsigned __int8 uint8_t;
#endif
#else
#include <stdint.h>
#endif

#if !defined _WIN32
#include <signal.h>
#endif

#ifdef ZMQ_HAVE_AIX
#include <poll.h>
#endif

/******************************************************************************/
/*  0MQ errors.                                                               */
/******************************************************************************/
#define ZMQ_HAUSNUMERO 156384712

#ifndef ENOTSUP
#define ENOTSUP (ZMQ_HAUSNUMERO + 1)
#endif
#ifndef EPROTONOSUPPORT
#define EPROTONOSUPPORT (ZMQ_HAUSNUMERO + 2)
#endif
#ifndef ENOBUFS
#define ENOBUFS (ZMQ_HAUSNUMERO + 3)
#endif
#ifndef ENETDOWN
#define ENETDOWN (ZMQ_HAUSNUMERO + 4)
#endif
#ifndef EADDRINUSE
#define EADDRINUSE (ZMQ_HAUSNUMERO + 5)
#endif
#ifndef EADDRNOTAVAIL
#define EADDRNOTAVAIL (ZMQ_HAUSNUMERO + 6)
#endif
#ifndef ECONNREFUSED
#define ECONNREFUSED (ZMQ_HAUSNUMERO + 7)
#endif
#ifndef EINPROGRESS
#define EINPROGRESS (ZMQ_HAUSNUMERO + 8)
#endif
#ifndef ENOTSOCK
#define ENOTSOCK (ZMQ_HAUSNUMERO + 9)
#endif
#ifndef EMSGSIZE
#define EMSGSIZE (ZMQ_HAUSNUMERO + 10)
#endif
#ifndef EAFNOSUPPORT
#define EAFNOSUPPORT (ZMQ_HAUSNUMERO + 11)
#endif
#ifndef ENETUNREACH
#define ENETUNREACH (ZMQ_HAUSNUMERO + 12)
#endif
#ifndef ECONNABORTED
#define ECONNABORTED (ZMQ_HAUSNUMERO + 13)
#endif
#ifndef ECONNRESET
#define ECONNRESET (ZMQ_HAUSNUMERO + 14)
#endif
#ifndef ENOTCONN
#define ENOTCONN (ZMQ_HAUSNUMERO + 15)
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT (ZMQ_HAUSNUMERO + 16)
#endif
#ifndef EHOSTUNREACH
#define EHOSTUNREACH (ZMQ_HAUSNUMERO + 17)
#endif
#ifndef ENETRESET
#define ENETRESET (ZMQ_HAUSNUMERO + 18)
#endif

#define EFSM (ZMQ_HAUSNUMERO + 51)
#define ENOCOMPATPROTO (ZMQ_HAUSNUMERO + 52)
#define ETERM (ZMQ_HAUSNUMERO + 53)
#define EMTHREAD (ZMQ_HAUSNUMERO + 54)

ZMQ_EXPORT int zmq_errno (void);
ZMQ_EXPORT const char *zmq_strerror (int errnum_);
ZMQ_EXPORT void zmq_version (int *major_, int *minor_, int *patch_);

/******************************************************************************/
/*  0MQ infrastructure (a.k.a. context) initialisation & termination.         */
/******************************************************************************/
#define ZMQ_IO_THREADS 1
#define ZMQ_MAX_SOCKETS 2
#define ZMQ_SOCKET_LIMIT 3
#define ZMQ_THREAD_PRIORITY 3
#define ZMQ_THREAD_SCHED_POLICY 4
#define ZMQ_MAX_MSGSZ 5
#define ZMQ_MSG_T_SIZE 6
#define ZMQ_THREAD_AFFINITY_CPU_ADD 7
#define ZMQ_THREAD_AFFINITY_CPU_REMOVE 8
#define ZMQ_THREAD_NAME_PREFIX 9

#define ZMQ_IO_THREADS_DFLT 1
#define ZMQ_MAX_SOCKETS_DFLT 1023
#define ZMQ_THREAD_PRIORITY_DFLT -1
#define ZMQ_THREAD_SCHED_POLICY_DFLT -1

ZMQ_EXPORT void *zmq_ctx_new (void);
ZMQ_EXPORT int zmq_ctx_term (void *context_);
ZMQ_EXPORT int zmq_ctx_shutdown (void *context_);
ZMQ_EXPORT int zmq_ctx_set (void *context_, int option_, int optval_);
ZMQ_EXPORT int zmq_ctx_get (void *context_, int option_);

/******************************************************************************/
/*  0MQ message definition.                                                   */
/******************************************************************************/
typedef struct zmq_msg_t
{
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
    __declspec(align (8)) unsigned char _[64];
#elif defined(_MSC_VER)                                                        \
  && (defined(_M_IX86) || defined(_M_ARM_ARMV7VE) || defined(_M_ARM))
    __declspec(align (4)) unsigned char _[64];
#elif defined(__GNUC__) || defined(__INTEL_COMPILER)                           \
  || (defined(__SUNPRO_C) && __SUNPRO_C >= 0x590)                              \
  || (defined(__SUNPRO_CC) && __SUNPRO_CC >= 0x590)
    unsigned char _[64] __attribute__ ((aligned (sizeof (void *))));
#else
    unsigned char _[64];
#endif
} zmq_msg_t;

typedef struct zmq_routing_id_t
{
    uint8_t size;
    uint8_t data[255];
} zmq_routing_id_t;

typedef void (zmq_free_fn) (void *data_, void *hint_);

ZMQ_EXPORT int zmq_msg_init (zmq_msg_t *msg_);
ZMQ_EXPORT int zmq_msg_init_size (zmq_msg_t *msg_, size_t size_);
ZMQ_EXPORT int zmq_msg_init_data (
  zmq_msg_t *msg_, void *data_, size_t size_, zmq_free_fn *ffn_, void *hint_);
ZMQ_EXPORT int zmq_msg_send (zmq_msg_t *msg_, void *s_, int flags_);
ZMQ_EXPORT int zmq_msg_recv (zmq_msg_t *msg_, void *s_, int flags_);
ZMQ_EXPORT int zmq_msg_close (zmq_msg_t *msg_);
ZMQ_EXPORT int zmq_msg_move (zmq_msg_t *dest_, zmq_msg_t *src_);
ZMQ_EXPORT int zmq_msg_copy (zmq_msg_t *dest_, zmq_msg_t *src_);
ZMQ_EXPORT void *zmq_msg_data (zmq_msg_t *msg_);
ZMQ_EXPORT size_t zmq_msg_size (const zmq_msg_t *msg_);
ZMQ_EXPORT int zmq_msg_more (const zmq_msg_t *msg_);
ZMQ_EXPORT int zmq_msg_get (const zmq_msg_t *msg_, int property_);
ZMQ_EXPORT int zmq_msg_set (zmq_msg_t *msg_, int property_, int optval_);
ZMQ_EXPORT const char *zmq_msg_gets (const zmq_msg_t *msg_,
                                     const char *property_);

/******************************************************************************/
/*  0MQ socket definition.                                                    */
/******************************************************************************/
#define ZMQ_PAIR 0
#define ZMQ_PUB 1
#define ZMQ_SUB 2
#define ZMQ_DEALER 5
#define ZMQ_ROUTER 6
#define ZMQ_XPUB 9
#define ZMQ_XSUB 10
#define ZMQ_STREAM 11

#define ZMQ_AFFINITY 4
#define ZMQ_ROUTING_ID 5
#define ZMQ_SUBSCRIBE 6
#define ZMQ_UNSUBSCRIBE 7
#define ZMQ_RATE 8
#define ZMQ_RECOVERY_IVL 9
#define ZMQ_SNDBUF 11
#define ZMQ_RCVBUF 12
#define ZMQ_RCVMORE 13
#define ZMQ_FD 14
#define ZMQ_EVENTS 15
#define ZMQ_TYPE 16
#define ZMQ_LINGER 17
#define ZMQ_RECONNECT_IVL 18
#define ZMQ_BACKLOG 19
#define ZMQ_RECONNECT_IVL_MAX 21
#define ZMQ_MAXMSGSIZE 22
#define ZMQ_SNDHWM 23
#define ZMQ_RCVHWM 24
#define ZMQ_MULTICAST_HOPS 25
#define ZMQ_RCVTIMEO 27
#define ZMQ_SNDTIMEO 28
#define ZMQ_LAST_ENDPOINT 32
#define ZMQ_ROUTER_MANDATORY 33
#define ZMQ_TCP_KEEPALIVE 34
#define ZMQ_TCP_KEEPALIVE_CNT 35
#define ZMQ_TCP_KEEPALIVE_IDLE 36
#define ZMQ_TCP_KEEPALIVE_INTVL 37
#define ZMQ_IMMEDIATE 39
#define ZMQ_XPUB_VERBOSE 40
#define ZMQ_IPV6 42
#define ZMQ_PROBE_ROUTER 51
#define ZMQ_CONFLATE 54
#define ZMQ_ROUTER_HANDOVER 56
#define ZMQ_TOS 57
#define ZMQ_CONNECT_ROUTING_ID 61
#define ZMQ_HANDSHAKE_IVL 66
#define ZMQ_XPUB_NODROP 69
#define ZMQ_BLOCKY 70
#define ZMQ_XPUB_MANUAL 71
#define ZMQ_XPUB_WELCOME_MSG 72
#define ZMQ_INVERT_MATCHING 74
#define ZMQ_HEARTBEAT_IVL 75
#define ZMQ_HEARTBEAT_TTL 76
#define ZMQ_HEARTBEAT_TIMEOUT 77
#define ZMQ_XPUB_VERBOSER 78
#define ZMQ_CONNECT_TIMEOUT 79
#define ZMQ_TCP_MAXRT 80
#define ZMQ_THREAD_SAFE 81
#define ZMQ_MULTICAST_MAXTPDU 84
#define ZMQ_USE_FD 89
#define ZMQ_REQUEST_TIMEOUT 90
#define ZMQ_REQUEST_CORRELATE 91
#define ZMQ_BINDTODEVICE 92
#define ZMQ_XPUB_MANUAL_LAST_VALUE 98
#define ZMQ_ONLY_FIRST_SUBSCRIBE 108
#define ZMQ_TOPICS_COUNT 116
#define ZMQ_ZMP_METADATA 117

//  TLS protocol options
#define ZMQ_TLS_CERT 95
#define ZMQ_TLS_KEY 96
#define ZMQ_TLS_CA 97
#define ZMQ_TLS_VERIFY 98
#define ZMQ_TLS_REQUIRE_CLIENT_CERT 99
#define ZMQ_TLS_HOSTNAME 100
#define ZMQ_TLS_TRUST_SYSTEM 101
#define ZMQ_TLS_PASSWORD 102

#define ZMQ_MORE 1
#define ZMQ_SHARED 3

#define ZMQ_DONTWAIT 1
#define ZMQ_SNDMORE 2

#define ZMQ_NULL 0
#define ZMQ_PLAIN 1

/******************************************************************************/
/*  0MQ socket events and monitoring                                          */
/******************************************************************************/
#define ZMQ_EVENT_CONNECTED 0x0001
#define ZMQ_EVENT_CONNECT_DELAYED 0x0002
#define ZMQ_EVENT_CONNECT_RETRIED 0x0004
#define ZMQ_EVENT_LISTENING 0x0008
#define ZMQ_EVENT_BIND_FAILED 0x0010
#define ZMQ_EVENT_ACCEPTED 0x0020
#define ZMQ_EVENT_ACCEPT_FAILED 0x0040
#define ZMQ_EVENT_CLOSED 0x0080
#define ZMQ_EVENT_CLOSE_FAILED 0x0100
#define ZMQ_EVENT_DISCONNECTED 0x0200
#define ZMQ_EVENT_MONITOR_STOPPED 0x0400
#define ZMQ_EVENT_ALL 0xFFFF
#define ZMQ_EVENT_HANDSHAKE_FAILED_NO_DETAIL 0x0800
#define ZMQ_EVENT_CONNECTION_READY 0x1000
#define ZMQ_EVENT_HANDSHAKE_FAILED_PROTOCOL 0x2000
#define ZMQ_EVENT_HANDSHAKE_FAILED_AUTH 0x4000

#define ZMQ_DISCONNECT_UNKNOWN 0
#define ZMQ_DISCONNECT_LOCAL 1
#define ZMQ_DISCONNECT_REMOTE 2
#define ZMQ_DISCONNECT_HANDSHAKE_FAILED 3
#define ZMQ_DISCONNECT_TRANSPORT_ERROR 4
#define ZMQ_DISCONNECT_CTX_TERM 5

#define ZMQ_PROTOCOL_ERROR_ZMP_UNSPECIFIED 0x10000000
#define ZMQ_PROTOCOL_ERROR_ZMP_UNEXPECTED_COMMAND 0x10000001
#define ZMQ_PROTOCOL_ERROR_ZMP_INVALID_SEQUENCE 0x10000002
#define ZMQ_PROTOCOL_ERROR_ZMP_KEY_EXCHANGE 0x10000003
#define ZMQ_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_UNSPECIFIED 0x10000011
#define ZMQ_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_MESSAGE 0x10000012
#define ZMQ_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_HELLO 0x10000013
#define ZMQ_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_INITIATE 0x10000014
#define ZMQ_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_ERROR 0x10000015
#define ZMQ_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_READY 0x10000016
#define ZMQ_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_WELCOME 0x10000017
#define ZMQ_PROTOCOL_ERROR_ZMP_INVALID_METADATA 0x10000018
#define ZMQ_PROTOCOL_ERROR_ZMP_CRYPTOGRAPHIC 0x11000001
#define ZMQ_PROTOCOL_ERROR_ZMP_MECHANISM_MISMATCH 0x11000002
#define ZMQ_PROTOCOL_ERROR_WS_UNSPECIFIED 0x30000000

ZMQ_EXPORT void *zmq_socket (void *, int type_);
ZMQ_EXPORT void *zmq_socket_threadsafe (void *, int type_);
ZMQ_EXPORT int zmq_close (void *s_);
ZMQ_EXPORT int
zmq_setsockopt (void *s_, int option_, const void *optval_, size_t optvallen_);
ZMQ_EXPORT int
zmq_getsockopt (void *s_, int option_, void *optval_, size_t *optvallen_);
ZMQ_EXPORT int zmq_is_threadsafe (void *s_);
ZMQ_EXPORT int zmq_bind (void *s_, const char *addr_);
ZMQ_EXPORT int zmq_connect (void *s_, const char *addr_);
ZMQ_EXPORT int zmq_unbind (void *s_, const char *addr_);
ZMQ_EXPORT int zmq_disconnect (void *s_, const char *addr_);
ZMQ_EXPORT int zmq_send (void *s_, const void *buf_, size_t len_, int flags_);
ZMQ_EXPORT int
zmq_send_const (void *s_, const void *buf_, size_t len_, int flags_);
ZMQ_EXPORT int zmq_recv (void *s_, void *buf_, size_t len_, int flags_);
ZMQ_EXPORT int zmq_socket_monitor (void *s_, const char *addr_, int events_);
ZMQ_EXPORT void *zmq_socket_monitor_open (void *s_, int events_);

typedef struct {
    uint64_t event;
    uint64_t value;
    zmq_routing_id_t routing_id;
    char local_addr[256];
    char remote_addr[256];
} zmq_monitor_event_t;

ZMQ_EXPORT int zmq_monitor_recv (void *monitor_socket_,
                                 zmq_monitor_event_t *event_,
                                 int flags_);

typedef struct {
    uint64_t msgs_sent;
    uint64_t msgs_received;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t msgs_dropped;
    uint64_t monitor_events_dropped;
    uint32_t queue_size;
    uint32_t hwm_reached;
    uint32_t peer_count;
} zmq_socket_stats_t;

ZMQ_EXPORT int zmq_socket_stats (void *socket_, zmq_socket_stats_t *stats_);

typedef struct {
    uint64_t msgs_sent;
    uint64_t msgs_received;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t msgs_dropped;
    uint64_t monitor_events_dropped;
    uint32_t queue_outbound;
    uint32_t queue_inbound;
    uint32_t hwm_reached;
    uint32_t peer_count;
    uint64_t drops_hwm;
    uint64_t drops_no_peers;
    uint64_t drops_filter;
    uint64_t last_send_ms;
    uint64_t last_recv_ms;
} zmq_socket_stats_ex_t;

ZMQ_EXPORT int zmq_socket_stats_ex (void *socket_,
                                   zmq_socket_stats_ex_t *stats_);

typedef struct {
    zmq_routing_id_t routing_id;
    char remote_addr[256];
    uint64_t connected_time;
    uint64_t msgs_sent;
    uint64_t msgs_received;
} zmq_peer_info_t;

ZMQ_EXPORT int zmq_socket_peer_info (void *socket_,
                                     const zmq_routing_id_t *routing_id_,
                                     zmq_peer_info_t *info_);
ZMQ_EXPORT int zmq_socket_peer_routing_id (void *socket_,
                                           int index_,
                                           zmq_routing_id_t *out_);
ZMQ_EXPORT int zmq_socket_peer_count (void *socket_);
ZMQ_EXPORT int zmq_socket_peers (void *socket_,
                                 zmq_peer_info_t *peers_,
                                 size_t *count_);

/******************************************************************************/
/*  Request/Reply API (thread-safe sockets only)                              */
/******************************************************************************/

#define ZMQ_REQUEST_TIMEOUT_DEFAULT -2

#define ETIMEDOUT_ZMQ 110
#define ECANCELED_ZMQ 125

typedef void (*zmq_request_cb_fn) (uint64_t request_id,
                                   zmq_msg_t *reply_parts,
                                   size_t reply_count,
                                   int error);

typedef void (*zmq_server_cb_fn) (zmq_msg_t *request_parts,
                                  size_t part_count,
                                  const zmq_routing_id_t *routing_id,
                                  uint64_t request_id);

typedef struct zmq_completion_t
{
    uint64_t request_id;
    zmq_msg_t *parts;
    size_t part_count;
    int error;
} zmq_completion_t;

ZMQ_EXPORT uint64_t zmq_request (void *socket,
                                 const zmq_routing_id_t *routing_id,
                                 zmq_msg_t *parts,
                                 size_t part_count,
                                 zmq_request_cb_fn callback,
                                 int timeout_ms);
ZMQ_EXPORT uint64_t zmq_group_request (void *socket,
                                       const zmq_routing_id_t *routing_id,
                                       uint64_t group_id,
                                       zmq_msg_t *parts,
                                       size_t part_count,
                                       zmq_request_cb_fn callback,
                                       int timeout_ms);
ZMQ_EXPORT int zmq_on_request (void *socket, zmq_server_cb_fn handler);
ZMQ_EXPORT int zmq_reply (void *socket,
                          const zmq_routing_id_t *routing_id,
                          uint64_t request_id,
                          zmq_msg_t *parts,
                          size_t part_count);
ZMQ_EXPORT int zmq_reply_simple (void *socket,
                                 zmq_msg_t *parts,
                                 size_t part_count);
ZMQ_EXPORT void zmq_msgv_close (zmq_msg_t *parts, size_t part_count);
ZMQ_EXPORT uint64_t zmq_request_send (void *socket,
                                      const zmq_routing_id_t *routing_id,
                                      zmq_msg_t *parts,
                                      size_t part_count);
ZMQ_EXPORT int zmq_request_recv (void *socket,
                                 zmq_completion_t *completion,
                                 int timeout_ms);
ZMQ_EXPORT int zmq_pending_requests (void *socket);
ZMQ_EXPORT int zmq_cancel_all_requests (void *socket);

/******************************************************************************/
/*  Service Discovery API                                                     */
/******************************************************************************/

typedef struct {
    char service_name[256];
    char endpoint[256];
    zmq_routing_id_t routing_id;
    uint32_t weight;
    uint64_t registered_at;
} zmq_provider_info_t;

typedef struct {
    char service_name[256];
    uint64_t request_id;
    int error;
    zmq_msg_t *parts;
    size_t part_count;
} zmq_gateway_completion_t;

typedef void (*zmq_gateway_request_cb_fn) (uint64_t request_id,
                                           zmq_msg_t *reply_parts,
                                           size_t reply_count,
                                           int error);

/* Registry */
ZMQ_EXPORT void *zmq_registry_new (void *ctx);
ZMQ_EXPORT int zmq_registry_set_endpoints (void *registry,
                                           const char *pub_endpoint,
                                           const char *router_endpoint);
ZMQ_EXPORT int zmq_registry_set_id (void *registry, uint32_t registry_id);
ZMQ_EXPORT int zmq_registry_add_peer (void *registry,
                                      const char *peer_pub_endpoint);
ZMQ_EXPORT int zmq_registry_set_heartbeat (void *registry,
                                           uint32_t interval_ms,
                                           uint32_t timeout_ms);
ZMQ_EXPORT int zmq_registry_set_broadcast_interval (void *registry,
                                                    uint32_t interval_ms);
ZMQ_EXPORT int zmq_registry_start (void *registry);
ZMQ_EXPORT int zmq_registry_destroy (void **registry_p);

/* Discovery */
ZMQ_EXPORT void *zmq_discovery_new (void *ctx);
ZMQ_EXPORT int zmq_discovery_connect_registry (
  void *discovery, const char *registry_pub_endpoint);
ZMQ_EXPORT int zmq_discovery_subscribe (void *discovery,
                                        const char *service_name);
ZMQ_EXPORT int zmq_discovery_unsubscribe (void *discovery,
                                          const char *service_name);
ZMQ_EXPORT int zmq_discovery_get_providers (void *discovery,
                                            const char *service_name,
                                            zmq_provider_info_t *providers,
                                            size_t *count);
ZMQ_EXPORT int zmq_discovery_provider_count (void *discovery,
                                             const char *service_name);
ZMQ_EXPORT int zmq_discovery_service_available (void *discovery,
                                                const char *service_name);
ZMQ_EXPORT int zmq_discovery_destroy (void **discovery_p);

/* Gateway */
ZMQ_EXPORT void *zmq_gateway_new (void *ctx, void *discovery);
ZMQ_EXPORT int zmq_gateway_send (void *gateway,
                                 const char *service_name,
                                 zmq_msg_t *parts,
                                 size_t part_count,
                                 int flags,
                                 uint64_t *request_id_out);
ZMQ_EXPORT int zmq_gateway_recv (void *gateway,
                                 zmq_msg_t **parts,
                                 size_t *part_count,
                                 int flags,
                                 char *service_name_out,
                                 uint64_t *request_id_out);
ZMQ_EXPORT void *zmq_gateway_threadsafe_router (void *gateway,
                                                const char *service_name);

ZMQ_EXPORT uint64_t zmq_gateway_request (void *gateway,
                                         const char *service_name,
                                         zmq_msg_t *parts,
                                         size_t part_count,
                                         zmq_gateway_request_cb_fn callback,
                                         int timeout_ms);
ZMQ_EXPORT uint64_t zmq_gateway_request_send (void *gateway,
                                              const char *service_name,
                                              zmq_msg_t *parts,
                                              size_t part_count,
                                              int flags);
ZMQ_EXPORT int zmq_gateway_request_recv (void *gateway,
                                         zmq_gateway_completion_t *completion,
                                         int timeout_ms);

#define ZMQ_GATEWAY_LB_ROUND_ROBIN 0
#define ZMQ_GATEWAY_LB_WEIGHTED 1

ZMQ_EXPORT int zmq_gateway_set_lb_strategy (void *gateway,
                                            const char *service_name,
                                            int strategy);
ZMQ_EXPORT int zmq_gateway_connection_count (void *gateway,
                                             const char *service_name);
ZMQ_EXPORT int zmq_gateway_destroy (void **gateway_p);

/* Provider */
ZMQ_EXPORT void *zmq_provider_new (void *ctx);
ZMQ_EXPORT int zmq_provider_bind (void *provider,
                                  const char *bind_endpoint);
ZMQ_EXPORT int zmq_provider_connect_registry (void *provider,
                                              const char *registry_endpoint);
ZMQ_EXPORT int zmq_provider_register (void *provider,
                                      const char *service_name,
                                      const char *advertise_endpoint,
                                      uint32_t weight);
ZMQ_EXPORT int zmq_provider_update_weight (void *provider,
                                           const char *service_name,
                                           uint32_t weight);
ZMQ_EXPORT int zmq_provider_unregister (void *provider,
                                        const char *service_name);
ZMQ_EXPORT int zmq_provider_register_result (void *provider,
                                             const char *service_name,
                                             int *status,
                                             char *resolved_endpoint,
                                             char *error_message);
ZMQ_EXPORT void *zmq_provider_threadsafe_router (void *provider);
ZMQ_EXPORT int zmq_provider_destroy (void **provider_p);

/******************************************************************************/
/*  SPOT Topic PUB/SUB API                                                    */
/******************************************************************************/

#define ZMQ_SPOT_TOPIC_QUEUE 0
#define ZMQ_SPOT_TOPIC_RINGBUFFER 1

/* SPOT Node */
ZMQ_EXPORT void *zmq_spot_node_new (void *ctx);
ZMQ_EXPORT int zmq_spot_node_destroy (void **node_p);
ZMQ_EXPORT int zmq_spot_node_bind (void *node, const char *endpoint);
ZMQ_EXPORT int zmq_spot_node_connect_registry (void *node,
                                               const char *registry_endpoint);
ZMQ_EXPORT int zmq_spot_node_connect_peer_pub (void *node,
                                               const char *peer_pub_endpoint);
ZMQ_EXPORT int zmq_spot_node_disconnect_peer_pub (
  void *node, const char *peer_pub_endpoint);
ZMQ_EXPORT int zmq_spot_node_register (void *node,
                                       const char *service_name,
                                       const char *advertise_endpoint);
ZMQ_EXPORT int zmq_spot_node_unregister (void *node,
                                         const char *service_name);
ZMQ_EXPORT int zmq_spot_node_set_discovery (void *node,
                                            void *discovery,
                                            const char *service_name);

/* SPOT Instance */
ZMQ_EXPORT void *zmq_spot_new (void *node);
ZMQ_EXPORT void *zmq_spot_new_threadsafe (void *node);
ZMQ_EXPORT int zmq_spot_destroy (void **spot_p);
ZMQ_EXPORT int zmq_spot_topic_create (void *spot,
                                      const char *topic_id,
                                      int mode);
ZMQ_EXPORT int zmq_spot_topic_destroy (void *spot, const char *topic_id);
ZMQ_EXPORT int zmq_spot_publish (void *spot,
                                 const char *topic_id,
                                 zmq_msg_t *parts,
                                 size_t part_count,
                                 int flags);
ZMQ_EXPORT int zmq_spot_subscribe (void *spot, const char *topic_id);
ZMQ_EXPORT int zmq_spot_subscribe_pattern (void *spot, const char *pattern);
ZMQ_EXPORT int zmq_spot_unsubscribe (void *spot,
                                     const char *topic_id_or_pattern);
ZMQ_EXPORT int zmq_spot_recv (void *spot,
                              zmq_msg_t **parts,
                              size_t *part_count,
                              int flags,
                              char *topic_id_out,
                              size_t *topic_id_len);

#if defined _WIN32
#if defined _WIN64
typedef unsigned __int64 zmq_fd_t;
#else
typedef unsigned int zmq_fd_t;
#endif
#else
typedef int zmq_fd_t;
#endif

#define ZMQ_POLLIN 1
#define ZMQ_POLLOUT 2
#define ZMQ_POLLERR 4
#define ZMQ_POLLPRI 8

typedef struct zmq_pollitem_t
{
    void *socket;
    zmq_fd_t fd;
    short events;
    short revents;
} zmq_pollitem_t;

#define ZMQ_POLLITEMS_DFLT 16

ZMQ_EXPORT int zmq_poll (zmq_pollitem_t *items_, int nitems_, long timeout_);

ZMQ_EXPORT int zmq_proxy (void *frontend_, void *backend_, void *capture_);
ZMQ_EXPORT int zmq_proxy_steerable (void *frontend_,
                                    void *backend_,
                                    void *capture_,
                                    void *control_);

ZMQ_EXPORT int zmq_has (const char *capability_);

/******************************************************************************/
/*  Atomic utility methods                                                    */
/******************************************************************************/
ZMQ_EXPORT void *zmq_atomic_counter_new (void);
void zmq_atomic_counter_set (void *counter_, int value_);
int zmq_atomic_counter_inc (void *counter_);
int zmq_atomic_counter_dec (void *counter_);
int zmq_atomic_counter_value (void *counter_);
void zmq_atomic_counter_destroy (void **counter_p_);

/******************************************************************************/
/*  Scheduling timers                                                         */
/******************************************************************************/
typedef void (zmq_timer_fn) (int timer_id, void *arg);

ZMQ_EXPORT void *zmq_timers_new (void);
ZMQ_EXPORT int zmq_timers_destroy (void **timers_p);
ZMQ_EXPORT int
zmq_timers_add (void *timers, size_t interval, zmq_timer_fn handler, void *arg);
ZMQ_EXPORT int zmq_timers_cancel (void *timers, int timer_id);
ZMQ_EXPORT int
zmq_timers_set_interval (void *timers, int timer_id, size_t interval);
ZMQ_EXPORT int zmq_timers_reset (void *timers, int timer_id);
ZMQ_EXPORT long zmq_timers_timeout (void *timers);
ZMQ_EXPORT int zmq_timers_execute (void *timers);

ZMQ_EXPORT void *zmq_stopwatch_start (void);
ZMQ_EXPORT unsigned long zmq_stopwatch_intermediate (void *watch_);
ZMQ_EXPORT unsigned long zmq_stopwatch_stop (void *watch_);
ZMQ_EXPORT void zmq_sleep (int seconds_);

typedef void (zmq_thread_fn) (void *);
ZMQ_EXPORT void *zmq_threadstart (zmq_thread_fn *func_, void *arg_);
ZMQ_EXPORT void zmq_threadclose (void *thread_);

#undef ZMQ_EXPORT

#ifdef __cplusplus
}
#endif

#endif
