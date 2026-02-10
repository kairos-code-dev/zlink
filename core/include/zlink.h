/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_H_INCLUDED__
#define __ZLINK_H_INCLUDED__

/*  Version macros for compile-time API version detection                     */
#define ZLINK_VERSION_MAJOR 0
#define ZLINK_VERSION_MINOR 9
#define ZLINK_VERSION_PATCH 0

#define ZLINK_MAKE_VERSION(major, minor, patch)                                  \
    ((major) *10000 + (minor) *100 + (patch))
#define ZLINK_VERSION                                                            \
    ZLINK_MAKE_VERSION (ZLINK_VERSION_MAJOR, ZLINK_VERSION_MINOR, ZLINK_VERSION_PATCH)

#ifdef __cplusplus
extern "C" {
#endif

#if !defined _WIN32_WCE
#include <errno.h>
#endif
#include <stddef.h>
#include <stdio.h>

/*  Handle DSO symbol visibility                                             */
#if defined ZLINK_NO_EXPORT
#define ZLINK_EXPORT
#else
#if defined _WIN32
#if defined ZLINK_STATIC
#define ZLINK_EXPORT
#elif defined DLL_EXPORT
#define ZLINK_EXPORT __declspec(dllexport)
#else
#define ZLINK_EXPORT __declspec(dllimport)
#endif
#else
#if defined __SUNPRO_C || defined __SUNPRO_CC
#define ZLINK_EXPORT __global
#elif (defined __GNUC__ && __GNUC__ >= 4) || defined __INTEL_COMPILER
#define ZLINK_EXPORT __attribute__ ((visibility ("default")))
#else
#define ZLINK_EXPORT
#endif
#endif
#endif

/*  Define integer types needed for event interface                          */
#define ZLINK_DEFINED_STDINT 1
#if defined ZLINK_HAVE_SOLARIS || defined ZLINK_HAVE_OPENVMS
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

#ifdef ZLINK_HAVE_AIX
#include <poll.h>
#endif

/******************************************************************************/
/*  0MQ errors.                                                               */
/******************************************************************************/
#define ZLINK_HAUSNUMERO 156384712

#ifndef ENOTSUP
#define ENOTSUP (ZLINK_HAUSNUMERO + 1)
#endif
#ifndef EPROTONOSUPPORT
#define EPROTONOSUPPORT (ZLINK_HAUSNUMERO + 2)
#endif
#ifndef ENOBUFS
#define ENOBUFS (ZLINK_HAUSNUMERO + 3)
#endif
#ifndef ENETDOWN
#define ENETDOWN (ZLINK_HAUSNUMERO + 4)
#endif
#ifndef EADDRINUSE
#define EADDRINUSE (ZLINK_HAUSNUMERO + 5)
#endif
#ifndef EADDRNOTAVAIL
#define EADDRNOTAVAIL (ZLINK_HAUSNUMERO + 6)
#endif
#ifndef ECONNREFUSED
#define ECONNREFUSED (ZLINK_HAUSNUMERO + 7)
#endif
#ifndef EINPROGRESS
#define EINPROGRESS (ZLINK_HAUSNUMERO + 8)
#endif
#ifndef ENOTSOCK
#define ENOTSOCK (ZLINK_HAUSNUMERO + 9)
#endif
#ifndef EMSGSIZE
#define EMSGSIZE (ZLINK_HAUSNUMERO + 10)
#endif
#ifndef EAFNOSUPPORT
#define EAFNOSUPPORT (ZLINK_HAUSNUMERO + 11)
#endif
#ifndef ENETUNREACH
#define ENETUNREACH (ZLINK_HAUSNUMERO + 12)
#endif
#ifndef ECONNABORTED
#define ECONNABORTED (ZLINK_HAUSNUMERO + 13)
#endif
#ifndef ECONNRESET
#define ECONNRESET (ZLINK_HAUSNUMERO + 14)
#endif
#ifndef ENOTCONN
#define ENOTCONN (ZLINK_HAUSNUMERO + 15)
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT (ZLINK_HAUSNUMERO + 16)
#endif
#ifndef EHOSTUNREACH
#define EHOSTUNREACH (ZLINK_HAUSNUMERO + 17)
#endif
#ifndef ENETRESET
#define ENETRESET (ZLINK_HAUSNUMERO + 18)
#endif

#define EFSM (ZLINK_HAUSNUMERO + 51)
#define ENOCOMPATPROTO (ZLINK_HAUSNUMERO + 52)
#define ETERM (ZLINK_HAUSNUMERO + 53)
#define EMTHREAD (ZLINK_HAUSNUMERO + 54)

/**
 * @brief Return the errno for the current thread.
 * @return errno value (POSIX errno or ZLINK_HAUSNUMERO-based extended code).
 */
ZLINK_EXPORT int zlink_errno (void);

/**
 * @brief Return a human-readable string for the given error number.
 * @param errnum_  Error number (e.g. return value of zlink_errno()).
 * @return Static string pointer. Must not be modified or freed.
 */
ZLINK_EXPORT const char *zlink_strerror (int errnum_);

/**
 * @brief Return the runtime library version.
 * @param[out] major_  Major version.
 * @param[out] minor_  Minor version.
 * @param[out] patch_  Patch version.
 */
ZLINK_EXPORT void zlink_version (int *major_, int *minor_, int *patch_);

/******************************************************************************/
/*  0MQ infrastructure (a.k.a. context) initialisation & termination.         */
/******************************************************************************/
#define ZLINK_IO_THREADS 1
#define ZLINK_MAX_SOCKETS 2
#define ZLINK_SOCKET_LIMIT 3
#define ZLINK_THREAD_PRIORITY 3
#define ZLINK_THREAD_SCHED_POLICY 4
#define ZLINK_MAX_MSGSZ 5
#define ZLINK_MSG_T_SIZE 6
#define ZLINK_THREAD_AFFINITY_CPU_ADD 7
#define ZLINK_THREAD_AFFINITY_CPU_REMOVE 8
#define ZLINK_THREAD_NAME_PREFIX 9

#define ZLINK_IO_THREADS_DFLT 2
#define ZLINK_MAX_SOCKETS_DFLT 1023
#define ZLINK_THREAD_PRIORITY_DFLT -1
#define ZLINK_THREAD_SCHED_POLICY_DFLT -1

/**
 * @brief Create a new zlink context.
 *
 * A context manages I/O threads and serves as the foundation for
 * creating sockets. Must be released with zlink_ctx_term().
 *
 * @return Context handle, or NULL on failure (errno is set).
 */
ZLINK_EXPORT void *zlink_ctx_new (void);

/**
 * @brief Terminate the context and release all resources.
 *
 * May block until all sockets are closed.
 *
 * @param context_  Context handle.
 * @return 0 on success, -1 on failure (errno is set).
 */
ZLINK_EXPORT int zlink_ctx_term (void *context_);

/**
 * @brief Shut down the context immediately.
 *
 * Interrupts any blocking calls with ETERM.
 * zlink_ctx_term() must still be called for final cleanup.
 *
 * @param context_  Context handle.
 * @return 0 on success, -1 on failure (errno is set).
 */
ZLINK_EXPORT int zlink_ctx_shutdown (void *context_);

/**
 * @brief Set a context option.
 * @param context_  Context handle.
 * @param option_   Option name (ZLINK_IO_THREADS, ZLINK_MAX_SOCKETS, etc.).
 * @param optval_   Option value.
 * @return 0 on success, -1 on failure (errno is set).
 */
ZLINK_EXPORT int zlink_ctx_set (void *context_, int option_, int optval_);

/**
 * @brief Get a context option.
 * @param context_  Context handle.
 * @param option_   Option name.
 * @return Option value, or -1 on failure (errno is set).
 */
ZLINK_EXPORT int zlink_ctx_get (void *context_, int option_);

/******************************************************************************/
/*  0MQ message definition.                                                   */
/******************************************************************************/
typedef struct zlink_msg_t
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
} zlink_msg_t;

