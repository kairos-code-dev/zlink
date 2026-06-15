/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_NODE_HPP_INCLUDED__
#define __ZLINK_SPOT_NODE_HPP_INCLUDED__

#include "core/ctx.hpp"
#include "core/msg.hpp"
#include "core/signaler.hpp"
#include "core/socket_poller.hpp"
#include "core/thread.hpp"
#include "services/common/service_public_api.hpp"
#include "services/common/service_mode_state.hpp"
#include "services/common/service_runtime_base.hpp"
#include "services/discovery/discovery.hpp"
#include "services/spot/dispatch/spot_internal_receiver.hpp"
#include "services/spot/node/spot_node_state.hpp"
#include "services/spot/node/spot_peer_state.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/mutex.hpp"

#include <atomic>
#include <condition_variable>
#include <map>
#include <deque>
#include <memory>
#include <stddef.h>
#include <set>
#include <string>
#include <vector>

struct spot_handle_t;

namespace zlink
{
class socket_base_t;
class spot_pub_t;
class spot_sub_t;
class spot_internal_receiver_t;
struct spot_runtime_t;
struct spot_node_access_t;

class spot_node_t : public discovery_observer_t
{
  public:
    typedef spot_node_option_setting_t option_setting_t;
    typedef spot_node_pub_defaults_t pub_defaults_t;
    typedef spot_node_sub_defaults_t sub_defaults_t;

    spot_node_t (ctx_t *ctx_, zlink_spot_node_mode_t mode_);
    ~spot_node_t ();

    bool check_tag () const;
    service_public_api_guard_t &public_api_guard () { return _public_api; }
    service_mode_state_t &mode_state () { return _mode_state; }
    const service_mode_state_t &mode_state () const { return _mode_state; }
    zlink_spot_node_mode_t spot_node_mode () const { return _spot_node_mode; }
    bool pubsub_enabled () const;
    bool routed_enabled () const;
    int set_node_routing_id (const void *data_, size_t size_);
    int node_routing_id (zlink_routing_id_t *out_) const;
    bool spot_owner_route_synced () const;
    bool actor_route_sync_enabled () const;
    int bind_actor_route (const char *actor_id_, const void *value_, size_t value_size_);
    int unbind_actor_route (const char *actor_id_);

    int set_pub_bind (const char *endpoint_);
    int set_router_bind (const char *endpoint_);
    int connect_peer_pub (const char *peer_pub_endpoint_);
    int disconnect_peer_pub (const char *peer_pub_endpoint_);
    int disconnect_peer_pub_rid (const zlink_routing_id_t *target_node_rid_);
    int connect_router_channel_peer (const char *channel_name_, const char *endpoint_);
    int connect_router_channel_peer_rid (const char *channel_name_,
                                         const zlink_routing_id_t *peer_rid_,
                                         const char *endpoint_);
    int disconnect_router_channel_peer (const char *channel_name_, const char *endpoint_);
    int disconnect_router_channel_peer_rid (const char *channel_name_,
                                            const zlink_routing_id_t *peer_rid_);
    int attach_router_channel_discovery (const char *channel_name_, discovery_t *discovery_);
    int attach_discovery (discovery_t *discovery_);
    int attach_channel_dealer (discovery_t *discovery_, socket_base_t *dealer_);
    int attach_channel_dealer_manual (const char *channel_name_, socket_base_t *dealer_);
    int attach_pub_ingress (socket_base_t *pub_);
    int try_register_spot_facade (spot_handle_t *spot_);
    void unregister_spot_facade (spot_handle_t *spot_);
    bool is_last_spot_facade_for_logical_state (spot_handle_t *spot_);
    std::shared_ptr<spot_logical_state_t> create_user_spot_state ();
    std::shared_ptr<spot_logical_state_t> entry_spot_state ();
    std::shared_ptr<spot_logical_state_t> lookup_spot_state (const zlink_routing_id_t *spot_rid_);
    std::shared_ptr<spot_logical_state_t>
    get_or_new_spot_state (const zlink_routing_id_t *spot_rid_, bool *created_out_);
    bool publish_get_or_new_spot_state (const std::shared_ptr<spot_logical_state_t> &state_);
    void cancel_get_or_new_spot_state (const std::shared_ptr<spot_logical_state_t> &state_);
    void remove_spot_state_if_unfacaded (const std::shared_ptr<spot_logical_state_t> &state_);
    void snapshot_spot_states (std::vector<std::shared_ptr<spot_logical_state_t>> *out_) const;
    int fanout_local_publish (const zlink_routing_id_t *source_rid_,
                              const char *topic_id_,
                              zlink_msg_t *parts_,
                              size_t part_count_);
    int update_spot_routing_id (spot_handle_t *spot_, const void *data_, size_t size_);
    void lock_entry_spot_rid ();
    int set_tls_server (const char *cert_, const char *key_);
    int set_tls_client (const char *ca_cert_, const char *hostname_, int trust_system_);
    int set_send_ready_handler (zlink_send_ready_handler_fn handler_, void *userdata_);
    int send_ready_fd (zlink_fd_t *fd_out_) const;
    void drain_send_ready_signal ();
    void notify_send_ready_recovery ();
    void dispatch_send_ready_handler ();
    static void
    dispatch_logical_send_ready_handler (const std::shared_ptr<spot_logical_state_t> &state_);
    int set_pub_option (int option_, const void *optval_, size_t optvallen_);
    int set_sub_option (int option_, const void *optval_, size_t optvallen_);
    int set_node_option (int option_, const void *optval_, size_t optvallen_);
    int get_node_option (int option_, void *optval_, size_t *optvallen_) const;
    pub_defaults_t load_pub_defaults () const;
    sub_defaults_t load_sub_defaults () const;

