/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_H_INCLUDED__
#define __ZLINK_H_INCLUDED__

/*  Version macros for compile-time API version detection                     */
#define ZLINK_VERSION_MAJOR 5
#define ZLINK_VERSION_MINOR 0
#define ZLINK_VERSION_PATCH 7

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
#define ZLINK_IO_THREADS_DFLT 1
#define ZLINK_MAX_SOCKETS_DFLT 4095
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
    ZLINK_THREAD_NAME_PREFIX = 9,
    ZLINK_CTX_OPT_BLOCKY = 10
} zlink_ctx_option_t;

typedef uint32_t zlink_send_flags_t;

typedef enum zlink_send_result_t
{
    ZLINK_SEND_RESULT_SENT = 0,
    ZLINK_SEND_RESULT_BACKPRESSURED = 1,
    ZLINK_SEND_RESULT_NOT_READY = 2
} zlink_send_result_t;

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
 * @param ffn_   Callback invoked when the last owning message releases the
 *               buffer. May be NULL.
 * @param hint_  User data passed to @p ffn_.
 *
 * If @p ffn_ is NULL, the message keeps a borrowed reference to @p data_ and
 * never frees it. Such messages report as shared because the storage is not
 * uniquely owned by the message object.
 */
ZLINK_EXPORT int zlink_msg_init_data (
  zlink_msg_t *msg_, void *data_, size_t size_, zlink_free_fn *ffn_, void *hint_);

/**
 * @brief Release this message object's ownership of its content.
 *
 * The message becomes invalid after this call. For reference-counted message
 * storage, the underlying buffer is released only when the last owning message
 * is closed or consumed by send.
 */
ZLINK_EXPORT int zlink_msg_close (zlink_msg_t *msg_);

/**
 * @brief Move message content from src_ to dest_ without copying payload.
 *
 * Ownership is transferred to @p dest_. The source becomes an empty message.
 * Existing shared/reference-counted state moves with the message; move does
 * not increment any reference count.
 */
ZLINK_EXPORT int zlink_msg_move (zlink_msg_t *dest_, zlink_msg_t *src_);

/**
 * @brief Copy a message from src_ to dest_.
 *
 * Small inline messages are copied by value. Large or externally stored
 * messages share the same underlying storage and are tracked internally by
 * reference count rather than duplicating the payload buffer.
 */
ZLINK_EXPORT int zlink_msg_copy (zlink_msg_t *dest_, zlink_msg_t *src_);

/** @brief Return a pointer to the message data buffer. */
ZLINK_EXPORT void *zlink_msg_data (zlink_msg_t *msg_);

/** @brief Return the message data size in bytes. */
ZLINK_EXPORT size_t zlink_msg_size (const zlink_msg_t *msg_);

/**
 * @brief Return the message storage reference count.
 *
 * Reference-counted large/zero-copy messages return their current internal
 * reference count. Message kinds that are not managed by internal reference
 * counting (for example inline or borrowed-constant storage) return 1.
 */
ZLINK_EXPORT int zlink_msg_refcnt (const zlink_msg_t *msg_);

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

typedef enum zlink_option_t
{
    /* Queue, buffer, and message size options */
    ZLINK_OPT_AFFINITY = 0x3001,
    ZLINK_OPT_RATE = 0x3003,
    ZLINK_OPT_RECOVERY_IVL = 0x3004,
    ZLINK_OPT_SNDBUF = 0x3005,
    ZLINK_OPT_RCVBUF = 0x3006,
    ZLINK_OPT_MAXMSGSIZE = 0x300E,
    ZLINK_OPT_SNDHWM = 0x300F,
    ZLINK_OPT_RCVHWM = 0x3010,

    /* Lifecycle, timeout, and reconnect options */
    ZLINK_OPT_LINGER = 0x300A,
    ZLINK_OPT_RECONNECT_IVL = 0x300B,
    ZLINK_OPT_BACKLOG = 0x300C,
    ZLINK_OPT_RECONNECT_IVL_MAX = 0x300D,
    ZLINK_OPT_MULTICAST_HOPS = 0x3011,
    ZLINK_OPT_RCVTIMEO = 0x3012,
    ZLINK_OPT_SNDTIMEO = 0x3013,
    ZLINK_OPT_CONNECT_TIMEOUT = 0x3024,
    ZLINK_OPT_HANDSHAKE_IVL = 0x301D,

    /* TCP keepalive and transport-level TCP options */
    ZLINK_OPT_TCP_KEEPALIVE = 0x3015,
    ZLINK_OPT_TCP_KEEPALIVE_CNT = 0x3016,
    ZLINK_OPT_TCP_KEEPALIVE_IDLE = 0x3017,
    ZLINK_OPT_TCP_KEEPALIVE_INTVL = 0x3018,
    ZLINK_OPT_TCP_MAXRT = 0x3025,
    ZLINK_OPT_TCP_NODELAY = 0x3031,

    /* Heartbeat options */
    ZLINK_OPT_HEARTBEAT_IVL = 0x3021,
    ZLINK_OPT_HEARTBEAT_TTL = 0x3022,
    ZLINK_OPT_HEARTBEAT_TIMEOUT = 0x3023,

    /* Network and address-family options */
    ZLINK_OPT_IPV6 = 0x301A,
    ZLINK_OPT_TOS = 0x301C,
    ZLINK_OPT_MULTICAST_MAXTPDU = 0x3026,
    ZLINK_OPT_BINDTODEVICE = 0x3027,
    ZLINK_OPT_TLS_CERT = 0x3028,
    ZLINK_OPT_TLS_KEY = 0x3029,
    ZLINK_OPT_TLS_CA = 0x302A,
    ZLINK_OPT_TLS_VERIFY = 0x302B,
    ZLINK_OPT_TLS_REQUIRE_CLIENT_CERT = 0x302C,
    ZLINK_OPT_TLS_HOSTNAME = 0x302D,
    ZLINK_OPT_TLS_TRUST_SYSTEM = 0x302E,
    ZLINK_OPT_TLS_PASSWORD = 0x302F,

    /* Delivery, buffering policy, and filter semantics */
    ZLINK_OPT_IMMEDIATE = 0x3019,
    ZLINK_OPT_CONFLATE = 0x301B,
    ZLINK_OPT_BLOCKY = 0x301E,
    ZLINK_OPT_INVERT_MATCHING = 0x3020,

    /* Introspection and protocol metadata */
    ZLINK_OPT_FD = 0x3007,
    ZLINK_OPT_EVENTS = 0x3008,
    ZLINK_OPT_TYPE = 0x3009,
    ZLINK_OPT_LAST_ENDPOINT = 0x3014,
    ZLINK_OPT_ZMP_METADATA = 0x3030,
    ZLINK_OPT_DISCOVERY_METADATA_MAX_SIZE = 0x3032,
} zlink_option_t;