typedef struct zlink_routing_id_t
{
    uint8_t size;
    uint8_t data[255];
} zlink_routing_id_t;

typedef void (zlink_free_fn) (void *data_, void *hint_);

/** @brief Initialize an empty message. Must be closed with zlink_msg_close(). */
ZLINK_EXPORT int zlink_msg_init (zlink_msg_t *msg_);

/** @brief Initialize a message of the given size. */
ZLINK_EXPORT int zlink_msg_init_size (zlink_msg_t *msg_, size_t size_);

/**
 * @brief Initialize a message from an external data buffer (zero-copy).
 * @param msg_   Message object.
 * @param data_  External data buffer.
 * @param size_  Data size in bytes.
 * @param ffn_   Callback invoked when the message is released. May be NULL.
 * @param hint_  User data passed to @p ffn_.
 */
ZLINK_EXPORT int zlink_msg_init_data (
  zlink_msg_t *msg_, void *data_, size_t size_, zlink_free_fn *ffn_, void *hint_);

/** @brief Send a message on a socket. On success, ownership is transferred. */
ZLINK_EXPORT int zlink_msg_send (zlink_msg_t *msg_, void *s_, int flags_);

/** @brief Receive a message from a socket. */
ZLINK_EXPORT int zlink_msg_recv (zlink_msg_t *msg_, void *s_, int flags_);

/** @brief Release message resources. Must be called after init. */
ZLINK_EXPORT int zlink_msg_close (zlink_msg_t *msg_);

/** @brief Move message content from src_ to dest_. src_ becomes empty. */
ZLINK_EXPORT int zlink_msg_move (zlink_msg_t *dest_, zlink_msg_t *src_);

/** @brief Copy a message from src_ to dest_. */
ZLINK_EXPORT int zlink_msg_copy (zlink_msg_t *dest_, zlink_msg_t *src_);

/** @brief Return a pointer to the message data buffer. */
ZLINK_EXPORT void *zlink_msg_data (zlink_msg_t *msg_);

/** @brief Return the message data size in bytes. */
ZLINK_EXPORT size_t zlink_msg_size (const zlink_msg_t *msg_);

/** @brief Return 1 if more parts follow in a multipart message. */
ZLINK_EXPORT int zlink_msg_more (const zlink_msg_t *msg_);

/** @brief Get an integer message property. */
ZLINK_EXPORT int zlink_msg_get (const zlink_msg_t *msg_, int property_);

/** @brief Set an integer message property. */
ZLINK_EXPORT int zlink_msg_set (zlink_msg_t *msg_, int property_, int optval_);

/** @brief Get a string message property (e.g. metadata). */
ZLINK_EXPORT const char *zlink_msg_gets (const zlink_msg_t *msg_,
                                     const char *property_);

/******************************************************************************/
/*  0MQ socket definition.                                                    */
/******************************************************************************/
#define ZLINK_PAIR 0
#define ZLINK_PUB 1
#define ZLINK_SUB 2
#define ZLINK_DEALER 5
#define ZLINK_ROUTER 6
#define ZLINK_XPUB 9
#define ZLINK_XSUB 10
#define ZLINK_STREAM 11