    spot_pub_t *create_spot_pub ();
    spot_sub_t *create_spot_sub ();
    spot_sub_t *ensure_default_sub ();
    spot_sub_t *default_sub () const;
    void remove_spot_pub (spot_pub_t *pub_);
    void remove_spot_sub (spot_sub_t *sub_);

    int destroy ();

    // discovery_observer_t
    void on_service_update (const std::string &channel_name_) ZLINK_OVERRIDE;
    void on_discovery_shutdown_requested (discovery_t *discovery_) ZLINK_OVERRIDE;
    void on_discovery_destroyed (discovery_t *discovery_) ZLINK_OVERRIDE;

    std::string public_endpoint () const;
    bool has_local_filtered_subs () const;
    int replay_subscriptions_if_active_peers ();
    std::string first_active_peer_endpoint () const;
    int ensure_healthy () const;
    void debug_mark_fault (int err_);
    void untrack_owned_socket (const socket_base_t *socket_);
    bool owns_socket (const socket_base_t *socket_) const;
    void snapshot_raw_subscription_filters (std::set<std::string> *out_) const;
    bool
    update_aggregate_subscription (const std::string &raw_filter_, bool pattern_, bool subscribe_);
    int update_logical_spot_subscription (const std::string &raw_filter_,
                                          bool pattern_,
                                          bool subscribe_);
    void snapshot_subscription_subjects (std::vector<spot_sub_t::subject_descriptor_t> *out_) const;
    void snapshot_subject_summary_entries (
      std::vector<spot_node_summary_state_t::subject_snapshot_entry_t> *out_) const;
    void snapshot_status_subject_counts (uint32_t *subject_count_out_,
                                         uint32_t *ready_subject_count_out_) const;
    int snapshot_status (zlink_spot_node_status_t *out_);
    int snapshot_peers (const zlink_spot_node_peer_filter_t *filter_,
                        std::vector<zlink_spot_node_peer_entry_t> *out_) const;
    int snapshot_subjects (const zlink_spot_node_subject_filter_t *filter_,
                           std::vector<zlink_spot_node_subject_entry_t> *out_) const;
    int snapshot_internal_sockets (const zlink_spot_node_socket_filter_t *filter_,
                                   std::vector<zlink_spot_node_socket_entry_t> *out_) const;
    void notify_pub_delivery_ready_ack (const std::string &target_endpoint_,
                                        const std::string &subject_,
                                        const std::string &ack_source_id_,
                                        bool subscribe_);
    bool external_route_id_for_peer_endpoint (const std::string &peer_endpoint_,
                                              std::string *out_) const;
    bool peer_has_positive_weight (const zlink_routing_id_t *peer_rid_) const;
    socket_base_t *select_service_router (const std::string &channel_name_);
    socket_base_t *service_pub_socket (const std::string &channel_name_) const;
    int service_subscribe_recv (zlink_routing_id_t *source_rid_out_,
                                zlink_msg_t **parts_out_,
                                size_t *part_count_out_,
                                char *topic_id_out_,
                                size_t *topic_id_len_out_,
                                zlink_recv_flags_t flags_);
    int service_subscription_event_recv (zlink_routing_id_t *source_rid_out_,
                                         int *subscribed_out_,
                                         char *topic_id_out_,
                                         size_t *topic_id_len_out_,
                                         zlink_recv_flags_t flags_);
    ctx_t *ctx () const;
    mutex_t &sync ();
    spot_runtime_t *runtime () const { return _runtime; }
    bool is_shutting_down () const;
    socket_base_t *create_socket (int socket_type_) const;
    void track_owned_socket (socket_base_t *socket_);
    int destroy_attachment (uint64_t attachment_id_);
    int destroy_attachment_async (uint64_t attachment_id_);
    spot_internal_receiver_t *ensure_internal_receiver ();
    spot_internal_receiver_t *internal_receiver () const;
    std::set<actor_handle_t *> &actor_handles ();
    std::map<std::string, actor_handle_t *> &actors_by_id ();
    uint64_t &next_actor_generation ();
    void wake_control_task ();
    void submit_pub_summary (spot_pub_t *pub_, uint16_t state_, int error_code_);
    void submit_sub_summary (spot_sub_t *sub_, uint16_t state_, int error_code_);
    int send_subscription_update (const std::string &raw_filter_, bool subscribe_);
    int send_ready_ack_update (const std::string &target_endpoint_,
                               const std::string &raw_filter_,
                               const std::string &ack_source_id_,
                               bool subscribe_);
    void schedule_subscription_replay ();
    void note_local_sub_filters_changed (bool had_filters_, bool has_filters_);
    bool has_active_peers () const;
    void notify_subscription_forwarded (const std::string &raw_filter_);
    void mark_subject_changed (const std::string &subject_, uint32_t subject_kind_);
    std::string summary_channel_name () const;
    void snapshot_active_peer_endpoints (std::set<std::string> *out_) const;
    void snapshot_tls_client_config (std::string *ca_out_,
                                     std::string *host_out_,
                                     int *trust_system_out_) const;
    void snapshot_tls_server_config (std::string *cert_out_, std::string *key_out_) const;
    void mark_bound_endpoint_and_server_tls_locked (const std::string &bound_endpoint_);
    void snapshot_router_bind_endpoint (std::string *out_) const;
    int bind_endpoint (const char *endpoint_);
    void mark_mesh_client_tls_locked ();
    int close_owned_socket (socket_base_t *&socket_, int timeout_ms_);
    int close_owned_socket_and_wait (socket_base_t *&socket_, int timeout_ms_);
    static bool recv_ctrl_reply (socket_base_t *socket_, int *out_errno_);
    static int
    apply_tls_server (socket_base_t *socket_, const std::string &cert_, const std::string &key_);
    static int apply_tls_client (socket_base_t *socket_,
                                 const std::string &ca_cert_,
                                 const std::string &hostname_,
                                 int trust_system_);