typedef enum zlink_router_option_t
{
    ZLINK_ROUTER_OPT_MANDATORY = 0x3101,
    ZLINK_ROUTER_OPT_HANDOVER = 0x3102,
    ZLINK_ROUTER_OPT_PROBE = 0x3103,
    ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID = 0x3104
} zlink_router_option_t;

typedef enum zlink_dealer_option_t
{
    ZLINK_DEALER_OPT_PROBE = 0x3201
} zlink_dealer_option_t;

typedef enum zlink_pub_option_t
{
    ZLINK_PUB_OPT_VERBOSE = 0x3301,
    ZLINK_PUB_OPT_VERBOSER = 0x3302,
    ZLINK_PUB_OPT_MANUAL = 0x3303,
    ZLINK_PUB_OPT_MANUAL_LAST_VALUE = 0x3304,
    ZLINK_PUB_OPT_NODROP = 0x3305,
    ZLINK_PUB_OPT_WELCOME_MSG = 0x3306,
    ZLINK_PUB_OPT_TOPICS_COUNT = 0x3307,
    ZLINK_PUB_OPT_APPROVE_SUBSCRIBE = 0x3308,
    ZLINK_PUB_OPT_REJECT_SUBSCRIBE = 0x3309
} zlink_pub_option_t;

typedef enum zlink_sub_option_t
{
    ZLINK_SUB_OPT_TOPICS_COUNT = 0x3400
} zlink_sub_option_t;

typedef enum zlink_stream_option_t
{
    ZLINK_STREAM_OPT_NOTIFY = 0x3501
} zlink_stream_option_t;

#define ZLINK_DONTWAIT ((zlink_send_flags_t) 0x0001u)
#define ZLINK_SEND_FLAG_DONTWAIT ZLINK_DONTWAIT

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
#define ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY_CHANGED                  \
    ((zlink_socket_monitor_event_mask_t) 0x1000u)
#define ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_PROTOCOL                 \
    ((zlink_socket_monitor_event_mask_t) 0x2000u)
#define ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_AUTH                     \
    ((zlink_socket_monitor_event_mask_t) 0x4000u)
#define ZLINK_SOCKET_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED                \
    ((zlink_socket_monitor_event_mask_t) 0x8000u)
#define ZLINK_SOCKET_MONITOR_EVENT_PUB_DELIVERY_READY_CHANGED                \
    ((zlink_socket_monitor_event_mask_t) 0x10000u)

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
#define ZLINK_EVENT_CONNECTION_READY_CHANGED                                 \
    ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY_CHANGED
#define ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL                                \
    ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_PROTOCOL
#define ZLINK_EVENT_HANDSHAKE_FAILED_AUTH                                    \
    ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_AUTH
#define ZLINK_EVENT_SUB_DELIVERY_READY_CHANGED                               \
    ZLINK_SOCKET_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED
#define ZLINK_EVENT_PUB_DELIVERY_READY_CHANGED                               \
    ZLINK_SOCKET_MONITOR_EVENT_PUB_DELIVERY_READY_CHANGED

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
  size_t part_count_,
  void *userdata_);

typedef void (*zlink_subscribe_handler_fn) (
  const zlink_routing_id_t *source_rid_,
  const char *topic_,
  size_t topic_len_,
  zlink_msg_t *parts_,
  size_t part_count_,
  void *userdata_);

typedef void (*zlink_send_ready_handler_fn) (void *subject_, void *userdata_);

/**
 * @brief Create a socket.
 * @param context_  Context handle (return value of zlink_ctx_new()).
 * @param type_     Socket type.
 * @return Socket handle, or NULL on failure (errno is set).
 */
ZLINK_EXPORT void *zlink_socket (void *, zlink_socket_type_t type_);

/**
 * @brief Attach a direct receive handler to a multipart receive subject.
 *
 * Supported subjects:
 * - raw `PAIR`
 * - raw `DEALER`
 * - raw `ROUTER`
 * - raw `STREAM`
 *
 * The subject starts in recv model. After a successful attach, direct recv on
 * the same subject and data-plane poller `ZLINK_POLLIN` registration fail with
 * errno=EBUSY. A second attach on the same subject also fails with errno=EBUSY.
 *
 * Unsupported subjects fail with errno=ENOTSUP.
 */
