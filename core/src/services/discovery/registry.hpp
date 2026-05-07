/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_DISCOVERY_REGISTRY_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_REGISTRY_HPP_INCLUDED__

#include "core/ctx.hpp"
#include "services/common/service_public_api.hpp"
#include "services/common/service_runtime_base.hpp"
#include "services/discovery/route_limits_internal.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/clock.hpp"
#include "utils/mutex.hpp"

#include <map>
#include <set>
#include <string>
#include <string.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace zlink
{
class registry_t
{
  public:
    explicit registry_t (ctx_t *ctx_);
    ~registry_t ();

    bool check_tag () const;

    int bind (const char *pub_endpoint_, const char *router_endpoint_);
    int set_id (uint32_t registry_id_);
    int add_peer (const char *peer_pub_endpoint_);
    int set_heartbeat (uint32_t interval_ms_, uint32_t timeout_ms_);
    int set_broadcast_interval (uint32_t interval_ms_);
    int set_socket_option (int socket_role_,
                           int option_,
                           const void *optval_,
                           size_t optvallen_);
    int set_tls_server (const char *cert_,
                        const char *key_,
                        int require_client_cert_);
    int set_tls_client (const char *ca_cert_,
                        const char *hostname_,
                        int trust_system_);
    int topology_snapshot (zlink_registry_topology_entry_t *entries_,
                           size_t *count_);
    int topology_query (const zlink_registry_topology_filter_t *filter_,
                        zlink_registry_topology_entry_t *entries_,
                        size_t *count_);
    int member_peers (const char *channel_name_,
                      zlink_member_peer_entry_t *entries_,
                      size_t *count_);
    int status_snapshot (zlink_registry_status_t *out_);
    int service_summary_snapshot (
      const zlink_registry_service_summary_filter_t *filter_,
      std::vector<zlink_registry_service_summary_entry_t> *out_);
    int start ();
    int destroy ();
    service_public_api_guard_t &public_api_guard_for_testing ()
    {
        return _public_api;
    }

  private:
    struct service_key_t
    {
        std::string channel_name;

        bool operator< (const service_key_t &other_) const
        {
            return channel_name < other_.channel_name;
        }
    };

    struct provider_entry_t
    {
        uint16_t service_role;
        std::string endpoint;
        zlink_routing_id_t routing_id;
        uint32_t weight;
        int64_t value;
        std::vector<unsigned char> metadata;
        uint64_t registration_id;
        uint64_t provider_update_seq;
        uint64_t registered_at;
        uint64_t last_heartbeat;
        uint32_t source_registry;

        provider_entry_t () :
            service_role (0),
            weight (100),
            value (0),
            registration_id (0),
            provider_update_seq (0),
            registered_at (0),
            last_heartbeat (0),
            source_registry (0)
        {
            memset (&routing_id, 0, sizeof (routing_id));
        }
    };

    struct owner_identity_t
    {
        std::string channel_name;
        uint16_t service_role;
        std::string routing_id_key;
        uint32_t source_registry;
        uint64_t registration_id;

        owner_identity_t () :
            service_role (0),
            source_registry (0),
            registration_id (0)
        {
        }
        bool operator< (const owner_identity_t &other_) const
        {
            if (channel_name != other_.channel_name)
                return channel_name < other_.channel_name;
            if (service_role != other_.service_role)
                return service_role < other_.service_role;
            if (routing_id_key != other_.routing_id_key)
                return routing_id_key < other_.routing_id_key;
            if (source_registry != other_.source_registry)
                return source_registry < other_.source_registry;
            return registration_id < other_.registration_id;
        }

        bool operator== (const owner_identity_t &other_) const
        {
            return channel_name == other_.channel_name
                   && service_role == other_.service_role
                   && routing_id_key == other_.routing_id_key
                   && source_registry == other_.source_registry
                   && registration_id == other_.registration_id;
        }
    };

    struct provider_key_t
    {
        uint16_t service_role;
        std::string endpoint;

        bool operator< (const provider_key_t &other_) const
        {
            if (service_role != other_.service_role)
                return service_role < other_.service_role;
            return endpoint < other_.endpoint;
        }
    };

    typedef std::map<provider_key_t, provider_entry_t> provider_map_t;

    struct service_entry_t
    {
        uint16_t auto_connect_type;
        provider_map_t providers;

        service_entry_t () : auto_connect_type (0) {}
    };

    typedef std::map<service_key_t, service_entry_t> service_map_t;

    struct topology_key_t
    {
        uint16_t service_kind;
        uint16_t service_role;
        std::string routing_id_key;
        std::string channel_name;
        std::string endpoint;

        bool operator< (const topology_key_t &other_) const
        {
            if (service_kind != other_.service_kind)
                return service_kind < other_.service_kind;
            if (service_role != other_.service_role)
                return service_role < other_.service_role;
            if (routing_id_key != other_.routing_id_key)
                return routing_id_key < other_.routing_id_key;
            if (channel_name != other_.channel_name)
                return channel_name < other_.channel_name;
            return endpoint < other_.endpoint;
        }
    };

    struct topology_entry_t
    {
        zlink_registry_topology_entry_t entry;
        owner_identity_t owner;
        bool has_owner;

        topology_entry_t () : has_owner (false) {}
    };

    typedef std::set<topology_key_t> topology_key_set_t;
    typedef std::map<owner_identity_t, topology_key_set_t>
      topology_owner_index_t;

    struct route_key_t
    {
        std::string channel_name;
        zlink_route_kind_t kind;
        std::string key;

        route_key_t () : kind (ZLINK_ROUTE_KIND_INVALID) {}

        bool operator< (const route_key_t &other_) const
        {
            if (channel_name != other_.channel_name)
                return channel_name < other_.channel_name;
            if (kind != other_.kind)
                return kind < other_.kind;
            return key < other_.key;
        }

        bool operator== (const route_key_t &other_) const
        {
            return channel_name == other_.channel_name && kind == other_.kind
                   && key == other_.key;
        }
    };

    static size_t hash_combine (size_t seed_, size_t value_)
    {
        return seed_ ^ (value_ + 0x9e3779b97f4a7c15ULL + (seed_ << 6)
                        + (seed_ >> 2));
    }

    static uint64_t route_hash_rotl64 (uint64_t value_, int shift_)
    {
        return (value_ << shift_) | (value_ >> (64 - shift_));
    }

    static uint64_t route_hash_read_le64 (const unsigned char *data_)
    {
        uint64_t value = 0;
        for (int i = 0; i < 8; ++i)
            value |= static_cast<uint64_t> (data_[i]) << (i * 8);
        return value;
    }

    static void route_hash_sip_round (uint64_t *v0_,
                                      uint64_t *v1_,
                                      uint64_t *v2_,
                                      uint64_t *v3_)
    {
        *v0_ += *v1_;
        *v1_ = route_hash_rotl64 (*v1_, 13);
        *v1_ ^= *v0_;
        *v0_ = route_hash_rotl64 (*v0_, 32);
        *v2_ += *v3_;
        *v3_ = route_hash_rotl64 (*v3_, 16);
        *v3_ ^= *v2_;
        *v0_ += *v3_;
        *v3_ = route_hash_rotl64 (*v3_, 21);
        *v3_ ^= *v0_;
        *v2_ += *v1_;
        *v1_ = route_hash_rotl64 (*v1_, 17);
        *v1_ ^= *v2_;
        *v2_ = route_hash_rotl64 (*v2_, 32);
    }

    static uint64_t route_hash_sip24 (const unsigned char *data_, size_t len_)
    {
        const uint64_t k0 = 0x0706050403020100ULL;
        const uint64_t k1 = 0x0f0e0d0c0b0a0908ULL;
        uint64_t v0 = 0x736f6d6570736575ULL ^ k0;
        uint64_t v1 = 0x646f72616e646f6dULL ^ k1;
        uint64_t v2 = 0x6c7967656e657261ULL ^ k0;
        uint64_t v3 = 0x7465646279746573ULL ^ k1;

        const unsigned char *p = data_;
        size_t remaining = len_;
        while (remaining >= 8) {
            const uint64_t m = route_hash_read_le64 (p);
            v3 ^= m;
            route_hash_sip_round (&v0, &v1, &v2, &v3);
            route_hash_sip_round (&v0, &v1, &v2, &v3);
            v0 ^= m;
            p += 8;
            remaining -= 8;
        }

        uint64_t b = static_cast<uint64_t> (len_) << 56;
        for (size_t i = 0; i < remaining; ++i)
            b |= static_cast<uint64_t> (p[i]) << (i * 8);

        v3 ^= b;
        route_hash_sip_round (&v0, &v1, &v2, &v3);
        route_hash_sip_round (&v0, &v1, &v2, &v3);
        v0 ^= b;
        v2 ^= 0xff;
        route_hash_sip_round (&v0, &v1, &v2, &v3);
        route_hash_sip_round (&v0, &v1, &v2, &v3);
        route_hash_sip_round (&v0, &v1, &v2, &v3);
        route_hash_sip_round (&v0, &v1, &v2, &v3);
        return v0 ^ v1 ^ v2 ^ v3;
    }

    static size_t route_hash_bytes (const void *data_, size_t len_)
    {
        const unsigned char *bytes =
          static_cast<const unsigned char *> (data_);
        return static_cast<size_t> (route_hash_sip24 (bytes, len_));
    }

    static size_t route_hash_u64 (uint64_t value_)
    {
        unsigned char bytes[8];
        for (int i = 0; i < 8; ++i)
            bytes[i] =
              static_cast<unsigned char> ((value_ >> (i * 8)) & 0xff);
        return route_hash_bytes (bytes, sizeof (bytes));
    }

    struct route_key_hash_t
    {
        size_t operator() (const route_key_t &key_) const
        {
            size_t seed = route_hash_bytes (key_.channel_name.data (),
                                            key_.channel_name.size ());
            seed = hash_combine (seed, route_hash_u64 (key_.kind));
            return hash_combine (
              seed, route_hash_bytes (key_.key.data (), key_.key.size ()));
        }
    };

    struct owner_identity_hash_t
    {
        size_t operator() (const owner_identity_t &owner_) const
        {
            size_t seed = route_hash_bytes (owner_.channel_name.data (),
                                            owner_.channel_name.size ());
            seed = hash_combine (seed, route_hash_u64 (owner_.service_role));
            seed = hash_combine (
              seed, route_hash_bytes (owner_.routing_id_key.data (),
                                      owner_.routing_id_key.size ()));
            seed = hash_combine (seed,
                                 route_hash_u64 (owner_.source_registry));
            return hash_combine (seed,
                                 route_hash_u64 (owner_.registration_id));
        }
    };

    typedef std::unordered_set<route_key_t, route_key_hash_t> route_key_set_t;

    struct route_entry_t
    {
        route_key_t key;
        std::vector<unsigned char> value;
        owner_identity_t owner;
        uint64_t updated_at_ms;
        uint32_t advertising_registry;

        route_entry_t () : updated_at_ms (0), advertising_registry (0)
        {
        }
    };

    struct route_observation_key_t
    {
        route_key_t route_key;
        owner_identity_t owner;
        uint32_t advertising_registry;

        route_observation_key_t () : advertising_registry (0) {}

        bool operator== (const route_observation_key_t &other_) const
        {
            return route_key == other_.route_key && owner == other_.owner
                   && advertising_registry == other_.advertising_registry;
        }
    };

    struct route_observation_key_hash_t
    {
        size_t operator() (const route_observation_key_t &key_) const
        {
            size_t seed = route_key_hash_t () (key_.route_key);
            seed = hash_combine (seed, owner_identity_hash_t () (key_.owner));
            return hash_combine (
              seed, std::hash<uint32_t> () (key_.advertising_registry));
        }
    };

    #include "services/discovery/registry_route_materialized_table.inc"

    typedef route_materialized_table_t route_map_t;
    typedef std::unordered_map<owner_identity_t, route_key_set_t,
                               owner_identity_hash_t>
      route_owner_index_t;
    typedef std::unordered_map<uint32_t, route_key_set_t>
      route_advertiser_index_t;
    typedef std::unordered_set<route_observation_key_t,
                               route_observation_key_hash_t>
      route_observation_key_set_t;
    typedef std::unordered_map<route_observation_key_t, route_entry_t,
                               route_observation_key_hash_t>
      route_observation_map_t;
    typedef std::unordered_map<route_key_t, route_observation_key_set_t,
                               route_key_hash_t>
      route_observations_by_route_t;

    struct route_store_limits_t
    {
        route_store_limits_t () :
            max_materialized_routes (1000000),
            max_observations_per_owner (1000000),
            memory_budget_bytes (512ULL * 1024ULL * 1024ULL),
            snapshot_chunk_records (1024),
            staging_memory_budget_bytes (256ULL * 1024ULL * 1024ULL)
        {
        }

        size_t max_materialized_routes;
        size_t max_observations_per_owner;
        size_t memory_budget_bytes;
        uint32_t snapshot_chunk_records;
        size_t staging_memory_budget_bytes;
    };

    struct route_store_stats_t
    {
        route_store_stats_t () :
            memory_bytes (0),
            winner_recompute_count (0),
            winner_recompute_observation_visits (0),
            owner_cleanup_count (0),
            owner_cleanup_observation_visits (0),
            advertiser_cleanup_count (0),
            advertiser_cleanup_observation_visits (0),
            snapshot_staging_abort_count (0)
        {
        }

        size_t memory_bytes;
        uint64_t winner_recompute_count;
        uint64_t winner_recompute_observation_visits;
        uint64_t owner_cleanup_count;
        uint64_t owner_cleanup_observation_visits;
        uint64_t advertiser_cleanup_count;
        uint64_t advertiser_cleanup_observation_visits;
        uint64_t snapshot_staging_abort_count;
    };

    struct route_snapshot_staging_t
    {
        route_snapshot_staging_t () :
            seq (0),
            next_chunk_index (0),
            chunk_count (0),
            memory_bytes (0),
            active (false)
        {
        }

        uint64_t seq;
        uint32_t next_chunk_index;
        uint32_t chunk_count;
        route_observation_map_t observations;
        size_t memory_bytes;
        bool active;
    };

    private:

    struct channel_contract_t
    {
        uint16_t auto_connect_type;
        uint64_t created_at;
        uint32_t owner_registry_id;

        channel_contract_t () :
            auto_connect_type (0),
            created_at (0),
            owner_registry_id (0)
        {
        }
    };

    static void control_task (void *arg_);
    void tick ();
    int ensure_sockets ();
    void close_sockets ();
    void handle_router (void *router_);
    void handle_peer (void *sub_);
    void handle_register (void *router_, const zlink_msg_t *frames_,
                          size_t frame_count_,
                          const zlink_routing_id_t &sender_id_);
    void handle_unregister (void *router_, const zlink_msg_t *frames_,
                            size_t frame_count_,
                            const zlink_routing_id_t &sender_id_);
    void handle_heartbeat (const zlink_msg_t *frames_, size_t frame_count_);
    void handle_bootstrap (void *router_,
                           const zlink_msg_t *frames_,
                           size_t frame_count_,
                           const zlink_routing_id_t &sender_id_);
    void handle_topology_report (const zlink_msg_t *frames_,
                                 size_t frame_count_);
    void handle_topology_query (void *router_,
                                const zlink_msg_t *frames_,
                                size_t frame_count_,
                                const zlink_routing_id_t &sender_id_);
    void handle_bind_route (void *router_,
                            const zlink_msg_t *frames_,
                            size_t frame_count_,
                            const zlink_routing_id_t &sender_id_);
    void handle_unbind_route (void *router_,
                              const zlink_msg_t *frames_,
                              size_t frame_count_,
                              const zlink_routing_id_t &sender_id_);
    void handle_resolve_route (void *router_,
                               const zlink_msg_t *frames_,
                               size_t frame_count_,
                               const zlink_routing_id_t &sender_id_);
    void handle_update_attributes (void *router_,
                                   const zlink_msg_t *frames_,
                                   size_t frame_count_,
                                   const zlink_routing_id_t &sender_id_);
    void send_register_ack (void *router_,
                            const zlink_routing_id_t &sender_id_,
                            uint8_t status_,
                            const std::string &endpoint_,
                            uint32_t source_registry_,
                            uint64_t registration_id_,
                            const std::string &error_);
    void send_unregister_ack (void *router_,
                              const zlink_routing_id_t &sender_id_,
                              uint8_t status_,
                              const std::string &error_);
    void send_topology_reply (void *router_,
                              const zlink_routing_id_t &sender_id_,
                              const std::vector<zlink_registry_topology_entry_t>
                                &entries_);
    void send_route_reply (void *router_,
                           const zlink_routing_id_t &sender_id_,
                           uint8_t status_,
                           const zlink_routing_id_t *owner_rid_,
                           const std::vector<unsigned char> *value_,
                           const std::string &error_);
    bool read_route_key (zlink_route_kind_t kind_,
                         const zlink_msg_t &key_frame_,
                         const std::string &channel_name_,
                         route_key_t *out_) const;
    bool owner_routing_id_from_key (const owner_identity_t &owner_,
                                    zlink_routing_id_t *out_) const;
    void collect_topology_entries_locked (
      const zlink_registry_topology_filter_t *filter_,
      std::vector<zlink_registry_topology_entry_t> *out_) const;
    void collect_matching_topology_entries_locked (
      const zlink_registry_topology_filter_t *filter_,
      std::vector<zlink_registry_topology_entry_t> *out_) const;
    bool select_spot_owner_entry_locked (
      const std::vector<zlink_registry_topology_entry_t> &matched_,
      const char *channel_name_,
      zlink_registry_topology_entry_t *entry_out_) const;
    void send_bootstrap_reply (void *router_,
                               const zlink_routing_id_t &sender_id_,
                               uint32_t status_errno_ = 0);
    int ensure_channel_contract_locked (const std::string &channel_name_,
                                        uint16_t auto_connect_type_,
                                        uint64_t now_ms_,
                                        uint32_t owner_registry_id_);
    void remove_channel_providers_locked (const std::string &channel_name_);
    void upsert_topology_entry (const zlink_registry_topology_entry_t &entry_,
                                uint64_t now_ms_);
    bool find_provider_owner_locked (const std::string &channel_name_,
                                     uint16_t service_role_,
                                     const std::string &endpoint_,
                                     owner_identity_t *owner_out_,
                                     zlink_routing_id_t *routing_id_out_) const;
    bool owner_is_live_locked (const owner_identity_t &owner_) const;
    void cleanup_owner_records_locked (const owner_identity_t &owner_,
                                       uint64_t now_ms_);
    void remove_topology_owner_index_locked (const topology_key_t &key_,
                                             const topology_entry_t &entry_);
    void index_topology_owner_locked (const topology_key_t &key_,
                                      const topology_entry_t &entry_);
    bool route_entry_wins_locked (const route_entry_t &candidate_,
                                  const route_entry_t &current_) const;
    size_t route_entry_memory_bytes (const route_entry_t &entry_) const;
    bool route_store_can_fit_locked (const route_entry_t &entry_,
                                     size_t removed_memory_,
                                     int *err_out_) const;
    void erase_route_observation_locked (const route_observation_key_t &key_,
                                         route_key_set_t *dirty_routes_);
    void erase_route_observations_by_route_advertiser_locked (
      const route_key_t &route_key_,
      uint32_t advertising_registry_,
      route_key_set_t *dirty_routes_);
    void upsert_route_observation_locked (const route_entry_t &entry_,
                                          route_key_set_t *dirty_routes_);
    void materialize_route_winner_locked (const route_key_t &route_key_);
    void materialize_dirty_routes_locked (const route_key_set_t &dirty_routes_);
    void promote_owner_route_records_locked (const owner_identity_t &owner_);
    void cleanup_advertised_route_records_locked (uint32_t advertising_registry_);
    void send_service_list (void *pub_);
    void send_route_list (void *pub_);
    void remove_expired (uint64_t now_ms_);

    ctx_t *_ctx;
    uint32_t _tag;
    service_runtime_base_t _lifecycle;
    service_public_api_guard_t _public_api;

    struct socket_opt_t
    {
        int option;
        std::vector<unsigned char> value;
    };
    void apply_socket_opts (socket_base_t *socket_,
                            const std::vector<socket_opt_t> &opts_);
    void promote_runtime_sockets (socket_base_t *pub_,
                                  socket_base_t *router_,
                                  uint64_t now_ms_,
                                  socket_base_t **old_pub_out_,
                                  socket_base_t **old_router_out_);
    int ensure_peer_sub_socket ();
    void connect_peer_sub_endpoints (void *peer_sub_,
                                     const std::vector<std::string> &peer_pubs_);

    mutex_t _sync;

    struct endpoint_config_t
    {
        std::string pub_endpoint;
        std::string router_endpoint;
        std::vector<std::string> peer_pubs;
    };

    struct coordination_state_t
    {
        coordination_state_t () :
            registry_id (0),
            registry_id_set (false),
            list_seq (0),
            next_registration_id (1),
            next_provider_update_seq (1),
            route_snapshot_announced (false),
            last_summary_error (0),
            summary_last_changed_ms (0),
            heartbeat_interval_ms (5000),
            heartbeat_timeout_ms (15000),
            broadcast_interval_ms (30000)
        {
        }

        uint32_t registry_id;
        bool registry_id_set;
        uint64_t list_seq;
        uint64_t next_registration_id;
        uint64_t next_provider_update_seq;
        bool route_snapshot_announced;
        int last_summary_error;
        uint64_t summary_last_changed_ms;
        uint32_t heartbeat_interval_ms;
        uint32_t heartbeat_timeout_ms;
        uint32_t broadcast_interval_ms;
    };

    struct socket_option_state_t
    {
        std::vector<socket_opt_t> pub_opts;
        std::vector<socket_opt_t> router_opts;
        std::vector<socket_opt_t> peer_sub_opts;
    };

    struct runtime_socket_state_t
    {
        runtime_socket_state_t () :
            stop (0),
            task_id (0),
            pub_socket (NULL),
            router_socket (NULL),
            peer_sub_socket (NULL),
            next_broadcast_ms (0),
            last_sent_seq (0),
            started (false),
            next_socket_retry_ms (0)
        {
        }

        atomic_counter_t stop;
        uint64_t task_id;
        void *pub_socket;
        void *router_socket;
        void *peer_sub_socket;
        std::set<std::string> peer_connected;
        uint64_t next_broadcast_ms;
        uint64_t last_sent_seq;
        bool started;
        uint64_t next_socket_retry_ms;
    };

    struct projection_state_t
    {
        service_map_t services;
        std::map<std::string, channel_contract_t> channel_contracts;
        std::map<topology_key_t, topology_entry_t> topology;
        topology_owner_index_t topology_by_owner;
        route_map_t routes;
        route_observation_map_t route_observations;
        route_observations_by_route_t route_observations_by_route;
        route_owner_index_t routes_by_owner;
        route_advertiser_index_t routes_by_advertiser;
        std::map<uint32_t, route_snapshot_staging_t> route_snapshot_staging;
        route_store_limits_t route_limits;
        route_store_stats_t route_stats;
        std::map<uint32_t, uint64_t> peer_seq;
        std::map<uint32_t, uint64_t> peer_route_seq;
        std::map<uint32_t, uint64_t> peer_last_seen;
    };

    endpoint_config_t _endpoint_config;
    coordination_state_t _coordination_state;
    socket_option_state_t _socket_option_state;
    runtime_socket_state_t _runtime_socket_state;
    projection_state_t _projection_state;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (registry_t)
};
}

#endif