#define ZLINK_AFFINITY 4
#define ZLINK_ROUTING_ID 5
#define ZLINK_SUBSCRIBE 6
#define ZLINK_UNSUBSCRIBE 7
#define ZLINK_RATE 8
#define ZLINK_RECOVERY_IVL 9
#define ZLINK_SNDBUF 11
#define ZLINK_RCVBUF 12
#define ZLINK_RCVMORE 13
#define ZLINK_FD 14
#define ZLINK_EVENTS 15
#define ZLINK_TYPE 16
#define ZLINK_LINGER 17
#define ZLINK_RECONNECT_IVL 18
#define ZLINK_BACKLOG 19
#define ZLINK_RECONNECT_IVL_MAX 21
#define ZLINK_MAXMSGSIZE 22
#define ZLINK_SNDHWM 23
#define ZLINK_RCVHWM 24
#define ZLINK_MULTICAST_HOPS 25
#define ZLINK_RCVTIMEO 27
#define ZLINK_SNDTIMEO 28
#define ZLINK_LAST_ENDPOINT 32
#define ZLINK_ROUTER_MANDATORY 33
#define ZLINK_TCP_KEEPALIVE 34
#define ZLINK_TCP_KEEPALIVE_CNT 35
#define ZLINK_TCP_KEEPALIVE_IDLE 36
#define ZLINK_TCP_KEEPALIVE_INTVL 37
#define ZLINK_IMMEDIATE 39
#define ZLINK_XPUB_VERBOSE 40
#define ZLINK_IPV6 42
#define ZLINK_PROBE_ROUTER 51
#define ZLINK_CONFLATE 54
#define ZLINK_ROUTER_HANDOVER 56
#define ZLINK_TOS 57
#define ZLINK_CONNECT_ROUTING_ID 61
#define ZLINK_HANDSHAKE_IVL 66
#define ZLINK_XPUB_NODROP 69
#define ZLINK_BLOCKY 70
#define ZLINK_XPUB_MANUAL 71
#define ZLINK_XPUB_WELCOME_MSG 72
#define ZLINK_INVERT_MATCHING 74
#define ZLINK_HEARTBEAT_IVL 75
#define ZLINK_HEARTBEAT_TTL 76
#define ZLINK_HEARTBEAT_TIMEOUT 77
#define ZLINK_XPUB_VERBOSER 78
#define ZLINK_CONNECT_TIMEOUT 79
#define ZLINK_TCP_MAXRT 80
#define ZLINK_MULTICAST_MAXTPDU 84
#define ZLINK_USE_FD 89
#define ZLINK_REQUEST_TIMEOUT 90
#define ZLINK_REQUEST_CORRELATE 91
#define ZLINK_BINDTODEVICE 92
#define ZLINK_XPUB_MANUAL_LAST_VALUE 98
#define ZLINK_ONLY_FIRST_SUBSCRIBE 108
#define ZLINK_TOPICS_COUNT 116
#define ZLINK_ZMP_METADATA 117

//  TLS protocol options
#define ZLINK_TLS_CERT 95
#define ZLINK_TLS_KEY 96
#define ZLINK_TLS_CA 97
#define ZLINK_TLS_VERIFY 98
#define ZLINK_TLS_REQUIRE_CLIENT_CERT 99
#define ZLINK_TLS_HOSTNAME 100
#define ZLINK_TLS_TRUST_SYSTEM 101
#define ZLINK_TLS_PASSWORD 102

#define ZLINK_MORE 1
#define ZLINK_SHARED 3

#define ZLINK_DONTWAIT 1
#define ZLINK_SNDMORE 2

#define ZLINK_NULL 0
#define ZLINK_PLAIN 1

/******************************************************************************/
/*  0MQ socket events and monitoring                                          */
/******************************************************************************/
#define ZLINK_EVENT_CONNECTED 0x0001
#define ZLINK_EVENT_CONNECT_DELAYED 0x0002
#define ZLINK_EVENT_CONNECT_RETRIED 0x0004
#define ZLINK_EVENT_LISTENING 0x0008
#define ZLINK_EVENT_BIND_FAILED 0x0010
#define ZLINK_EVENT_ACCEPTED 0x0020
#define ZLINK_EVENT_ACCEPT_FAILED 0x0040
#define ZLINK_EVENT_CLOSED 0x0080
#define ZLINK_EVENT_CLOSE_FAILED 0x0100
#define ZLINK_EVENT_DISCONNECTED 0x0200
#define ZLINK_EVENT_MONITOR_STOPPED 0x0400
#define ZLINK_EVENT_ALL 0xFFFF
#define ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL 0x0800
#define ZLINK_EVENT_CONNECTION_READY 0x1000
#define ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL 0x2000
#define ZLINK_EVENT_HANDSHAKE_FAILED_AUTH 0x4000

#define ZLINK_DISCONNECT_UNKNOWN 0
#define ZLINK_DISCONNECT_LOCAL 1
#define ZLINK_DISCONNECT_REMOTE 2
#define ZLINK_DISCONNECT_HANDSHAKE_FAILED 3
#define ZLINK_DISCONNECT_TRANSPORT_ERROR 4
#define ZLINK_DISCONNECT_CTX_TERM 5

#define ZLINK_PROTOCOL_ERROR_ZMP_UNSPECIFIED 0x10000000
#define ZLINK_PROTOCOL_ERROR_ZMP_UNEXPECTED_COMMAND 0x10000001
#define ZLINK_PROTOCOL_ERROR_ZMP_INVALID_SEQUENCE 0x10000002
#define ZLINK_PROTOCOL_ERROR_ZMP_KEY_EXCHANGE 0x10000003
#define ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_UNSPECIFIED 0x10000011
#define ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_MESSAGE 0x10000012
#define ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_HELLO 0x10000013
#define ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_INITIATE 0x10000014
#define ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_ERROR 0x10000015
#define ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_READY 0x10000016
#define ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_WELCOME 0x10000017
#define ZLINK_PROTOCOL_ERROR_ZMP_INVALID_METADATA 0x10000018
#define ZLINK_PROTOCOL_ERROR_ZMP_CRYPTOGRAPHIC 0x11000001
#define ZLINK_PROTOCOL_ERROR_ZMP_MECHANISM_MISMATCH 0x11000002
#define ZLINK_PROTOCOL_ERROR_WS_UNSPECIFIED 0x30000000