ZLINK_EXPORT int zlink_recv_handler (
  void *s_, zlink_socket_msg_handler_fn handler_, void *userdata_);

/**
 * @brief Attach a direct topic-aware receive handler to a subscribe subject.
 *
 * Supported subjects:
 * - raw `SUB`
 * - raw `XSUB`
 * - unified `spot`
 * - `spot node`
 *
 * The subject starts in recv model. After a successful attach,
 * `zlink_subscribe()` and data-plane poller `ZLINK_POLLIN` registration on the
 * same subject fail with errno=EBUSY. A second attach on the same subject also
 * fails with errno=EBUSY.
 *
 * Unsupported subjects fail with errno=ENOTSUP.
 */
ZLINK_EXPORT int zlink_subscribe_handler (
  void *s_, zlink_subscribe_handler_fn handler_, void *userdata_);

/**
 * @brief Install or replace the send-ready callback for a send-capable subject.
 *
 * The handler is replace-only. Passing NULL is invalid. A successful replace is
 * visible from the next writable transition. If called reentrantly from the
 * same handle's send-ready callback, the call fails with errno=EDEADLK.
 *
 * Supported handles:
 * - raw `PAIR`
 * - raw `PUB`
 * - raw `XPUB`
 * - raw `DEALER`
 * - raw `ROUTER`
 * - raw `STREAM`
 * - unified `spot`
 * - `spot node`
 *
 * Send-ready is independent from receive callback mode. After a successful
 * attach, data-plane poller `ZLINK_POLLOUT` registration on the same subject
 * fails with errno=EBUSY.
 *
 * Unsupported subjects fail with errno=ENOTSUP.
 */
ZLINK_EXPORT int zlink_send_ready_handler (
  void *s_, zlink_send_ready_handler_fn handler_, void *userdata_);

/**
 * @brief Close a socket and release its resources.
 *
 * Public handles use a tiered concurrency contract: send/publish hot paths
 * allow same-handle concurrent use, low-frequency control paths serialize for
 * correctness, and close/destroy uses a stricter lifecycle gate. If another
 * thread has an in-flight callback or admitted API on the same handle, close
 * fails with errno=EBUSY. Once close is accepted, new API entry fails with
 * errno=ESHUTDOWN. Discovery-attached raw service participants also reject
 * close until their owning discovery is destroyed. Self-close from a
 * send-ready or monitor callback is deferred until callback epilogue. For
 * STREAM raw callbacks, close from inside the raw callback is not supported
 * and fails with errno=EBUSY.
 */
ZLINK_EXPORT int zlink_close (void *s_);

ZLINK_EXPORT int zlink_set_option (void *handle_,
                                   zlink_option_t option_,
                                   const void *optval_,
                                   size_t optvallen_);
ZLINK_EXPORT int zlink_get_option (void *handle_,
                                   zlink_option_t option_,
                                   void *optval_,
                                   size_t *optvallen_);
ZLINK_EXPORT int zlink_set_routing_id (void *handle_,
                                       const void *data_,
                                       size_t size_);
ZLINK_EXPORT int zlink_get_routing_id (void *handle_,
                                       zlink_routing_id_t *out_);
ZLINK_EXPORT int zlink_set_tls_server (void *handle_,
                                       const char *cert_,
                                       const char *key_,
                                       int require_client_cert_);
ZLINK_EXPORT int zlink_set_tls_client (void *handle_,
                                       const char *ca_cert_,
                                       const char *hostname_,
                                       int trust_system_);
ZLINK_EXPORT int zlink_set_router_option (void *handle_,
                                          zlink_router_option_t option_,
                                          const void *optval_,
                                          size_t optvallen_);
ZLINK_EXPORT int zlink_get_router_option (void *handle_,
                                          zlink_router_option_t option_,
                                          void *optval_,
                                          size_t *optvallen_);
ZLINK_EXPORT int zlink_set_dealer_option (void *handle_,
                                          zlink_dealer_option_t option_,
                                          const void *optval_,
                                          size_t optvallen_);
ZLINK_EXPORT int zlink_set_stream_option (void *handle_,
                                          zlink_stream_option_t option_,
                                          const void *optval_,
                                          size_t optvallen_);
ZLINK_EXPORT int zlink_get_stream_option (void *handle_,
                                          zlink_stream_option_t option_,
                                          void *optval_,
                                          size_t *optvallen_);

/*
 * PUB/XPUB socket, spot-pub, spotnode-pub:
 * - zlink_pub_option_t for pub-specific options
 * - use zlink_set_option()/zlink_get_option() for common options
 */
ZLINK_EXPORT int zlink_set_pub_option (void *handle_,
                                       zlink_pub_option_t option_,
                                       const void *optval_,
                                       size_t optvallen_);
ZLINK_EXPORT int zlink_get_pub_option (void *handle_,
                                       zlink_pub_option_t option_,
                                       void *optval_,
                                       size_t *optvallen_);

/*
 * SUB/XSUB socket, spot-sub, spotnode-sub:
 * - zlink_sub_option_t for sub-specific options
 * - use zlink_set_option()/zlink_get_option() for common options
 */
