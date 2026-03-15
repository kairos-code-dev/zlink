/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_H_INCLUDED__
#define __ZLINK_H_INCLUDED__

/*  Version macros for compile-time API version detection                     */
#define ZLINK_VERSION_MAJOR 4
#define ZLINK_VERSION_MINOR 0
#define ZLINK_VERSION_PATCH 2

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
#define ZLINK_IO_THREADS_DFLT 2
#define ZLINK_MAX_SOCKETS_DFLT 1023
#define ZLINK_THREAD_PRIORITY_DFLT -1
#define ZLINK_THREAD_SCHED_POLICY_DFLT -1

typedef enum zlink_ctx_option_t
{
    ZLINK_IO_THREADS = 1,
    ZLINK_MAX_SOCKETS = 2,
    ZLINK_SOCKET_LIMIT = 3,
    ZLINK_THREAD_PRIORITY = 3,
    ZLINK_THREAD_SCHED_POLICY = 4,
    ZLINK_MAX_MSGSZ = 5,
    ZLINK_MSG_T_SIZE = 6,
    ZLINK_THREAD_AFFINITY_CPU_ADD = 7,
    ZLINK_THREAD_AFFINITY_CPU_REMOVE = 8,
    ZLINK_THREAD_NAME_PREFIX = 9
} zlink_ctx_option_t;

typedef uint32_t zlink_send_flags_t;

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
ZLINK_EXPORT int zlink_ctx_set (void *context_,
                                zlink_ctx_option_t option_,
                                int optval_);

/**
 * @brief Get a context option.
 * @param context_  Context handle.
 * @param option_   Option name.
 * @return Option value, or -1 on failure (errno is set).
 */
ZLINK_EXPORT int zlink_ctx_get (void *context_, zlink_ctx_option_t option_);

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
ZLINK_EXPORT int zlink_msg_send (zlink_msg_t *msg_,
                                 void *s_,
                                 zlink_send_flags_t flags_);

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
typedef enum zlink_socket_type_t
{
    ZLINK_SOCKET_PAIR = 0x1001,
    ZLINK_SOCKET_PUB = 0x1002,
    ZLINK_SOCKET_SUB = 0x1003,
    ZLINK_SOCKET_DEALER = 0x1004,
    ZLINK_SOCKET_ROUTER = 0x1005,
    ZLINK_SOCKET_XPUB = 0x1006,
    ZLINK_SOCKET_XSUB = 0x1007,
    ZLINK_SOCKET_STREAM = 0x1008
} zlink_socket_type_t;

typedef enum zlink_socket_option_t
{
    ZLINK_SOCKOPT_AFFINITY = 0x1101,
    ZLINK_SOCKOPT_ROUTING_ID = 0x1102,
    ZLINK_SOCKOPT_SUBSCRIBE = 0x1103,
    ZLINK_SOCKOPT_UNSUBSCRIBE = 0x1104,
    ZLINK_SOCKOPT_RATE = 0x1105,
    ZLINK_SOCKOPT_RECOVERY_IVL = 0x1106,
    ZLINK_SOCKOPT_SNDBUF = 0x1107,
    ZLINK_SOCKOPT_RCVBUF = 0x1108,
    ZLINK_SOCKOPT_RCVMORE = 0x1109,
    ZLINK_SOCKOPT_FD = 0x110A,
    ZLINK_SOCKOPT_EVENTS = 0x110B,
    ZLINK_SOCKOPT_TYPE = 0x110C,
    ZLINK_SOCKOPT_LINGER = 0x110D,
    ZLINK_SOCKOPT_RECONNECT_IVL = 0x110E,
    ZLINK_SOCKOPT_BACKLOG = 0x110F,
    ZLINK_SOCKOPT_RECONNECT_IVL_MAX = 0x1110,
    ZLINK_SOCKOPT_MAXMSGSIZE = 0x1111,
    ZLINK_SOCKOPT_SNDHWM = 0x1112,
    ZLINK_SOCKOPT_RCVHWM = 0x1113,
    ZLINK_SOCKOPT_MULTICAST_HOPS = 0x1114,
    ZLINK_SOCKOPT_RCVTIMEO = 0x1115,
    ZLINK_SOCKOPT_SNDTIMEO = 0x1116,
    ZLINK_SOCKOPT_LAST_ENDPOINT = 0x1117,
    ZLINK_SOCKOPT_ROUTER_MANDATORY = 0x1118,
    ZLINK_SOCKOPT_TCP_KEEPALIVE = 0x1119,
    ZLINK_SOCKOPT_TCP_KEEPALIVE_CNT = 0x111A,
    ZLINK_SOCKOPT_TCP_KEEPALIVE_IDLE = 0x111B,
    ZLINK_SOCKOPT_TCP_KEEPALIVE_INTVL = 0x111C,
    ZLINK_SOCKOPT_IMMEDIATE = 0x111D,
    ZLINK_SOCKOPT_XPUB_VERBOSE = 0x111E,
    ZLINK_SOCKOPT_IPV6 = 0x111F,
    ZLINK_SOCKOPT_PROBE_ROUTER = 0x1120,
    ZLINK_SOCKOPT_CONFLATE = 0x1121,
    ZLINK_SOCKOPT_ROUTER_HANDOVER = 0x1122,
    ZLINK_SOCKOPT_TOS = 0x1123,
    ZLINK_SOCKOPT_CONNECT_ROUTING_ID = 0x1124,
    ZLINK_SOCKOPT_HANDSHAKE_IVL = 0x1125,
    ZLINK_SOCKOPT_XPUB_NODROP = 0x1126,
    ZLINK_SOCKOPT_BLOCKY = 0x1127,
    ZLINK_SOCKOPT_XPUB_MANUAL = 0x1128,
    ZLINK_SOCKOPT_XPUB_WELCOME_MSG = 0x1129,
    ZLINK_SOCKOPT_STREAM_NOTIFY = 0x112A,
    ZLINK_SOCKOPT_INVERT_MATCHING = 0x112B,
    ZLINK_SOCKOPT_HEARTBEAT_IVL = 0x112C,
    ZLINK_SOCKOPT_HEARTBEAT_TTL = 0x112D,
    ZLINK_SOCKOPT_HEARTBEAT_TIMEOUT = 0x112E,
    ZLINK_SOCKOPT_XPUB_VERBOSER = 0x112F,
    ZLINK_SOCKOPT_CONNECT_TIMEOUT = 0x1130,
    ZLINK_SOCKOPT_TCP_MAXRT = 0x1131,
    ZLINK_SOCKOPT_MULTICAST_MAXTPDU = 0x1132,
    ZLINK_SOCKOPT_BINDTODEVICE = 0x1134,
    ZLINK_SOCKOPT_TLS_CERT = 0x1135,
    ZLINK_SOCKOPT_TLS_KEY = 0x1136,
    ZLINK_SOCKOPT_TLS_CA = 0x1137,
    ZLINK_SOCKOPT_TLS_VERIFY = 0x1138,
    ZLINK_SOCKOPT_XPUB_MANUAL_LAST_VALUE = 0x1139,
    ZLINK_SOCKOPT_TLS_REQUIRE_CLIENT_CERT = 0x113A,
    ZLINK_SOCKOPT_TLS_HOSTNAME = 0x113B,
    ZLINK_SOCKOPT_TLS_TRUST_SYSTEM = 0x113C,
    ZLINK_SOCKOPT_TLS_PASSWORD = 0x113D,
    ZLINK_SOCKOPT_ONLY_FIRST_SUBSCRIBE = 0x113E,
    ZLINK_SOCKOPT_TOPICS_COUNT = 0x113F,
    ZLINK_SOCKOPT_ZMP_METADATA = 0x1140,
    ZLINK_SOCKOPT_TCP_NODELAY = 0x1141
} zlink_socket_option_t;

#ifdef ZLINK_INTERNAL_BUILD
#define ZLINK_PAIR ((zlink_socket_type_t) 0)
#define ZLINK_PUB ((zlink_socket_type_t) 1)
#define ZLINK_SUB ((zlink_socket_type_t) 2)
#define ZLINK_DEALER ((zlink_socket_type_t) 5)
#define ZLINK_ROUTER ((zlink_socket_type_t) 6)
#define ZLINK_XPUB ((zlink_socket_type_t) 9)
#define ZLINK_XSUB ((zlink_socket_type_t) 10)
#define ZLINK_STREAM ((zlink_socket_type_t) 11)