/**
 * @brief Create a socket.
 * @param context_  Context handle (return value of zlink_ctx_new()).
 * @param type_     Socket type (ZLINK_PAIR, ZLINK_PUB, ZLINK_SUB, etc.).
 * @return Socket handle, or NULL on failure (errno is set).
 */
ZLINK_EXPORT void *zlink_socket (void *, int type_);

/** @brief Close a socket and release its resources. */
ZLINK_EXPORT int zlink_close (void *s_);

/**
 * @brief Set a socket option.
 * @param s_         Socket handle.
 * @param option_    Option name (ZLINK_SNDHWM, ZLINK_RCVHWM, ZLINK_LINGER, etc.).
 * @param optval_    Option value buffer.
 * @param optvallen_ Option value size in bytes.
 */
ZLINK_EXPORT int
zlink_setsockopt (void *s_, int option_, const void *optval_, size_t optvallen_);

/** @brief Get a socket option. */
ZLINK_EXPORT int
zlink_getsockopt (void *s_, int option_, void *optval_, size_t *optvallen_);

/**
 * @brief Bind a socket to an address.
 * @param addr_  Endpoint (e.g. @c tcp://host:5555, @c inproc://name).
 */
ZLINK_EXPORT int zlink_bind (void *s_, const char *addr_);

/** @brief Connect a socket to a remote address. */
ZLINK_EXPORT int zlink_connect (void *s_, const char *addr_);

/** @brief Unbind a socket from an address. */
ZLINK_EXPORT int zlink_unbind (void *s_, const char *addr_);

/** @brief Disconnect a socket from a remote address. */
ZLINK_EXPORT int zlink_disconnect (void *s_, const char *addr_);

/**
 * @brief Send buffer data on a socket.
 * @param flags_  0, ZLINK_DONTWAIT, ZLINK_SNDMORE, or a combination.
 * @return Number of bytes sent, or -1 on failure (errno is set).
 */
ZLINK_EXPORT int zlink_send (void *s_, const void *buf_, size_t len_, int flags_);

/** @brief Send constant data on a socket (zero-copy hint). */
ZLINK_EXPORT int
zlink_send_const (void *s_, const void *buf_, size_t len_, int flags_);

/**
 * @brief Receive data from a socket.
 * @param buf_   Receive buffer.
 * @param len_   Maximum buffer size.
 * @param flags_ 0 or ZLINK_DONTWAIT.
 * @return Number of bytes received, or -1 on failure (errno is set).
 */
ZLINK_EXPORT int zlink_recv (void *s_, void *buf_, size_t len_, int flags_);

/**
 * @brief Start a socket monitor via an inproc address (legacy).
 * @param addr_    Monitor inproc endpoint.
 * @param events_  Event bitmask (combination of ZLINK_EVENT_* flags).
 */
ZLINK_EXPORT int zlink_socket_monitor (void *s_, const char *addr_, int events_);

/**
 * @brief Open and return a socket monitor handle directly.
 * @param events_  Event bitmask.
 * @return Monitor handle, or NULL on failure.
 */
ZLINK_EXPORT void *zlink_socket_monitor_open (void *s_, int events_);

typedef struct {
    uint64_t event;
    uint64_t value;
    zlink_routing_id_t routing_id;
    char local_addr[256];
    char remote_addr[256];
} zlink_monitor_event_t;

/**
 * @brief Receive an event from a monitor handle.
 * @param monitor_socket_  Monitor handle (from zlink_socket_monitor_open()).
 * @param[out] event_      Event structure.
 * @param flags_           0 or ZLINK_DONTWAIT.
 */
ZLINK_EXPORT int zlink_monitor_recv (void *monitor_socket_,
                                 zlink_monitor_event_t *event_,
                                 int flags_);

typedef struct {
    zlink_routing_id_t routing_id;
    char remote_addr[256];
    uint64_t connected_time;
    uint64_t msgs_sent;
    uint64_t msgs_received;
} zlink_peer_info_t;

/** @brief Get peer info by routing_id. */
ZLINK_EXPORT int zlink_socket_peer_info (void *socket_,
                                     const zlink_routing_id_t *routing_id_,
                                     zlink_peer_info_t *info_);

/** @brief Get a peer's routing_id by index. */
ZLINK_EXPORT int zlink_socket_peer_routing_id (void *socket_,
                                           int index_,
                                           zlink_routing_id_t *out_);

/** @brief Return the number of connected peers. */
ZLINK_EXPORT int zlink_socket_peer_count (void *socket_);

/** @brief Get info for all connected peers as an array. */
ZLINK_EXPORT int zlink_socket_peers (void *socket_,
                                 zlink_peer_info_t *peers_,
                                 size_t *count_);

/** @brief Close all parts in a multipart message array. */
ZLINK_EXPORT void zlink_msgv_close (zlink_msg_t *parts, size_t part_count);

/******************************************************************************/
/*  Service Discovery API                                                     */
/******************************************************************************/

typedef struct {
    char service_name[256];
    char endpoint[256];
    zlink_routing_id_t routing_id;
    uint32_t weight;
    uint64_t registered_at;
} zlink_receiver_info_t;