  private:
    typedef spot_node_summary_state_t summary_state_t;
    typedef spot_node_aggregate_subscription_state_t aggregate_subscription_state_t;
    typedef spot_node_discovery_binding_state_t discovery_binding_state_t;
    typedef spot_node_tls_state_t tls_state_t;
    typedef spot_node_endpoint_state_t endpoint_state_t;
    typedef spot_node_handle_state_t handle_state_t;
    typedef spot_node_actor_state_t actor_state_t;
    typedef spot_node_attachment_monitor_handle_t attachment_monitor_handle_t;
    typedef spot_node_service_discovery_topology_t service_discovery_topology_t;
    typedef spot_node_service_discovery_socket_plan_t service_discovery_socket_plan_t;
    typedef spot_node_service_attachment_t service_attachment_t;
    typedef spot_node_service_attachment_state_t service_attachment_state_t;
    typedef spot_node_service_attachments_t service_attachments_t;

    static void control_task (void *arg_);

    service_attachment_state_t &service_attachments ();
    const service_attachment_state_t &service_attachments () const;
    const std::string &sub_fanout_endpoint () const;
    void control_tick ();
    int ensure_control_task_running ();
    bool can_suspend_control_task () const;
    int destroy_handles ();
    int destroy_internal_receiver ();
    void close_control_sockets ();
    void stop_data_plane_sockets ();
    int start_data_plane ();
    int send_data_plane_command (const char *verb_, const char *arg_ = NULL) const;
    int wait_facade_peer (socket_base_t *socket_) const;
    int wait_owned_socket_removals (int timeout_ms_);
    spot_pub_t *create_spot_pub_with_defaults (const pub_defaults_t &defaults_,
                                               bool node_owned_default_);
    spot_sub_t *create_spot_sub_with_defaults (const sub_defaults_t &defaults_,
                                               bool node_owned_default_);
    int apply_pub_defaults (spot_pub_t *pub_, const pub_defaults_t &defaults_);
    int apply_sub_defaults (spot_sub_t *sub_, const sub_defaults_t &defaults_);
    int resolve_advertise_endpoint (const char *advertise_endpoint_, std::string *out_) const;
    void refresh_local_fanout_hwm ();
    void refresh_discovery_peers ();
    void refresh_connected_peer_endpoints ();
    void emit_pending_subscription_replays ();
    void submit_spot_owner_summary (const std::shared_ptr<spot_logical_state_t> &state_,
                                    uint16_t state,
                                    int error_code_);
    void submit_spot_owner_summary_for_rid (const zlink_routing_id_t &rid_,
                                            zlink_spot_kind_t spot_kind_,
                                            uint16_t state_,
                                            int error_code_);
    void submit_stopped_summaries ();
    void refresh_existing_summaries ();
    void refresh_sub_peer_summaries (bool has_active_peers, bool lost_transition);
    std::shared_ptr<spot_logical_state_t> create_logical_spot_state_locked (
      bool entry_, const zlink_routing_id_t *spot_rid_ = NULL, bool publish_ = true);
    bool spot_owner_summary_publishable_locked () const;
    void schedule_subscription_ready_refresh ();
    void schedule_pub_delivery_ready_refresh ();
    void clear_peer_readiness_locked (
      std::vector<std::pair<std::string, uint32_t>> *pub_ready_updates_out_);
    void queue_all_subscription_ready_filters ();
    void queue_subscription_ready_filter (const std::string &raw_filter_);
    void emit_pending_subscription_ready_events ();
    void emit_pending_pub_delivery_ready_events ();
    std::string first_connected_peer_endpoint () const;
    uint32_t max_pub_delivery_ready_count_locked () const;
    void publish_mesh_pub_hwm_hint_locked ();
    void notify_pub_first_delivery_ready_settled (const std::string &subject_,
                                                  uint32_t ready_count_);
    int ensure_registered ();
    int unregister_registered ();
    int apply_service_subscription_filters ();
    void refresh_service_discovery_attachments ();
    void refresh_router_channel_discovery_peers ();
    void snapshot_service_discovery_topology (discovery_t *discovery_,
                                              const std::string &channel_name_,
                                              std::vector<provider_info_t> *provider_scratch_,
                                              service_discovery_topology_t *out_) const;
    service_discovery_socket_plan_t
    plan_service_discovery_sockets_locked (const std::string &channel_name_,
                                           const service_discovery_topology_t &topology_);
    void install_service_discovery_sockets (const std::string &channel_name_,
                                            const service_discovery_socket_plan_t &plan_,
                                            const std::set<std::string> &current_filters_);
    void sync_service_discovery_topology (const std::string &channel_name_,
                                          const service_discovery_topology_t &topology_);
    void replay_pending_service_discovery_filters (const std::string &channel_name_,
                                                   const std::set<std::string> &current_filters_);
    void notify_service_subscribe_readable ();
    int validate_destroyable_handles_locked () const;
    void begin_destroy_detach_phase (
      discovery_t **discovery_out_,
      std::map<std::string, discovery_t *> *service_discoveries_out_,
      std::map<std::string, discovery_t *> *channel_dealer_discoveries_out_,
      std::vector<std::string> *active_peer_endpoints_out_,
      std::string *bound_endpoint_out_);
    void clear_service_attachment_runtime_locked (
      std::deque<attachment_monitor_handle_t> *monitors_out_);
    void close_attachment_monitors (std::deque<attachment_monitor_handle_t> *monitors_);
    void reset_spot_discovery_state_locked ();

    static bool validate_public_endpoint (const std::string &endpoint_);
    ctx_t *_ctx;
    uint32_t _tag;

    mutable mutex_t _sync;
    std::condition_variable_any _spot_creation_cv;

    spot_runtime_t *_runtime;
    zlink_spot_node_mode_t _spot_node_mode;
    zlink_routing_id_t _node_routing_id;
    spot_peer_state_t _peer_state;
    std::atomic<zlink_send_ready_handler_fn> _send_ready_handler;
    std::atomic<void *> _send_ready_handler_userdata;
    signaler_t _send_ready_signaler;
    bool _send_ready_signal_armed;
    mutable service_public_api_guard_t _public_api;
    service_mode_state_t _mode_state;
    service_runtime_base_t _lifecycle;

    summary_state_t _summary_state;
    aggregate_subscription_state_t _aggregate_subscriptions;
    discovery_binding_state_t _discovery_state;
    tls_state_t _tls_state;
    endpoint_state_t _endpoint_state;
    handle_state_t _handle_state;
    actor_state_t _actor_state;
    service_attachments_t _service_attachments;
    ZLINK_NON_COPYABLE_NOR_MOVABLE (spot_node_t)
};
}

#endif