ZLINK_EXPORT int zlink_set_sub_option (void *handle_,
                                       zlink_sub_option_t option_,
                                       const void *optval_,
                                       size_t optvallen_);
ZLINK_EXPORT int zlink_get_sub_option (void *handle_,
                                       zlink_sub_option_t option_,
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
 * @brief Attach a raw ROUTER/DEALER/PUB/SUB socket to a discovery service view.
 *
 * Attached sockets delegate provider registration, peer refresh, and shutdown
 * ownership to the discovery instance.
 *
 * While attached, manual `connect`, `disconnect`, `unbind`, and `close`
 * operations fail. Destroy the discovery instance to terminate the attached
 * socket lifecycle.
 */
ZLINK_EXPORT int zlink_socket_attach_discovery (void *socket_,
                                                void *discovery_);

/**
 * @brief Send a multipart message on a socket or handle.
 *
 * Ownership of all parts is transferred to the callee when the send attempt
 * begins. On any return path the input parts must be treated as moved-from
 * message handles and must not be reused by the caller.
 *
 * This is the canonical public send API for non-directed sends.
 * Public multipart send does not use ZLINK_SNDMORE; part boundaries are
 * defined only by @p parts_ and @p part_count_.
 */
ZLINK_EXPORT int zlink_send (void *s_,
                             zlink_msg_t *parts_,
                             size_t part_count_,
                             zlink_send_flags_t flags_);

/**
 * @brief Send a multipart message to a specific peer routing id.
 *
 * Ownership of all parts is transferred to the callee when the send attempt
 * begins. On any return path the input parts must be treated as moved-from
 * message handles and must not be reused by the caller.
 */
ZLINK_EXPORT int zlink_send_rid (void *s_,
                                 const zlink_routing_id_t *target_rid_,
                                 zlink_msg_t *parts_,
                                 size_t part_count_,
                                 zlink_send_flags_t flags_);

/**
 * @brief Receive a multipart message from a socket or handle.
 *
 * Direct recv is the synchronous counterpart of direct callback dispatch.
 * The returned payload shape must match the callback payload shape for the
 * same socket or handle family.
 *
 * On success, ownership of the returned `zlink_msg_t` payload instances is
 * transferred to the caller, but the `parts_out_` array itself is a
 * thread-local view owned by the library. The caller must close each part with
 * `zlink_msg_close()` (or `zlink_multipart_close()`), must not call `free()`
 * on `parts_out_`, and must not retain the view across the next recv-like call
 * on the same thread.
 */
ZLINK_EXPORT int zlink_recv (void *s_,
                             zlink_routing_id_t *source_rid_out_,
                             zlink_msg_t **parts_out_,
                             size_t *part_count_out_,
                             zlink_send_flags_t flags_);

ZLINK_EXPORT int zlink_publish (void *subject_,
                                const char *topic_id_,
                                zlink_msg_t *parts_,
                                size_t part_count_,
                                zlink_send_flags_t flags_);

ZLINK_EXPORT int zlink_set_subscription (void *handle_,
                                         const char *filter_);
ZLINK_EXPORT int zlink_unset_subscription (void *handle_,
                                           const char *filter_);
ZLINK_EXPORT int zlink_subscription_at (void *handle_,
                                        size_t index_,
                                        char *filter_out_,
                                        size_t *filter_len_inout_,
                                        int *is_pattern_out_);

ZLINK_EXPORT int zlink_subscribe (void *subject_,
                                  zlink_routing_id_t *source_rid_out_,
                                  zlink_msg_t **parts_out_,
                                  size_t *part_count_out_,
                                  char *topic_id_out_,
                                  size_t *topic_id_len_out_,
                                  zlink_send_flags_t flags_);

ZLINK_EXPORT int zlink_subscription_event (
  void *subject_,
  zlink_routing_id_t *source_rid_out_,
  int *subscribed_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_send_flags_t flags_);

typedef struct {
    uint64_t event;
    uint64_t value;
    zlink_routing_id_t routing_id;
    char local_addr[256];
    char remote_addr[256];
} zlink_monitor_event_t;

typedef void (*zlink_monitor_handler_fn) (
  const zlink_monitor_event_t *event_, void *userdata_);

typedef zlink_monitor_event_t zlink_socket_monitor_event_t;
typedef zlink_monitor_handler_fn zlink_socket_monitor_handler_fn;

typedef struct zlink_socket_monitor_open_options_t
{
    zlink_socket_monitor_event_mask_t events;
} zlink_socket_monitor_open_options_t;

/**
 * @brief Ignore socket monitor events while keeping a valid handler symbol.
 *
 * Pass this when you want snapshot or direct polling on the returned monitor
 * handle without automatic callback dispatch.
 */
ZLINK_EXPORT void zlink_monitor_ignore_handler (
  const zlink_monitor_event_t *event_, void *userdata_);

typedef enum zlink_monitor_source_kind_t
{
    ZLINK_MONITOR_SOURCE_SOCKET = 1,
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

#define ZLINK_MONITOR_SNAPSHOT_DETAIL_READY_COUNT                         \
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
    uint32_t ready_count;
    uint64_t snd_pending_msgs;
    uint64_t rcv_pending_msgs;
} zlink_monitor_snapshot_t;

/**
 * @brief Open and return a socket monitor handle directly.
 * @param events_  Event bitmask.
 * @return Monitor handle, or NULL on failure.
 */
ZLINK_EXPORT void *zlink_socket_monitor_open (
  void *s_, const zlink_socket_monitor_open_options_t *options_);

ZLINK_EXPORT int zlink_socket_monitor_handler (
  void *monitor_,
  zlink_socket_monitor_handler_fn handler_,
  void *userdata_);

ZLINK_EXPORT int zlink_socket_monitor_recv (
  void *monitor_,
  zlink_socket_monitor_event_t *out_,
  zlink_send_flags_t flags_);

/** @brief Read the current snapshot for a socket or service monitor handle. */
ZLINK_EXPORT int zlink_monitor_snapshot (void *monitor_,
                                         zlink_monitor_snapshot_t *out_);

ZLINK_EXPORT int zlink_monitor_close (void **monitor_p_);

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

/** @brief Destroy the registry and release all resources. */
ZLINK_EXPORT int zlink_registry_destroy (void **registry_p);

/* Discovery ---------------------------------------------------------------- */

/** @name Service registration types */
/** @{ */
typedef enum zlink_service_type_t
{
    ZLINK_SERVICE_TYPE_SPOT = 0x3002,
    ZLINK_SERVICE_TYPE_SOCKET = 0x3003
} zlink_service_type_t;
/** @} */

typedef enum zlink_service_role_t
{
    ZLINK_SERVICE_ROLE_INVALID = 0,
    ZLINK_SERVICE_ROLE_SPOT = 2,
    ZLINK_SERVICE_ROLE_ROUTER = 3,
    ZLINK_SERVICE_ROLE_DEALER = 4,
    ZLINK_SERVICE_ROLE_PUB = 5,
    ZLINK_SERVICE_ROLE_SUB = 6
} zlink_service_role_t;

/**
 * @brief Create a Discovery instance with a fixed service view.
 *
 * The service type and service name are fixed at creation time and cannot be
 * changed. All subscribe/get/count queries operate within that one logical
 * service view.
 *
 * @param ctx           Context handle.
 * @param service_type  Service family for this handle.
 * @param service_name  Fixed logical service name for this handle.
 * @return Discovery handle, or NULL on failure.
 */
ZLINK_EXPORT void *zlink_discovery_new (void *ctx,
                                        zlink_service_type_t service_type,
                                        const char *service_name);

/**
 * @brief Connect Discovery to a Registry bootstrap/control endpoint.
 *
 * Discovery learns the Registry broadcast and topology-uplink endpoints from
 * this bootstrap connection and configures its internal sockets automatically.
 */
ZLINK_EXPORT int zlink_discovery_connect_registry (
  void *discovery, const char *registry_endpoint);

ZLINK_EXPORT int zlink_discovery_set_value (void *discovery_, int64_t value_);
ZLINK_EXPORT int zlink_discovery_get_value (void *discovery_,
                                            int64_t *value_out_);
ZLINK_EXPORT int zlink_discovery_set_metadata (void *discovery_,
                                               const void *data_,
                                               size_t size_);
ZLINK_EXPORT int zlink_discovery_get_metadata (void *discovery_,
                                               zlink_msg_t *metadata_out_);

/**
 * @brief Destroy the discovery instance and release all resources.
 *
 * Destroying a discovery also shuts down every attached service participant
 * that delegated lifecycle ownership to this service view.
 */
ZLINK_EXPORT int zlink_discovery_destroy (void **discovery_p);

/******************************************************************************/
/*  SPOT PUB/SUB API                                                          */
/******************************************************************************/

/* SPOT -------------------------------------------------------------------- */

/**
 * @brief Create a unified SPOT facade over an existing SPOT node.
 *
 * The returned handle borrows `node`, lazily creates side sockets as needed,
 * and must be released with `zlink_spot_destroy()`. Destroying the facade does
 * not destroy the underlying node.
 */
ZLINK_EXPORT void *zlink_spot_new (void *node);

/** @brief Destroy a unified SPOT handle. */
ZLINK_EXPORT int zlink_spot_destroy (void **spot_p);

/* SPOT Node --------------------------------------------------------------- */

/**
 * @brief Create a SPOT node runtime for topology, discovery, and lifecycle.
 *
 * SPOT node handles own the internal pub/sub runtime that backs
 * `zlink_spot_new(node)` but do not expose the generic data-plane facade
 * directly.
 */
ZLINK_EXPORT void *zlink_spot_node_new (void *ctx);

/**
 * @brief Destroy a SPOT node and release all resources.
 *
 * Attached spot nodes are normally shut down by `zlink_discovery_destroy()`.
 */
ZLINK_EXPORT int zlink_spot_node_destroy (void **node_p);

/** @brief Bind the SPOT node to an endpoint. */
ZLINK_EXPORT int zlink_spot_node_bind (void *node, const char *endpoint);

/**
 * @brief Connect to a peer SPOT node endpoint (mesh topology).
 *
 * Returns EFSM if discovery is already attached.
 */
ZLINK_EXPORT int zlink_spot_node_connect_peer (void *node,
                                               const char *peer_endpoint);

/**
 * @brief Disconnect from a peer SPOT node endpoint.
 *
 * Returns EFSM if discovery is already attached.
 */
ZLINK_EXPORT int zlink_spot_node_disconnect_peer (
  void *node, const char *peer_endpoint);

/**
 * @brief Attach a Discovery instance for discovery-owned peer connection.
 *
 * After attach, the node takes its service identity from the discovery and
 * discovery destroy owns participant shutdown.
 */
ZLINK_EXPORT int zlink_spot_node_attach_discovery (void *node,
                                                   void *discovery);

typedef enum zlink_spot_role_t
{
    ZLINK_SPOT_ROLE_PUB = 1,
    ZLINK_SPOT_ROLE_SUB = 2
} zlink_spot_role_t;

/******************************************************************************/
/*  Service Monitor / Topology API                                            */
/******************************************************************************/

typedef enum zlink_service_kind_t
{
    ZLINK_SERVICE_KIND_DISCOVERY = 1,
    ZLINK_SERVICE_KIND_SPOT_SUB = 3,
    ZLINK_SERVICE_KIND_SPOT_PUB = 4,
    ZLINK_SERVICE_KIND_SOCKET = 5
} zlink_service_kind_t;

typedef uint32_t zlink_discovery_monitor_event_mask_t;
typedef uint32_t zlink_spot_monitor_event_mask_t;
typedef uint32_t zlink_service_event_detail_mask_t;
typedef uint32_t zlink_service_monitor_event_mask_t;

#define ZLINK_DISCOVERY_MONITOR_EVENT_READY_CHANGED                          \
    ((zlink_discovery_monitor_event_mask_t) (1u << 0))
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

#define ZLINK_SPOT_MONITOR_EVENT_READY_CHANGED                               \
    ((zlink_spot_monitor_event_mask_t) (1u << 0))
#define ZLINK_SPOT_MONITOR_EVENT_PEER_UP                                     \
    ((zlink_spot_monitor_event_mask_t) (1u << 2))
#define ZLINK_SPOT_MONITOR_EVENT_PEER_DOWN                                   \
    ((zlink_spot_monitor_event_mask_t) (1u << 3))
#define ZLINK_SPOT_MONITOR_EVENT_ERROR                                       \
    ((zlink_spot_monitor_event_mask_t) (1u << 4))
#define ZLINK_SPOT_MONITOR_EVENT_SUB_FILTER_APPLIED                          \
    ((zlink_spot_monitor_event_mask_t) (1u << 13))
#define ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY_CHANGED                  \
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

#define ZLINK_MONITOR_EVENT_PEER_UP ZLINK_SPOT_MONITOR_EVENT_PEER_UP
#define ZLINK_MONITOR_EVENT_PEER_DOWN ZLINK_SPOT_MONITOR_EVENT_PEER_DOWN
#define ZLINK_MONITOR_EVENT_ERROR ZLINK_DISCOVERY_MONITOR_EVENT_ERROR
#define ZLINK_DISCOVERY_SERVICE_UP ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_UP
#define ZLINK_DISCOVERY_SERVICE_DOWN ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_DOWN
#define ZLINK_DISCOVERY_PROVIDERS_CHANGED ZLINK_DISCOVERY_MONITOR_EVENT_PROVIDERS_CHANGED
#define ZLINK_SPOT_SUB_FILTER_APPLIED ZLINK_SPOT_MONITOR_EVENT_SUB_FILTER_APPLIED
#define ZLINK_SPOT_PUB_QUEUE_FULL ZLINK_SPOT_MONITOR_EVENT_PUB_QUEUE_FULL
#define ZLINK_SPOT_PUB_QUEUE_DRAINED ZLINK_SPOT_MONITOR_EVENT_PUB_QUEUE_DRAINED
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
  const zlink_service_event_t *event_, void *userdata_);

