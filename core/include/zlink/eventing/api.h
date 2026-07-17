/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_EVENTING_API_H_INCLUDED
#define ZLINK_EVENTING_API_H_INCLUDED

#include <zlink/common.h>
#include <zlink/socket/api.h>
#include <zlink/service/mesh_node.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint64_t event;
    uint64_t value;
    zlink_routing_id_t routing_id;
    char local_addr[256];
    char remote_addr[256];
} zlink_monitor_event_t;

typedef void (*zlink_monitor_handler_fn) (const zlink_monitor_event_t *event_, void *userdata_);

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
ZLINK_EXPORT void zlink_monitor_ignore_handler (const zlink_monitor_event_t *event_,
                                                void *userdata_);

typedef struct zlink_monitor_status_t
{
    /* Snapshot source kind: raw socket. */
    zlink_monitor_source_kind_t source_kind;

    /* Current state bits such as READY, BOUND_READY, and CLOSED. */
    zlink_monitor_state_mask_t state_flags;

    /* Bitmask describing which detail fields are populated. */
    zlink_monitor_status_detail_mask_t detail_flags;

    /* Current pending send message count. */
    uint64_t snd_pending_msgs;

    /* Current pending receive message count. Some sources report an estimate. */
    uint64_t rcv_pending_msgs;

    /* Non-zero when automatic HWM policy is enabled for this source. */
    uint32_t auto_hwm_enabled;

    /* Current automatic HWM profile value. Matches zlink_auto_hwm_profile_t. */
    uint32_t auto_hwm_profile;

    /* Socket role used by the automatic HWM calculation. Diagnostic only. */
    uint32_t auto_hwm_role;

    /* Automatic HWM policy class selected from role and socket type. */
    uint32_t auto_hwm_policy_class;

    /* Unit budget in bytes used to calculate message slots for this socket. */
    uint64_t auto_hwm_unit_budget_bytes;

    /* Message slot cap selected from profile and policy class. */
    uint32_t auto_hwm_size_cap;

    /* Message slot count calculated from the unit budget and message size. */
    uint64_t auto_hwm_socket_message_slots;

    /* Non-zero when a connection-count bucket limited this socket plan. */
    uint32_t auto_hwm_connection_bucket_enabled;

    /* Connection count observed by the automatic HWM bucket planner. */
    uint32_t auto_hwm_connection_bucket_count;

    /* Selected connection bucket index, or UINT32_MAX when no bucket applies. */
    uint32_t auto_hwm_connection_bucket_index;

    /* Selected bucket HWM for a 4 KiB message unit, or 0 when no bucket applies. */
    uint32_t auto_hwm_connection_bucket_hwm_4k;

    /* Non-zero when hysteresis retained the previous connection bucket. */
    uint32_t auto_hwm_connection_bucket_hysteresis_retained;

    /* Message size in bytes used by the automatic HWM calculation. */
    uint64_t auto_hwm_effective_message_bytes;

    /* Current send HWM applied to the socket. */
    int32_t auto_hwm_applied_sndhwm;

    /* Current receive HWM applied to the socket. */
    int32_t auto_hwm_applied_rcvhwm;

    /* Current send buffer size applied to the socket, in bytes. */
    int32_t auto_hwm_effective_sndbuf;

    /* Current receive buffer size applied to the socket, in bytes. */
    int32_t auto_hwm_effective_rcvbuf;

    /* Last automatic HWM recalculation timestamp, in milliseconds. */
    uint64_t auto_hwm_last_recalc_ms;

    /* Last automatic HWM recalculation reason. ZLINK_AUTO_HWM_RECALC_REASON_*. */
    uint32_t auto_hwm_last_recalc_reason;

    /* Ratio of send attempts blocked by backpressure, in ppm. */
    uint32_t auto_hwm_send_blocked_ratio_ppm;

    /* Target send HWM while shrink is deferred, or -1 when none. */
    int32_t auto_hwm_deferred_sndhwm;

    /* Target receive HWM while shrink is deferred, or -1 when none. */
    int32_t auto_hwm_deferred_rcvhwm;
} zlink_monitor_status_t;

/**
 * @brief Open and return a socket monitor handle directly.
 * @param events_  Event bitmask.
 * @return Monitor handle, or NULL on failure.
 */
ZLINK_EXPORT void *zlink_socket_monitor_open (void *s_,
                                              const zlink_socket_monitor_open_options_t *options_);

ZLINK_EXPORT zlink_handler_result_t zlink_socket_monitor_handler (
  void *monitor_, zlink_socket_monitor_handler_fn handler_, void *userdata_);

ZLINK_EXPORT zlink_recv_result_t zlink_socket_monitor_recv (void *monitor_,
                                                            zlink_socket_monitor_event_t *out_,
                                                            zlink_recv_flags_t flags_);

/** @brief Read the current snapshot for a monitor handle. */
ZLINK_EXPORT zlink_config_result_t zlink_monitor_status (void *monitor_,
                                                         zlink_monitor_status_t *out_);

ZLINK_EXPORT zlink_close_result_t zlink_monitor_close (void **monitor_p_);


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
ZLINK_EXPORT int zlink_poller_size (void *poller_, zlink_config_result_t *error_out_);
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
                                    zlink_poller_event_t *events_,
                                    int n_events_,
                                    long timeout_,
                                    zlink_config_result_t *error_out_);
/******************************************************************************/
/*  Timers                                                                    */
/******************************************************************************/