/* Registry ----------------------------------------------------------------- */

/**
 * @brief Create a service registry.
 *
 * A registry accepts service registration/deregistration/heartbeat
 * requests and periodically broadcasts the service list.
 *
 * @param ctx  Context handle.
 * @return Registry handle, or NULL on failure.
 */
ZLINK_EXPORT void *zlink_registry_new (void *ctx);

/**
 * @brief Set the registry PUB and ROUTER endpoints.
 * @param pub_endpoint     PUB endpoint for broadcasting.
 * @param router_endpoint  ROUTER endpoint for receiving registrations.
 */
ZLINK_EXPORT int zlink_registry_set_endpoints (void *registry,
                                           const char *pub_endpoint,
                                           const char *router_endpoint);

/** @brief Set the registry unique ID (used for cluster configuration). */
ZLINK_EXPORT int zlink_registry_set_id (void *registry, uint32_t registry_id);

/** @brief Add a peer registry PUB endpoint (for cluster synchronization). */
ZLINK_EXPORT int zlink_registry_add_peer (void *registry,
                                      const char *peer_pub_endpoint);

/**
 * @brief Set heartbeat interval and timeout.
 * @param interval_ms  Heartbeat send interval in milliseconds.
 * @param timeout_ms   Expiry time when no heartbeat is received, in milliseconds.
 */
ZLINK_EXPORT int zlink_registry_set_heartbeat (void *registry,
                                           uint32_t interval_ms,
                                           uint32_t timeout_ms);

/** @brief Set the service list broadcast interval in milliseconds. */
ZLINK_EXPORT int zlink_registry_set_broadcast_interval (void *registry,
                                                    uint32_t interval_ms);

/* Registry socket roles */
#define ZLINK_REGISTRY_SOCKET_PUB 1
#define ZLINK_REGISTRY_SOCKET_ROUTER 2
#define ZLINK_REGISTRY_SOCKET_PEER_SUB 3

/** @brief Set a socket option on an internal registry socket. */
ZLINK_EXPORT int zlink_registry_setsockopt (void *registry,
                                        int socket_role,
                                        int option,
                                        const void *optval,
                                        size_t optvallen);

/** @brief Start the registry. Spawns an internal thread. */
ZLINK_EXPORT int zlink_registry_start (void *registry);

/** @brief Destroy the registry and release all resources. */
ZLINK_EXPORT int zlink_registry_destroy (void **registry_p);

/* Discovery ---------------------------------------------------------------- */

/** @name Service registration types */
/** @{ */
#define ZLINK_SERVICE_TYPE_GATEWAY 1  /**< For Gateway/Receiver */
#define ZLINK_SERVICE_TYPE_SPOT 2     /**< For SPOT Node */
/** @} */

/**
 * @brief Create a typed Discovery instance.
 *
 * The type is fixed at creation time and cannot be changed.
 * All subscribe/get/count queries operate within the given service_type scope.
 *
 * @param ctx           Context handle.
 * @param service_type  ZLINK_SERVICE_TYPE_GATEWAY or ZLINK_SERVICE_TYPE_SPOT.
 * @return Discovery handle, or NULL on failure.
 */
ZLINK_EXPORT void *zlink_discovery_new_typed (void *ctx, uint16_t service_type);

/** @brief Connect to a registry PUB endpoint. */
ZLINK_EXPORT int zlink_discovery_connect_registry (
  void *discovery, const char *registry_pub_endpoint);

/** @brief Subscribe to a service name. Receives matching entries from broadcasts. */
ZLINK_EXPORT int zlink_discovery_subscribe (void *discovery,
                                        const char *service_name);

/** @brief Unsubscribe from a service name. */
ZLINK_EXPORT int zlink_discovery_unsubscribe (void *discovery,
                                          const char *service_name);

/**
 * @brief Get the list of receivers for a service.
 * @param[out] providers  Array to receive results.
 * @param[in,out] count   On input, array capacity; on output, actual count.
 */
ZLINK_EXPORT int zlink_discovery_get_receivers (void *discovery,
                                            const char *service_name,
                                            zlink_receiver_info_t *providers,
                                            size_t *count);

/** @brief Return the number of registered receivers for a service. */
ZLINK_EXPORT int zlink_discovery_receiver_count (void *discovery,
                                             const char *service_name);

/** @brief Check if a service is available (at least one receiver exists). */
ZLINK_EXPORT int zlink_discovery_service_available (void *discovery,
                                                const char *service_name);

/* Discovery socket roles */
#define ZLINK_DISCOVERY_SOCKET_SUB 1

/** @brief Set a socket option on an internal discovery socket. */
ZLINK_EXPORT int zlink_discovery_setsockopt (void *discovery,
                                         int socket_role,
                                         int option,
                                         const void *optval,
                                         size_t optvallen);

/** @brief Destroy the discovery instance and release all resources. */
ZLINK_EXPORT int zlink_discovery_destroy (void **discovery_p);

/* Gateway ------------------------------------------------------------------ */

/**
 * @brief Create a Gateway.
 *
 * Resolves service locations automatically via Discovery and provides
 * load-balanced request/reply communication.
 *
 * @param ctx         Context handle.
 * @param discovery   Discovery handle (ZLINK_SERVICE_TYPE_GATEWAY type).
 * @param routing_id  Unique identifier for this Gateway.
 * @return Gateway handle, or NULL on failure.
 */
ZLINK_EXPORT void *zlink_gateway_new (void *ctx,
                                      void *discovery,
                                      const char *routing_id);

/**
 * @brief Send a message to a service (load-balanced).
 * @param service_name  Target service name.
 * @param parts         Multipart message array.
 * @param part_count    Number of parts.
 * @param flags         Send flags (0 or ZLINK_DONTWAIT).
 */