typedef zlink_service_event_t zlink_service_monitor_event_t;
typedef zlink_service_event_detail_mask_t
  zlink_service_monitor_event_detail_mask_t;

typedef struct zlink_service_monitor_open_options_t
{
    zlink_service_monitor_event_mask_t events;
} zlink_service_monitor_open_options_t;

typedef enum zlink_monitor_target_kind_t
{
    ZLINK_MONITOR_TARGET_SOCKET = 1,
    ZLINK_MONITOR_TARGET_DISCOVERY = 2,
    ZLINK_MONITOR_TARGET_SPOT = 4,
    ZLINK_MONITOR_TARGET_SPOT_NODE = 5
} zlink_monitor_target_kind_t;

#define ZLINK_SERVICE_MONITOR_EVENT_ERROR ZLINK_DISCOVERY_MONITOR_EVENT_ERROR
#define ZLINK_SERVICE_MONITOR_EVENT_CLOSED ZLINK_DISCOVERY_MONITOR_EVENT_CLOSED
#define ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_READY_CHANGED                  \
    ZLINK_DISCOVERY_MONITOR_EVENT_READY_CHANGED
#define ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_UP                      \
    ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_UP
#define ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_DOWN                    \
    ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_DOWN
#define ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_PROVIDERS_CHANGED              \
    ZLINK_DISCOVERY_MONITOR_EVENT_PROVIDERS_CHANGED
