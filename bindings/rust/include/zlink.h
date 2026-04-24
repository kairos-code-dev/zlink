/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_H_INCLUDED__
#define __ZLINK_H_INCLUDED__

/*  Version macros for compile-time API version detection                     */
#define ZLINK_VERSION_MAJOR 5
#define ZLINK_VERSION_MINOR 3
#define ZLINK_VERSION_PATCH 4

#define ZLINK_MAKE_VERSION(major, minor, patch)                                  \
    ((major) *10000 + (minor) *100 + (patch))
#define ZLINK_VERSION                                                            \
    ZLINK_MAKE_VERSION (ZLINK_VERSION_MAJOR, ZLINK_VERSION_MINOR, ZLINK_VERSION_PATCH)

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdio.h>

#if !defined(__cplusplus)
#include <stdbool.h>
#endif

#include "zlink_enum.h"
#include "zlink_errno.h"

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

/**
 * @brief Public declarations for the helper substrate layer.
 *
 * The helper substrate layer exposes the `*_part` primitives. These entry
 * points are the low-level building blocks intended for bindings
 * implementations that need part-by-part control over send and recv paths.
 */

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
#define ZLINK_SPOT_WORKER_THREADS_DFLT 0
#define ZLINK_CTX_AUTO_HWM_ENABLE_DFLT 1
#define ZLINK_CTX_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB_DFLT 128

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
ZLINK_EXPORT zlink_close_result_t zlink_ctx_term (void *context_);

/**
 * @brief Shut down the context immediately.
 *
 * Interrupts any blocking calls with ETERM.
 * zlink_ctx_term() must still be called for final cleanup.
 *
 * @param context_  Context handle.
 * @return 0 on success, -1 on failure (errno is set).
 */
ZLINK_EXPORT zlink_close_result_t zlink_ctx_shutdown (void *context_);

/**
 * @brief Set a context option.
 * @param context_  Context handle.
 * @param option_   Option name (ZLINK_IO_THREADS, ZLINK_MAX_SOCKETS, etc.).
 * @param optval_   Option value.
 * @return 0 on success, -1 on failure (errno is set).
 */
ZLINK_EXPORT zlink_config_result_t zlink_ctx_set (void *context_,
                                zlink_ctx_option_t option_,
                                int optval_);

/**
 * @brief Get a context option.
 * @param context_  Context handle.
 * @param option_   Option name.
 * @return Option value, or -1 on failure (errno is set).
 */
ZLINK_EXPORT int zlink_ctx_get (void *context_, zlink_ctx_option_t option_,
                                zlink_config_result_t *error_out_);

/******************************************************************************/
/*  0MQ message definition.                                                   */
/******************************************************************************/
/* ========== Message type and helpers ========== */
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

#define ZLINK_MSG_METADATA_KEY_USER_MIN 0x0100
#define ZLINK_MSG_METADATA_VALUE_MAX 65535

/** @brief Initialize an empty message. Must be closed with zlink_msg_close(). */
ZLINK_EXPORT zlink_config_result_t zlink_msg_init (zlink_msg_t *msg_);

/** @brief Initialize a message of the given size. */
ZLINK_EXPORT zlink_config_result_t zlink_msg_init_size (zlink_msg_t *msg_, size_t size_);

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
ZLINK_EXPORT zlink_config_result_t zlink_msg_init_data (
  zlink_msg_t *msg_, void *data_, size_t size_, zlink_free_fn *ffn_, void *hint_);

/**
 * @brief Release this message object's ownership of its content.
 *
 * The message becomes invalid after this call. For reference-counted message
 * storage, the underlying buffer is released only when the last owning message
 * is closed or consumed by send.
 */
ZLINK_EXPORT zlink_config_result_t zlink_msg_close (zlink_msg_t *msg_);

/**
 * @brief Move message content from src_ to dest_ without copying payload.
 *
 * Ownership is transferred to @p dest_. The source becomes an empty message.
 * Existing shared/reference-counted state moves with the message; move does
 * not increment any reference count.
 */
ZLINK_EXPORT zlink_config_result_t zlink_msg_move (zlink_msg_t *dest_, zlink_msg_t *src_);

/**
 * @brief Copy a message from src_ to dest_.
 *
 * Small inline messages are copied by value. Large or externally stored
 * messages share the same underlying storage and are tracked internally by
 * reference count rather than duplicating the payload buffer.
 */
ZLINK_EXPORT zlink_config_result_t zlink_msg_copy (zlink_msg_t *dest_, zlink_msg_t *src_);

/**
 * @brief Adopt ownership from src_ into dest_ without an extra init+move step.
 *
 * This is intended for bindings that already have storage for @p dest_ and
 * need to take ownership of a freshly received native message efficiently.
 * Unlike @ref zlink_msg_move, @p dest_ must not currently own an initialized
 * message.
 *
 * On success, @p dest_ owns the original content and @p src_ becomes an empty
 * initialized message.
 */