typedef void (*zlink_timer_handler_fn) (void *timer_, uint64_t fire_count_, void *userdata_);

ZLINK_EXPORT void *zlink_timer_new (void);
ZLINK_EXPORT void *zlink_spot_timer_new (void *spot_);
ZLINK_EXPORT zlink_close_result_t zlink_timer_destroy (void **timer_p_);
ZLINK_EXPORT zlink_config_result_t zlink_timer_start (void *timer_,
                                                      uint64_t interval_ns_,
                                                      uint64_t repeat_count_);
ZLINK_EXPORT zlink_config_result_t zlink_timer_stop (void *timer_);
ZLINK_EXPORT zlink_recv_result_t zlink_timer_recv (void *timer_, uint64_t *fire_count_out_);
ZLINK_EXPORT zlink_handler_result_t zlink_timer_handler (void *timer_,
                                                         zlink_timer_handler_fn handler_,
                                                         void *userdata_);

/******************************************************************************/
/*  MeshNode monitor. Contract: core/doc/spec/core/07-monitoring.md          */
/******************************************************************************/

#define ZLINK_MESH_MONITOR_ABI_VERSION 1u
#define ZLINK_MESH_MONITOR_CHANNEL_MAX 255u

typedef uint64_t zlink_mesh_monitor_event_mask_t;

typedef enum zlink_mesh_monitor_event_kind_t {
  ZLINK_MESH_MONITOR_STATE_CHANGED       = 1,
  ZLINK_MESH_MONITOR_PEER_CONNECTING     = 2,
  ZLINK_MESH_MONITOR_PEER_ADMITTED       = 3,
  ZLINK_MESH_MONITOR_PEER_DRAINING       = 4,
  ZLINK_MESH_MONITOR_PEER_CLOSED         = 5,
  ZLINK_MESH_MONITOR_PEER_REJECTED       = 6,
  ZLINK_MESH_MONITOR_CHANNEL_CHANGED     = 7,
  ZLINK_MESH_MONITOR_MESSAGE_SUBMITTED   = 8,
  ZLINK_MESH_MONITOR_MULTICAST_COMMITTED = 9,
  ZLINK_MESH_MONITOR_MULTICAST_DROPPED   = 10,
  ZLINK_MESH_MONITOR_BACKPRESSURED       = 11,
  ZLINK_MESH_MONITOR_OPERATION_COMPLETED = 12,
  ZLINK_MESH_MONITOR_PROTOCOL_ERROR      = 13,
  ZLINK_MESH_MONITOR_CLAIM_REVOKED       = 14
} zlink_mesh_monitor_event_kind_t;

typedef struct zlink_mesh_monitor_open_options_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_mesh_monitor_event_mask_t events;
} zlink_mesh_monitor_open_options_t;

typedef struct zlink_mesh_monitor_event_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_mesh_monitor_event_kind_t kind;
  uint64_t timestamp_ms;
  uint64_t mesh_lifecycle_generation;
  uint64_t mesh_descriptor_revision;
  zlink_mesh_node_state_t mesh_state;
  zlink_routing_id_t peer_rid;
  uint64_t peer_lifecycle_generation;
  uint64_t peer_descriptor_revision;
  zlink_mesh_owner_kind_t owner_kind;
  zlink_routing_id_t spot_rid;
  zlink_actor_ref_t actor;
  char channel_name[ZLINK_MESH_MONITOR_CHANNEL_MAX + 1];
  uint64_t operation_id_high;
  uint64_t operation_id_low;
  uint32_t snapshot_remote_target_count;
  uint32_t admitted_remote_target_count;
  uint32_t dropped_remote_target_count;
  uint32_t unreachable_remote_target_count;
  uint32_t snapshot_local_spot_count;
  uint32_t admitted_local_spot_count;
  uint32_t dropped_local_spot_count;
  int32_t result_code;
  int32_t failure_errno;
} zlink_mesh_monitor_event_t;

typedef struct zlink_mesh_monitor_status_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_mesh_node_state_t state;
  uint64_t peer_admitted;
  uint64_t peer_rejected;
  uint64_t submitted_messages;
  uint64_t completed_operations;
  uint64_t backpressured_submits;
  uint64_t multicast_messages;
  uint64_t multicast_dropped_targets;
  uint64_t active_claims;
  uint64_t pending_application_messages;
  uint64_t pending_infrastructure_messages;
  uint64_t pending_bytes;
} zlink_mesh_monitor_status_t;

typedef void (*zlink_mesh_monitor_handler_fn)(
  const zlink_mesh_monitor_event_t *event,
  void *userdata);

ZLINK_EXPORT void *zlink_mesh_node_monitor_open(
  void *mesh_node,
  const zlink_mesh_monitor_open_options_t *options);
ZLINK_EXPORT zlink_handler_result_t zlink_mesh_node_monitor_handler(
  void *monitor,
  zlink_mesh_monitor_handler_fn handler,
  void *userdata);
ZLINK_EXPORT zlink_recv_result_t zlink_mesh_node_monitor_recv(
  void *monitor,
  zlink_mesh_monitor_event_t *event_out,
  zlink_recv_flags_t flags);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_monitor_status(
  void *monitor,
  zlink_mesh_monitor_status_t *status_out);
ZLINK_EXPORT zlink_close_result_t zlink_mesh_node_monitor_close(void **monitor_p);

#ifdef __cplusplus
}
#endif

#endif