#define ZLINK_AFFINITY ((zlink_socket_option_t) 4)
#define ZLINK_ROUTING_ID ((zlink_socket_option_t) 5)
#define ZLINK_SUBSCRIBE ((zlink_socket_option_t) 6)
#define ZLINK_UNSUBSCRIBE ((zlink_socket_option_t) 7)
#define ZLINK_RATE ((zlink_socket_option_t) 8)
#define ZLINK_RECOVERY_IVL ((zlink_socket_option_t) 9)
#define ZLINK_SNDBUF ((zlink_socket_option_t) 11)
#define ZLINK_RCVBUF ((zlink_socket_option_t) 12)
#define ZLINK_RCVMORE ((zlink_socket_option_t) 13)
#define ZLINK_LINGER ((zlink_socket_option_t) 17)
#define ZLINK_RECONNECT_IVL ((zlink_socket_option_t) 18)
#define ZLINK_BACKLOG ((zlink_socket_option_t) 19)
#define ZLINK_RECONNECT_IVL_MAX ((zlink_socket_option_t) 21)
#define ZLINK_MAXMSGSIZE ((zlink_socket_option_t) 22)
#define ZLINK_SNDHWM ((zlink_socket_option_t) 23)
#define ZLINK_RCVHWM ((zlink_socket_option_t) 24)
#define ZLINK_MULTICAST_HOPS ((zlink_socket_option_t) 25)
#define ZLINK_RCVTIMEO ((zlink_socket_option_t) 27)
#define ZLINK_SNDTIMEO ((zlink_socket_option_t) 28)
#define ZLINK_ROUTER_MANDATORY ((zlink_socket_option_t) 33)
#define ZLINK_TCP_KEEPALIVE ((zlink_socket_option_t) 34)
#define ZLINK_TCP_KEEPALIVE_CNT ((zlink_socket_option_t) 35)
#define ZLINK_TCP_KEEPALIVE_IDLE ((zlink_socket_option_t) 36)
#define ZLINK_TCP_KEEPALIVE_INTVL ((zlink_socket_option_t) 37)
#define ZLINK_IMMEDIATE ((zlink_socket_option_t) 39)
#define ZLINK_XPUB_VERBOSE ((zlink_socket_option_t) 40)
#define ZLINK_IPV6 ((zlink_socket_option_t) 42)
#define ZLINK_PROBE_ROUTER ((zlink_socket_option_t) 51)
#define ZLINK_CONFLATE ((zlink_socket_option_t) 54)
#define ZLINK_ROUTER_HANDOVER ((zlink_socket_option_t) 56)
#define ZLINK_TOS ((zlink_socket_option_t) 57)
#define ZLINK_CONNECT_ROUTING_ID ((zlink_socket_option_t) 61)
#define ZLINK_HANDSHAKE_IVL ((zlink_socket_option_t) 66)
#define ZLINK_XPUB_NODROP ((zlink_socket_option_t) 69)
#define ZLINK_BLOCKY ((zlink_socket_option_t) 70)
#define ZLINK_XPUB_MANUAL ((zlink_socket_option_t) 71)
#define ZLINK_XPUB_WELCOME_MSG ((zlink_socket_option_t) 72)
#define ZLINK_STREAM_NOTIFY ((zlink_socket_option_t) 73)
#define ZLINK_INVERT_MATCHING ((zlink_socket_option_t) 74)
#define ZLINK_HEARTBEAT_IVL ((zlink_socket_option_t) 75)
#define ZLINK_HEARTBEAT_TTL ((zlink_socket_option_t) 76)
#define ZLINK_HEARTBEAT_TIMEOUT ((zlink_socket_option_t) 77)
#define ZLINK_XPUB_VERBOSER ((zlink_socket_option_t) 78)
#define ZLINK_CONNECT_TIMEOUT ((zlink_socket_option_t) 79)
#define ZLINK_TCP_MAXRT ((zlink_socket_option_t) 80)
#define ZLINK_MULTICAST_MAXTPDU ((zlink_socket_option_t) 84)
#define ZLINK_BINDTODEVICE ((zlink_socket_option_t) 92)
#define ZLINK_TLS_CERT ((zlink_socket_option_t) 95)
#define ZLINK_TLS_KEY ((zlink_socket_option_t) 96)
#define ZLINK_TLS_CA ((zlink_socket_option_t) 97)
#define ZLINK_TLS_VERIFY ((zlink_socket_option_t) 98)
#define ZLINK_XPUB_MANUAL_LAST_VALUE ((zlink_socket_option_t) 98)
#define ZLINK_TLS_REQUIRE_CLIENT_CERT ((zlink_socket_option_t) 99)
#define ZLINK_TLS_HOSTNAME ((zlink_socket_option_t) 100)
#define ZLINK_TLS_TRUST_SYSTEM ((zlink_socket_option_t) 101)
#define ZLINK_TLS_PASSWORD ((zlink_socket_option_t) 102)
#define ZLINK_ONLY_FIRST_SUBSCRIBE ((zlink_socket_option_t) 108)
#define ZLINK_TOPICS_COUNT ((zlink_socket_option_t) 116)
#define ZLINK_ZMP_METADATA ((zlink_socket_option_t) 117)
#define ZLINK_TCP_NODELAY ((zlink_socket_option_t) 118)
#else
#define ZLINK_PAIR ZLINK_SOCKET_PAIR
#define ZLINK_PUB ZLINK_SOCKET_PUB
#define ZLINK_SUB ZLINK_SOCKET_SUB
#define ZLINK_DEALER ZLINK_SOCKET_DEALER
#define ZLINK_ROUTER ZLINK_SOCKET_ROUTER
#define ZLINK_XPUB ZLINK_SOCKET_XPUB
#define ZLINK_XSUB ZLINK_SOCKET_XSUB
#define ZLINK_STREAM ZLINK_SOCKET_STREAM

#define ZLINK_AFFINITY ZLINK_SOCKOPT_AFFINITY
#define ZLINK_ROUTING_ID ZLINK_SOCKOPT_ROUTING_ID
#define ZLINK_SUBSCRIBE ZLINK_SOCKOPT_SUBSCRIBE
#define ZLINK_UNSUBSCRIBE ZLINK_SOCKOPT_UNSUBSCRIBE
#define ZLINK_RATE ZLINK_SOCKOPT_RATE
#define ZLINK_RECOVERY_IVL ZLINK_SOCKOPT_RECOVERY_IVL
#define ZLINK_SNDBUF ZLINK_SOCKOPT_SNDBUF
#define ZLINK_RCVBUF ZLINK_SOCKOPT_RCVBUF
#define ZLINK_RCVMORE ZLINK_SOCKOPT_RCVMORE
#define ZLINK_LINGER ZLINK_SOCKOPT_LINGER
#define ZLINK_RECONNECT_IVL ZLINK_SOCKOPT_RECONNECT_IVL
#define ZLINK_BACKLOG ZLINK_SOCKOPT_BACKLOG
#define ZLINK_RECONNECT_IVL_MAX ZLINK_SOCKOPT_RECONNECT_IVL_MAX
#define ZLINK_MAXMSGSIZE ZLINK_SOCKOPT_MAXMSGSIZE
#define ZLINK_SNDHWM ZLINK_SOCKOPT_SNDHWM
#define ZLINK_RCVHWM ZLINK_SOCKOPT_RCVHWM
#define ZLINK_MULTICAST_HOPS ZLINK_SOCKOPT_MULTICAST_HOPS
#define ZLINK_RCVTIMEO ZLINK_SOCKOPT_RCVTIMEO
#define ZLINK_SNDTIMEO ZLINK_SOCKOPT_SNDTIMEO
#define ZLINK_ROUTER_MANDATORY ZLINK_SOCKOPT_ROUTER_MANDATORY
#define ZLINK_TCP_KEEPALIVE ZLINK_SOCKOPT_TCP_KEEPALIVE
#define ZLINK_TCP_KEEPALIVE_CNT ZLINK_SOCKOPT_TCP_KEEPALIVE_CNT
#define ZLINK_TCP_KEEPALIVE_IDLE ZLINK_SOCKOPT_TCP_KEEPALIVE_IDLE
#define ZLINK_TCP_KEEPALIVE_INTVL ZLINK_SOCKOPT_TCP_KEEPALIVE_INTVL
#define ZLINK_IMMEDIATE ZLINK_SOCKOPT_IMMEDIATE
#define ZLINK_XPUB_VERBOSE ZLINK_SOCKOPT_XPUB_VERBOSE
#define ZLINK_IPV6 ZLINK_SOCKOPT_IPV6
#define ZLINK_PROBE_ROUTER ZLINK_SOCKOPT_PROBE_ROUTER
#define ZLINK_CONFLATE ZLINK_SOCKOPT_CONFLATE
#define ZLINK_ROUTER_HANDOVER ZLINK_SOCKOPT_ROUTER_HANDOVER
#define ZLINK_TOS ZLINK_SOCKOPT_TOS
#define ZLINK_CONNECT_ROUTING_ID ZLINK_SOCKOPT_CONNECT_ROUTING_ID
#define ZLINK_HANDSHAKE_IVL ZLINK_SOCKOPT_HANDSHAKE_IVL
#define ZLINK_XPUB_NODROP ZLINK_SOCKOPT_XPUB_NODROP
#define ZLINK_BLOCKY ZLINK_SOCKOPT_BLOCKY
#define ZLINK_XPUB_MANUAL ZLINK_SOCKOPT_XPUB_MANUAL
#define ZLINK_XPUB_WELCOME_MSG ZLINK_SOCKOPT_XPUB_WELCOME_MSG
#define ZLINK_STREAM_NOTIFY ZLINK_SOCKOPT_STREAM_NOTIFY
#define ZLINK_INVERT_MATCHING ZLINK_SOCKOPT_INVERT_MATCHING
#define ZLINK_HEARTBEAT_IVL ZLINK_SOCKOPT_HEARTBEAT_IVL
#define ZLINK_HEARTBEAT_TTL ZLINK_SOCKOPT_HEARTBEAT_TTL
#define ZLINK_HEARTBEAT_TIMEOUT ZLINK_SOCKOPT_HEARTBEAT_TIMEOUT
#define ZLINK_XPUB_VERBOSER ZLINK_SOCKOPT_XPUB_VERBOSER
#define ZLINK_CONNECT_TIMEOUT ZLINK_SOCKOPT_CONNECT_TIMEOUT
#define ZLINK_TCP_MAXRT ZLINK_SOCKOPT_TCP_MAXRT
#define ZLINK_MULTICAST_MAXTPDU ZLINK_SOCKOPT_MULTICAST_MAXTPDU
#define ZLINK_BINDTODEVICE ZLINK_SOCKOPT_BINDTODEVICE
#define ZLINK_TLS_CERT ZLINK_SOCKOPT_TLS_CERT
#define ZLINK_TLS_KEY ZLINK_SOCKOPT_TLS_KEY
#define ZLINK_TLS_CA ZLINK_SOCKOPT_TLS_CA
#define ZLINK_TLS_VERIFY ZLINK_SOCKOPT_TLS_VERIFY
#define ZLINK_XPUB_MANUAL_LAST_VALUE ZLINK_SOCKOPT_XPUB_MANUAL_LAST_VALUE
#define ZLINK_TLS_REQUIRE_CLIENT_CERT ZLINK_SOCKOPT_TLS_REQUIRE_CLIENT_CERT
#define ZLINK_TLS_HOSTNAME ZLINK_SOCKOPT_TLS_HOSTNAME
#define ZLINK_TLS_TRUST_SYSTEM ZLINK_SOCKOPT_TLS_TRUST_SYSTEM
#define ZLINK_TLS_PASSWORD ZLINK_SOCKOPT_TLS_PASSWORD
#define ZLINK_ONLY_FIRST_SUBSCRIBE ZLINK_SOCKOPT_ONLY_FIRST_SUBSCRIBE
#define ZLINK_TOPICS_COUNT ZLINK_SOCKOPT_TOPICS_COUNT
#define ZLINK_ZMP_METADATA ZLINK_SOCKOPT_ZMP_METADATA
#define ZLINK_TCP_NODELAY ZLINK_SOCKOPT_TCP_NODELAY
#endif