#define ZLINK_SERVICE_MONITOR_EVENT_SPOT_READY_CHANGED                       \
    ZLINK_SPOT_MONITOR_EVENT_READY_CHANGED
#define ZLINK_SERVICE_MONITOR_EVENT_SPOT_FILTER_APPLIED                       \
    ZLINK_SPOT_MONITOR_EVENT_SUB_FILTER_APPLIED
#define ZLINK_SERVICE_MONITOR_EVENT_SPOT_SUBSCRIPTION_READY_CHANGED           \
    ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY_CHANGED
#define ZLINK_SERVICE_MONITOR_EVENT_SPOT_PUB_DELIVERY_READY_CHANGED          \
    ZLINK_SPOT_MONITOR_EVENT_PUB_DELIVERY_READY_CHANGED
#define ZLINK_SERVICE_MONITOR_EVENT_SPOT_SUB_DELIVERY_READY_CHANGED           \
    ZLINK_SPOT_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED
#define ZLINK_SERVICE_MONITOR_EVENT_SPOT_FIRST_DELIVERY_READY_CHANGED         \
    ZLINK_SPOT_MONITOR_EVENT_PUB_FIRST_DELIVERY_READY_CHANGED
#define ZLINK_SERVICE_MONITOR_EVENT_ALL                                       \
    ((zlink_service_monitor_event_mask_t)                                     \
      (ZLINK_SERVICE_MONITOR_EVENT_ERROR                                      \
       | ZLINK_SERVICE_MONITOR_EVENT_CLOSED                                   \
       | ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_READY_CHANGED                  \
       | ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_UP                     \
       | ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_DOWN                   \
       | ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_PROVIDERS_CHANGED              \
       | ZLINK_SERVICE_MONITOR_EVENT_SPOT_READY_CHANGED                       \
       | ZLINK_SERVICE_MONITOR_EVENT_SPOT_FILTER_APPLIED                      \
       | ZLINK_SERVICE_MONITOR_EVENT_SPOT_SUBSCRIPTION_READY_CHANGED          \
       | ZLINK_SERVICE_MONITOR_EVENT_SPOT_PUB_DELIVERY_READY_CHANGED          \
       | ZLINK_SERVICE_MONITOR_EVENT_SPOT_SUB_DELIVERY_READY_CHANGED          \
       | ZLINK_SERVICE_MONITOR_EVENT_SPOT_FIRST_DELIVERY_READY_CHANGED))

