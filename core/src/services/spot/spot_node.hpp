/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_NODE_HPP_INCLUDED__
#define __ZLINK_SPOT_NODE_HPP_INCLUDED__

#include "core/ctx.hpp"
#include "core/msg.hpp"
#include "core/socket_poller.hpp"
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
#include <deque>
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
    int disconnect_peer_pub_rid (const zlink_routing_id_t *target_node_rid_);
    int attach_discovery (discovery_t *discovery_);
    int attach_channel_dealer (discovery_t *discovery_, socket_base_t *dealer_);
    int attach_channel_dealer_manual (const char *channel_name_,
                                      socket_base_t *dealer_);
    int attach_pub_ingress (socket_base_t *pub_);
    int try_register_spot_facade (spot_handle_t *spot_);
    void unregister_spot_facade (spot_handle_t *spot_);
    int set_tls_server (const char *cert_, const char *key_);
    int set_tls_client (const char *ca_cert_,
                        const char *hostname_,
                        int trust_system_);
    int set_send_ready_handler (zlink_send_ready_handler_fn handler_,
                                void *userdata_);
    int set_weight (uint32_t weight_);
    int get_weight (uint32_t *weight_out_) const;
    int set_pub_option (int option_,
                        const void *optval_,
                        size_t optvallen_);
    int set_sub_option (int option_,
                        const void *optval_,
                        size_t optvallen_);
    int set_node_option (int option_,
                         const void *optval_,
                         size_t optvallen_);
    int get_node_option (int option_,
                         void *optval_,
                         size_t *optvallen_) const;
    pub_defaults_t load_pub_defaults () const;
    sub_defaults_t load_sub_defaults () const;

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
    std::string single_peer_route_endpoint () const;
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
    bool peer_has_positive_weight (const zlink_routing_id_t *peer_rid_) const;
    socket_base_t *select_service_router (const std::string &service_name_);
    socket_base_t *service_pub_socket (const std::string &service_name_) const;
    int service_subscribe_recv (zlink_routing_id_t *source_rid_out_,
                                zlink_msg_t **parts_out_,
                                size_t *part_count_out_,
                                char *service_name_out_,
                                size_t *service_name_len_out_,
                                char *topic_id_out_,
                                size_t *topic_id_len_out_,
                                zlink_recv_flags_t flags_);
    int service_subscription_event_recv (zlink_routing_id_t *source_rid_out_,
                                         int *subscribed_out_,
                                         char *service_name_out_,
                                         size_t *service_name_len_out_,
                                         char *topic_id_out_,
                                         size_t *topic_id_len_out_,
                                         zlink_recv_flags_t flags_);

  private:
    friend class spot_pub_t;
    friend class spot_sub_t;
    friend class spot_data_plane_t;
    friend struct spot_node_access_t;

    struct service_monitor_handle_t;
    struct service_attachment_t;
    struct service_discovery_topology_t;
    struct service_discovery_socket_plan_t;

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
    int apply_service_subscription_filters ();
    void collect_pending_service_discoveries_locked (
      std::vector<std::pair<std::string, discovery_t *> > *out_);
    void refresh_service_discovery_attachments ();
    void snapshot_service_discovery_topology (
      discovery_t *discovery_,
      const std::string &service_name_,
      std::vector<provider_info_t> *provider_scratch_,
      service_discovery_topology_t *out_) const;
    service_discovery_socket_plan_t plan_service_discovery_sockets_locked (
      const std::string &service_name_,
      const service_discovery_topology_t &topology_);
    void install_service_discovery_sockets (
      const std::string &service_name_,
      const service_discovery_socket_plan_t &plan_,
      const std::set<std::string> &current_filters_);
    void sync_service_discovery_topology (
      const std::string &service_name_,
      const service_discovery_topology_t &topology_);
    void replay_pending_service_discovery_filters (
      const std::string &service_name_,
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
      std::deque<service_monitor_handle_t> *monitors_out_);
    void close_service_monitors (
      std::deque<service_monitor_handle_t> *monitors_);
    int validate_socket_service_discovery_attach_locked (
      const std::string &service_name_, discovery_t *discovery_) const;
    void register_service_monitor_locked (
      socket_base_t *owner_socket_,
      void *monitor_handle_,
      const std::string &service_name_);
    void rebuild_service_attachment_caches_locked ();
    void reset_spot_discovery_state_locked ();
    void queue_service_discovery_refresh_locked (
      const std::string &service_name_);
    void remove_service_monitors_by_owner_locked (
      const std::vector<socket_base_t *> &sockets_);
    bool detach_discovered_service_locked (
      discovery_t *discovery_, std::vector<socket_base_t *> *sockets_to_close_out_);

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
    spot_peer_state_t _peer_state;
    uint32_t _weight;
    std::atomic<zlink_send_ready_handler_fn> _send_ready_handler;
    std::atomic<void *> _send_ready_handler_userdata;
    service_public_api_guard_t _public_api;
    service_mode_state_t _mode_state;
    service_runtime_base_t _lifecycle;

    struct summary_state_t
    {
        summary_state_t () : last_summary_error (0), summary_last_changed_ms (0)
        {
        }

        std::map<std::string, uint64_t> subject_last_changed_ms;
        int last_summary_error;
        uint64_t summary_last_changed_ms;
    };

    struct discovery_binding_state_t
    {
        discovery_binding_state_t () :
            discovery (NULL),
            discovery_seq (0),
            registered (false)
        {
        }

        discovery_t *discovery;
        std::string discovery_service;
        uint64_t discovery_seq;
        std::set<std::string> pending_service_updates;
        bool registered;
        std::string advertise_endpoint;
        std::string registration_uplink_endpoint;
    };

    struct tls_state_t
    {
        tls_state_t () :
            tls_trust_system (0),
            server_tls_locked (false),
            mesh_client_tls_locked (false),
            registration_tls_locked (false)
        {
        }

        std::string tls_cert;
        std::string tls_key;
        std::string tls_ca;
        std::string tls_hostname;
        int tls_trust_system;
        bool server_tls_locked;
        bool mesh_client_tls_locked;
        bool registration_tls_locked;
    };

    struct endpoint_state_t
    {
        endpoint_state_t () :
            local_pub_ingress_rcvhwm_cfg (0),
            local_fanout_sndhwm_cfg (0),
            local_pub_ingress_rcvhwm_default (0),
            local_fanout_sndhwm_default (0),
            local_filtered_sub_count (0),
            active_peer_count (0)
        {
        }

        std::string bound_endpoint;
        int local_pub_ingress_rcvhwm_cfg;
        int local_fanout_sndhwm_cfg;
        int local_pub_ingress_rcvhwm_default;
        int local_fanout_sndhwm_default;
        std::atomic<uint32_t> local_filtered_sub_count;
        std::atomic<uint32_t> active_peer_count;
    };

    struct handle_state_t
    {
        spot_node_default_handles_t handle_defaults;
        std::set<spot_pub_t *> pubs;
        std::set<spot_sub_t *> subs;
        std::set<spot_handle_t *> facades;
    };

    struct service_monitor_handle_t
    {
        service_monitor_handle_t () : handle (NULL), owner_socket (NULL)
        {
        }

        void *handle;
        socket_base_t *owner_socket;
        std::string service_name;
    };

    struct service_discovery_topology_t
    {
        std::set<std::string> router_endpoints;
        std::set<std::string> pub_endpoints;
        std::set<std::string> sub_endpoints;

        void clear ()
        {
            router_endpoints.clear ();
            pub_endpoints.clear ();
            sub_endpoints.clear ();
        }

        bool pubsub_active () const
        {
            return !pub_endpoints.empty () && !sub_endpoints.empty ();
        }
    };

    struct service_discovery_socket_plan_t
    {
        std::vector<std::pair<std::string, socket_base_t *> > new_router_sockets;
        socket_base_t *pub_socket;
        socket_base_t *sub_socket;

        service_discovery_socket_plan_t () : pub_socket (NULL), sub_socket (NULL)
        {
        }
    };

    struct service_attachment_t
    {
        struct manual_state_t
        {
            manual_state_t () :
                pub (NULL),
                sub (NULL),
                channel_dealer_discovery (NULL)
            {
            }

            std::vector<socket_base_t *> routers;
            socket_base_t *pub;
            socket_base_t *sub;
            discovery_t *channel_dealer_discovery;
        };

        struct discovered_state_t
        {
            enum auto_sub_replay_state_t
            {
                auto_sub_replay_none = 0,
                auto_sub_replay_initial,
                auto_sub_replay_reconnect
            };

            discovered_state_t () :
                pub (NULL),
                sub (NULL),
                auto_sub_replay_state (auto_sub_replay_none)
            {
            }

            bool has_pubsub () const
            {
                return pub != NULL && sub != NULL && !pub_endpoints.empty ()
                       && !sub_endpoints.empty ();
            }
            uint32_t router_count () const
            {
                return static_cast<uint32_t> (router_endpoints.size ());
            }
            uint32_t pub_count () const
            {
                return static_cast<uint32_t> (pub_endpoints.size ());
            }
            uint32_t sub_count () const
            {
                return static_cast<uint32_t> (sub_endpoints.size ());
            }
            bool needs_sub_replay () const
            {
                return auto_sub_replay_state != auto_sub_replay_none;
            }
            void mark_sub_replay_pending (auto_sub_replay_state_t state_)
            {
                auto_sub_replay_state = state_;
            }
            void clear_sub_replay ()
            {
                auto_sub_replay_state = auto_sub_replay_none;
            }

            std::map<std::string, socket_base_t *> routers;
            socket_base_t *pub;
            socket_base_t *sub;
            auto_sub_replay_state_t auto_sub_replay_state;
            std::set<std::string> router_endpoints;
            std::set<std::string> pub_endpoints;
            std::set<std::string> sub_endpoints;
        };

        service_attachment_t () : next_router_index (0)
        {
        }

        bool has_manual_pubsub () const
        {
            return manual.pub != NULL && manual.sub != NULL;
        }
        bool has_auto_pubsub () const
        {
            return discovered.has_pubsub ();
        }
        uint32_t auto_router_count () const
        {
            return discovered.router_count ();
        }
        uint32_t auto_pub_count () const
        {
            return discovered.pub_count ();
        }
        uint32_t auto_sub_count () const
        {
            return discovered.sub_count ();
        }
        bool needs_auto_sub_replay () const
        {
            return discovered.needs_sub_replay ();
        }
        void mark_auto_sub_replay_pending (
          discovered_state_t::auto_sub_replay_state_t state_)
        {
            discovered.mark_sub_replay_pending (state_);
        }
        void clear_auto_sub_replay ()
        {
            discovered.clear_sub_replay ();
        }

        manual_state_t manual;
        discovered_state_t discovered;
        std::vector<socket_base_t *> router_cache;
        size_t next_router_index;
        std::set<std::string> applied_filters;
    };

    struct service_attachment_state_t
    {
        struct service_sub_cache_entry_t
        {
            std::string service_name;
            socket_base_t *socket;
        };

        typedef std::vector<service_sub_cache_entry_t> service_sub_cache_t;
        typedef std::vector<socket_base_t *> readable_sub_cache_t;

        std::map<std::string, service_attachment_t> attachments;
        std::map<const socket_base_t *, std::string> socket_index;
        std::deque<service_monitor_handle_t> monitors;
        std::map<std::string, discovery_t *> discoveries;
        std::map<std::string, discovery_t *> channel_dealer_discoveries;
        std::set<std::string> pending_refresh_services;
        std::shared_ptr<service_sub_cache_t> sub_cache;
        std::shared_ptr<readable_sub_cache_t> readable_sub_cache;
        std::shared_ptr<socket_poller_t> readable_sub_poller;
        socket_base_t *pub_ingress;

        service_attachment_state_t () :
            sub_cache (new service_sub_cache_t ()),
            readable_sub_cache (new readable_sub_cache_t ()),
            readable_sub_poller (new socket_poller_t ()),
            pub_ingress (NULL)
        {
        }
    };

    summary_state_t _summary_state;
    discovery_binding_state_t _discovery_state;
    tls_state_t _tls_state;
    endpoint_state_t _endpoint_state;
    handle_state_t _handle_state;
    service_attachment_state_t _service_attachment_state;

    std::string &_bound_endpoint;
    std::map<std::string, uint64_t> &_subject_last_changed_ms;
    int &_last_summary_error;
    uint64_t &_summary_last_changed_ms;
    discovery_t *&_discovery;
    std::string &_discovery_service;
    uint64_t &_discovery_seq;
    std::set<std::string> &_pending_service_updates;
    bool &_registered;
    std::string &_advertise_endpoint;
    std::string &_registration_uplink_endpoint;
    std::string &_tls_cert;
    std::string &_tls_key;
    std::string &_tls_ca;
    std::string &_tls_hostname;
    int &_tls_trust_system;
    bool &_server_tls_locked;
    bool &_mesh_client_tls_locked;
    bool &_registration_tls_locked;
    int &_local_pub_ingress_rcvhwm_cfg;
    int &_local_fanout_sndhwm_cfg;
    int &_local_pub_ingress_rcvhwm_default;
    int &_local_fanout_sndhwm_default;
    std::atomic<uint32_t> &_local_filtered_sub_count;
    std::atomic<uint32_t> &_active_peer_count;
    spot_node_default_handles_t &_handle_defaults;
    std::set<spot_pub_t *> &_pubs;
    std::set<spot_sub_t *> &_subs;
    std::set<spot_handle_t *> &_facades;
    std::map<std::string, service_attachment_t> &_service_attachments;
    std::map<const socket_base_t *, std::string> &_service_attachment_socket_index;
    std::deque<service_monitor_handle_t> &_service_monitors;
    std::map<std::string, discovery_t *> &_service_discoveries;
    std::map<std::string, discovery_t *> &_channel_dealer_discoveries;
    socket_base_t *&_pub_ingress;

    friend struct spot_runtime_t;
    friend class spot_data_plane_t;
    friend struct spot_data_plane_protocol_t;
    friend class spot_pub_t;
    friend class spot_sub_t;
    ZLINK_NON_COPYABLE_NOR_MOVABLE (spot_node_t)
};
}

#endif