#define ZLINK_FD ZLINK_SOCKOPT_FD
#define ZLINK_EVENTS ZLINK_SOCKOPT_EVENTS
#define ZLINK_TYPE ZLINK_SOCKOPT_TYPE
#define ZLINK_LAST_ENDPOINT ZLINK_SOCKOPT_LAST_ENDPOINT

typedef enum zlink_msg_property_t
{
    ZLINK_MORE = 1,
    ZLINK_SHARED = 3
} zlink_msg_property_t;

#define ZLINK_DONTWAIT ((zlink_send_flags_t) 0x0001u)
#define ZLINK_SNDMORE ((zlink_send_flags_t) 0x0002u)
#define ZLINK_SEND_FLAG_DONTWAIT ZLINK_DONTWAIT
#define ZLINK_SEND_FLAG_SNDMORE ZLINK_SNDMORE

#define ZLINK_NULL 0
#define ZLINK_PLAIN 1

/******************************************************************************/
/*  0MQ socket events and monitoring                                          */
/******************************************************************************/
typedef uint32_t zlink_socket_monitor_event_mask_t;

#define ZLINK_SOCKET_MONITOR_EVENT_CONNECTED                                 \
    ((zlink_socket_monitor_event_mask_t) 0x0001u)
#define ZLINK_SOCKET_MONITOR_EVENT_CONNECT_DELAYED                           \
    ((zlink_socket_monitor_event_mask_t) 0x0002u)
#define ZLINK_SOCKET_MONITOR_EVENT_CONNECT_RETRIED                           \
    ((zlink_socket_monitor_event_mask_t) 0x0004u)
#define ZLINK_SOCKET_MONITOR_EVENT_LISTENING                                 \
    ((zlink_socket_monitor_event_mask_t) 0x0008u)
#define ZLINK_SOCKET_MONITOR_EVENT_BIND_FAILED                               \
    ((zlink_socket_monitor_event_mask_t) 0x0010u)
#define ZLINK_SOCKET_MONITOR_EVENT_ACCEPTED                                  \
    ((zlink_socket_monitor_event_mask_t) 0x0020u)
#define ZLINK_SOCKET_MONITOR_EVENT_ACCEPT_FAILED                             \
    ((zlink_socket_monitor_event_mask_t) 0x0040u)
#define ZLINK_SOCKET_MONITOR_EVENT_CLOSED                                    \
    ((zlink_socket_monitor_event_mask_t) 0x0080u)
#define ZLINK_SOCKET_MONITOR_EVENT_CLOSE_FAILED                              \
    ((zlink_socket_monitor_event_mask_t) 0x0100u)
#define ZLINK_SOCKET_MONITOR_EVENT_DISCONNECTED                              \
    ((zlink_socket_monitor_event_mask_t) 0x0200u)
#define ZLINK_SOCKET_MONITOR_EVENT_MONITOR_STOPPED                           \
    ((zlink_socket_monitor_event_mask_t) 0x0400u)
#define ZLINK_SOCKET_MONITOR_EVENT_ALL                                       \
    ((zlink_socket_monitor_event_mask_t) 0xFFFFu)
#define ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_NO_DETAIL                \
    ((zlink_socket_monitor_event_mask_t) 0x0800u)
#define ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY                          \
    ((zlink_socket_monitor_event_mask_t) 0x1000u)
#define ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_PROTOCOL                 \
    ((zlink_socket_monitor_event_mask_t) 0x2000u)
#define ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_AUTH                     \
    ((zlink_socket_monitor_event_mask_t) 0x4000u)

#define ZLINK_EVENT_CONNECTED ZLINK_SOCKET_MONITOR_EVENT_CONNECTED
#define ZLINK_EVENT_CONNECT_DELAYED ZLINK_SOCKET_MONITOR_EVENT_CONNECT_DELAYED
#define ZLINK_EVENT_CONNECT_RETRIED ZLINK_SOCKET_MONITOR_EVENT_CONNECT_RETRIED
#define ZLINK_EVENT_LISTENING ZLINK_SOCKET_MONITOR_EVENT_LISTENING
#define ZLINK_EVENT_BIND_FAILED ZLINK_SOCKET_MONITOR_EVENT_BIND_FAILED
#define ZLINK_EVENT_ACCEPTED ZLINK_SOCKET_MONITOR_EVENT_ACCEPTED
#define ZLINK_EVENT_ACCEPT_FAILED ZLINK_SOCKET_MONITOR_EVENT_ACCEPT_FAILED
#define ZLINK_EVENT_CLOSED ZLINK_SOCKET_MONITOR_EVENT_CLOSED
#define ZLINK_EVENT_CLOSE_FAILED ZLINK_SOCKET_MONITOR_EVENT_CLOSE_FAILED
#define ZLINK_EVENT_DISCONNECTED ZLINK_SOCKET_MONITOR_EVENT_DISCONNECTED
#define ZLINK_EVENT_MONITOR_STOPPED ZLINK_SOCKET_MONITOR_EVENT_MONITOR_STOPPED
#define ZLINK_EVENT_ALL ZLINK_SOCKET_MONITOR_EVENT_ALL
#define ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL                               \
    ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_NO_DETAIL
#define ZLINK_EVENT_CONNECTION_READY ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY
#define ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL                                \
    ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_PROTOCOL
#define ZLINK_EVENT_HANDSHAKE_FAILED_AUTH                                    \
    ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_AUTH

typedef enum zlink_disconnect_reason_t
{
    ZLINK_DISCONNECT_REASON_UNKNOWN = 0,
    ZLINK_DISCONNECT_REASON_HANDSHAKE_FAILED = 3,
    ZLINK_DISCONNECT_REASON_TRANSPORT_ERROR = 4,
    ZLINK_DISCONNECT_REASON_CTX_TERM = 5
} zlink_disconnect_reason_t;