ZLINK_EXPORT void *zlink_service_monitor_open (
  void *target_,
  const zlink_service_monitor_open_options_t *options_);

ZLINK_EXPORT int zlink_service_monitor_handler (
  void *monitor_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_);

ZLINK_EXPORT int zlink_service_monitor_recv (
  void *monitor_,
  zlink_service_monitor_event_t *out_,
  zlink_send_flags_t flags_);

typedef enum zlink_spot_node_state_t
{
    ZLINK_SPOT_NODE_STATE_IDLE = 1,
    ZLINK_SPOT_NODE_STATE_CONNECTING = 2,
    ZLINK_SPOT_NODE_STATE_PARTIAL_READY = 3,
    ZLINK_SPOT_NODE_STATE_READY = 4,
    ZLINK_SPOT_NODE_STATE_ERROR = 5
} zlink_spot_node_state_t;

typedef struct zlink_spot_node_status_t
{
    char service_name[256];
    char local_endpoint[256];
    zlink_routing_id_t node_routing_id;
    zlink_spot_node_state_t state;
    uint32_t configured_peer_count;
    uint32_t active_peer_count;
    uint32_t connected_peer_count;
    uint32_t subject_count;
    uint32_t ready_subject_count;
    int32_t last_error;
    uint64_t last_changed_ms;
} zlink_spot_node_status_t;

typedef enum zlink_spot_peer_source_t
{
    ZLINK_SPOT_PEER_SOURCE_MANUAL = 1,
    ZLINK_SPOT_PEER_SOURCE_DISCOVERY = 2,
    ZLINK_SPOT_PEER_SOURCE_MIXED = 3
} zlink_spot_peer_source_t;

typedef enum zlink_spot_peer_state_t
{
    ZLINK_SPOT_PEER_STATE_CONFIGURED = 1,
    ZLINK_SPOT_PEER_STATE_CONNECTING = 2,
    ZLINK_SPOT_PEER_STATE_CONNECTED = 3
} zlink_spot_peer_state_t;

typedef struct zlink_spot_node_peer_entry_t
{
    char service_name[256];
    char local_endpoint[256];
    char peer_endpoint[256];
    zlink_spot_peer_source_t source;
    zlink_spot_peer_state_t state;
    uint64_t connected_since_ms;
    uint64_t last_changed_ms;
} zlink_spot_node_peer_entry_t;

typedef struct zlink_spot_node_peer_filter_t
{
    char peer_endpoint[256];
    zlink_spot_peer_source_t source;
    zlink_spot_peer_state_t state;
} zlink_spot_node_peer_filter_t;

typedef struct zlink_spot_node_subject_entry_t
{
    zlink_spot_role_t role;
    char subject[256];
    uint32_t subject_kind;
    uint32_t ready_peer_count;
    uint32_t active_peer_count;
    uint64_t last_changed_ms;
} zlink_spot_node_subject_entry_t;

typedef struct zlink_spot_node_subject_filter_t
{
    zlink_spot_role_t role;
    char subject[256];
    uint32_t subject_kind;
} zlink_spot_node_subject_filter_t;

typedef enum zlink_registry_state_t
{
    ZLINK_REGISTRY_STATE_IDLE = 1,
    ZLINK_REGISTRY_STATE_ACTIVE = 2,
    ZLINK_REGISTRY_STATE_DEGRADED = 3,
    ZLINK_REGISTRY_STATE_ERROR = 4
} zlink_registry_state_t;

typedef struct zlink_registry_status_t
{
    uint32_t registry_id;
    char bind_endpoint[256];
    zlink_registry_state_t state;
    uint32_t topology_entry_count;
    uint32_t peer_registry_count;
    uint32_t connected_peer_registry_count;
    uint64_t list_seq;
    int32_t last_error;
    uint64_t last_changed_ms;
} zlink_registry_status_t;