ZLINK_EXPORT int zlink_gateway_send (void *gateway,
                                     const char *service_name,
                                     zlink_msg_t *parts,
                                     size_t part_count,
                                     int flags);

/**
 * @brief Receive a message.
 * @param[out] parts             Received multipart message (caller must free).
 * @param[out] part_count        Number of parts.
 * @param flags                  0 or ZLINK_DONTWAIT.
 * @param[out] service_name_out  Originating service name (256-byte buffer).
 */
ZLINK_EXPORT int zlink_gateway_recv (void *gateway,
                                     zlink_msg_t **parts,
                                     size_t *part_count,
                                     int flags,
                                     char *service_name_out);

/** @brief Send a message directly to a specific Receiver by routing_id. */
ZLINK_EXPORT int zlink_gateway_send_rid (void *gateway,
                                         const char *service_name,
                                         const zlink_routing_id_t *routing_id,
                                         zlink_msg_t *parts,
                                         size_t part_count,
                                     int flags);

/** @name Load-balancing strategies */
/** @{ */
#define ZLINK_GATEWAY_LB_ROUND_ROBIN 0  /**< Round-robin (default) */
#define ZLINK_GATEWAY_LB_WEIGHTED 1     /**< Weighted */
/** @} */

/** @brief Set the load-balancing strategy for a service. */
ZLINK_EXPORT int zlink_gateway_set_lb_strategy (void *gateway,
                                                const char *service_name,
                                                int strategy);

/** @brief Set a Gateway socket option. */
ZLINK_EXPORT int zlink_gateway_setsockopt (void *gateway,
                                           int option,
                                           const void *optval,
                                           size_t optvallen);

/* Gateway socket role */
#define ZLINK_GATEWAY_SOCKET_ROUTER 1

/** @brief Configure TLS client settings for the Gateway. */
ZLINK_EXPORT int zlink_gateway_set_tls_client (void *gateway,
                                           const char *ca_cert,
                                           const char *hostname,
                                           int trust_system);

/** @brief Return the internal ROUTER socket handle (for diagnostics). */
ZLINK_EXPORT void *zlink_gateway_router (void *gateway);

/** @brief Return the number of receivers connected for a service. */
ZLINK_EXPORT int zlink_gateway_connection_count (void *gateway,
                                             const char *service_name);

/** @brief Destroy the Gateway and release all resources. */
ZLINK_EXPORT int zlink_gateway_destroy (void **gateway_p);

/* Receiver ----------------------------------------------------------------- */

/**
 * @brief Create a Receiver.
 *
 * Server-side role that receives requests from Gateways and sends replies.
 * Registers services with the Registry for automatic discovery by Gateways.
 *
 * @param ctx         Context handle.
 * @param routing_id  Unique identifier for this Receiver.
 */
ZLINK_EXPORT void *zlink_receiver_new (void *ctx, const char *routing_id);

/** @brief Bind the ROUTER socket to an endpoint. */
ZLINK_EXPORT int zlink_receiver_bind (void *provider,
                                  const char *bind_endpoint);

/** @brief Connect to a Registry ROUTER endpoint (for registration/heartbeat). */
ZLINK_EXPORT int zlink_receiver_connect_registry (void *provider,
                                              const char *registry_endpoint);

/**
 * @brief Register a service with the Registry.
 * @param service_name        Service name.
 * @param advertise_endpoint  Endpoint that Gateways will connect to.
 * @param weight              Load-balancing weight.
 */
ZLINK_EXPORT int zlink_receiver_register (void *provider,
                                      const char *service_name,
                                      const char *advertise_endpoint,
                                      uint32_t weight);

/** @brief Update the weight of a registered service. */
ZLINK_EXPORT int zlink_receiver_update_weight (void *provider,
                                           const char *service_name,
                                           uint32_t weight);

/** @brief Unregister a service. */
ZLINK_EXPORT int zlink_receiver_unregister (void *provider,
                                        const char *service_name);

/**
 * @brief Query the registration result (async registration confirmation).
 * @param[out] status             Registration status code.
 * @param[out] resolved_endpoint  Endpoint resolved by the Registry (256-byte buffer).
 * @param[out] error_message      Error message (256-byte buffer).
 */
ZLINK_EXPORT int zlink_receiver_register_result (void *provider,
                                             const char *service_name,
                                             int *status,
                                             char *resolved_endpoint,
                                             char *error_message);

/** @brief Set TLS server certificate. */
ZLINK_EXPORT int zlink_receiver_set_tls_server (void *provider,
                                            const char *cert,
                                            const char *key);

/* Provider socket roles */
#define ZLINK_RECEIVER_SOCKET_ROUTER 1
#define ZLINK_RECEIVER_SOCKET_DEALER 2

/** @brief Set a socket option on an internal Receiver socket. */
ZLINK_EXPORT int zlink_receiver_setsockopt (void *provider,
                                        int socket_role,
                                        int option,
                                        const void *optval,
                                        size_t optvallen);

/** @brief Return the internal ROUTER socket handle (for diagnostics). */
ZLINK_EXPORT void *zlink_receiver_router (void *provider);

/** @brief Destroy the Receiver and release all resources. */
ZLINK_EXPORT int zlink_receiver_destroy (void **provider_p);

/******************************************************************************/
/*  SPOT PUB/SUB API                                                          */
/******************************************************************************/

/* SPOT Node --------------------------------------------------------------- */

/** @brief Create a SPOT node. Manages PUB/SUB/DEALER sockets for topic messaging. */
ZLINK_EXPORT void *zlink_spot_node_new (void *ctx);