#define ZLINK_DISCONNECT_UNKNOWN ZLINK_DISCONNECT_REASON_UNKNOWN
#define ZLINK_DISCONNECT_HANDSHAKE_FAILED ZLINK_DISCONNECT_REASON_HANDSHAKE_FAILED
#define ZLINK_DISCONNECT_TRANSPORT_ERROR ZLINK_DISCONNECT_REASON_TRANSPORT_ERROR
#define ZLINK_DISCONNECT_CTX_TERM ZLINK_DISCONNECT_REASON_CTX_TERM

typedef enum zlink_protocol_error_t
{
    ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_HELLO = 0x10000013
} zlink_protocol_error_t;

/**
 * @brief Callback type for direct multipart socket dispatch.
 *
 * Callback is invoked on the owning socket I/O thread.
 * Ownership of all message parts is transferred to the callback.
 * Each part must be closed or otherwise consumed exactly once before return.
 *
 * @param source_rid_ Sender routing id for the received message.
 * @param parts_ Received multipart payload frames.
 * @param part_count_ Number of entries in @p parts_.
 */
typedef void (*zlink_socket_msg_handler_fn) (
  const zlink_routing_id_t *source_rid_,
  zlink_msg_t *parts_,
  size_t part_count_);

typedef void (*zlink_spot_handler_fn) (const zlink_routing_id_t *source_rid_,
                                       const char *topic_,
                                       size_t topic_len_,
                                       zlink_msg_t *parts_,
                                       size_t part_count_);

typedef void (*zlink_xpub_handler_fn) (int subscribed_,
                                       const uint8_t *topic_,
                                       size_t topic_len_);

typedef enum zlink_socket_handler_kind_t
{
    ZLINK_SOCKET_HANDLER_MSG = 0x1201,
    ZLINK_SOCKET_HANDLER_SPOT = 0x1202,
    ZLINK_SOCKET_HANDLER_XPUB = 0x1203
} zlink_socket_handler_kind_t;

typedef struct zlink_socket_handler_t
{
    zlink_socket_handler_kind_t kind;
    union
    {
        zlink_socket_msg_handler_fn msg;
        zlink_spot_handler_fn spot;
        zlink_xpub_handler_fn xpub;
    } fn;
} zlink_socket_handler_t;

typedef void (*zlink_send_ready_handler_fn) (void *subject_);

/**
 * @brief Create a socket.
 * @param context_  Context handle (return value of zlink_ctx_new()).
 * @param type_     Socket type.
 * @param handler_  Direct receive handler descriptor.
 * @return Socket handle, or NULL on failure (errno is set).
 */
ZLINK_EXPORT void *zlink_socket (void *,
                                 zlink_socket_type_t type_,
                                 const zlink_socket_handler_t *handler_);

/**
 * @brief Install or replace the send-ready callback for a send-capable handle.
 *
 * The handler is replace-only. Passing NULL is invalid. A successful replace is
 * visible from the next writable transition. If called reentrantly from the
 * same handle's send-ready callback, the call fails with errno=EDEADLK.
 */
ZLINK_EXPORT int zlink_socket_set_send_ready_handler (
  void *s_, zlink_send_ready_handler_fn handler_);

/**
 * @brief Close a socket and release its resources.
 *
 * Public handles use a tiered concurrency contract: send/publish hot paths
 * allow same-handle concurrent use, low-frequency control paths serialize for
 * correctness, and close/destroy uses a stricter lifecycle gate. If another
 * thread has an in-flight callback or admitted API on the same handle, close
 * fails with errno=EBUSY. Once close is accepted, new API entry fails with
 * errno=ESHUTDOWN. Self-close from a send-ready or monitor callback is
 * deferred until callback epilogue. For STREAM raw callbacks, close from
 * inside the raw callback is not supported and fails with errno=EBUSY.
 */
ZLINK_EXPORT int zlink_close (void *s_);

/**
 * @brief Set a socket option.
 * @param s_         Socket handle.
 * @param option_    Option name (ZLINK_SNDHWM, ZLINK_RCVHWM, ZLINK_LINGER, etc.).
 * @param optval_    Option value buffer.
 * @param optvallen_ Option value size in bytes.
 */
ZLINK_EXPORT int
zlink_setsockopt (void *s_,
                  zlink_socket_option_t option_,
                  const void *optval_,
                  size_t optvallen_);

/** @brief Get a socket option. */
ZLINK_EXPORT int
zlink_getsockopt (void *s_,
                  zlink_socket_option_t option_,
                  void *optval_,
                  size_t *optvallen_);

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
ZLINK_EXPORT int zlink_send (void *s_,
                             const void *buf_,
                             size_t len_,
                             zlink_send_flags_t flags_);

/**
 * @brief Callback type for raw STREAM chunk dispatch.
 *
 * Callback is invoked on the owning STREAM I/O thread.
 * Returning non-zero requests dispatcher shutdown.
 *
 * @param rid_ Routing id for the peer that produced this chunk.
 * @param msg_ Raw stream chunk. Ownership is transferred to the callback.
 *             The callback must release it exactly once
 *             (e.g. zlink_msg_close() or consume via zlink_stream_send_msg())
 *             before return, and must not retain this pointer after return.
 * @return 0 to continue dispatch, non-zero to stop.
 */
typedef int (*zlink_stream_on_raw_fn) (const zlink_routing_id_t *rid_,
                                       zlink_msg_t *msg_);

/**
 * @brief Attach raw STREAM callback dispatch.
 *
 * Valid only for ZLINK_STREAM sockets.
 * If a callback is already attached for the socket, returns -1 with
 * errno=EBUSY.
 * STREAM receive is callback-only; recv()/zlink_msg_recv() are not supported.
 * Attach/detach are safe to call from application threads and serialized with
 * STREAM send/close. Calling attach/detach from the raw callback is not
 * supported.
 *
 * @param s_ STREAM socket.
 * @param on_raw_ Callback for raw stream chunks.
 * @return 0 on success, -1 on failure (errno is set).
 */
ZLINK_EXPORT int zlink_stream_attach_raw (void *s_,
                                          zlink_stream_on_raw_fn on_raw_);

/**
 * @brief Detach STREAM callback dispatch from a socket.
 *
 * Safe to call from application threads and serialized with STREAM
 * send/close. Calling detach from the raw callback is not supported.
 *
 * @param s_ STREAM socket.
 * @return 0 on success, -1 on failure (errno is set).
 */
ZLINK_EXPORT int zlink_stream_detach (void *s_);

/**
 * @brief Send STREAM payload to a specific peer by routing id.
 *
 * Sends routing id as the first STREAM frame and payload as the second frame.
 * STREAM send APIs are safe to call from application threads and STREAM
 * dispatch callbacks; internally the socket serializes outgoing state.
 *
 * @param s_    STREAM socket.
 * @param rid_  Target peer routing id.
 * @param data_ Payload data buffer (may be NULL when size_ == 0).
 * @param size_ Payload size in bytes.
 * @param flags_ Send flags (0 or ZLINK_DONTWAIT).
 * @return Number of payload bytes accepted (size_), or -1 on failure.
 */
ZLINK_EXPORT int zlink_stream_send (void *s_,
                                    const zlink_routing_id_t *rid_,
                                    const void *data_,
                                    size_t size_,
                                    zlink_send_flags_t flags_);

/**
 * @brief Send STREAM payload message to a specific peer by routing id.
 *
 * This API consumes @p msg_ and reinitializes it before returning.
 * STREAM send APIs are safe to call from application threads and STREAM
 * dispatch callbacks; internally the socket serializes outgoing state.
 *
 * @param s_    STREAM socket.
 * @param rid_  Target peer routing id.
 * @param msg_  Payload message to send (consumed by this call).
 * @param flags_ Send flags (0 or ZLINK_DONTWAIT).
 * @return Number of payload bytes accepted, or -1 on failure.
 */
ZLINK_EXPORT int zlink_stream_send_msg (void *s_,
                                        const zlink_routing_id_t *rid_,
                                        zlink_msg_t *msg_,
                                        zlink_send_flags_t flags_);

typedef struct {
    uint64_t event;
    uint64_t value;
    zlink_routing_id_t routing_id;
    char local_addr[256];
    char remote_addr[256];
} zlink_monitor_event_t;

typedef void (*zlink_monitor_handler_fn) (
  const zlink_monitor_event_t *event_);

/**
 * @brief Ignore socket monitor events while keeping a valid handler symbol.
 *
 * Pass this when you want snapshot or direct polling on the returned monitor
 * handle without automatic callback dispatch.
 */
ZLINK_EXPORT void zlink_monitor_ignore_handler (
  const zlink_monitor_event_t *event_);

typedef enum zlink_monitor_source_kind_t
{
    ZLINK_MONITOR_SOURCE_SOCKET = 1,
    ZLINK_MONITOR_SOURCE_GATEWAY = 2,
    ZLINK_MONITOR_SOURCE_SPOT_PUB = 3,
    ZLINK_MONITOR_SOURCE_SPOT_SUB = 4
} zlink_monitor_source_kind_t;

