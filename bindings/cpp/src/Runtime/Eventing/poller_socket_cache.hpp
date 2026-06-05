/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_EVENTING_POLLER_SOCKET_CACHE_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_EVENTING_POLLER_SOCKET_CACHE_HPP_INCLUDED

#include <zlink/Contracts/Eventing/poll_event.hpp>

#include <zlink.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace zlink
{

class timer_t;

struct poller_item_t
{
    void *socket_handle = nullptr;
    int fd = 0;
    void *timer_handle = nullptr;
    timer_t *timer = nullptr;
    poll_source_kind_t source_kind = poll_source_kind_t::socket;
    poll_event_flag_t events = poll_event_flag_t::none;
    std::uintptr_t slot = 0;
    bool native_poller_only = false;
};

inline bool is_socket_poll_item_active (const poller_item_t &item_, short events_) noexcept
{
    return events_ != 0 && !item_.native_poller_only && item_.source_kind == poll_source_kind_t::socket;
}

struct socket_poll_cache_t
{
    std::vector<zlink_pollitem_t> poll_items;
    std::vector<size_t> item_indexes;
    std::vector<size_t> item_positions;
    bool dirty = true;
    static constexpr size_t npos = static_cast<size_t> (-1);

    void clear ()
    {
        poll_items.clear ();
        item_indexes.clear ();
        item_positions.clear ();
        dirty = true;
    }

    void mark_dirty () noexcept { dirty = true; }

    void rebuild_if_needed (const std::vector<std::unique_ptr<poller_item_t>> &items_)
    {
        if (!dirty)
            return;

        poll_items.clear ();
        item_indexes.clear ();
        item_positions.assign (items_.size (), npos);
        poll_items.reserve (items_.size ());
        item_indexes.reserve (items_.size ());

        for (size_t i = 0; i < items_.size (); ++i) {
            const poller_item_t &item = *items_[i];
            if (item.source_kind != poll_source_kind_t::socket || item.native_poller_only)
                continue;
            const short events = static_cast<short> (item.events);
            if (events == 0)
                continue;

            zlink_pollitem_t poll_item;
            poll_item.socket = item.socket_handle;
            poll_item.fd = 0;
            poll_item.events = events;
            poll_item.revents = 0;
            item_positions[i] = poll_items.size ();
            poll_items.push_back (poll_item);
            item_indexes.push_back (i);
        }

        dirty = false;
    }

    void reset_revents () noexcept
    {
        for (size_t i = 0; i < poll_items.size (); ++i)
            poll_items[i].revents = 0;
    }

    void update_if_clean (std::vector<std::unique_ptr<poller_item_t>> &items_, size_t index_, poll_event_flag_t events_)
    {
        poller_item_t &item = *items_[index_];
        if (dirty) {
            item.events = events_;
            return;
        }

        if (item_positions.size () != items_.size ()) {
            dirty = true;
            item.events = events_;
            return;
        }

        const short old_events = static_cast<short> (item.events);
        const short new_events = static_cast<short> (events_);
        const bool old_active = is_socket_poll_item_active (item, old_events);
        const bool new_active = is_socket_poll_item_active (item, new_events);

        item.events = events_;

        const size_t position = item_positions[index_];
        if (old_active && position >= poll_items.size ()) {
            dirty = true;
            return;
        }

        if (old_active && new_active) {
            poll_items[position].events = new_events;
            return;
        }

        if (old_active && !new_active) {
            poll_items.erase (poll_items.begin () + position);
            item_indexes.erase (item_indexes.begin () + position);
            item_positions[index_] = npos;
            for (size_t i = position; i < item_indexes.size (); ++i)
                item_positions[item_indexes[i]] = i;
            return;
        }

        if (!old_active && new_active) {
            size_t insert_at = item_indexes.size ();
            for (size_t i = 0; i < item_indexes.size (); ++i) {
                if (item_indexes[i] > index_) {
                    insert_at = i;
                    break;
                }
            }
            zlink_pollitem_t poll_item;
            poll_item.socket = item.socket_handle;
            poll_item.fd = 0;
            poll_item.events = new_events;
            poll_item.revents = 0;
            poll_items.insert (poll_items.begin () + insert_at, poll_item);
            item_indexes.insert (item_indexes.begin () + insert_at, index_);
            for (size_t i = insert_at; i < item_indexes.size (); ++i)
                item_positions[item_indexes[i]] = i;
        }
    }
};

} // namespace zlink

#endif