ZLINK_EXPORT zlink_config_result_t zlink_msg_adopt (zlink_msg_t *dest_, zlink_msg_t *src_);

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
ZLINK_EXPORT int zlink_msg_refcnt (const zlink_msg_t *msg_,
                                   zlink_config_result_t *error_out_);

/** @brief Get a string message property (e.g. metadata). */
ZLINK_EXPORT const char *zlink_msg_gets (const zlink_msg_t *msg_,
                                     const char *property_);

/******************************************************************************/
/*  0MQ socket definition.                                                    */
/******************************************************************************/
/*  Backward-compatible aliases for send/recv flag enum values.               */
#define ZLINK_DONTWAIT           ZLINK_SEND_FLAGS_DONTWAIT
#define ZLINK_SEND_FLAG_DONTWAIT ZLINK_SEND_FLAGS_DONTWAIT

#define ZLINK_NULL 0
#define ZLINK_PLAIN 1

/******************************************************************************/
/*  0MQ socket events and monitoring                                          */
/******************************************************************************/

#define ZLINK_DISCONNECT_UNKNOWN ZLINK_DISCONNECT_REASON_UNKNOWN
#define ZLINK_DISCONNECT_HANDSHAKE_FAILED ZLINK_DISCONNECT_REASON_HANDSHAKE_FAILED
#define ZLINK_DISCONNECT_TRANSPORT_ERROR ZLINK_DISCONNECT_REASON_TRANSPORT_ERROR
#define ZLINK_DISCONNECT_CTX_TERM ZLINK_DISCONNECT_REASON_CTX_TERM

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

typedef void (*zlink_stream_packet_handler_fn) (
  void *stream_,
  const zlink_routing_id_t *source_rid_,
  zlink_msg_t *header_,
  zlink_msg_t *body_,
  void *userdata_);

typedef void (*zlink_send_ready_handler_fn) (void *subject_, void *userdata_);