typedef uint32_t zlink_monitor_state_mask_t;
typedef uint32_t zlink_monitor_snapshot_detail_mask_t;

#define ZLINK_MONITOR_STATE_READY ((zlink_monitor_state_mask_t) (1u << 0))
#define ZLINK_MONITOR_STATE_BOUND_READY                                   \
    ((zlink_monitor_state_mask_t) (1u << 1))
#define ZLINK_MONITOR_STATE_SEND_READY                                    \
    ((zlink_monitor_state_mask_t) (1u << 2))
#define ZLINK_MONITOR_STATE_CLOSED ((zlink_monitor_state_mask_t) (1u << 3))

#define ZLINK_MONITOR_SNAPSHOT_DETAIL_READY_PEER_COUNT                    \
    ((zlink_monitor_snapshot_detail_mask_t) (1u << 0))
#define ZLINK_MONITOR_SNAPSHOT_DETAIL_SND_PENDING_MSGS                    \
    ((zlink_monitor_snapshot_detail_mask_t) (1u << 1))
#define ZLINK_MONITOR_SNAPSHOT_DETAIL_RCV_PENDING_MSGS                    \
    ((zlink_monitor_snapshot_detail_mask_t) (1u << 2))

typedef struct zlink_monitor_snapshot_t
{
    zlink_monitor_source_kind_t source_kind;
    zlink_monitor_state_mask_t state_flags;
    zlink_monitor_snapshot_detail_mask_t detail_flags;
    uint32_t ready_peer_count;
    uint64_t snd_pending_msgs;
    uint64_t rcv_pending_msgs;
} zlink_monitor_snapshot_t;

/**
 * @brief Open and return a socket monitor handle directly.
 * @param events_  Event bitmask.
 * @return Monitor handle, or NULL on failure.
 */
ZLINK_EXPORT void *zlink_socket_monitor_open (void *s_,
                                              zlink_socket_monitor_event_mask_t events_,
                                              zlink_monitor_handler_fn handler_);

/** @brief Read the current snapshot for a socket or service monitor handle. */
ZLINK_EXPORT int zlink_monitor_snapshot (void *monitor_,
                                         zlink_monitor_snapshot_t *out_);

/** @brief Close all parts in a multipart message array. */
ZLINK_EXPORT void zlink_multipart_close (zlink_msg_t *parts, size_t part_count);

/******************************************************************************/
/*  Service Discovery API                                                     */
/******************************************************************************/

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
 * @brief Bind the registry PUB and ROUTER endpoints and start the registry.
 * @param pub_endpoint     PUB endpoint for broadcasting.
 * @param router_endpoint  ROUTER endpoint for receiving registrations.
 */
ZLINK_EXPORT int zlink_registry_bind (void *registry,
                                      const char *pub_endpoint,
                                      const char *router_endpoint);

/** @brief Set the registry unique ID (used for cluster configuration). */
ZLINK_EXPORT int zlink_registry_set_id (void *registry, uint32_t registry_id);

/** @brief Add a peer registry PUB endpoint (for cluster synchronization). */
ZLINK_EXPORT int zlink_registry_add_peer (void *registry,
                                      const char *peer_pub_endpoint);

/**
 * @brief Set heartbeat interval and timeout.
 * Defaults are 5000 ms for the heartbeat interval and 15000 ms for
 * the timeout.
 * @param interval_ms  Heartbeat send interval in milliseconds.
 * @param timeout_ms   Expiry time when no heartbeat is received, in
 *                     milliseconds.
 */
ZLINK_EXPORT int zlink_registry_set_heartbeat (void *registry,
                                           uint32_t interval_ms,
                                           uint32_t timeout_ms);

/**
 * @brief Set the service list broadcast interval in milliseconds.
 * Default is 30000 ms.
 */
ZLINK_EXPORT int zlink_registry_set_broadcast_interval (void *registry,
                                                    uint32_t interval_ms);

/* Registry socket roles */
typedef enum zlink_registry_socket_role_t
{
    ZLINK_REGISTRY_SOCKET_PUB = 1,
    ZLINK_REGISTRY_SOCKET_ROUTER = 2,
    ZLINK_REGISTRY_SOCKET_PEER_SUB = 3
} zlink_registry_socket_role_t;

/**
 * @brief Set a socket option on an internal registry socket.
 *
 * Internal registry socket options already applied:
 * - PUB: `ZLINK_XPUB_VERBOSE=1`
 * - ROUTER: `ZLINK_ROUTER_MANDATORY=1` by default
 * - PEER_SUB: `ZLINK_SUBSCRIBE=""` (subscribe to all topics)
 */
ZLINK_EXPORT int zlink_registry_setsockopt (void *registry,
                                            zlink_registry_socket_role_t socket_role,
                                            zlink_socket_option_t option,
                                            const void *optval,
                                            size_t optvallen);

/** @brief Destroy the registry and release all resources. */
ZLINK_EXPORT int zlink_registry_destroy (void **registry_p);

/* Discovery ---------------------------------------------------------------- */

/** @name Service registration types */
/** @{ */
typedef enum zlink_service_type_t
{
    ZLINK_SERVICE_TYPE_GATEWAY = 0x3001,
    ZLINK_SERVICE_TYPE_SPOT = 0x3002
} zlink_service_type_t;
/** @} */

typedef enum zlink_discovery_socket_role_t
{
    ZLINK_DISCOVERY_SOCKET_SUB = 1
} zlink_discovery_socket_role_t;

/**
 * @brief Create a Discovery instance with a fixed service family.
 *
 * The service type is fixed at creation time and cannot be changed.
 * All subscribe/get/count queries operate within the given service_type scope.
 *
 * @param ctx           Context handle.
 * @param service_type  Service family for this handle.
 * @return Discovery handle, or NULL on failure.
 */
ZLINK_EXPORT void *zlink_discovery_new (void *ctx,
                                        zlink_service_type_t service_type);

/**
 * @brief Connect Discovery to a Registry bootstrap/control endpoint.
 *
 * Discovery learns the Registry broadcast and topology-uplink endpoints from
 * this bootstrap connection and configures its internal sockets automatically.
 */
ZLINK_EXPORT int zlink_discovery_connect_registry (
  void *discovery, const char *registry_endpoint);

ZLINK_EXPORT int zlink_discovery_setsockopt (
  void *discovery,
  zlink_discovery_socket_role_t socket_role,
  zlink_socket_option_t option,
  const void *optval,
  size_t optvallen);

/**
 * @brief Override the representative routing id before first query/connect.
 */
ZLINK_EXPORT int zlink_discovery_set_routing_id (void *discovery,
                                                 const void *data,
                                                 size_t size);

/** @brief Return the representative routing id for this Discovery. */
ZLINK_EXPORT int zlink_discovery_routing_id (void *discovery,
                                             zlink_routing_id_t *out);

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
 * @param service_name Service identity fixed at handle creation.
 * @param routing_id  Unique identifier for this Gateway.
 * @param handler     Direct receive callback fixed at handle creation.
 * @return Gateway handle, or NULL on failure.
 */
ZLINK_EXPORT void *zlink_gateway_new (void *ctx,
                                      const char *service_name,
                                      const char *routing_id,
                                      zlink_socket_msg_handler_fn handler);

ZLINK_EXPORT int zlink_gateway_attach_discovery (void *gateway,
                                                 void *discovery);

ZLINK_EXPORT int zlink_gateway_set_send_ready_handler (
  void *gateway,
  zlink_send_ready_handler_fn handler);

ZLINK_EXPORT int zlink_gateway_bind (void *gateway,
                                     const char *bind_endpoint);

/**
 * @brief Connect the Gateway to a manually managed remote peer route.
 *
 * The remote routing id identifies the peer for request dispatch.
 * Returns EFSM if discovery is already attached.
 */
ZLINK_EXPORT int zlink_gateway_connect (void *gateway,
                                        const char *endpoint,
                                        const zlink_routing_id_t *routing_id);

/**
 * @brief Disconnect a manually managed remote peer route.
 *
 * Returns EFSM if discovery is already attached.
 */
ZLINK_EXPORT int zlink_gateway_disconnect (void *gateway,
                                           const char *endpoint);

/**
 * @brief Send a message to the bound service (load-balanced).
 * @param parts         Multipart message array.
 * @param part_count    Number of parts.
 * @param flags         Send flags (0 or ZLINK_DONTWAIT).
 */
ZLINK_EXPORT int zlink_gateway_send (void *gateway,
                                     zlink_msg_t *parts,
                                     size_t part_count,
                                     zlink_send_flags_t flags);

/** @brief Send a message directly to a specific Receiver by routing_id. */
ZLINK_EXPORT int zlink_gateway_send_rid (void *gateway,
                                         const zlink_routing_id_t *routing_id,
                                         zlink_msg_t *parts,
                                         size_t part_count,
                                         zlink_send_flags_t flags);

