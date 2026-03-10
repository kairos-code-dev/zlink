/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_NODE_HPP_INCLUDED__
#define __ZLINK_SPOT_NODE_HPP_INCLUDED__

#include "core/ctx.hpp"
#include "core/msg.hpp"
#include "core/thread.hpp"
#include "services/discovery/discovery.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/mutex.hpp"

#include <set>
#include <string>

namespace zlink
{
class socket_base_t;
class spot_pub_t;
class spot_sub_t;
class spot_data_plane_t;

class spot_node_t : public discovery_observer_t
{
  public:
    struct option_setting_t
    {
        option_setting_t () : enabled (false), value (0), size (0) {}

        bool enabled;
        int value;
        size_t size;
    };

    struct pub_defaults_t
    {
        option_setting_t sndhwm;
        option_setting_t sndtimeo;
        option_setting_t linger;
        option_setting_t nodrop;
        option_setting_t sndbuf;
        option_setting_t rcvbuf;
    };

    struct sub_defaults_t
    {
        option_setting_t rcvhwm;
        option_setting_t rcvtimeo;
        option_setting_t linger;
        option_setting_t sndbuf;
        option_setting_t rcvbuf;
    };

    explicit spot_node_t (ctx_t *ctx_);
    ~spot_node_t ();

    bool check_tag () const;

    int bind (const char *endpoint_);
    int connect_peer_pub (const char *peer_pub_endpoint_);
    int disconnect_peer_pub (const char *peer_pub_endpoint_);
    int register_node (const char *service_name_,
                       const char *advertise_endpoint_);
    int unregister_node (const char *service_name_);
    int set_discovery (discovery_t *discovery_,
                       const char *service_name_);
    int set_tls_server (const char *cert_, const char *key_);
    int set_tls_client (const char *ca_cert_,
                        const char *hostname_,
                        int trust_system_);
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
    void remove_spot_pub (spot_pub_t *pub_);
    void remove_spot_sub (spot_sub_t *sub_);

    int destroy ();

    // discovery_observer_t
    void on_service_update (const std::string &service_name_) ZLINK_OVERRIDE;
    void on_discovery_destroyed (discovery_t *discovery_) ZLINK_OVERRIDE;

    ctx_t *ctx () const { return _ctx; }
    const std::string &pub_ingress_endpoint () const
    {
        return _pub_ingress_endpoint;
    }
    const std::string &sub_fanout_endpoint () const
    {
        return _sub_fanout_endpoint;
    }
    int ensure_healthy () const;
    void debug_mark_fault (int err_);

  private:
    static void control_task (void *arg_);

    void control_tick ();
    void destroy_handles ();
    void close_control_sockets ();
    int start_data_plane ();
    int send_data_plane_command (const char *verb_,
                                 const char *arg_ = NULL) const;
    int wait_facade_peer (socket_base_t *socket_) const;
    spot_pub_t *create_spot_pub_with_defaults (const pub_defaults_t &defaults_,
                                               bool node_owned_default_);
    spot_sub_t *create_spot_sub_with_defaults (const sub_defaults_t &defaults_,
                                               bool node_owned_default_);
    int apply_pub_defaults (spot_pub_t *pub_, const pub_defaults_t &defaults_);
    int apply_sub_defaults (spot_sub_t *sub_, const sub_defaults_t &defaults_);
    static int validate_pub_option (int option_,
                                    const void *optval_,
                                    size_t optvallen_);
    static int validate_sub_option (int option_,
                                    const void *optval_,
                                    size_t optvallen_);
    static void copy_option_setting (option_setting_t *dst_,
                                     const void *optval_,
                                     size_t optvallen_);
    void store_pub_option (int option_, const void *optval_, size_t optvallen_);
    void store_sub_option (int option_, const void *optval_, size_t optvallen_);
    int resolve_advertise_endpoint (const char *advertise_endpoint_,
                                    std::string *out_) const;
    void refresh_local_pub_ingress_hwm ();
    void refresh_local_fanout_hwm ();
    void refresh_discovery_peers ();
    std::string summary_service_name () const;
    void submit_pub_summary (spot_pub_t *pub_, uint16_t state_, int error_code_);
    void submit_sub_summary (spot_sub_t *sub_, uint16_t state_, int error_code_);
    void submit_stopped_summaries ();
    void refresh_existing_summaries ();
    void refresh_sub_peer_summaries (bool has_active_peers,
                                     bool lost_transition);

    static bool validate_service_name (const std::string &name_);
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
    mutable mutex_t _ctrl_sync;
    mutable mutex_t _default_pub_sync;
    mutable mutex_t _default_sub_sync;

    socket_base_t *_data_ctrl_front;
    socket_base_t *_data_ctrl_back;
    socket_base_t *_mesh_pub;
    socket_base_t *_mesh_xsub;
    socket_base_t *_local_pub_ingress_sub;
    socket_base_t *_local_fanout_xpub;
    thread_t _data_plane_thread;
    atomic_counter_t _stop;

    uint64_t _task_id;
    uint32_t _node_id;

    std::string _pub_ingress_endpoint;
    std::string _sub_fanout_endpoint;
    std::string _data_ctrl_endpoint;
    std::string _bound_endpoint;
    std::set<std::string> _manual_peer_endpoints;
    std::set<std::string> _active_peer_endpoints;
    std::set<std::string> _discovery_peer_endpoints;

    discovery_t *_discovery;
    std::string _discovery_service;
    uint64_t _discovery_seq;
    std::set<std::string> _pending_service_updates;

    bool _registered;
    std::string _service_name;
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

    bool _faulted;
    int _fault_errno;

    int _local_pub_ingress_rcvhwm_cfg;
    int _local_fanout_sndhwm_cfg;
    int _local_pub_ingress_rcvhwm_default;
    int _local_fanout_sndhwm_default;

    spot_pub_t *_default_pub;
    spot_sub_t *_default_sub;
    pub_defaults_t _pub_defaults;
    sub_defaults_t _sub_defaults;
    std::set<spot_pub_t *> _pubs;
    std::set<spot_sub_t *> _subs;

    friend class spot_data_plane_t;
    friend class spot_pub_t;
    friend class spot_sub_t;
    ZLINK_NON_COPYABLE_NOR_MOVABLE (spot_node_t)
};
}

#endif