/** @brief Destroy a SPOT node and release all resources. */
ZLINK_EXPORT int zlink_spot_node_destroy (void **node_p);

/** @brief Bind the SPOT node to an endpoint. */
ZLINK_EXPORT int zlink_spot_node_bind (void *node, const char *endpoint);

/** @brief Connect to a Registry endpoint for service registration. */
ZLINK_EXPORT int zlink_spot_node_connect_registry (void *node,
                                               const char *registry_endpoint);

/** @brief Connect to a peer node's PUB endpoint (mesh topology). */
ZLINK_EXPORT int zlink_spot_node_connect_peer_pub (void *node,
                                               const char *peer_pub_endpoint);

/** @brief Disconnect from a peer node's PUB endpoint. */
ZLINK_EXPORT int zlink_spot_node_disconnect_peer_pub (
  void *node, const char *peer_pub_endpoint);

/**
 * @brief Register this node as a SPOT service with the Registry.
 * @param service_name        Service (group) name.
 * @param advertise_endpoint  Endpoint peers will connect to.
 */
ZLINK_EXPORT int zlink_spot_node_register (void *node,
                                       const char *service_name,
                                       const char *advertise_endpoint);

/** @brief Unregister this node from the Registry. */
ZLINK_EXPORT int zlink_spot_node_unregister (void *node,
                                         const char *service_name);

/**
 * @brief Attach a Discovery instance for automatic peer connection.
 * @param discovery     Discovery handle (must be ZLINK_SERVICE_TYPE_SPOT).
 * @param service_name  Service name to watch for peer discovery.
 */
ZLINK_EXPORT int zlink_spot_node_set_discovery (void *node,
                                            void *discovery,
                                            const char *service_name);

/** @brief Set TLS server certificate for the node. */
ZLINK_EXPORT int zlink_spot_node_set_tls_server (void *node,
                                             const char *cert,
                                             const char *key);

/** @brief Set TLS client settings for the node. */
ZLINK_EXPORT int zlink_spot_node_set_tls_client (void *node,
                                             const char *ca_cert,
                                             const char *hostname,
                                             int trust_system);

/** @brief Return the internal PUB socket handle (for diagnostics). */
ZLINK_EXPORT void *zlink_spot_node_pub_socket (void *node);

/** @brief Return the internal SUB socket handle (for diagnostics). */
ZLINK_EXPORT void *zlink_spot_node_sub_socket (void *node);

/* Spot Node socket roles */
#define ZLINK_SPOT_NODE_SOCKET_PUB 1
#define ZLINK_SPOT_NODE_SOCKET_SUB 2
#define ZLINK_SPOT_NODE_SOCKET_DEALER 3

/** @brief Set a socket option on an internal SPOT node socket. */
ZLINK_EXPORT int zlink_spot_node_setsockopt (void *node,
                                             int socket_role,
                                             int option,
                                             const void *optval,
                                             size_t optvallen);

/* SPOT Pub (default thread-safe) ------------------------------------------ */

/** @brief Create a thread-safe SPOT publisher attached to the given node. */
ZLINK_EXPORT void *zlink_spot_pub_new (void *node);

/** @brief Destroy a SPOT publisher. */
ZLINK_EXPORT int zlink_spot_pub_destroy (void **pub_p);

/**
 * @brief Publish a multipart message under a topic.
 *
 * Thread-safe: concurrent calls are serialized internally via mutex.
 *
 * @param topic_id    Topic identifier string.
 * @param parts       Multipart message array.
 * @param part_count  Number of parts.
 * @param flags       Send flags (typically 0).
 */
ZLINK_EXPORT int zlink_spot_pub_publish (void *pub,
                                         const char *topic_id,
                                         zlink_msg_t *parts,
                                         size_t part_count,
                                         int flags);

/** @brief Set a socket option on the SPOT publisher. */
ZLINK_EXPORT int zlink_spot_pub_setsockopt (void *pub,
                                            int option,
                                            const void *optval,
                                            size_t optvallen);

/* SPOT Sub ---------------------------------------------------------------- */

/**
 * @brief Callback function type for SPOT subscriber handler dispatch.
 *
 * When set via zlink_spot_sub_set_handler(), incoming messages are
 * automatically delivered through this callback instead of zlink_spot_sub_recv().
 *
 * @param topic       Topic string.
 * @param topic_len   Topic string length.
 * @param parts       Multipart message array (read-only).
 * @param part_count  Number of parts.
 * @param userdata    User-provided context pointer.
 */
typedef void (*zlink_spot_sub_handler_fn) (const char *topic,
                                           size_t topic_len,
                                           const zlink_msg_t *parts,
                                           size_t part_count,
                                           void *userdata);

/** @brief Create a SPOT subscriber attached to the given node. */
ZLINK_EXPORT void *zlink_spot_sub_new (void *node);

/** @brief Destroy a SPOT subscriber. */
ZLINK_EXPORT int zlink_spot_sub_destroy (void **sub_p);

/** @brief Subscribe to an exact topic. */
ZLINK_EXPORT int zlink_spot_sub_subscribe (void *sub, const char *topic_id);

/** @brief Subscribe to a topic pattern (prefix match). */
ZLINK_EXPORT int zlink_spot_sub_subscribe_pattern (void *sub, const char *pattern);

/** @brief Unsubscribe from a topic or pattern. */
ZLINK_EXPORT int zlink_spot_sub_unsubscribe (void *sub,
                                             const char *topic_id_or_pattern);

/**
 * @brief Set a callback handler for automatic message dispatch.
 *
 * When a handler is set, messages are delivered via the callback
 * and zlink_spot_sub_recv() must not be used concurrently.
 * Pass NULL to clear the handler and revert to recv()-based consumption.
 *
 * @param handler   Callback function, or NULL to clear.
 * @param userdata  User-provided context pointer passed to the callback.
 */