typedef enum zlink_gateway_lb_strategy_t
{
    ZLINK_GATEWAY_LB_STRATEGY_ROUND_ROBIN = 0,
    ZLINK_GATEWAY_LB_STRATEGY_WEIGHTED = 1
} zlink_gateway_lb_strategy_t;

#define ZLINK_GATEWAY_LB_ROUND_ROBIN ZLINK_GATEWAY_LB_STRATEGY_ROUND_ROBIN
#define ZLINK_GATEWAY_LB_WEIGHTED ZLINK_GATEWAY_LB_STRATEGY_WEIGHTED

/** @brief Set the load-balancing strategy for the bound service. */
ZLINK_EXPORT int zlink_gateway_set_lb_strategy (
  void *gateway, zlink_gateway_lb_strategy_t strategy);

typedef enum zlink_gateway_option_t
{
    ZLINK_GATEWAY_OPT_SNDHWM = 0x2101,   /**< Send high water mark (int, default: 1000) */
    ZLINK_GATEWAY_OPT_RCVHWM = 0x2102,   /**< Recv high water mark (int, default: 1000) */
    ZLINK_GATEWAY_OPT_SNDTIMEO = 0x2103,  /**< Send timeout in ms (int, default: -1 = blocking) */
    ZLINK_GATEWAY_OPT_LINGER = 0x2104,    /**< Linger time in ms (int, default: -1, internally forced to 0) */
    ZLINK_GATEWAY_OPT_SNDBUF = 0x2105,    /**< Kernel SO_SNDBUF in bytes (int, default: -1 = OS default) */
    ZLINK_GATEWAY_OPT_RCVBUF = 0x2106     /**< Kernel SO_RCVBUF in bytes (int, default: -1 = OS default) */
} zlink_gateway_option_t;

/**
 * @brief Set a Gateway service option.
 *
 * Internally the ROUTER socket is also configured with:
 *   - ROUTER_MANDATORY = 1  (unknown routing id causes error, not silent drop)
 *   - ROUTER_HANDOVER  = 1  (reconnect with same routing id replaces old peer)
 *   - LINGER           = 0  (pending messages discarded on close)
 *
 * Note: LINGER is forced to 0 at socket creation. To override, call
 * set_option with ZLINK_GATEWAY_OPT_LINGER after bind/connect.
 */
ZLINK_EXPORT int zlink_gateway_set_option (void *gateway,
                                           zlink_gateway_option_t option,
                                           const void *optval,
                                           size_t optvallen);

/**
 * @brief Override the representative routing id before first bind/connect.
 */
ZLINK_EXPORT int zlink_gateway_set_routing_id (void *gateway,
                                               const void *data,
                                               size_t size);

/** @brief Return the representative routing id for this Gateway. */
ZLINK_EXPORT int zlink_gateway_routing_id (void *gateway,
                                           zlink_routing_id_t *out);

/** @brief Configure TLS client settings for the Gateway. */
ZLINK_EXPORT int zlink_gateway_set_tls_client (void *gateway,
                                           const char *ca_cert,
                                           const char *hostname,
                                           int trust_system);

/** @brief Configure TLS server settings for the Gateway. */
ZLINK_EXPORT int zlink_gateway_set_tls_server (void *gateway,
                                               const char *cert,
                                               const char *key);

/** @brief Resolve the bound endpoint for this Gateway. */
ZLINK_EXPORT int zlink_gateway_last_endpoint (void *gateway,
                                              char *endpoint,
                                              size_t *size);

/** @brief Update the authoritative weight for a specific service peer. */
ZLINK_EXPORT int zlink_gateway_update_peer_weight (
  void *gateway,
  const zlink_routing_id_t *routing_id,
  uint32_t weight);

/** @brief Destroy the Gateway and release all resources. */
ZLINK_EXPORT int zlink_gateway_destroy (void **gateway_p);

/******************************************************************************/
/*  SPOT PUB/SUB API                                                          */
/******************************************************************************/

/* SPOT Node --------------------------------------------------------------- */

/**
 * @brief Create a service-bound SPOT node.
 *
 * Pass `NULL` for `handler` when node-level callback dispatch is not needed.
 */
ZLINK_EXPORT void *zlink_spot_node_new (void *ctx,
                                        const char *service_name,
                                        zlink_spot_handler_fn handler);

/** @brief Destroy a SPOT node and release all resources. */
ZLINK_EXPORT int zlink_spot_node_destroy (void **node_p);

/** @brief Bind the SPOT node to an endpoint. */
ZLINK_EXPORT int zlink_spot_node_bind (void *node, const char *endpoint);

/**
 * @brief Connect to a peer node's PUB endpoint (mesh topology).
 *
 * Returns EFSM if discovery is already attached.
 */
ZLINK_EXPORT int zlink_spot_node_connect_peer_pub (void *node,
                                               const char *peer_pub_endpoint);

/**
 * @brief Disconnect from a peer node's PUB endpoint.
 *
 * Returns EFSM if discovery is already attached.
 */
ZLINK_EXPORT int zlink_spot_node_disconnect_peer_pub (
  void *node, const char *peer_pub_endpoint);

/**
 * @brief Attach a Discovery instance for automatic peer connection.
 */
ZLINK_EXPORT int zlink_spot_node_attach_discovery (void *node,
                                                   void *discovery);

/** @brief Set TLS server certificate for the node. */
ZLINK_EXPORT int zlink_spot_node_set_tls_server (void *node,
                                             const char *cert,
                                             const char *key);

/** @brief Set TLS client settings for the node. */
ZLINK_EXPORT int zlink_spot_node_set_tls_client (void *node,
                                             const char *ca_cert,
                                             const char *hostname,
                                             int trust_system);

typedef enum zlink_spot_role_t
{
    ZLINK_SPOT_ROLE_PUB = 1,
    ZLINK_SPOT_ROLE_SUB = 2
} zlink_spot_role_t;

typedef enum zlink_spot_pub_option_t
{
    ZLINK_SPOT_PUB_OPT_SNDHWM = 0x2201,
    ZLINK_SPOT_PUB_OPT_SNDTIMEO = 0x2202,
    ZLINK_SPOT_PUB_OPT_LINGER = 0x2203,
    ZLINK_SPOT_PUB_OPT_NODROP = 0x2204,
    ZLINK_SPOT_PUB_OPT_SNDBUF = 0x2208,
    ZLINK_SPOT_PUB_OPT_RCVBUF = 0x2209
} zlink_spot_pub_option_t;

typedef enum zlink_spot_sub_option_t
{
    ZLINK_SPOT_SUB_OPT_RCVHWM = 0x2301,
    ZLINK_SPOT_SUB_OPT_LINGER = 0x2302,
    ZLINK_SPOT_SUB_OPT_SNDBUF = 0x2303,
    ZLINK_SPOT_SUB_OPT_RCVBUF = 0x2304,
    ZLINK_SPOT_SUB_OPT_RCVTIMEO = 0x2305
} zlink_spot_sub_option_t;


/** @brief Publish via the node-owned default SpotPub facade. */
ZLINK_EXPORT int zlink_spot_node_publish (void *node,
                                          const char *topic_id,
                                          zlink_msg_t *parts,
                                          size_t part_count,
                                          zlink_send_flags_t flags);

/** @brief Subscribe via the node-owned default SpotSub facade. */
ZLINK_EXPORT int zlink_spot_node_subscribe (void *node, const char *topic_id);

/** @brief Subscribe to a prefix pattern via the node-owned default SpotSub. */
ZLINK_EXPORT int zlink_spot_node_subscribe_pattern (void *node,
                                                    const char *pattern);

/** @brief Unsubscribe a topic or pattern via the node-owned default SpotSub. */
ZLINK_EXPORT int zlink_spot_node_unsubscribe (
  void *node, const char *topic_id_or_pattern);

ZLINK_EXPORT int zlink_spot_node_set_send_ready_handler (
  void *node,
  zlink_send_ready_handler_fn handler);

ZLINK_EXPORT void *zlink_spot_new (void *spot_node,
                                   zlink_spot_handler_fn handler);
ZLINK_EXPORT int zlink_spot_destroy (void **spot_p);
ZLINK_EXPORT int zlink_spot_publish (void *spot,
                                     const char *topic_id,
                                     zlink_msg_t *parts,
                                     size_t part_count,
                                     zlink_send_flags_t flags);
ZLINK_EXPORT int zlink_spot_sub_recv (void *sub,
                                      zlink_msg_t **parts,
                                      size_t *part_count,
                                      int flags,
                                      char *topic_id_out,
                                      size_t *topic_id_len);
ZLINK_EXPORT int zlink_spot_subscribe (void *spot, const char *topic_id);
ZLINK_EXPORT int zlink_spot_subscribe_pattern (void *spot, const char *pattern);
ZLINK_EXPORT int zlink_spot_unsubscribe (void *spot,
                                         const char *topic_id_or_pattern);
