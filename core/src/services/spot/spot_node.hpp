/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_NODE_HPP_INCLUDED__
#define __ZLINK_SPOT_NODE_HPP_INCLUDED__

#include "core/ctx.hpp"
#include "core/msg.hpp"
#include "core/thread.hpp"
#include "services/common/service_public_api.hpp"
#include "services/common/service_mode_state.hpp"
#include "services/common/service_runtime_base.hpp"
#include "services/discovery/discovery.hpp"
#include "services/spot/spot_internal_receiver.hpp"
#include "services/spot/spot_peer_state.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/mutex.hpp"

#include <atomic>
#include <map>
#include <set>
#include <string>

namespace zlink
{
class socket_base_t;
class spot_pub_t;
class spot_sub_t;
class spot_data_plane_t;
class spot_internal_receiver_t;
struct spot_data_plane_protocol_t;
struct spot_runtime_t;
struct spot_node_access_t;

class spot_node_t : public discovery_observer_t
{
  public:
    typedef spot_node_option_setting_t option_setting_t;
    typedef spot_node_pub_defaults_t pub_defaults_t;
    typedef spot_node_sub_defaults_t sub_defaults_t;

    spot_node_t (ctx_t *ctx_);
    ~spot_node_t ();

    bool check_tag () const;
    service_public_api_guard_t &public_api_guard () { return _public_api; }
    service_mode_state_t &mode_state () { return _mode_state; }
    const service_mode_state_t &mode_state () const { return _mode_state; }

    int bind (const char *endpoint_);
    int connect_peer_pub (const char *peer_pub_endpoint_);
    int disconnect_peer_pub (const char *peer_pub_endpoint_);
    int attach_discovery (discovery_t *discovery_);
    int set_tls_server (const char *cert_, const char *key_);
    int set_tls_client (const char *ca_cert_,
                        const char *hostname_,
                        int trust_system_);
    int set_send_ready_handler (zlink_send_ready_handler_fn handler_,
                                void *userdata_);
    int set_pub_option (int option_,
                        const void *optval_,
                        size_t optvallen_);
    int set_sub_option (int option_,
                        const void *optval_,
                        size_t optvallen_);

    spot_pub_t *create_spot_pub ();
    spot_sub_t *create_spot_sub ();
    spot_pub_t *ensure_default_pub ();
    spot_sub_t *ensure_default_sub ();
    spot_pub_t *default_pub () const;
    spot_sub_t *default_sub () const;
    void remove_spot_pub (spot_pub_t *pub_);
    void remove_spot_sub (spot_sub_t *sub_);

    int destroy ();

    // discovery_observer_t
    void on_service_update (const std::string &service_name_) ZLINK_OVERRIDE;
    void on_discovery_shutdown_requested (discovery_t *discovery_)
      ZLINK_OVERRIDE;
    void on_discovery_destroyed (discovery_t *discovery_) ZLINK_OVERRIDE;

    std::string public_endpoint () const;
    bool has_local_filtered_subs () const;
    int replay_subscriptions_if_active_peers ();
    std::string first_active_peer_endpoint () const;
    int ensure_healthy () const;
    void debug_mark_fault (int err_);
    void untrack_owned_socket (const socket_base_t *socket_);
    void snapshot_raw_subscription_filters (std::set<std::string> *out_) const;
    void snapshot_subscription_subjects (
      std::vector<spot_sub_t::subject_descriptor_t> *out_) const;
    int snapshot_status (zlink_spot_node_status_t *out_) const;
    int snapshot_peers (const zlink_spot_node_peer_filter_t *filter_,
                        std::vector<zlink_spot_node_peer_entry_t> *out_) const;
    int snapshot_subjects (
      const zlink_spot_node_subject_filter_t *filter_,
      std::vector<zlink_spot_node_subject_entry_t> *out_) const;
    void notify_pub_delivery_ready_ack (const std::string &target_endpoint_,
                                        const std::string &subject_,
                                        const std::string &ack_source_id_,
                                        bool subscribe_);

  private:
    friend class spot_pub_t;
    friend class spot_sub_t;
    friend class spot_data_plane_t;
    friend struct spot_node_access_t;

    static void control_task (void *arg_);