typedef struct zlink_registry_service_summary_entry_t
{
    zlink_service_kind_t service_kind;
    zlink_service_role_t service_role;
    char service_name[256];
    uint32_t total_count;
    uint32_t connecting_count;
    uint32_t ready_count;
    uint32_t error_count;
    uint32_t stopped_count;
    uint64_t last_reported_ms;
} zlink_registry_service_summary_entry_t;

typedef struct zlink_registry_service_summary_filter_t
{
    zlink_service_kind_t service_kind;
    zlink_service_role_t service_role;
    char service_name[256];
} zlink_registry_service_summary_filter_t;

typedef struct zlink_member_peer_entry_t
{
    zlink_service_type_t service_type;
    uint16_t service_role;
    char service_name[256];
    char endpoint[256];
    zlink_routing_id_t routing_id;
    int64_t value;
} zlink_member_peer_entry_t;

ZLINK_EXPORT int zlink_spot_node_status_snapshot (
  void *node_,
  zlink_spot_node_status_t *out_);
ZLINK_EXPORT int zlink_spot_node_peers_snapshot (
  void *node_,
  zlink_spot_node_peer_entry_t *entries_,
  size_t *count_);
ZLINK_EXPORT int zlink_spot_node_peers_query (
  void *node_,
  const zlink_spot_node_peer_filter_t *filter_,
  zlink_spot_node_peer_entry_t *entries_,
  size_t *count_);
ZLINK_EXPORT int zlink_spot_node_subjects_snapshot (
  void *node_,
  const zlink_spot_node_subject_filter_t *filter_,
  zlink_spot_node_subject_entry_t *entries_,
  size_t *count_);
ZLINK_EXPORT int zlink_registry_status_snapshot (
  void *registry_,
  zlink_registry_status_t *out_);
ZLINK_EXPORT int zlink_registry_service_summary_snapshot (
  void *registry_,
  const zlink_registry_service_summary_filter_t *filter_,
  zlink_registry_service_summary_entry_t *entries_,
  size_t *count_);
ZLINK_EXPORT int zlink_registry_member_peers (
  void *registry_,
  zlink_service_type_t service_type_,
  const char *service_name_,
  zlink_member_peer_entry_t *entries_,
  size_t *count_);
ZLINK_EXPORT int zlink_registry_member_peer_metadata (
  void *registry_,
  zlink_service_type_t service_type_,
  const char *service_name_,
  uint16_t service_role_,
  const char *endpoint_,
  zlink_msg_t *metadata_out_);
ZLINK_EXPORT int zlink_discovery_member_peers (void *discovery_,
                                               zlink_member_peer_entry_t *entries_,
                                               size_t *count_);
ZLINK_EXPORT int zlink_discovery_member_peer_metadata (
  void *discovery_,
  uint16_t service_role_,
  const char *endpoint_,
  zlink_msg_t *metadata_out_);

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
    zlink_service_role_t service_role;
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
    zlink_service_role_t service_role;
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

typedef short zlink_poller_event_mask_t;

typedef struct zlink_pollitem_t
{
    void *socket;
    zlink_fd_t fd;
    short events;
    short revents;
} zlink_pollitem_t;

typedef struct zlink_poller_event_t
{
    void *socket;
    zlink_fd_t fd;
    void *user_data;
    short events;
} zlink_poller_event_t;

#ifndef ZLINK_POLLIN
#define ZLINK_POLLIN 1
#endif
#ifndef ZLINK_POLLOUT
#define ZLINK_POLLOUT 2
#endif
#ifndef ZLINK_POLLERR
#define ZLINK_POLLERR 4
#endif
#ifndef ZLINK_POLLPRI
#define ZLINK_POLLPRI 8
#endif
#ifndef ZLINK_POLLITEMS_DFLT
#define ZLINK_POLLITEMS_DFLT 16
#endif
#ifndef ZLINK_HAVE_POLLER
#define ZLINK_HAVE_POLLER 1
#endif

ZLINK_EXPORT int zlink_poll (zlink_pollitem_t *items_,
                             int nitems_,
                             long timeout_);

ZLINK_EXPORT void *zlink_poller_new (void);
ZLINK_EXPORT int zlink_poller_destroy (void **poller_p_);
ZLINK_EXPORT int zlink_poller_size (void *poller_);
ZLINK_EXPORT int zlink_poller_add (void *poller_,
                                   void *socket_,
                                   void *user_data_,
                                   short events_);
ZLINK_EXPORT int zlink_poller_modify (void *poller_,
                                      void *socket_,
                                      short events_);
ZLINK_EXPORT int zlink_poller_remove (void *poller_, void *socket_);
ZLINK_EXPORT int zlink_poller_add_fd (void *poller_,
                                      zlink_fd_t fd_,
                                      void *user_data_,
                                      short events_);
ZLINK_EXPORT int zlink_poller_modify_fd (void *poller_,
                                         zlink_fd_t fd_,
                                         short events_);
ZLINK_EXPORT int zlink_poller_remove_fd (void *poller_, zlink_fd_t fd_);
ZLINK_EXPORT int zlink_poller_wait (void *poller_,
                                    zlink_poller_event_t *event_,
                                    long timeout_);
ZLINK_EXPORT int zlink_poller_wait_all (void *poller_,
                                        zlink_poller_event_t *events_,
                                        int n_events_,
                                        long timeout_);

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
#endif

#endif