ZLINK_EXPORT int zlink_spot_set_send_ready_handler (
  void *spot,
  zlink_send_ready_handler_fn handler);
ZLINK_EXPORT int zlink_spot_set_pub_option (void *spot,
                                            zlink_spot_pub_option_t option,
                                            const void *optval,
                                            size_t optvallen);
ZLINK_EXPORT int zlink_spot_set_sub_option (void *spot,
                                            zlink_spot_sub_option_t option,
                                            const void *optval,
                                            size_t optvallen);

/** @brief Set a default SpotPub option for the node and future child pubs. */
ZLINK_EXPORT int zlink_spot_node_set_pub_option (void *node,
                                                 zlink_spot_pub_option_t option,
                                                 const void *optval,
                                                 size_t optvallen);

/** @brief Set a default SpotSub option for the node and future child subs. */
ZLINK_EXPORT int zlink_spot_node_set_sub_option (void *node,
                                                 zlink_spot_sub_option_t option,
                                                 const void *optval,
                                                 size_t optvallen);

/******************************************************************************/
/*  Service Monitor / Topology API                                            */
/******************************************************************************/

typedef enum zlink_service_kind_t
{
    ZLINK_SERVICE_KIND_DISCOVERY = 1,
    ZLINK_SERVICE_KIND_GATEWAY = 2,
    ZLINK_SERVICE_KIND_SPOT_SUB = 3,
    ZLINK_SERVICE_KIND_SPOT_PUB = 4
} zlink_service_kind_t;

typedef uint32_t zlink_discovery_monitor_event_mask_t;
typedef uint32_t zlink_gateway_monitor_event_mask_t;
typedef uint32_t zlink_spot_monitor_event_mask_t;
typedef uint32_t zlink_service_event_detail_mask_t;

#define ZLINK_DISCOVERY_MONITOR_EVENT_READY                                  \
    ((zlink_discovery_monitor_event_mask_t) (1u << 0))
#define ZLINK_DISCOVERY_MONITOR_EVENT_LOST                                   \
    ((zlink_discovery_monitor_event_mask_t) (1u << 1))
#define ZLINK_DISCOVERY_MONITOR_EVENT_ERROR                                  \
    ((zlink_discovery_monitor_event_mask_t) (1u << 4))
#define ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_UP                             \
    ((zlink_discovery_monitor_event_mask_t) (1u << 5))
#define ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_DOWN                           \
    ((zlink_discovery_monitor_event_mask_t) (1u << 6))
#define ZLINK_DISCOVERY_MONITOR_EVENT_PROVIDERS_CHANGED                      \
    ((zlink_discovery_monitor_event_mask_t) (1u << 7))
#define ZLINK_DISCOVERY_MONITOR_EVENT_CLOSED                                 \
    ((zlink_discovery_monitor_event_mask_t) (1u << 17))

#define ZLINK_GATEWAY_MONITOR_EVENT_ERROR                                    \
    ((zlink_gateway_monitor_event_mask_t) (1u << 4))
#define ZLINK_GATEWAY_MONITOR_EVENT_SERVICE_READY                            \
    ((zlink_gateway_monitor_event_mask_t) (1u << 8))
#define ZLINK_GATEWAY_MONITOR_EVENT_SERVICE_LOST                             \
    ((zlink_gateway_monitor_event_mask_t) (1u << 9))
#define ZLINK_GATEWAY_MONITOR_EVENT_SEND_READY_CHANGED                       \
    ((zlink_gateway_monitor_event_mask_t) (1u << 10))
#define ZLINK_GATEWAY_MONITOR_EVENT_ROUTE_UP                                 \
    ((zlink_gateway_monitor_event_mask_t) (1u << 11))
#define ZLINK_GATEWAY_MONITOR_EVENT_ROUTE_DOWN                               \
    ((zlink_gateway_monitor_event_mask_t) (1u << 12))
#define ZLINK_GATEWAY_MONITOR_EVENT_CLOSED                                   \
    ((zlink_gateway_monitor_event_mask_t) (1u << 17))

#define ZLINK_SPOT_MONITOR_EVENT_READY                                       \
    ((zlink_spot_monitor_event_mask_t) (1u << 0))
#define ZLINK_SPOT_MONITOR_EVENT_LOST                                        \
    ((zlink_spot_monitor_event_mask_t) (1u << 1))
#define ZLINK_SPOT_MONITOR_EVENT_PEER_UP                                     \
    ((zlink_spot_monitor_event_mask_t) (1u << 2))
#define ZLINK_SPOT_MONITOR_EVENT_PEER_DOWN                                   \
    ((zlink_spot_monitor_event_mask_t) (1u << 3))
#define ZLINK_SPOT_MONITOR_EVENT_ERROR                                       \
    ((zlink_spot_monitor_event_mask_t) (1u << 4))
#define ZLINK_SPOT_MONITOR_EVENT_SUB_FILTER_APPLIED                          \
    ((zlink_spot_monitor_event_mask_t) (1u << 13))
#define ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY                          \
    ((zlink_spot_monitor_event_mask_t) (1u << 14))
#define ZLINK_SPOT_MONITOR_EVENT_PUB_QUEUE_FULL                              \
    ((zlink_spot_monitor_event_mask_t) (1u << 15))
#define ZLINK_SPOT_MONITOR_EVENT_PUB_QUEUE_DRAINED                           \
    ((zlink_spot_monitor_event_mask_t) (1u << 16))
#define ZLINK_SPOT_MONITOR_EVENT_CLOSED                                      \
    ((zlink_spot_monitor_event_mask_t) (1u << 17))
#define ZLINK_SPOT_MONITOR_EVENT_PUB_DELIVERY_READY_CHANGED                  \
    ((zlink_spot_monitor_event_mask_t) (1u << 18))
#define ZLINK_SPOT_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED                  \
    ((zlink_spot_monitor_event_mask_t) (1u << 19))
#define ZLINK_SPOT_MONITOR_EVENT_PUB_FIRST_DELIVERY_READY_CHANGED            \
    ((zlink_spot_monitor_event_mask_t) (1u << 20))

#define ZLINK_MONITOR_EVENT_READY ZLINK_DISCOVERY_MONITOR_EVENT_READY
#define ZLINK_MONITOR_EVENT_LOST ZLINK_DISCOVERY_MONITOR_EVENT_LOST
#define ZLINK_MONITOR_EVENT_PEER_UP ZLINK_SPOT_MONITOR_EVENT_PEER_UP
#define ZLINK_MONITOR_EVENT_PEER_DOWN ZLINK_SPOT_MONITOR_EVENT_PEER_DOWN
#define ZLINK_MONITOR_EVENT_ERROR ZLINK_DISCOVERY_MONITOR_EVENT_ERROR
#define ZLINK_DISCOVERY_SERVICE_UP ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_UP
#define ZLINK_DISCOVERY_SERVICE_DOWN ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_DOWN
#define ZLINK_DISCOVERY_PROVIDERS_CHANGED ZLINK_DISCOVERY_MONITOR_EVENT_PROVIDERS_CHANGED
#define ZLINK_GATEWAY_SERVICE_READY ZLINK_GATEWAY_MONITOR_EVENT_SERVICE_READY
#define ZLINK_GATEWAY_SERVICE_LOST ZLINK_GATEWAY_MONITOR_EVENT_SERVICE_LOST
#define ZLINK_GATEWAY_SEND_READY_CHANGED ZLINK_GATEWAY_MONITOR_EVENT_SEND_READY_CHANGED
#define ZLINK_GATEWAY_ROUTE_UP ZLINK_GATEWAY_MONITOR_EVENT_ROUTE_UP
#define ZLINK_GATEWAY_ROUTE_DOWN ZLINK_GATEWAY_MONITOR_EVENT_ROUTE_DOWN
#define ZLINK_SPOT_SUB_FILTER_APPLIED ZLINK_SPOT_MONITOR_EVENT_SUB_FILTER_APPLIED
#define ZLINK_SPOT_SUB_SUBSCRIPTION_READY ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY
#define ZLINK_SPOT_PUB_QUEUE_FULL ZLINK_SPOT_MONITOR_EVENT_PUB_QUEUE_FULL
#define ZLINK_SPOT_PUB_QUEUE_DRAINED ZLINK_SPOT_MONITOR_EVENT_PUB_QUEUE_DRAINED
#define ZLINK_SPOT_PUB_DELIVERY_READY_CHANGED                                \
    ZLINK_SPOT_MONITOR_EVENT_PUB_DELIVERY_READY_CHANGED
#define ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED                                \
    ZLINK_SPOT_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED
#define ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED                          \
    ZLINK_SPOT_MONITOR_EVENT_PUB_FIRST_DELIVERY_READY_CHANGED