    ctx_t *ctx () const { return _ctx; }
    spot_runtime_t *runtime () const { return _runtime; }
    spot_internal_receiver_t *ensure_internal_receiver ();
    spot_internal_receiver_t *internal_receiver () const;
    const std::string &pub_ingress_endpoint () const;
    const std::string &sub_fanout_endpoint () const;
    bool has_active_peers () const;
    void note_local_sub_filters_changed (bool had_filters_,
                                         bool has_filters_);
    void wake_control_task ();
    void schedule_subscription_replay ();
    void control_tick ();
    int ensure_control_task_running ();
    bool can_suspend_control_task () const;
    int destroy_handles ();
    int destroy_internal_receiver ();
    void close_control_sockets ();
    void stop_data_plane_sockets ();
    int start_data_plane ();
    int send_data_plane_command (const char *verb_,
                                 const char *arg_ = NULL) const;
    int wait_facade_peer (socket_base_t *socket_) const;
    void track_owned_socket (socket_base_t *socket_);
    int wait_owned_socket_removals (int timeout_ms_);
    bool is_shutting_down () const;
    int destroy_attachment (uint64_t attachment_id_);
    int destroy_attachment_async (uint64_t attachment_id_);
    spot_pub_t *create_spot_pub_with_defaults (const pub_defaults_t &defaults_,
                                               bool node_owned_default_);
    spot_sub_t *create_spot_sub_with_defaults (const sub_defaults_t &defaults_,
                                               bool node_owned_default_);
    int apply_pub_defaults (spot_pub_t *pub_, const pub_defaults_t &defaults_);
    int apply_sub_defaults (spot_sub_t *sub_, const sub_defaults_t &defaults_);
    int resolve_advertise_endpoint (const char *advertise_endpoint_,
                                    std::string *out_) const;
    void refresh_local_pub_ingress_hwm ();
    void refresh_local_fanout_hwm ();
    void refresh_discovery_peers ();
    void refresh_connected_peer_endpoints ();
    void emit_pending_subscription_replays ();
    std::string summary_service_name () const;
    void submit_pub_summary (spot_pub_t *pub_, uint16_t state_, int error_code_);
    void submit_sub_summary (spot_sub_t *sub_, uint16_t state_, int error_code_);
    void submit_stopped_summaries ();
    void refresh_existing_summaries ();
    void refresh_sub_peer_summaries (bool has_active_peers,
                                     bool lost_transition);
    void schedule_subscription_ready_refresh ();
    void schedule_pub_delivery_ready_refresh ();
    void clear_peer_readiness_locked (
      std::vector<std::pair<std::string, uint32_t> > *pub_ready_updates_out_);
    void queue_all_subscription_ready_filters ();
    void queue_subscription_ready_filter (const std::string &raw_filter_);
    void emit_pending_subscription_ready_events ();
    void emit_pending_pub_delivery_ready_events ();
    std::string first_connected_peer_endpoint () const;
    void notify_subscription_forwarded (const std::string &raw_filter_);
    uint32_t max_pub_delivery_ready_count_locked () const;
    void publish_mesh_pub_budget_hint_locked ();
    void notify_pub_first_delivery_ready_settled (const std::string &subject_,
                                                  uint32_t ready_count_);
    int send_subscription_update (const std::string &raw_filter_,
                                  bool subscribe_);
    int send_ready_ack_update (const std::string &target_endpoint_,
                               const std::string &raw_filter_,
                               const std::string &ack_source_id_,
                               bool subscribe_);
    int ensure_registered ();
    int unregister_registered ();

    static bool validate_public_endpoint (const std::string &endpoint_);
    static bool recv_ctrl_reply (socket_base_t *socket_, int *out_errno_);
    static int apply_tls_server (socket_base_t *socket_,
                                 const std::string &cert_,
                                 const std::string &key_);
    static int apply_tls_client (socket_base_t *socket_,
                                 const std::string &ca_cert_,
                                 const std::string &hostname_,
                                 int trust_system_);

    ctx_t *_ctx;
    uint32_t _tag;

    mutable mutex_t _sync;

    spot_runtime_t *_runtime;

    std::string _bound_endpoint;
    spot_peer_state_t _peer_state;
    std::map<std::string, uint64_t> _subject_last_changed_ms;
    int _last_summary_error;
    uint64_t _summary_last_changed_ms;

    discovery_t *_discovery;
    std::string _discovery_service;
    uint64_t _discovery_seq;
    std::set<std::string> _pending_service_updates;

    bool _registered;
    std::string _advertise_endpoint;
    std::string _registration_uplink_endpoint;

    std::string _tls_cert;
    std::string _tls_key;
    std::string _tls_ca;
    std::string _tls_hostname;
    int _tls_trust_system;
    bool _server_tls_locked;
    bool _mesh_client_tls_locked;
    bool _registration_tls_locked;
    std::atomic<zlink_send_ready_handler_fn> _send_ready_handler;
    std::atomic<void *> _send_ready_handler_userdata;
    service_public_api_guard_t _public_api;

    int _local_pub_ingress_rcvhwm_cfg;
    int _local_fanout_sndhwm_cfg;
    int _local_pub_ingress_rcvhwm_default;
    int _local_fanout_sndhwm_default;
    std::atomic<uint32_t> _local_filtered_sub_count;
    std::atomic<uint32_t> _active_peer_count;

    spot_node_default_handles_t _handle_defaults;
    std::set<spot_pub_t *> _pubs;
    std::set<spot_sub_t *> _subs;
    service_mode_state_t _mode_state;
    service_runtime_base_t _lifecycle;

    friend struct spot_runtime_t;
    friend class spot_data_plane_t;
    friend struct spot_data_plane_protocol_t;
    friend class spot_pub_t;
    friend class spot_sub_t;
    ZLINK_NON_COPYABLE_NOR_MOVABLE (spot_node_t)
};
}

#endif
