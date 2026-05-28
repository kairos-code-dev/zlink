/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_SOCKET_H_INCLUDED
#define ZLINK_SOCKET_H_INCLUDED

#include <zlink/common.h>
#include <zlink/message.h>
#include <zlink/actor.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/*  0MQ socket definition.                                                    */
/******************************************************************************/
#define ZLINK_DONTWAIT ZLINK_SEND_FLAGS_DONTWAIT

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

typedef void (*zlink_actor_join_spot_handler_fn) (
  const zlink_actor_join_result_t *result_,
  zlink_msg_t *parts_,
  size_t part_count_,
  void *userdata_);

typedef void (*zlink_actor_join_entry_spot_handler_fn) (
  const zlink_actor_join_entry_spot_result_t *result_,
  void *userdata_);

typedef void (*zlink_actor_lookup_handler_fn) (
  const zlink_actor_lookup_result_t *result_,
  void *userdata_);

typedef void (*zlink_subscribe_handler_fn) (
  const zlink_routing_id_t *source_rid_,
  const char *topic_,
  size_t topic_len_,
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

ZLINK_EXPORT zlink_config_result_t zlink_stream_attach_actor_gateway (
  void *stream_,
  void *node_);

ZLINK_EXPORT zlink_submit_result_t zlink_stream_bind_actor (
  void *stream_,
  const zlink_routing_id_t *session_rid_,
  const zlink_actor_ref_t *actor_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  uint32_t timeout_ms_);

ZLINK_EXPORT zlink_submit_result_t zlink_stream_unbind_actor (
  void *stream_,
  const zlink_routing_id_t *session_rid_,
  const char *actor_id_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  uint32_t timeout_ms_);

ZLINK_EXPORT zlink_submit_result_t zlink_stream_send_bound_actor_part (
  void *stream_,
  const zlink_routing_id_t *session_rid_,
  const char *actor_id_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_);

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

ZLINK_EXPORT zlink_recv_result_t zlink_dealer_recv_part (
  void *dealer_,
  uint8_t *message_type_out_,
  uint64_t *request_seq_out_,
  zlink_msg_t *part_out_,
  zlink_part_flag_t *has_more_out_,
  zlink_recv_flags_t flags_);

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
  const char *topic_id_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_);

/* ========== Helper substrate layer (*_part) ========== */
ZLINK_EXPORT zlink_recv_result_t zlink_spot_subscribe_part (
  void *spot_,
  const zlink_routing_id_t **source_rid_out_,
  char *topic_id_buf_,
  size_t topic_id_capacity_,
  size_t *topic_id_len_out_,
  zlink_msg_t *part_out_,
  zlink_part_flag_t *has_more_out_,
  zlink_recv_flags_t flags_);

ZLINK_EXPORT zlink_recv_result_t zlink_spot_recv_subscription_event (
  void *spot_,
  const zlink_routing_id_t **source_rid_out_,
  int *subscribed_out_,
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
ZLINK_EXPORT zlink_handler_result_t zlink_spot_dispatch_event_handler (
  void *spot_,
  zlink_spot_dispatch_event_handler_fn handler_,
  void *userdata_);

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

/* SpotNode is topology/configuration only. Direct publish on SpotNode returns
 * ZLINK_SUBMIT_NOT_SUPPORTED with errno ENOTSUP; use Spot for topic publish. */
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


#ifdef __cplusplus
}
#endif

#endif