#define ZLINK_MONITOR_EVENT_CLOSED ZLINK_DISCOVERY_MONITOR_EVENT_CLOSED
#define ZLINK_SERVICE_EVENT_DETAIL_SERVICE_NAME                              \
    ((zlink_service_event_detail_mask_t) 0x0001u)
#define ZLINK_SERVICE_EVENT_DETAIL_ENDPOINT                                  \
    ((zlink_service_event_detail_mask_t) 0x0002u)
#define ZLINK_SERVICE_EVENT_DETAIL_SUBJECT_RID                               \
    ((zlink_service_event_detail_mask_t) 0x0004u)
#define ZLINK_SERVICE_EVENT_DETAIL_PEER_RID                                  \
    ((zlink_service_event_detail_mask_t) 0x0008u)
#define ZLINK_SERVICE_EVENT_DETAIL_SUBJECT                                   \
    ((zlink_service_event_detail_mask_t) 0x0010u)
#define ZLINK_SERVICE_EVENT_DETAIL_SUBJECT_KIND                              \
    ((zlink_service_event_detail_mask_t) 0x0020u)

#define ZLINK_EVENT_DETAIL_SERVICE_NAME ZLINK_SERVICE_EVENT_DETAIL_SERVICE_NAME
#define ZLINK_EVENT_DETAIL_ENDPOINT ZLINK_SERVICE_EVENT_DETAIL_ENDPOINT
#define ZLINK_EVENT_DETAIL_SUBJECT_RID ZLINK_SERVICE_EVENT_DETAIL_SUBJECT_RID
#define ZLINK_EVENT_DETAIL_PEER_RID ZLINK_SERVICE_EVENT_DETAIL_PEER_RID
#define ZLINK_EVENT_DETAIL_SUBJECT ZLINK_SERVICE_EVENT_DETAIL_SUBJECT
#define ZLINK_EVENT_DETAIL_SUBJECT_KIND ZLINK_SERVICE_EVENT_DETAIL_SUBJECT_KIND

typedef enum zlink_service_event_subject_kind_t
{
    ZLINK_SERVICE_EVENT_SUBJECT_NONE = 0,
    ZLINK_SERVICE_EVENT_SUBJECT_TOPIC = 1,
    ZLINK_SERVICE_EVENT_SUBJECT_PATTERN = 2
} zlink_service_event_subject_kind_t;

typedef struct zlink_service_event_t
{
    zlink_service_kind_t service_kind;
    uint32_t event_type;
    int32_t status;
    int32_t error_code;
    uint32_t value;
    zlink_service_event_detail_mask_t detail_flags;
    char service_name[256];
    char endpoint[256];
    zlink_routing_id_t routing_id;
    char subject[256];
    uint32_t subject_kind;
} zlink_service_event_t;

typedef void (*zlink_service_monitor_handler_fn) (
  const zlink_service_event_t *event_);

/**
 * @brief Ignore service monitor events while keeping a valid handler symbol.
 *
 * Pass this when you want snapshot or direct polling on the returned monitor
 * handle without automatic callback dispatch.
 */
ZLINK_EXPORT void zlink_service_monitor_ignore_handler (
  const zlink_service_event_t *event_);

/**
 * @brief Open a service monitor handle with a fixed callback.
 *
 * Monitor handles participate in the same tiered contract: open/close are
 * serialized control-path operations, while callback replacement is not
 * supported after open.
 */
ZLINK_EXPORT void *zlink_discovery_monitor_open (
  void *discovery,
  zlink_discovery_monitor_event_mask_t events,
  zlink_service_monitor_handler_fn handler);
ZLINK_EXPORT void *zlink_gateway_monitor_open (
  void *gateway,
  zlink_gateway_monitor_event_mask_t events,
  zlink_service_monitor_handler_fn handler);
ZLINK_EXPORT void *zlink_spot_node_monitor_open (
  void *node,
  zlink_spot_role_t role,
  zlink_spot_monitor_event_mask_t events,
  zlink_service_monitor_handler_fn handler);
ZLINK_EXPORT void *zlink_spot_monitor_open (
  void *spot,
  zlink_spot_role_t role,
  zlink_spot_monitor_event_mask_t events,
  zlink_service_monitor_handler_fn handler);

/**
 * @brief Close a service monitor handle.
 *
 * If another thread is executing the monitor callback, the close fails with
 * errno=EBUSY. Self-close from the callback succeeds and is deferred until the
 * callback returns.
 */
ZLINK_EXPORT int zlink_service_monitor_close (void **monitor_p);

typedef enum zlink_topology_source_t
{
    ZLINK_TOPOLOGY_SOURCE_MANUAL = 1,
    ZLINK_TOPOLOGY_SOURCE_DISCOVERY = 2,
    ZLINK_TOPOLOGY_SOURCE_REGISTRY = 3
} zlink_topology_source_t;

typedef enum zlink_topology_state_t
{
    ZLINK_TOPOLOGY_STATE_DISCOVERED = 1,
    ZLINK_TOPOLOGY_STATE_CONNECTING = 2,
    ZLINK_TOPOLOGY_STATE_READY = 3,
    ZLINK_TOPOLOGY_STATE_LOST = 4,
    ZLINK_TOPOLOGY_STATE_ERROR = 5,
    ZLINK_TOPOLOGY_STATE_STOPPED = 6
} zlink_topology_state_t;

typedef struct zlink_registry_topology_entry_t
{
    zlink_routing_id_t routing_id;
    zlink_service_kind_t service_kind;
    char service_name[256];
    char endpoint[256];
    zlink_topology_source_t source;
    zlink_topology_state_t state;
    uint32_t desired_count;
    uint32_t ready_count;
    uint32_t error_code;
    uint64_t last_reported_ms;
} zlink_registry_topology_entry_t;

typedef struct zlink_registry_topology_filter_t
{
    zlink_service_kind_t service_kind;
    char service_name[256];
    zlink_routing_id_t routing_id;
    zlink_topology_state_t state;
    zlink_topology_source_t source;
} zlink_registry_topology_filter_t;

ZLINK_EXPORT int zlink_registry_topology_snapshot (
  void *registry,
  zlink_registry_topology_entry_t *entries,
  size_t *count);
ZLINK_EXPORT int zlink_registry_topology_query (
  void *registry,
  const zlink_registry_topology_filter_t *filter,
  zlink_registry_topology_entry_t *entries,
  size_t *count);

ZLINK_EXPORT void *zlink_registry_query_client_new (void *ctx);
ZLINK_EXPORT int zlink_registry_query_client_connect (void *client,
                                                      const char *endpoint);
ZLINK_EXPORT int zlink_registry_query_snapshot (
  void *client,
  const zlink_registry_topology_filter_t *filter,
  zlink_registry_topology_entry_t *entries,
  size_t *count);

typedef struct zlink_registry_gateway_peer_entry_t
{
    zlink_routing_id_t gateway_routing_id;
    char gateway_endpoint[256];
    char service_name[256];
    zlink_routing_id_t peer_routing_id;
    char peer_endpoint[256];
    zlink_topology_state_t state;
    uint32_t weight;
    uint64_t connected_since_ms;
    uint64_t last_reported_ms;
} zlink_registry_gateway_peer_entry_t;

typedef struct zlink_registry_gateway_peer_filter_t
{
    zlink_routing_id_t gateway_routing_id;
    char service_name[256];
    zlink_routing_id_t peer_routing_id;
    zlink_topology_state_t state;
} zlink_registry_gateway_peer_filter_t;

ZLINK_EXPORT int zlink_registry_gateway_peers_snapshot (
  void *registry,
  zlink_registry_gateway_peer_entry_t *entries,
  size_t *count);
ZLINK_EXPORT int zlink_registry_gateway_peers_query (
  void *registry,
  const zlink_registry_gateway_peer_filter_t *filter,
  zlink_registry_gateway_peer_entry_t *entries,
  size_t *count);
ZLINK_EXPORT int zlink_registry_query_gateway_peers_snapshot (
  void *client,
  const zlink_registry_gateway_peer_filter_t *filter,
  zlink_registry_gateway_peer_entry_t *entries,
  size_t *count);
ZLINK_EXPORT int zlink_registry_query_destroy (void **client_p);

#if defined _WIN32
#if defined _WIN64
typedef unsigned __int64 zlink_fd_t;
#else
typedef unsigned int zlink_fd_t;
#endif
#else
typedef int zlink_fd_t;
#endif

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
ZLINK_EXPORT void *zlink_thread_start (zlink_thread_fn *func_, void *arg_);

/** @brief Wait for a thread to finish and release its handle. */
ZLINK_EXPORT void zlink_thread_join (void *thread_);

#undef ZLINK_EXPORT

#ifdef __cplusplus
}

#ifdef ZLINK_INTERNAL_BUILD
void *zlink_socket (void *ctx_, zlink_socket_type_t type_);
#endif
#endif

#endif