typedef void (*zlink_reply_handler_fn) (
  zlink_request_result_t result_,
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

typedef void (*zlink_spot_handler_fn) (
  const zlink_routing_id_t *source_rid_,
  const zlink_routing_id_t *spot_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_,
  void *userdata_);

typedef struct zlink_spot_dispatch_info_t
{
    zlink_spot_dispatch_event_t event;
    zlink_spot_dispatch_subject_kind_t subject_kind;
    void *subject;
} zlink_spot_dispatch_info_t;

typedef void (*zlink_spot_dispatch_event_handler_fn) (
  void *spot_,
  const zlink_spot_dispatch_info_t *info_,
  void *userdata_);

typedef enum zlink_part_flag_t
{
    ZLINK_PART_FINAL = 0,
    ZLINK_PART_MORE = 1
} zlink_part_flag_t;

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
 * - raw `STREAM`
 *
 * Unsupported subjects fail with errno=ENOTSUP.
 */
ZLINK_EXPORT zlink_handler_result_t zlink_recv_handler (
  void *s_, zlink_socket_msg_handler_fn handler_, void *userdata_);

ZLINK_EXPORT zlink_handler_result_t zlink_stream_packet_handler (
  void *stream_, zlink_stream_packet_handler_fn handler_, void *userdata_);

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
 * Send-ready is independent from receive callback mode. `ZLINK_POLLOUT`
 * observes the same send-recovery readiness axis and may be registered on the
 * same subject. A readiness signal only means it is worth retrying send, not
 * that the retry is guaranteed to succeed.
 *
 * Unsupported subjects fail with errno=ENOTSUP.
 */
ZLINK_EXPORT zlink_handler_result_t zlink_send_ready_handler (
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
ZLINK_EXPORT zlink_close_result_t zlink_close (void *s_);

ZLINK_EXPORT zlink_config_result_t zlink_set_option (void *handle_,
                                    zlink_option_t option_,
                                    const void *optval_,
                                    size_t optvallen_);
ZLINK_EXPORT zlink_config_result_t zlink_get_option (void *handle_,
                                    zlink_option_t option_,
                                    void *optval_,
                                    size_t *optvallen_);
ZLINK_EXPORT zlink_config_result_t zlink_set_routing_id (void *handle_,
                                        const void *data_,
                                        size_t size_);
ZLINK_EXPORT zlink_config_result_t zlink_get_routing_id (void *handle_,
                                        zlink_routing_id_t *out_);
ZLINK_EXPORT zlink_config_result_t zlink_set_tls_server (void *handle_,
                                        const char *cert_,
                                        const char *key_,
                                        int require_client_cert_);
ZLINK_EXPORT zlink_config_result_t zlink_set_tls_client (void *handle_,
                                        const char *ca_cert_,
                                        const char *hostname_,
                                        int trust_system_);
ZLINK_EXPORT zlink_config_result_t zlink_set_router_option (
  void *handle_,
  zlink_router_option_t option_,
  const void *optval_,
  size_t optvallen_);
ZLINK_EXPORT zlink_config_result_t zlink_get_router_option (
  void *handle_,
  zlink_router_option_t option_,
  void *optval_,
  size_t *optvallen_);
ZLINK_EXPORT zlink_config_result_t zlink_set_dealer_option (
  void *handle_,
  zlink_dealer_option_t option_,
  const void *optval_,
  size_t optvallen_);
ZLINK_EXPORT zlink_config_result_t zlink_set_stream_option (
  void *handle_,
  zlink_stream_option_t option_,
  const void *optval_,
  size_t optvallen_);
ZLINK_EXPORT zlink_config_result_t zlink_get_stream_option (
  void *handle_,
  zlink_stream_option_t option_,
  void *optval_,
  size_t *optvallen_);

ZLINK_EXPORT zlink_config_result_t zlink_set_spot_option (void *handle_,
                                         zlink_spot_option_t option_,
                                         const void *optval_,
                                         size_t optvallen_);
ZLINK_EXPORT zlink_config_result_t zlink_get_spot_option (void *handle_,
                                         zlink_spot_option_t option_,
                                         void *optval_,
                                         size_t *optvallen_);

/*
 * PUB/XPUB socket, spot-pub, spotnode-pub:
 * - zlink_pub_option_t for pub-specific options
 * - use zlink_set_option()/zlink_get_option() for common options
 */
ZLINK_EXPORT zlink_config_result_t zlink_set_pub_option (void *handle_,
                                        zlink_pub_option_t option_,
                                        const void *optval_,
                                        size_t optvallen_);
ZLINK_EXPORT zlink_config_result_t zlink_get_pub_option (void *handle_,
                                        zlink_pub_option_t option_,
                                        void *optval_,
                                        size_t *optvallen_);

/*
 * SUB/XSUB socket, spot-sub, spotnode-sub:
 * - zlink_sub_option_t for sub-specific options
 * - use zlink_set_option()/zlink_get_option() for common options
 */
ZLINK_EXPORT zlink_config_result_t zlink_set_sub_option (void *handle_,
                                        zlink_sub_option_t option_,
                                        const void *optval_,
                                        size_t optvallen_);
ZLINK_EXPORT zlink_config_result_t zlink_get_sub_option (void *handle_,
                                        zlink_sub_option_t option_,
                                        void *optval_,
                                        size_t *optvallen_);

/*
 * SpotNode service-level batching options.
 */
ZLINK_EXPORT zlink_config_result_t zlink_set_spot_node_option (
  void *handle_,
  zlink_spot_node_option_t option_,
  const void *optval_,
  size_t optvallen_);
ZLINK_EXPORT zlink_config_result_t zlink_get_spot_node_option (
  void *handle_,
  zlink_spot_node_option_t option_,
  void *optval_,
  size_t *optvallen_);

/**
 * @brief Bind a socket to an address.
 * @param addr_  Endpoint (e.g. @c tcp://host:5555, @c inproc://name).
 */
ZLINK_EXPORT zlink_bind_result_t zlink_bind (void *s_, const char *addr_);

/** @brief Connect a socket to a remote address. */
ZLINK_EXPORT zlink_connect_result_t zlink_connect (void *s_, const char *addr_);

/** @brief Unbind a socket from an address. */
ZLINK_EXPORT zlink_connect_result_t zlink_unbind (void *s_, const char *addr_);

/** @brief Disconnect a socket from a remote address. */
ZLINK_EXPORT zlink_connect_result_t zlink_disconnect (void *s_, const char *addr_);

/**
 * @brief Disconnect the connected peer whose source routing id matches peer_rid_.
 *
 * Success means the matching pipe was asked to terminate asynchronously.
 * Discovery-attached sockets reject this manual lifecycle change.
 */
ZLINK_EXPORT zlink_connect_result_t zlink_disconnect_rid (
  void *s_,
  const zlink_routing_id_t *peer_rid_);

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
ZLINK_EXPORT zlink_config_result_t zlink_socket_attach_discovery (void *socket_,
                                                 void *discovery_);

/* ========== Helper substrate layer (*_part) ========== */
ZLINK_EXPORT zlink_submit_result_t zlink_send_part (
  void *s_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_);

/* ========== Helper substrate layer (*_part) ========== */
ZLINK_EXPORT zlink_submit_result_t zlink_send_part_rid (
  void *s_,
  const zlink_routing_id_t *target_rid_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_);

/* ========== Helper substrate layer (*_part) ========== */
ZLINK_EXPORT zlink_submit_result_t zlink_dealer_request_part (
  void *dealer_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_,
  uint32_t timeout_ms_,
  zlink_reply_handler_fn handler_,
  void *userdata_);

/* ========== Helper substrate layer (*_part) ========== */
ZLINK_EXPORT zlink_submit_result_t zlink_router_request_part (
  void *router_,
  const zlink_routing_id_t *peer_rid_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_,
  uint32_t timeout_ms_,
  zlink_reply_handler_fn handler_,
  void *userdata_);

/* ========== Helper substrate layer (*_part) ========== */
ZLINK_EXPORT zlink_submit_result_t zlink_router_reply_part (
  void *router_,
  const zlink_routing_id_t *peer_rid_,
  uint64_t request_seq_,
  zlink_msg_t *part_,
  zlink_part_flag_t part_flag_);

/* ========== Helper substrate layer (*_part) ========== */
ZLINK_EXPORT zlink_recv_result_t zlink_router_recv_part (
  void *router_,
  const zlink_routing_id_t **source_node_rid_out_,
  const zlink_routing_id_t **source_spot_rid_out_,
  uint64_t *request_seq_out_,
  zlink_msg_t *part_out_,
  zlink_part_flag_t *has_more_out_,
  zlink_recv_flags_t flags_);

/* ========== Helper substrate layer (*_part) ========== */
ZLINK_EXPORT zlink_submit_result_t zlink_spot_send_channel_part (
  void *spot_,
  const char *channel_name_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_);

/* ========== Helper substrate layer (*_part) ========== */
ZLINK_EXPORT zlink_submit_result_t zlink_spot_publish_part (
  void *spot_,
  const char *service_name_,
  const char *topic_id_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_);

/* ========== Helper substrate layer (*_part) ========== */
ZLINK_EXPORT zlink_recv_result_t zlink_spot_subscribe_part (
  void *spot_,
  const zlink_routing_id_t **source_rid_out_,
  char *service_name_buf_,
  size_t service_name_capacity_,
  size_t *service_name_len_out_,
  char *topic_id_buf_,
  size_t topic_id_capacity_,
  size_t *topic_id_len_out_,
  zlink_msg_t *part_out_,
  zlink_part_flag_t *has_more_out_,
  zlink_recv_flags_t flags_);

ZLINK_EXPORT zlink_recv_result_t zlink_spot_subscription_event_recv (
  void *spot_,
  const zlink_routing_id_t **source_rid_out_,
  int *subscribed_out_,
  char *service_name_buf_,
  size_t service_name_capacity_,
  size_t *service_name_len_out_,
  char *topic_id_buf_,
  size_t topic_id_capacity_,
  size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_);

/* ========== Helper substrate layer (*_part) ========== */
ZLINK_EXPORT zlink_submit_result_t zlink_spot_request_channel_part (
  void *spot_,
  const char *channel_name_,
  zlink_msg_t *part_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_,
  uint32_t timeout_ms_);

/* ========== Helper substrate layer (*_part) ========== */
ZLINK_EXPORT zlink_submit_result_t zlink_spot_request_spot_part (
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *part_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_,
  uint32_t timeout_ms_);

/* ========== Helper substrate layer (*_part) ========== */
ZLINK_EXPORT zlink_submit_result_t zlink_spot_request_router_part (
  void *spot_,
  const zlink_routing_id_t *peer_rid_,
  zlink_msg_t *part_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_,
  uint32_t timeout_ms_);

/* ========== Helper substrate layer (*_part) ========== */
ZLINK_EXPORT zlink_submit_result_t zlink_spot_send_spot_part (
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_);

/* ========== Helper substrate layer (*_part) ========== */
ZLINK_EXPORT zlink_submit_result_t zlink_spot_reply_spot_part (
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  uint64_t request_seq_,
  zlink_msg_t *part_,
  zlink_part_flag_t part_flag_);

/* ========== Helper substrate layer (*_part) ========== */
ZLINK_EXPORT zlink_submit_result_t zlink_spot_reply_router_part (
  void *spot_,
  const zlink_routing_id_t *peer_rid_,
  uint64_t request_seq_,
  zlink_msg_t *part_,
  zlink_part_flag_t part_flag_);

/* ========== Helper substrate layer (callback dispatch) ========== */
ZLINK_EXPORT zlink_handler_result_t zlink_spot_handler (
  void *spot_, zlink_spot_handler_fn handler_, void *userdata_);

ZLINK_EXPORT zlink_handler_result_t zlink_spot_dispatch_event_handler (
  void *spot_,
  zlink_spot_dispatch_event_handler_fn handler_,
  void *userdata_);

ZLINK_EXPORT int zlink_spot_channel_reply_progress_from (void *spot_,
                                                         void *dealer_);

ZLINK_EXPORT zlink_config_result_t zlink_socket_set_channel_name (
  void *socket_,
  const char *channel_name_);

ZLINK_EXPORT zlink_config_result_t zlink_socket_get_channel_name (
  void *socket_,
  char *channel_name_buf_,
  size_t channel_name_capacity_,
  size_t *channel_name_len_out_);

/* ========== Helper substrate layer (*_part) ========== */
ZLINK_EXPORT zlink_recv_result_t zlink_spot_recv_part (
  void *spot_,
  const zlink_routing_id_t **source_node_rid_out_,
  const zlink_routing_id_t **source_spot_rid_out_,
  uint64_t *request_seq_out_,
  zlink_msg_t *part_out_,
  zlink_part_flag_t *has_more_out_,
  zlink_recv_flags_t flags_);

/* ========== Helper substrate layer (*_part) ========== */
ZLINK_EXPORT zlink_submit_result_t zlink_router_request_spot_part (
  void *router_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *part_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_,
  uint32_t timeout_ms_);

/* ========== Helper substrate layer (*_part) ========== */
ZLINK_EXPORT zlink_submit_result_t zlink_router_reply_spot_part (
  void *router_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  uint64_t request_seq_,
  zlink_msg_t *part_,
  zlink_part_flag_t part_flag_);

/* ========== Helper substrate layer (*_part) ========== */
ZLINK_EXPORT zlink_submit_result_t zlink_router_send_spot_part (
  void *router_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_);

/* ========== Helper substrate layer (*_part) ========== */
ZLINK_EXPORT zlink_recv_result_t zlink_recv_part (
  void *s_,
  const zlink_routing_id_t **source_rid_out_,
  zlink_msg_t *part_out_,
  zlink_part_flag_t *has_more_out_,
  zlink_recv_flags_t flags_);

/* ========== Helper substrate layer (*_part) ========== */
ZLINK_EXPORT zlink_submit_result_t zlink_publish_part (
  void *subject_,
  const char *topic_id_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_);

/* ========== Helper substrate layer (subscription config) ========== */
ZLINK_EXPORT zlink_config_result_t zlink_set_subscription (void *handle_,
                                          const char *filter_);
ZLINK_EXPORT zlink_config_result_t zlink_unset_subscription (void *handle_,
                                            const char *filter_);
ZLINK_EXPORT zlink_config_result_t zlink_subscription_at (void *handle_,
                                         size_t index_,
                                         char *filter_out_,
                                         size_t *filter_len_inout_,
                                         int *is_pattern_out_);

/* ========== Helper substrate layer (*_part) ========== */
ZLINK_EXPORT zlink_recv_result_t zlink_subscribe_part (
  void *sub_,
  const zlink_routing_id_t **source_rid_out_,
  char *topic_id_buf_,
  size_t topic_id_capacity_,
  size_t *topic_id_len_out_,
  zlink_msg_t *part_out_,
  zlink_part_flag_t *has_more_out_,
  zlink_recv_flags_t flags_);

/* ========== Helper substrate layer (*_part) ========== */
ZLINK_EXPORT zlink_recv_result_t zlink_xpub_recv_part (
  void *xpub_,
  const zlink_routing_id_t **source_rid_out_,
  int *subscribed_out_,
  char *topic_id_buf_,
  size_t topic_id_capacity_,
  size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_);

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

typedef struct zlink_monitor_snapshot_t
{
    zlink_monitor_source_kind_t source_kind;
    zlink_monitor_state_mask_t state_flags;
    zlink_monitor_snapshot_detail_mask_t detail_flags;
    uint64_t snd_pending_msgs;
    uint64_t rcv_pending_msgs;
    uint32_t auto_hwm_enabled;
    uint32_t auto_hwm_role;
    uint32_t auto_hwm_managed_connections;
    uint32_t auto_hwm_active_hwm_connections;
    uint32_t auto_hwm_planning_transport_connections;
    uint32_t auto_hwm_base_floor_per_connection;
    int32_t auto_hwm_applied_sndhwm;
    int32_t auto_hwm_applied_rcvhwm;
    int32_t auto_hwm_requested_sndbuf;
    int32_t auto_hwm_requested_rcvbuf;
    int32_t auto_hwm_effective_sndbuf;
    int32_t auto_hwm_effective_rcvbuf;
    uint64_t auto_hwm_total_memory_budget_bytes;
    uint64_t auto_hwm_queue_budget_bytes;
    uint64_t auto_hwm_transport_budget_bytes;
    uint64_t auto_hwm_runtime_reserve_bytes;
    uint64_t auto_hwm_group_budget_bytes;
    uint64_t auto_hwm_group_message_slots;
    uint64_t auto_hwm_effective_message_bytes;
} zlink_monitor_snapshot_t;

/**
 * @brief Open and return a socket monitor handle directly.
 * @param events_  Event bitmask.
 * @return Monitor handle, or NULL on failure.
 */
ZLINK_EXPORT void *zlink_socket_monitor_open (
  void *s_, const zlink_socket_monitor_open_options_t *options_);

ZLINK_EXPORT zlink_handler_result_t zlink_socket_monitor_handler (
  void *monitor_,
  zlink_socket_monitor_handler_fn handler_,
  void *userdata_);

ZLINK_EXPORT zlink_recv_result_t zlink_socket_monitor_recv (
  void *monitor_,
  zlink_socket_monitor_event_t *out_,
  zlink_recv_flags_t flags_);

/** @brief Read the current snapshot for a socket or service monitor handle. */
ZLINK_EXPORT zlink_config_result_t zlink_monitor_snapshot (void *monitor_,
                                         zlink_monitor_snapshot_t *out_);

ZLINK_EXPORT zlink_close_result_t zlink_monitor_close (void **monitor_p_);

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
ZLINK_EXPORT zlink_bind_result_t zlink_registry_bind (void *registry,
                                      const char *pub_endpoint,
                                      const char *router_endpoint);

/** @brief Set the registry unique ID (used for cluster configuration). */
ZLINK_EXPORT zlink_config_result_t zlink_registry_set_id (void *registry, uint32_t registry_id);

/** @brief Add a peer registry PUB endpoint (for cluster synchronization). */
ZLINK_EXPORT zlink_config_result_t zlink_registry_add_peer (void *registry,
                                      const char *peer_pub_endpoint);

/**
 * @brief Set heartbeat interval and timeout.
 * Defaults are 5000 ms for the heartbeat interval and 15000 ms for
 * the timeout.
 * @param interval_ms  Heartbeat send interval in milliseconds.
 * @param timeout_ms   Expiry time when no heartbeat is received, in
 *                     milliseconds.
 */
ZLINK_EXPORT zlink_config_result_t zlink_registry_set_heartbeat (void *registry,
                                           uint32_t interval_ms,
                                           uint32_t timeout_ms);

/**
 * @brief Set the service list broadcast interval in milliseconds.
 * Default is 30000 ms.
 */
ZLINK_EXPORT zlink_config_result_t zlink_registry_set_broadcast_interval (void *registry,
                                                    uint32_t interval_ms);

/** @brief Destroy the registry and release all resources. */
ZLINK_EXPORT zlink_close_result_t zlink_registry_destroy (void **registry_p);

/* Discovery ---------------------------------------------------------------- */

/** @name Service registration types */
/** @{ */
/** @} */

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
ZLINK_EXPORT zlink_connect_result_t zlink_discovery_connect_registry (
  void *discovery, const char *registry_endpoint);

/**
 * @brief Set the auto-connect target policy used by DEALER sockets.
 *
 * This policy applies to the current Discovery service view. The default
 * policy is `ZLINK_DISCOVERY_DEALER_PEER_MODE_ROUTER`.
 */
ZLINK_EXPORT zlink_config_result_t zlink_discovery_set_dealer_peer_mode (
  void *discovery_, zlink_discovery_dealer_peer_mode_t mode_);

/**
 * @brief Resolve the current owner node for a logical SPOT routing id.
 *
 * This lookup is scoped to the current Discovery service view. Discovery may
 * answer from its local cache or refresh against Registry as needed. On
 * success, the returned node routing id should be paired with the original
 * `spot_rid_` when using router-side SPOT addressing APIs.
 *
 * This helper is intended for send/request destination lookup. Reply paths
 * must continue to use the concrete source addresses that were delivered with
 * the incoming request.
 *
 * @param discovery_          Discovery handle.
 * @param spot_rid_           Logical SPOT routing id to resolve.
 * @param owner_node_rid_out_ Output buffer for the current owner node id.
 * @return `ZLINK_CONFIG_OK` on success, or another
 *         `zlink_config_result_t` value on failure (`zlink_errno()` is set).
 */
ZLINK_EXPORT zlink_config_result_t zlink_discovery_resolve_spot (
  void *discovery_,
  const zlink_routing_id_t *spot_rid_,
  zlink_routing_id_t *owner_node_rid_out_);

ZLINK_EXPORT zlink_config_result_t zlink_discovery_set_value (void *discovery_, int64_t value_);
ZLINK_EXPORT zlink_config_result_t zlink_discovery_get_value (void *discovery_,
                                            int64_t *value_out_);
ZLINK_EXPORT zlink_config_result_t zlink_discovery_set_metadata (void *discovery_,
                                               const void *data_,
                                               size_t size_);
ZLINK_EXPORT zlink_config_result_t zlink_discovery_get_metadata (void *discovery_,
                                               zlink_msg_t *metadata_out_);

/**
 * @brief Destroy the discovery instance and release all resources.
 *
 * Destroying a discovery also shuts down every attached service participant
 * that delegated lifecycle ownership to this service view.
 */
ZLINK_EXPORT zlink_close_result_t zlink_discovery_destroy (void **discovery_p);

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
ZLINK_EXPORT zlink_close_result_t zlink_spot_destroy (void **spot_p);

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
ZLINK_EXPORT zlink_close_result_t zlink_spot_node_destroy (void **node_p);

/** @brief Bind the SPOT node to an endpoint.
 *
 * Supports port 0 for ephemeral port allocation (e.g. "tcp://127.0.0.1:0").
 * After a successful bind, use zlink_spot_node_status_snapshot() to retrieve
 * the resolved endpoint (local_endpoint field) with the actual assigned port.
 */
ZLINK_EXPORT zlink_bind_result_t zlink_spot_node_bind (void *node, const char *endpoint);

/**
 * @brief Connect to a peer SPOT node endpoint (mesh topology).
 *
 * Returns EBUSY if discovery is already attached.
 */
ZLINK_EXPORT zlink_connect_result_t zlink_spot_node_connect_peer (void *node,
                                               const char *peer_endpoint);

/**
 * @brief Disconnect from a peer SPOT node endpoint.
 *
 * Returns EBUSY if discovery is already attached.
 */
ZLINK_EXPORT zlink_connect_result_t zlink_spot_node_disconnect_peer (
  void *node, const char *peer_endpoint);
ZLINK_EXPORT zlink_connect_result_t zlink_spot_node_disconnect_peer_rid (
  void *node, const zlink_routing_id_t *target_node_rid);

/**
 * @brief Attach a Discovery instance for discovery-owned SPOT peer connection.
 *
 * The discovery must provide a SPOT channel view. After attach, the node takes
 * its mesh identity from that channel view and discovery destroy owns
 * participant shutdown.
 */
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_attach_discovery (void *node,
                                                   void *discovery);

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_attach_channel_dealer (
  void *node_,
  void *discovery_,
  void *dealer_);

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_attach_channel_dealer_manual (
  void *node_,
  const char *channel_name_,
  void *dealer_);

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_attach_pub_ingress (
  void *node_,
  void *pub_);

/******************************************************************************/
/*  Service Monitor / Topology API                                            */
/******************************************************************************/

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

ZLINK_EXPORT void *zlink_service_monitor_open (
  void *target_,
  const zlink_service_monitor_open_options_t *options_);

ZLINK_EXPORT zlink_handler_result_t zlink_service_monitor_handler (
  void *monitor_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_);

ZLINK_EXPORT zlink_recv_result_t zlink_service_monitor_recv (
  void *monitor_,
  zlink_service_monitor_event_t *out_,
  zlink_recv_flags_t flags_);

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

typedef struct zlink_spot_node_peer_entry_t
{
    char service_name[256];
    char local_endpoint[256];
    char peer_endpoint[256];
    zlink_spot_peer_source_t source;
    zlink_spot_peer_state_t state;
    uint32_t weight;
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
    uint32_t weight;
    int64_t value;
} zlink_member_peer_entry_t;

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_status_snapshot (
  void *node_,
  zlink_spot_node_status_t *out_);
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_peers_snapshot (
  void *node_,
  zlink_spot_node_peer_entry_t *entries_,
  size_t *count_);
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_peers_query (
  void *node_,
  const zlink_spot_node_peer_filter_t *filter_,
  zlink_spot_node_peer_entry_t *entries_,
  size_t *count_);
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_subjects_snapshot (
  void *node_,
  const zlink_spot_node_subject_filter_t *filter_,
  zlink_spot_node_subject_entry_t *entries_,
  size_t *count_);
ZLINK_EXPORT zlink_config_result_t zlink_registry_status_snapshot (
  void *registry_,
  zlink_registry_status_t *out_);
ZLINK_EXPORT zlink_config_result_t zlink_registry_service_summary_snapshot (
  void *registry_,
  const zlink_registry_service_summary_filter_t *filter_,
  zlink_registry_service_summary_entry_t *entries_,
  size_t *count_);
ZLINK_EXPORT zlink_config_result_t zlink_registry_member_peers (
  void *registry_,
  zlink_service_type_t service_type_,
  const char *service_name_,
  zlink_member_peer_entry_t *entries_,
  size_t *count_);
ZLINK_EXPORT zlink_config_result_t zlink_registry_member_peer_metadata (
  void *registry_,
  zlink_service_type_t service_type_,
  const char *service_name_,
  uint16_t service_role_,
  const char *endpoint_,
  zlink_msg_t *metadata_out_);
ZLINK_EXPORT zlink_config_result_t zlink_discovery_member_peers (void *discovery_,
                                               zlink_member_peer_entry_t *entries_,
                                               size_t *count_);
ZLINK_EXPORT zlink_config_result_t zlink_discovery_member_peer_metadata (
  void *discovery_,
  uint16_t service_role_,
  const char *endpoint_,
  zlink_msg_t *metadata_out_);

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

ZLINK_EXPORT zlink_config_result_t zlink_registry_topology_snapshot (
  void *registry,
  zlink_registry_topology_entry_t *entries,
  size_t *count);
ZLINK_EXPORT zlink_config_result_t zlink_registry_topology_query (
  void *registry,
  const zlink_registry_topology_filter_t *filter,
  zlink_registry_topology_entry_t *entries,
  size_t *count);

ZLINK_EXPORT void *zlink_registry_query_client_new (void *ctx);
ZLINK_EXPORT zlink_connect_result_t zlink_registry_query_client_connect (void *client,
                                                      const char *endpoint);
ZLINK_EXPORT zlink_config_result_t zlink_registry_query_snapshot (
  void *client,
  const zlink_registry_topology_filter_t *filter,
  zlink_registry_topology_entry_t *entries,
  size_t *count);

ZLINK_EXPORT zlink_close_result_t zlink_registry_query_destroy (void **client_p);

#if defined _WIN32
#if defined _WIN64
typedef unsigned __int64 zlink_fd_t;
#else
typedef unsigned int zlink_fd_t;
#endif
#else
typedef int zlink_fd_t;
#endif

typedef struct zlink_pollitem_t
{
    void *socket;
    zlink_fd_t fd;
    short events;
    short revents;
} zlink_pollitem_t;

typedef struct zlink_poller_event_t
{
    zlink_poller_source_kind_t source_kind;
    void *socket;
    zlink_fd_t fd;
    void *timer;
    void *user_data;
    short events;
} zlink_poller_event_t;

#ifndef ZLINK_HAVE_POLLER
#define ZLINK_HAVE_POLLER 1
#endif

ZLINK_EXPORT int zlink_poll (zlink_pollitem_t *items_,
                             int nitems_,
                             long timeout_,
                             zlink_config_result_t *error_out_);

ZLINK_EXPORT void *zlink_poller_new (void);
ZLINK_EXPORT zlink_close_result_t zlink_poller_destroy (void **poller_p_);
ZLINK_EXPORT int zlink_poller_size (void *poller_,
                                    zlink_config_result_t *error_out_);
ZLINK_EXPORT zlink_config_result_t zlink_poller_add (void *poller_,
                                   void *socket_,
                                   void *user_data_,
                                   short events_);
ZLINK_EXPORT zlink_config_result_t zlink_poller_modify (void *poller_,
                                      void *socket_,
                                      short events_);
ZLINK_EXPORT zlink_config_result_t zlink_poller_remove (void *poller_, void *socket_);
ZLINK_EXPORT zlink_config_result_t zlink_poller_add_fd (void *poller_,
                                      zlink_fd_t fd_,
                                      void *user_data_,
                                      short events_);
ZLINK_EXPORT zlink_config_result_t zlink_poller_add_timer (void *poller_,
                                         void *timer_,
                                         void *user_data_);
ZLINK_EXPORT zlink_config_result_t zlink_poller_modify_fd (void *poller_,
                                         zlink_fd_t fd_,
                                         short events_);
ZLINK_EXPORT zlink_config_result_t zlink_poller_remove_fd (void *poller_, zlink_fd_t fd_);
ZLINK_EXPORT zlink_config_result_t zlink_poller_remove_timer (void *poller_, void *timer_);
ZLINK_EXPORT int zlink_poller_wait (void *poller_,
                                    zlink_poller_event_t *event_,
                                    long timeout_,
                                    zlink_config_result_t *error_out_);
ZLINK_EXPORT int zlink_poller_wait_all (void *poller_,
                                        zlink_poller_event_t *events_,
                                        int n_events_,
                                        long timeout_,
                                        zlink_config_result_t *error_out_);

/** @brief Start a built-in proxy between frontend and backend sockets. */
ZLINK_EXPORT zlink_config_result_t zlink_proxy (void *frontend_, void *backend_, void *capture_);

/** @brief Start a steerable proxy with an additional control socket. */
ZLINK_EXPORT zlink_config_result_t zlink_proxy_steerable (void *frontend_,
                                    void *backend_,
                                    void *capture_,
                                    void *control_);

/** @brief Check if the library supports a given capability (e.g. "ipc", "tls"). */
ZLINK_EXPORT bool zlink_has (const char *capability_);

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
/*  Timers                                                                    */
/******************************************************************************/

typedef void (*zlink_timer_handler_fn) (void *timer_,
                                        uint64_t fire_count_,
                                        void *userdata_);

ZLINK_EXPORT void *zlink_timer_new (void);
ZLINK_EXPORT void *zlink_spot_timer_new (void *spot_);
ZLINK_EXPORT zlink_close_result_t zlink_timer_destroy (void **timer_p_);
ZLINK_EXPORT zlink_config_result_t zlink_timer_start (void *timer_,
                                    uint64_t interval_ns_,
                                    uint64_t repeat_count_);
ZLINK_EXPORT zlink_config_result_t zlink_timer_stop (void *timer_);
ZLINK_EXPORT zlink_recv_result_t zlink_timer_recv (void *timer_,
                                   uint64_t *fire_count_out_);
ZLINK_EXPORT zlink_handler_result_t zlink_timer_handler (void *timer_,
                                      zlink_timer_handler_fn handler_,
                                      void *userdata_);

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
