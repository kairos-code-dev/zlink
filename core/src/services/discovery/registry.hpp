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

    class route_materialized_table_t
    {
      private:
        typedef uint32_t node_index_t;
        static constexpr node_index_t npos =
          static_cast<node_index_t> (UINT32_MAX);
        static constexpr size_t initial_bucket_count = 4;
        static constexpr size_t node_block_size = 65536;
        static constexpr size_t byte_block_size = 1024 * 1024;
        static constexpr size_t max_bucket_load = 1;

        struct string_ref_t
        {
            uint32_t offset;
            uint32_t size;

            string_ref_t () : offset (0), size (0) {}
            string_ref_t (uint32_t offset_, uint32_t size_) :
                offset (offset_),
                size (size_)
            {
            }
        };

        struct node_t
        {
            size_t hash;
            uint64_t updated_at_ms;
            string_ref_t channel;
            string_ref_t key;
            string_ref_t value;
            node_index_t owner_id;
            node_index_t next;
            uint32_t advertising_registry;
            uint16_t kind;
            bool used;

            node_t () :
                hash (0),
                updated_at_ms (0),
                owner_id (npos),
                next (npos),
                advertising_registry (0),
                kind (static_cast<uint16_t> (ZLINK_ROUTE_KIND_INVALID)),
                used (false)
            {
            }
        };

        struct hash_table_t
        {
            std::vector<node_index_t> buckets;
            size_t used;

            hash_table_t () : used (0) {}
        };

      public:
        class iterator
        {
          public:
            iterator () : _table (NULL), _index (0) {}

            const route_entry_t &operator* () const
            {
                _cache = _table->entry_at (_index);
                return _cache;
            }

            const route_entry_t *operator-> () const
            {
                _cache = _table->entry_at (_index);
                return &_cache;
            }

            iterator &operator++ ()
            {
                ++_index;
                skip_unused ();
                return *this;
            }

            bool operator== (const iterator &other_) const
            {
                return _table == other_._table && _index == other_._index;
            }

            bool operator!= (const iterator &other_) const
            {
                return !(*this == other_);
            }

          private:
            friend class route_materialized_table_t;
            friend class const_iterator;

            iterator (route_materialized_table_t *table_, size_t index_) :
                _table (table_),
                _index (index_)
            {
                skip_unused ();
            }

            void skip_unused ()
            {
                if (!_table)
                    return;
                while (_index < _table->_node_count
                       && !_table->node_at (_index).used) {
                    ++_index;
                }
            }

            route_materialized_table_t *_table;
            size_t _index;
            mutable route_entry_t _cache;
        };

        class const_iterator
        {
          public:
            const_iterator () : _table (NULL), _index (0) {}
            const_iterator (const iterator &it_) :
                _table (it_._table),
                _index (it_._index)
            {
            }

            const route_entry_t &operator* () const
            {
                _cache = _table->entry_at (_index);
                return _cache;
            }

            const route_entry_t *operator-> () const
            {
                _cache = _table->entry_at (_index);
                return &_cache;
            }

            const_iterator &operator++ ()
            {
                ++_index;
                skip_unused ();
                return *this;
            }

            bool operator== (const const_iterator &other_) const
            {
                return _table == other_._table && _index == other_._index;
            }

            bool operator!= (const const_iterator &other_) const
            {
                return !(*this == other_);
            }

          private:
            friend class route_materialized_table_t;

            const_iterator (const route_materialized_table_t *table_,
                            size_t index_) :
                _table (table_),
                _index (index_)
            {
                skip_unused ();
            }

            void skip_unused ()
            {
                if (!_table)
                    return;
                while (_index < _table->_node_count
                       && !_table->node_at (_index).used) {
                    ++_index;
                }
            }

            const route_materialized_table_t *_table;
            size_t _index;
            mutable route_entry_t _cache;
        };

        class mapped_reference
        {
          public:
            mapped_reference (route_materialized_table_t *table_,
                              size_t index_) :
                _table (table_),
                _index (index_)
            {
            }

            mapped_reference &operator= (const route_entry_t &entry_)
            {
                _table->assign_entry (_index, entry_);
                return *this;
            }

            operator route_entry_t () const { return _table->entry_at (_index); }

          private:
            route_materialized_table_t *_table;
            size_t _index;
        };

        class rehash_pause_guard_t
        {
          public:
            explicit rehash_pause_guard_t (route_materialized_table_t &table_) :
                _table (&table_)
            {
                _table->pause_rehash ();
            }

            ~rehash_pause_guard_t () { _table->resume_rehash (); }

          private:
            rehash_pause_guard_t (const rehash_pause_guard_t &);
            rehash_pause_guard_t &operator= (const rehash_pause_guard_t &);

            route_materialized_table_t *_table;
        };

        route_materialized_table_t () :
            _rehashidx (npos),
            _free_head (npos),
            _node_count (0),
            _byte_count (0),
            _size (0),
            _rehash_step_count (0),
            _max_chain_length (0),
            _rehash_pause (0)
        {
        }

        ~route_materialized_table_t ()
        {
            destroy_node_blocks ();
            destroy_byte_blocks ();
        }

        bool empty () const { return _size == 0; }
        size_t size () const { return _size; }
        bool is_rehashing () const { return _rehashidx != npos; }
        bool rehash_paused () const { return _rehash_pause != 0; }
        uint64_t rehash_step_count () const { return _rehash_step_count; }
        size_t max_chain_length () const { return _max_chain_length; }

        void pause_rehash () { ++_rehash_pause; }

        void resume_rehash ()
        {
            if (_rehash_pause > 0)
                --_rehash_pause;
        }

        iterator begin ()
        {
            rehash_step (1);
            return iterator (this, 0);
        }

        iterator end () { return iterator (this, _node_count); }

        const_iterator begin () const { return const_iterator (this, 0); }

        const_iterator end () const
        {
            return const_iterator (this, _node_count);
        }

        size_t snapshot_values (size_t cursor_,
                                size_t max_records_,
                                std::vector<route_entry_t> *out_) const
        {
            size_t cursor = cursor_;
            while (cursor < _node_count && out_->size () < max_records_) {
                if (node_at (cursor).used)
                    out_->push_back (entry_at (cursor));
                ++cursor;
            }
            return cursor;
        }

        iterator find (const route_key_t &key_)
        {
            rehash_step (1);
            const size_t hash = route_key_hash_t () (key_);
            const node_index_t node = find_node (key_, hash);
            return node == npos ? end () : iterator (this, node);
        }

        const_iterator find (const route_key_t &key_) const
        {
            const size_t hash = route_key_hash_t () (key_);
            const node_index_t node = find_node (key_, hash);
            return node == npos ? end () : const_iterator (this, node);
        }

        mapped_reference operator[] (const route_key_t &key_)
        {
            rehash_step (1);
            ensure_capacity_for_insert ();
            const size_t hash = route_key_hash_t () (key_);
            node_index_t node = find_node (key_, hash);
            if (node != npos)
                return mapped_reference (this, node);

            node = allocate_node ();
            node_t &stored = node_at (node);
            store_key (stored, key_);
            stored.hash = hash;
            stored.used = true;
            const size_t table = is_rehashing () ? 1 : 0;
            ensure_table (table, initial_bucket_count);
            const size_t bucket = bucket_index (table, hash);
            stored.next = _tables[table].buckets[bucket];
            _tables[table].buckets[bucket] = node;
            _tables[table].used++;
            _size++;
            update_chain_length (table, bucket);
            return mapped_reference (this, node);
        }

        size_t erase (const route_key_t &key_)
        {
            rehash_step (1);
            const size_t hash = route_key_hash_t () (key_);
            for (size_t table = 0; table < 2; ++table) {
                if (_tables[table].buckets.empty ())
                    continue;
                if (table == 0 && is_rehashing ()
                    && bucket_index (table, hash) < _rehashidx) {
                    continue;
                }
                const size_t bucket = bucket_index (table, hash);
                node_index_t prev = npos;
                node_index_t node = _tables[table].buckets[bucket];
                while (node != npos) {
                    node_t &candidate = node_at (node);
                    if (candidate.hash == hash
                        && node_key_equals (candidate, key_)) {
                        if (prev == npos)
                            _tables[table].buckets[bucket] = candidate.next;
                        else
                            node_at (prev).next = candidate.next;
                        release_node (node);
                        _tables[table].used--;
                        _size--;
                        finish_rehash_if_done ();
                        return 1;
                    }
                    prev = node;
                    node = candidate.next;
                }
                if (!is_rehashing ())
                    break;
            }
            return 0;
        }

        void clear ()
        {
            _tables[0] = hash_table_t ();
            _tables[1] = hash_table_t ();
            destroy_node_blocks ();
            destroy_byte_blocks ();
            _channel_refs.clear ();
            _owners.clear ();
            _owner_ids.clear ();
            _rehashidx = npos;
            _free_head = npos;
            _node_count = 0;
            _size = 0;
            _rehash_pause = 0;
        }

      private:
        route_materialized_table_t (const route_materialized_table_t &);
        route_materialized_table_t &operator= (
          const route_materialized_table_t &);

        typedef std::unordered_map<owner_identity_t, size_t,
                                   owner_identity_hash_t>
          owner_intern_map_t;
        typedef std::unordered_map<std::string, string_ref_t>
          channel_ref_map_t;

        route_entry_t entry_at (size_t index_) const
        {
            route_entry_t entry;
            const node_t &node = node_at (index_);
            entry.key.channel_name = string_from_ref (node.channel);
            entry.key.kind = static_cast<zlink_route_kind_t> (node.kind);
            entry.key.key = string_from_ref (node.key);
            value_from_ref (node.value, &entry.value);
            if (node.owner_id != npos && node.owner_id < _owners.size ())
                entry.owner = _owners[node.owner_id];
            entry.updated_at_ms = node.updated_at_ms;
            entry.advertising_registry = node.advertising_registry;
            return entry;
        }

        void assign_entry (size_t index_, const route_entry_t &entry_)
        {
            node_t &node = node_at (index_);
            if (!entry_.key.channel_name.empty () && !entry_.key.key.empty ())
                store_key (node, entry_.key);
            node.value = append_bytes (entry_.value.empty ()
                                         ? NULL
                                         : &entry_.value[0],
                                       entry_.value.size ());
            node.owner_id = intern_owner (entry_.owner);
            node.updated_at_ms = entry_.updated_at_ms;
            node.advertising_registry = entry_.advertising_registry;
        }

        size_t intern_owner (const owner_identity_t &owner_)
        {
            owner_intern_map_t::const_iterator it = _owner_ids.find (owner_);
            if (it != _owner_ids.end ())
                return it->second;
            const size_t id = _owners.size ();
            _owners.push_back (owner_);
            _owner_ids[owner_] = id;
            return id;
        }

        string_ref_t append_bytes (const unsigned char *data_, size_t size_)
        {
            if (size_ == 0)
                return string_ref_t ();
            if (_byte_blocks.empty ()
                || _byte_count % byte_block_size + size_ > byte_block_size) {
                _byte_blocks.push_back (new unsigned char[byte_block_size]);
                _byte_count = (_byte_blocks.size () - 1) * byte_block_size;
            }
            const size_t offset = _byte_count;
            memcpy (byte_ptr (offset), data_, size_);
            _byte_count += size_;
            return string_ref_t (static_cast<uint32_t> (offset),
                                 static_cast<uint32_t> (size_));
        }

        string_ref_t append_string (const std::string &value_)
        {
            return append_bytes (
              reinterpret_cast<const unsigned char *> (value_.data ()),
              value_.size ());
        }

        string_ref_t intern_channel (const std::string &channel_)
        {
            if (channel_.empty ())
                return string_ref_t ();
            channel_ref_map_t::const_iterator it = _channel_refs.find (channel_);
            if (it != _channel_refs.end ())
                return it->second;
            const string_ref_t ref = append_string (channel_);
            _channel_refs[channel_] = ref;
            return ref;
        }

        void store_key (node_t &node_, const route_key_t &key_)
        {
            node_.channel = intern_channel (key_.channel_name);
            node_.kind = static_cast<uint16_t> (key_.kind);
            node_.key = append_string (key_.key);
        }

        std::string string_from_ref (const string_ref_t &ref_) const
        {
            if (ref_.size == 0)
                return std::string ();
            return std::string (reinterpret_cast<const char *> (
                                  byte_ptr (ref_.offset)),
                                ref_.size);
        }

        void value_from_ref (const string_ref_t &ref_,
                             std::vector<unsigned char> *out_) const
        {
            if (ref_.size == 0) {
                out_->clear ();
                return;
            }
            const unsigned char *data = byte_ptr (ref_.offset);
            out_->assign (data, data + ref_.size);
        }

        bool ref_equals (const string_ref_t &ref_,
                         const std::string &value_) const
        {
            return ref_.size == value_.size ()
                   && (ref_.size == 0
                       || memcmp (byte_ptr (ref_.offset), value_.data (),
                                  ref_.size)
                            == 0);
        }

        bool node_key_equals (const node_t &node_,
                              const route_key_t &key_) const
        {
            return node_.kind == key_.kind
                   && ref_equals (node_.channel, key_.channel_name)
                   && ref_equals (node_.key, key_.key);
        }

        static size_t next_power_of_two (size_t value_)
        {
            size_t value = initial_bucket_count;
            while (value < value_)
                value <<= 1;
            return value;
        }

        void ensure_table (size_t table_, size_t size_)
        {
            if (_tables[table_].buckets.empty ())
                _tables[table_].buckets.assign (next_power_of_two (size_),
                                                npos);
        }

        size_t bucket_index (size_t table_, size_t hash_) const
        {
            return hash_ & (_tables[table_].buckets.size () - 1);
        }

        void ensure_capacity_for_insert ()
        {
            if (_tables[0].buckets.empty ()) {
                ensure_table (0, initial_bucket_count);
                return;
            }
            if (is_rehashing ())
                return;
            if (_tables[0].used + 1
                <= _tables[0].buckets.size () * max_bucket_load)
                return;
            _tables[1].buckets.assign (
              next_power_of_two ((_tables[0].used + max_bucket_load)
                                 / max_bucket_load),
              npos);
            _tables[1].used = 0;
            _rehashidx = 0;
        }

        node_index_t allocate_node ()
        {
            if (_free_head != npos) {
                const node_index_t node = _free_head;
                _free_head = node_at (node).next;
                node_at (node) = node_t ();
                return node;
            }
            if (_node_count % node_block_size == 0)
                _nodes.push_back (new node_t[node_block_size]);
            return static_cast<node_index_t> (_node_count++);
        }

        void release_node (node_index_t node_)
        {
            node_at (node_) = node_t ();
            node_at (node_).hash = 0;
            node_at (node_).used = false;
            node_at (node_).next = _free_head;
            _free_head = node_;
        }

        node_index_t find_node (const route_key_t &key_, size_t hash_) const
        {
            if (_size == 0)
                return npos;
            for (size_t table = 0; table < 2; ++table) {
                if (_tables[table].buckets.empty ())
                    continue;
                const size_t bucket = bucket_index (table, hash_);
                if (table == 0 && is_rehashing () && bucket < _rehashidx)
                    continue;
                node_index_t node = _tables[table].buckets[bucket];
                while (node != npos) {
                    const node_t &candidate = node_at (node);
                    if (candidate.hash == hash_
                        && node_key_equals (candidate, key_))
                        return node;
                    node = candidate.next;
                }
                if (!is_rehashing ())
                    break;
            }
            return npos;
        }

        void rehash_step (size_t steps_)
        {
            if (!is_rehashing () || rehash_paused ())
                return;
            size_t empty_visits = steps_ * 10;
            while (steps_ > 0 && _tables[0].used > 0) {
                while (_rehashidx < _tables[0].buckets.size ()
                       && _tables[0].buckets[_rehashidx] == npos) {
                    ++_rehashidx;
                    if (--empty_visits == 0)
                        return;
                }
                if (_rehashidx >= _tables[0].buckets.size ())
                    break;

                node_index_t node = _tables[0].buckets[_rehashidx];
                _tables[0].buckets[_rehashidx] = npos;
                while (node != npos) {
                    node_t &moving = node_at (node);
                    const node_index_t next = moving.next;
                    const size_t bucket = bucket_index (1, moving.hash);
                    moving.next = _tables[1].buckets[bucket];
                    _tables[1].buckets[bucket] = node;
                    _tables[0].used--;
                    _tables[1].used++;
                    update_chain_length (1, bucket);
                    node = next;
                }
                ++_rehashidx;
                ++_rehash_step_count;
                --steps_;
            }
            finish_rehash_if_done ();
        }

        void finish_rehash_if_done ()
        {
            if (!is_rehashing () || _tables[0].used != 0)
                return;
            _tables[0] = _tables[1];
            _tables[1] = hash_table_t ();
            _rehashidx = npos;
        }

        void update_chain_length (size_t table_, size_t bucket_)
        {
            size_t length = 0;
            node_index_t node = _tables[table_].buckets[bucket_];
            while (node != npos) {
                ++length;
                node = node_at (node).next;
            }
            if (length > _max_chain_length)
                _max_chain_length = length;
        }

        node_t &node_at (size_t index_)
        {
            return _nodes[index_ / node_block_size][index_ % node_block_size];
        }

        const node_t &node_at (size_t index_) const
        {
            return _nodes[index_ / node_block_size][index_ % node_block_size];
        }

        void destroy_node_blocks ()
        {
            for (std::vector<node_t *>::iterator it = _nodes.begin ();
                 it != _nodes.end (); ++it)
                delete[] *it;
            _nodes.clear ();
        }

        unsigned char *byte_ptr (size_t offset_)
        {
            return _byte_blocks[offset_ / byte_block_size]
                   + (offset_ % byte_block_size);
        }

        const unsigned char *byte_ptr (size_t offset_) const
        {
            return _byte_blocks[offset_ / byte_block_size]
                   + (offset_ % byte_block_size);
        }

        void destroy_byte_blocks ()
        {
            for (std::vector<unsigned char *>::iterator it =
                   _byte_blocks.begin ();
                 it != _byte_blocks.end (); ++it)
                delete[] *it;
            _byte_blocks.clear ();
            _byte_count = 0;
        }

        hash_table_t _tables[2];
        std::vector<node_t *> _nodes;
        std::vector<unsigned char *> _byte_blocks;
        size_t _byte_count;
        channel_ref_map_t _channel_refs;
        std::vector<owner_identity_t> _owners;
        owner_intern_map_t _owner_ids;
        size_t _rehashidx;
        node_index_t _free_head;
        size_t _node_count;
        size_t _size;
        uint64_t _rehash_step_count;
        size_t _max_chain_length;
        size_t _rehash_pause;
    };

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