ZLINK_EXPORT int zlink_spot_sub_set_handler (void *sub,
                                             zlink_spot_sub_handler_fn handler,
                                             void *userdata);

/**
 * @brief Receive a message from the subscriber (polling mode).
 * @param[out] parts         Received multipart message (caller must free).
 * @param[out] part_count    Number of parts.
 * @param flags              0 or ZLINK_DONTWAIT.
 * @param[out] topic_id_out  Topic string buffer.
 * @param[in,out] topic_id_len  On input, buffer size; on output, actual length.
 */
ZLINK_EXPORT int zlink_spot_sub_recv (void *sub,
                                      zlink_msg_t **parts,
                                      size_t *part_count,
                                      int flags,
                                      char *topic_id_out,
                                      size_t *topic_id_len);

/** @brief Set a socket option on the SPOT subscriber. */
ZLINK_EXPORT int zlink_spot_sub_setsockopt (void *sub,
                                            int option,
                                            const void *optval,
                                            size_t optvallen);

/** @brief Return the raw SUB socket handle (for advanced use/polling). */
ZLINK_EXPORT void *zlink_spot_sub_socket (void *sub);

#if defined _WIN32
#if defined _WIN64
typedef unsigned __int64 zlink_fd_t;
#else
typedef unsigned int zlink_fd_t;
#endif
#else
typedef int zlink_fd_t;
#endif

#define ZLINK_POLLIN 1
#define ZLINK_POLLOUT 2
#define ZLINK_POLLERR 4
#define ZLINK_POLLPRI 8

typedef struct zlink_pollitem_t
{
    void *socket;
    zlink_fd_t fd;
    short events;
    short revents;
} zlink_pollitem_t;

#define ZLINK_POLLITEMS_DFLT 16

/**
 * @brief Poll for events on a set of sockets and/or file descriptors.
 * @param items_    Array of poll items.
 * @param nitems_   Number of items.
 * @param timeout_  Timeout in milliseconds (-1 = infinite, 0 = immediate).
 * @return Number of items with events, or -1 on failure.
 */
ZLINK_EXPORT int zlink_poll (zlink_pollitem_t *items_, int nitems_, long timeout_);

/** @brief Start a built-in proxy between frontend and backend sockets. */
ZLINK_EXPORT int zlink_proxy (void *frontend_, void *backend_, void *capture_);

/** @brief Start a steerable proxy with an additional control socket. */
ZLINK_EXPORT int zlink_proxy_steerable (void *frontend_,
                                    void *backend_,
                                    void *capture_,
                                    void *control_);

/** @brief Check if the library supports a given capability (e.g. "ipc", "tls"). */
ZLINK_EXPORT int zlink_has (const char *capability_);

/******************************************************************************/
/*  Atomic utility methods                                                    */
/******************************************************************************/

/** @brief Create a new atomic counter, initialized to zero. */
ZLINK_EXPORT void *zlink_atomic_counter_new (void);
void zlink_atomic_counter_set (void *counter_, int value_);
int zlink_atomic_counter_inc (void *counter_);
int zlink_atomic_counter_dec (void *counter_);
int zlink_atomic_counter_value (void *counter_);
void zlink_atomic_counter_destroy (void **counter_p_);

/******************************************************************************/
/*  Scheduling timers                                                         */
/******************************************************************************/
typedef void (zlink_timer_fn) (int timer_id, void *arg);

/** @brief Create a new timer set. */
ZLINK_EXPORT void *zlink_timers_new (void);

/** @brief Destroy a timer set and release all resources. */
ZLINK_EXPORT int zlink_timers_destroy (void **timers_p);

/** @brief Add a timer with the given interval (ms) and callback. Returns timer ID. */
ZLINK_EXPORT int
zlink_timers_add (void *timers, size_t interval, zlink_timer_fn handler, void *arg);

/** @brief Cancel a timer by its ID. */
ZLINK_EXPORT int zlink_timers_cancel (void *timers, int timer_id);

/** @brief Change the interval of an existing timer. */
ZLINK_EXPORT int
zlink_timers_set_interval (void *timers, int timer_id, size_t interval);

/** @brief Reset a timer's countdown to its full interval. */
ZLINK_EXPORT int zlink_timers_reset (void *timers, int timer_id);

/** @brief Return milliseconds until the next timer fires, or -1 if none. */
ZLINK_EXPORT long zlink_timers_timeout (void *timers);

/** @brief Execute all expired timers. */
ZLINK_EXPORT int zlink_timers_execute (void *timers);

/** @brief Start a high-resolution stopwatch. Returns an opaque handle. */
ZLINK_EXPORT void *zlink_stopwatch_start (void);

/** @brief Return elapsed microseconds without stopping the stopwatch. */
ZLINK_EXPORT unsigned long zlink_stopwatch_intermediate (void *watch_);

/** @brief Stop the stopwatch and return total elapsed microseconds. */
ZLINK_EXPORT unsigned long zlink_stopwatch_stop (void *watch_);

/** @brief Sleep for the given number of seconds. */
ZLINK_EXPORT void zlink_sleep (int seconds_);

typedef void (zlink_thread_fn) (void *);

/** @brief Start a new thread running the given function. Returns a thread handle. */
ZLINK_EXPORT void *zlink_threadstart (zlink_thread_fn *func_, void *arg_);

/** @brief Wait for a thread to finish and release its handle. */
ZLINK_EXPORT void zlink_threadclose (void *thread_);

#undef ZLINK_EXPORT

#ifdef __cplusplus
}
#endif

#endif
