/* SPDX-License-Identifier: MPL-2.0 */

class route_materialized_table_t
{
  private:
    typedef uint32_t node_index_t;
    static constexpr node_index_t npos = static_cast<node_index_t> (UINT32_MAX);
    static constexpr size_t initial_bucket_count = 4;
    static constexpr size_t node_block_size = 65536;
    static constexpr size_t byte_block_size = 1024 * 1024;
    static constexpr size_t max_bucket_load = 1;

    struct string_ref_t
    {
        uint32_t offset;
        uint32_t size;

        string_ref_t () : offset (0), size (0) {}
        string_ref_t (uint32_t offset_, uint32_t size_) : offset (offset_), size (size_) {}
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

        const route_entry_t *operator->() const
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

        bool operator!= (const iterator &other_) const { return !(*this == other_); }

      private:
        friend class route_materialized_table_t;
        friend class const_iterator;

        iterator (route_materialized_table_t *table_, size_t index_) :
            _table (table_), _index (index_)
        {
            skip_unused ();
        }

        void skip_unused ()
        {
            if (!_table)
                return;
            while (_index < _table->_node_count && !_table->node_at (_index).used) {
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
        const_iterator (const iterator &it_) : _table (it_._table), _index (it_._index) {}

        const route_entry_t &operator* () const
        {
            _cache = _table->entry_at (_index);
            return _cache;
        }

        const route_entry_t *operator->() const
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

        bool operator!= (const const_iterator &other_) const { return !(*this == other_); }

      private:
        friend class route_materialized_table_t;

        const_iterator (const route_materialized_table_t *table_, size_t index_) :
            _table (table_), _index (index_)
        {
            skip_unused ();
        }

        void skip_unused ()
        {
            if (!_table)
                return;
            while (_index < _table->_node_count && !_table->node_at (_index).used) {
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
        mapped_reference (route_materialized_table_t *table_, size_t index_) :
            _table (table_), _index (index_)
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
        explicit rehash_pause_guard_t (route_materialized_table_t &table_) : _table (&table_)
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

    const_iterator end () const { return const_iterator (this, _node_count); }

    size_t
    snapshot_values (size_t cursor_, size_t max_records_, std::vector<route_entry_t> *out_) const
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
            if (table == 0 && is_rehashing () && bucket_index (table, hash) < _rehashidx) {
                continue;
            }
            const size_t bucket = bucket_index (table, hash);
            node_index_t prev = npos;
            node_index_t node = _tables[table].buckets[bucket];
            while (node != npos) {
                node_t &candidate = node_at (node);
                if (candidate.hash == hash && node_key_equals (candidate, key_)) {
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
    route_materialized_table_t &operator= (const route_materialized_table_t &);

    typedef std::unordered_map<owner_identity_t, size_t, owner_identity_hash_t> owner_intern_map_t;
    typedef std::unordered_map<std::string, string_ref_t> channel_ref_map_t;

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
        node.value =
          append_bytes (entry_.value.empty () ? NULL : &entry_.value[0], entry_.value.size ());
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
        if (_byte_blocks.empty () || _byte_count % byte_block_size + size_ > byte_block_size) {
            _byte_blocks.push_back (new unsigned char[byte_block_size]);
            _byte_count = (_byte_blocks.size () - 1) * byte_block_size;
        }
        const size_t offset = _byte_count;
        memcpy (byte_ptr (offset), data_, size_);
        _byte_count += size_;
        return string_ref_t (static_cast<uint32_t> (offset), static_cast<uint32_t> (size_));
    }

    string_ref_t append_string (const std::string &value_)
    {
        return append_bytes (reinterpret_cast<const unsigned char *> (value_.data ()),
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
        return std::string (reinterpret_cast<const char *> (byte_ptr (ref_.offset)), ref_.size);
    }

    void value_from_ref (const string_ref_t &ref_, std::vector<unsigned char> *out_) const
    {
        if (ref_.size == 0) {
            out_->clear ();
            return;
        }
        const unsigned char *data = byte_ptr (ref_.offset);
        out_->assign (data, data + ref_.size);
    }

    bool ref_equals (const string_ref_t &ref_, const std::string &value_) const
    {
        return ref_.size == value_.size ()
               && (ref_.size == 0
                   || memcmp (byte_ptr (ref_.offset), value_.data (), ref_.size) == 0);
    }

    bool node_key_equals (const node_t &node_, const route_key_t &key_) const
    {
        return node_.kind == key_.kind && ref_equals (node_.channel, key_.channel_name)
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
            _tables[table_].buckets.assign (next_power_of_two (size_), npos);
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
        if (_tables[0].used + 1 <= _tables[0].buckets.size () * max_bucket_load)
            return;
        _tables[1].buckets.assign (
          next_power_of_two ((_tables[0].used + max_bucket_load) / max_bucket_load), npos);
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
                if (candidate.hash == hash_ && node_key_equals (candidate, key_))
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
        for (std::vector<node_t *>::iterator it = _nodes.begin (); it != _nodes.end (); ++it)
            delete[] *it;
        _nodes.clear ();
    }

    unsigned char *byte_ptr (size_t offset_)
    {
        return _byte_blocks[offset_ / byte_block_size] + (offset_ % byte_block_size);
    }

    const unsigned char *byte_ptr (size_t offset_) const
    {
        return _byte_blocks[offset_ / byte_block_size] + (offset_ % byte_block_size);
    }

    void destroy_byte_blocks ()
    {
        for (std::vector<unsigned char *>::iterator it = _byte_blocks.begin ();
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
