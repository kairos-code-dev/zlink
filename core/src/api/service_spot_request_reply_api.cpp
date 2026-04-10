/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "api/request_reply_protocol_internal.hpp"
#include "api/service_api_internal.hpp"
#include "api/socket_api_internal.hpp"
#include "core/recv_tls_view.hpp"
#include "services/spot/spot_pub.hpp"

namespace
{
enum : uint8_t
{
    zmp_spot_routed_protocol_id = 0x02,
    zmp_protocol_version = 0x01,
    zmp_spot_class = 0x01,
    zmp_router_class = 0x02
};

const size_t spot_routed_control_part_count = 8;

struct routing_pair_t
{
    std::string node_rid;
    std::string spot_rid;
};

struct pending_spot_key_t
{
    uint8_t source_class;
    std::string source_rid;
    std::string source_spot_rid;
    uint64_t request_seq;

    bool operator< (const pending_spot_key_t &other_) const
    {
        if (request_seq != other_.request_seq)
            return request_seq < other_.request_seq;
        if (source_class != other_.source_class)
            return source_class < other_.source_class;
        if (source_rid != other_.source_rid)
            return source_rid < other_.source_rid;
        return source_spot_rid < other_.source_spot_rid;
    }
};

struct pending_reply_t
{
    pending_spot_key_t key;
    zlink_reply_handler_fn handler;
    void *userdata;
};

struct queued_spot_message_t
{
    zlink_routing_id_t source_rid;
    zlink_routing_id_t spot_rid;
    uint64_t request_seq;
    std::vector<zlink_msg_t> parts;

    queued_spot_message_t () : request_seq (0)
    {
        memset (&source_rid, 0, sizeof (source_rid));
        memset (&spot_rid, 0, sizeof (spot_rid));
    }
};

struct parsed_spot_envelope_t
{
    uint8_t source_class;
    std::string source_node_rid;
    std::string source_endpoint_rid;
    uint8_t destination_class;
    std::string destination_node_rid;
    std::string destination_endpoint_rid;
    zlink_msg_t *payload_parts;
    size_t payload_part_count;
};

struct local_spot_request_reply_state_t
{
    explicit local_spot_request_reply_state_t (void *owner_) :
        owner (owner_),
        default_timeout_ms (zlink::request_reply::default_timeout_ms),
        next_request_seq (1),
        request_handler (NULL),
        request_handler_userdata (NULL),
        dispatch_event_handler (NULL),
        dispatch_event_handler_userdata (NULL)
    {
    }

    void *owner;
    std::mutex mutex;
    uint32_t default_timeout_ms;
    uint64_t next_request_seq;
    std::set<uint64_t> pending_sequences;
    std::map<pending_spot_key_t, pending_reply_t> pending_replies;
    std::condition_variable recv_cv;
    std::deque<queued_spot_message_t> queued_messages;
    zlink_spot_handler_fn request_handler;
    void *request_handler_userdata;
    zlink_spot_dispatch_event_handler_fn dispatch_event_handler;
    void *dispatch_event_handler_userdata;
};

struct spot_timer_handle_t
{
    explicit spot_timer_handle_t (void *owner_) :
        tag (0x1e6700dd),
        owner (owner_),
        destroyed (false),
        running (false),
        receive_callback_active (false),
        recv_in_progress (false),
        stop_requested (false),
        interval_ns (0),
        repeat_count (0),
        next_fire_count (1),
        handler (NULL),
        handler_userdata (NULL)
    {
    }

    bool check_tag () const { return tag == 0x1e6700dd; }

    uint32_t tag;
    void *owner;
    std::mutex mutex;
    std::condition_variable cv;
    std::condition_variable recv_cv;
    std::thread worker;
    bool destroyed;
    bool running;
    bool receive_callback_active;
    bool recv_in_progress;
    bool stop_requested;
    uint64_t interval_ns;
    uint64_t repeat_count;
    uint64_t next_fire_count;
    std::deque<uint64_t> fired_counts;
    zlink_spot_timer_handler_fn handler;
    void *handler_userdata;
};

struct local_router_spot_state_t
{
    explicit local_router_spot_state_t (void *owner_) :
        owner (owner_),
        default_timeout_ms (zlink::request_reply::default_timeout_ms),
        next_request_seq (1),
        handler (NULL),
        handler_userdata (NULL)
    {
    }

    void *owner;
    std::string router_rid;
    std::mutex mutex;
    std::condition_variable recv_cv;
    uint32_t default_timeout_ms;
    uint64_t next_request_seq;
    std::set<uint64_t> pending_sequences;
    std::map<uint64_t, pending_reply_t> pending_replies;
    std::deque<queued_spot_message_t> queued_messages;
    zlink_router_spot_handler_fn handler;
    void *handler_userdata;
};

typedef std::map<void *, std::shared_ptr<local_spot_request_reply_state_t> >
  local_spot_state_map_t;
typedef std::map<void *, std::shared_ptr<local_router_spot_state_t> >
  local_router_state_map_t;

std::mutex g_local_spot_states_mutex;
local_spot_state_map_t g_local_spot_states;
local_router_state_map_t g_local_router_states;
std::mutex g_spot_timers_mutex;
std::map<void *, std::set<spot_timer_handle_t *> > g_spot_timers_by_owner;

int validate_request_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if ((!parts_ && part_count_ > 0) || part_count_ == 0) {
        errno = EFAULT;
        return -1;
    }

    return 0;
}

bool has_valid_routing_id (const zlink_routing_id_t *peer_rid_)
{
    return peer_rid_ && peer_rid_->size > 0
           && peer_rid_->size <= sizeof (peer_rid_->data);
}

std::shared_ptr<local_spot_request_reply_state_t> try_find_spot_state (void *spot_)
{
    std::lock_guard<std::mutex> lock (g_local_spot_states_mutex);
    local_spot_state_map_t::iterator it = g_local_spot_states.find (spot_);
    if (it == g_local_spot_states.end ())
        return std::shared_ptr<local_spot_request_reply_state_t> ();
    return it->second;
}

spot_timer_handle_t *as_spot_timer_handle (void *timer_)
{
    spot_timer_handle_t *timer = static_cast<spot_timer_handle_t *> (timer_);
    return timer && timer->check_tag () ? timer : NULL;
}

void maybe_dispatch_spot_event (local_spot_request_reply_state_t *state_,
                                zlink_spot_dispatch_event_t event_)
{
    zlink_spot_dispatch_event_handler_fn handler = NULL;
    void *userdata = NULL;
    void *owner = NULL;
    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        handler = state_->dispatch_event_handler;
        userdata = state_->dispatch_event_handler_userdata;
        owner = state_->owner;
    }

    if (handler)
        handler (owner, event_, userdata);
}

void notify_spot_dispatch_event (void *spot_,
                                 zlink_spot_dispatch_event_t event_)
{
    std::shared_ptr<local_spot_request_reply_state_t> state =
      try_find_spot_state (spot_);
    if (!state)
        return;
    maybe_dispatch_spot_event (state.get (), event_);
}

void stop_spot_timer_worker (spot_timer_handle_t *timer_, bool join_)
{
    if (!timer_)
        return;

    {
        std::lock_guard<std::mutex> lock (timer_->mutex);
        timer_->stop_requested = true;
        timer_->running = false;
    }
    timer_->cv.notify_all ();
    timer_->recv_cv.notify_all ();

    if (join_ && timer_->worker.joinable ()) {
        if (timer_->worker.get_id () == std::this_thread::get_id ())
            timer_->worker.detach ();
        else
            timer_->worker.join ();
    }
}

void unregister_spot_timer (spot_timer_handle_t *timer_)
{
    if (!timer_)
        return;

    std::lock_guard<std::mutex> lock (g_spot_timers_mutex);
    std::map<void *, std::set<spot_timer_handle_t *> >::iterator it =
      g_spot_timers_by_owner.find (timer_->owner);
    if (it == g_spot_timers_by_owner.end ())
        return;

    it->second.erase (timer_);
    if (it->second.empty ())
        g_spot_timers_by_owner.erase (it);
}

void run_spot_timer_loop (spot_timer_handle_t *timer_)
{
    for (;;) {
        uint64_t interval_ns = 0;
        uint64_t remaining = 0;
        {
            std::unique_lock<std::mutex> lock (timer_->mutex);
            if (!timer_->running || timer_->stop_requested || timer_->destroyed)
                break;
            interval_ns = timer_->interval_ns;
            remaining = timer_->repeat_count;
        }

        if (interval_ns == 0) {
            std::this_thread::yield ();
            continue;
        }

        std::unique_lock<std::mutex> sleep_lock (timer_->mutex);
        if (timer_->cv.wait_for (
              sleep_lock, std::chrono::nanoseconds (interval_ns),
              [timer_]() { return timer_->stop_requested || timer_->destroyed; })) {
            continue;
        }

        if (!timer_->running || timer_->stop_requested || timer_->destroyed)
            continue;

        zlink_spot_timer_handler_fn handler = timer_->handler;
        void *handler_userdata = timer_->handler_userdata;
        const uint64_t fire_count = timer_->next_fire_count++;

        if (handler) {
            timer_->receive_callback_active = true;
            sleep_lock.unlock ();
            handler (timer_, fire_count, handler_userdata);
            sleep_lock.lock ();
        } else {
            timer_->fired_counts.push_back (fire_count);
            timer_->recv_cv.notify_one ();
        }

        notify_spot_dispatch_event (timer_->owner,
                                    ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE);

        if (remaining > 0) {
            --timer_->repeat_count;
            if (timer_->repeat_count == 0)
                timer_->running = false;
        }
    }
}

int queue_spot_message (
  local_spot_request_reply_state_t *state_,
  const zlink_routing_id_t *source_rid_,
  const zlink_routing_id_t *spot_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    if (!state_ || !parts_) {
        errno = EFAULT;
        return -1;
    }

    queued_spot_message_t queued;
    if (has_valid_routing_id (source_rid_))
        queued.source_rid = *source_rid_;
    if (has_valid_routing_id (spot_rid_))
        queued.spot_rid = *spot_rid_;
    queued.request_seq = request_seq_;
    queued.parts.resize (part_count_);
    for (size_t i = 0; i < part_count_; ++i) {
        zlink_msg_init (&queued.parts[i]);
        if (zlink_msg_move (&queued.parts[i], &parts_[i]) != 0) {
            const int saved_errno = errno;
            for (size_t j = 0; j <= i; ++j)
                zlink_msg_close (&queued.parts[j]);
            errno = saved_errno;
            return -1;
        }
    }

    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        state_->queued_messages.push_back (std::move (queued));
    }
    state_->recv_cv.notify_one ();
    maybe_dispatch_spot_event (state_,
                               ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE);
    return 0;
}

int export_queued_spot_message (queued_spot_message_t *queued_,
                                const zlink_routing_id_t **source_rid_out_,
                                const zlink_routing_id_t **spot_rid_out_,
                                uint64_t *request_seq_out_,
                                zlink_msg_t **parts_out_,
                                size_t *part_count_out_)
{
    if (!queued_ || !source_rid_out_ || !spot_rid_out_ || !request_seq_out_
        || !parts_out_ || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }

    if (zlink::recv_tls_view::begin (parts_out_, part_count_out_) != 0)
        return -1;

    for (size_t i = 0; i < queued_->parts.size (); ++i) {
        if (zlink::recv_tls_view::push (&queued_->parts[i]) != 0) {
            const int saved_errno = errno;
            zlink::recv_tls_view::abort ();
            errno = saved_errno;
            return -1;
        }
    }

    *source_rid_out_ = &queued_->source_rid;
    *spot_rid_out_ = queued_->spot_rid.size > 0 ? &queued_->spot_rid : NULL;
    *request_seq_out_ = queued_->request_seq;
    return zlink::recv_tls_view::commit (parts_out_, part_count_out_);
}

void dispatch_spot_message (local_spot_request_reply_state_t *state_,
                            const zlink_routing_id_t *source_rid_,
                            const zlink_routing_id_t *spot_rid_,
                            uint64_t request_seq_,
                            zlink_msg_t *parts_,
                            size_t part_count_)
{
    zlink_spot_handler_fn handler = NULL;
    void *handler_userdata = NULL;
    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        handler = state_->request_handler;
        handler_userdata = state_->request_handler_userdata;
    }

    if (handler) {
        handler (source_rid_, spot_rid_, request_seq_, parts_, part_count_,
                 handler_userdata);
        return;
    }

    if (queue_spot_message (state_, source_rid_, spot_rid_, request_seq_,
                            parts_, part_count_)
        != 0) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
    }
}

void dispatch_router_spot_message (local_router_spot_state_t *state_,
                                   const zlink_routing_id_t *source_node_rid_,
                                   const zlink_routing_id_t *source_spot_rid_,
                                   uint64_t request_seq_,
                                   zlink_msg_t *parts_,
                                   size_t part_count_)
{
    zlink_router_spot_handler_fn handler = NULL;
    void *handler_userdata = NULL;
    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        handler = state_->handler;
        handler_userdata = state_->handler_userdata;
    }

    if (handler) {
        handler (source_node_rid_, source_spot_rid_, request_seq_, parts_,
                 part_count_, handler_userdata);
        return;
    }

    queued_spot_message_t queued;
    if (has_valid_routing_id (source_node_rid_))
        queued.source_rid = *source_node_rid_;
    if (has_valid_routing_id (source_spot_rid_))
        queued.spot_rid = *source_spot_rid_;
    queued.request_seq = request_seq_;
    queued.parts.resize (part_count_);
    for (size_t i = 0; i < part_count_; ++i) {
        zlink_msg_init (&queued.parts[i]);
        if (zlink_msg_move (&queued.parts[i], &parts_[i]) != 0) {
            const int saved_errno = errno;
            for (size_t j = 0; j <= i; ++j)
                zlink_msg_close (&queued.parts[j]);
            zlink::request_reply::close_request_reply_parts (parts_, part_count_);
            errno = saved_errno;
            return;
        }
    }

    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        state_->queued_messages.push_back (std::move (queued));
    }
    state_->recv_cv.notify_one ();
}

std::string routing_id_key (const zlink_routing_id_t *peer_rid_)
{
    if (!has_valid_routing_id (peer_rid_))
        return std::string ();

    return std::string (reinterpret_cast<const char *> (peer_rid_->data),
                        peer_rid_->size);
}

void routing_id_from_string (const std::string &value_, zlink_routing_id_t *out_)
{
    if (!out_)
        return;

    memset (out_, 0, sizeof (*out_));
    if (value_.empty ())
        return;

    const size_t size =
      value_.size () > sizeof (out_->data) ? sizeof (out_->data) : value_.size ();
    memcpy (out_->data, value_.data (), size);
    out_->size = static_cast<uint8_t> (size);
}

bool parse_spot_routed_envelope (zlink_msg_t *parts_,
                                 size_t part_count_,
                                 parsed_spot_envelope_t *out_)
{
    if (!parts_ || !out_ || part_count_ < spot_routed_control_part_count)
        return false;

    zlink::msg_t *protocol_id =
      reinterpret_cast<zlink::msg_t *> (&parts_[0]);
    if (!protocol_id->check ()
        || (protocol_id->flags () & zlink::msg_t::command) == 0
        || !zlink::request_reply::frame_is_single_byte_value (
          &parts_[0], zmp_spot_routed_protocol_id)
        || !zlink::request_reply::frame_is_single_byte_value (
          &parts_[1], zmp_protocol_version)) {
        return false;
    }

    if (!zlink::request_reply::frame_is_single_byte_value (&parts_[2],
                                                           zmp_spot_class)
        && !zlink::request_reply::frame_is_single_byte_value (&parts_[2],
                                                              zmp_router_class)) {
        return false;
    }
    if (!zlink::request_reply::frame_is_single_byte_value (&parts_[5],
                                                           zmp_spot_class)
        && !zlink::request_reply::frame_is_single_byte_value (&parts_[5],
                                                              zmp_router_class)) {
        return false;
    }

    out_->source_class =
      static_cast<const unsigned char *> (zlink_msg_data (&parts_[2]))[0];
    out_->source_node_rid.assign (
      static_cast<const char *> (zlink_msg_data (&parts_[3])),
      zlink_msg_size (&parts_[3]));
    out_->source_endpoint_rid.assign (
      static_cast<const char *> (zlink_msg_data (&parts_[4])),
      zlink_msg_size (&parts_[4]));
    out_->destination_class =
      static_cast<const unsigned char *> (zlink_msg_data (&parts_[5]))[0];
    out_->destination_node_rid.assign (
      static_cast<const char *> (zlink_msg_data (&parts_[6])),
      zlink_msg_size (&parts_[6]));
    out_->destination_endpoint_rid.assign (
      static_cast<const char *> (zlink_msg_data (&parts_[7])),
      zlink_msg_size (&parts_[7]));
    out_->payload_parts = parts_ + spot_routed_control_part_count;
    out_->payload_part_count = part_count_ - spot_routed_control_part_count;
    return true;
}

bool resolve_spot_identity (void *spot_, routing_pair_t *out_)
{
    if (!out_) {
        errno = EFAULT;
        return false;
    }

    if (spot_handle_t *spot = as_spot_handle (spot_)) {
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return false;

        zlink::spot_pub_t *spot_pub = ensure_spot_pub (spot);
        zlink::spot_pub_t *node_pub =
          spot->node ? spot->node->ensure_default_pub () : NULL;
        if (!spot_pub || !node_pub)
            return false;

        zlink_routing_id_t node_rid;
        zlink_routing_id_t spot_rid;
        memset (&node_rid, 0, sizeof (node_rid));
        memset (&spot_rid, 0, sizeof (spot_rid));
        if (node_pub->routing_id (&node_rid) != 0
            || spot_pub->routing_id (&spot_rid) != 0) {
            return false;
        }

        out_->node_rid = routing_id_key (&node_rid);
        out_->spot_rid = routing_id_key (&spot_rid);
        return !out_->node_rid.empty () && !out_->spot_rid.empty ();
    }

    errno = EFAULT;
    return false;
}

std::shared_ptr<local_spot_request_reply_state_t>
find_or_create_spot_state (void *spot_)
{
    std::lock_guard<std::mutex> lock (g_local_spot_states_mutex);
    local_spot_state_map_t::iterator it = g_local_spot_states.find (spot_);
    if (it != g_local_spot_states.end ())
        return it->second;

    std::shared_ptr<local_spot_request_reply_state_t> state (
      new local_spot_request_reply_state_t (spot_));
    g_local_spot_states[spot_] = state;
    return state;
}

std::shared_ptr<local_router_spot_state_t>
find_or_create_router_state (void *router_)
{
    std::lock_guard<std::mutex> lock (g_local_spot_states_mutex);
    local_router_state_map_t::iterator it = g_local_router_states.find (router_);
    if (it != g_local_router_states.end ())
        return it->second;

    std::shared_ptr<local_router_spot_state_t> state (
      new local_router_spot_state_t (router_));
    g_local_router_states[router_] = state;
    return state;
}

std::shared_ptr<local_spot_request_reply_state_t>
find_spot_state_by_identity (const std::string &node_rid_,
                             const std::string &spot_rid_)
{
    std::lock_guard<std::mutex> lock (g_local_spot_states_mutex);
    for (local_spot_state_map_t::const_iterator it = g_local_spot_states.begin ();
         it != g_local_spot_states.end (); ++it) {
        routing_pair_t current;
        if (!resolve_spot_identity (it->first, &current))
            continue;
        if (current.node_rid == node_rid_ && current.spot_rid == spot_rid_)
            return it->second;
    }
    return std::shared_ptr<local_spot_request_reply_state_t> ();
}

std::shared_ptr<local_router_spot_state_t>
find_router_state_by_rid (const std::string &router_rid_)
{
    std::lock_guard<std::mutex> lock (g_local_spot_states_mutex);
    for (local_router_state_map_t::const_iterator it =
           g_local_router_states.begin ();
         it != g_local_router_states.end (); ++it) {
        if (it->second && it->second->router_rid == router_rid_)
            return it->second;
    }
    return std::shared_ptr<local_router_spot_state_t> ();
}

uint64_t allocate_request_seq (uint64_t *next_request_seq_,
                               const std::set<uint64_t> &pending_sequences_)
{
    if (!next_request_seq_) {
        errno = EFAULT;
        return 0;
    }

    const uint64_t start = *next_request_seq_ == 0 ? 1 : *next_request_seq_;
    uint64_t candidate = start;

    do {
        if (candidate == 0)
            candidate = 1;

        if (pending_sequences_.count (candidate) == 0) {
            uint64_t next = candidate + 1;
            if (next == 0)
                next = 1;
            *next_request_seq_ = next;
            return candidate;
        }

        ++candidate;
        if (candidate == 0)
            candidate = 1;
    } while (candidate != start);

    errno = EBUSY;
    return 0;
}

void schedule_spot_timeout (
  std::shared_ptr<local_spot_request_reply_state_t> state_,
  const pending_spot_key_t &key_,
  uint32_t timeout_ms_)
{
    std::thread ([state_, key_, timeout_ms_]() {
        const uint32_t sleep_slice_ms = 10;
        uint32_t waited_ms = 0;
        while (waited_ms < timeout_ms_) {
            {
                std::lock_guard<std::mutex> lock (state_->mutex);
                if (state_->pending_replies.find (key_)
                    == state_->pending_replies.end ()) {
                    return;
                }
            }

            const uint32_t remaining_ms = timeout_ms_ - waited_ms;
            const uint32_t step_ms =
              remaining_ms < sleep_slice_ms ? remaining_ms : sleep_slice_ms;
            std::this_thread::sleep_for (std::chrono::milliseconds (step_ms));
            waited_ms += step_ms;
        }

        pending_reply_t pending;
        bool found = false;
        {
            std::lock_guard<std::mutex> lock (state_->mutex);
            std::map<pending_spot_key_t, pending_reply_t>::iterator it =
              state_->pending_replies.find (key_);
            if (it == state_->pending_replies.end ())
                return;
            pending = it->second;
            state_->pending_sequences.erase (key_.request_seq);
            state_->pending_replies.erase (it);
            found = true;
        }

        if (found)
            zlink::request_reply::complete_reply_callback (
              pending.handler, ETIMEDOUT, NULL, 0, pending.userdata);
    }).detach ();
}

void schedule_router_timeout (
  std::shared_ptr<local_router_spot_state_t> state_,
  uint64_t request_seq_,
  uint32_t timeout_ms_)
{
    std::thread ([state_, request_seq_, timeout_ms_]() {
        const uint32_t sleep_slice_ms = 10;
        uint32_t waited_ms = 0;
        while (waited_ms < timeout_ms_) {
            {
                std::lock_guard<std::mutex> lock (state_->mutex);
                if (state_->pending_replies.find (request_seq_)
                    == state_->pending_replies.end ()) {
                    return;
                }
            }

            const uint32_t remaining_ms = timeout_ms_ - waited_ms;
            const uint32_t step_ms =
              remaining_ms < sleep_slice_ms ? remaining_ms : sleep_slice_ms;
            std::this_thread::sleep_for (std::chrono::milliseconds (step_ms));
            waited_ms += step_ms;
        }

        pending_reply_t pending;
        bool found = false;
        {
            std::lock_guard<std::mutex> lock (state_->mutex);
            std::map<uint64_t, pending_reply_t>::iterator it =
              state_->pending_replies.find (request_seq_);
            if (it == state_->pending_replies.end ())
                return;
            pending = it->second;
            state_->pending_sequences.erase (request_seq_);
            state_->pending_replies.erase (it);
            found = true;
        }

        if (found)
            zlink::request_reply::complete_reply_callback (
              pending.handler, ETIMEDOUT, NULL, 0, pending.userdata);
    }).detach ();
}

bool parse_combined_local_message (
  std::vector<zlink_msg_t> *combined_,
  parsed_spot_envelope_t *spot_envelope_out_,
  zlink::request_reply::parsed_envelope_t *request_reply_envelope_out_)
{
    if (!combined_ || !spot_envelope_out_ || !request_reply_envelope_out_) {
        errno = EFAULT;
        return false;
    }

    if (!parse_spot_routed_envelope (&(*combined_)[0], combined_->size (),
                                     spot_envelope_out_)) {
        errno = EPROTO;
        return false;
    }

    if (!zlink::request_reply::parse_envelope (
          spot_envelope_out_->payload_parts, spot_envelope_out_->payload_part_count,
          request_reply_envelope_out_)) {
        errno = EPROTO;
        return false;
    }

    return true;
}

int build_spot_request_reply_message (uint8_t source_class_,
                                      const std::string &source_node_rid_,
                                      const std::string &source_endpoint_rid_,
                                      uint8_t destination_class_,
                                      const std::string &destination_node_rid_,
                                      const std::string &destination_endpoint_rid_,
                                      uint8_t message_type_,
                                      uint64_t request_seq_,
                                      zlink_msg_t *parts_,
                                      size_t part_count_,
                                      std::vector<zlink_msg_t> *out_)
{
    if (!parts_ || part_count_ == 0 || request_seq_ == 0 || !out_) {
        errno = EINVAL;
        return -1;
    }

    const size_t total_part_count =
      spot_routed_control_part_count + zlink::request_reply::control_part_count
      + part_count_;
    out_->resize (total_part_count);
    for (size_t i = 0; i < total_part_count; ++i)
        zlink_msg_init (&(*out_)[i]);

    unsigned char spot_protocol_id = zmp_spot_routed_protocol_id;
    unsigned char version = zmp_protocol_version;
    unsigned char source_class = source_class_;
    unsigned char destination_class = destination_class_;
    unsigned char rr_protocol_id = zlink::request_reply::protocol_id;
    unsigned char rr_type = message_type_;
    unsigned char seq_buf[8];
    zlink::request_reply::encode_u64_be (request_seq_, seq_buf);

    if (zlink::request_reply::init_control_part (&(*out_)[0], &spot_protocol_id, 1)
          != 0
        || zlink::request_reply::init_control_part (&(*out_)[1], &version, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[2], &source_class, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[3],
                                                    source_node_rid_.data (),
                                                    source_node_rid_.size ())
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[4],
                                                    source_endpoint_rid_.data (),
                                                    source_endpoint_rid_.size ())
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[5],
                                                    &destination_class, 1)
             != 0
        || zlink::request_reply::init_control_part (
             &(*out_)[6], destination_node_rid_.data (),
             destination_node_rid_.size ())
             != 0
        || zlink::request_reply::init_control_part (
             &(*out_)[7], destination_endpoint_rid_.data (),
             destination_endpoint_rid_.size ())
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[8],
                                                    &rr_protocol_id, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[9], &version, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[10], &rr_type, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[11], seq_buf,
                                                    sizeof (seq_buf))
             != 0) {
        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (out_);
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        errno = saved_errno;
        return -1;
    }

    for (size_t i = 0; i < part_count_; ++i) {
        if (zlink_msg_move (&(*out_)[spot_routed_control_part_count
                                     + zlink::request_reply::control_part_count
                                     + i],
                            &parts_[i])
            != 0) {
            const int saved_errno = errno;
            zlink::request_reply::close_built_parts (out_);
            zlink::request_reply::consume_send_frames_from (parts_, i,
                                                            part_count_);
            errno = saved_errno;
            return -1;
        }
    }

    return 0;
}

int deliver_reply_to_spot (
  const parsed_spot_envelope_t &spot_envelope_,
  const zlink::request_reply::parsed_envelope_t &rr_envelope_)
{
    std::shared_ptr<local_spot_request_reply_state_t> state =
      find_spot_state_by_identity (spot_envelope_.destination_node_rid,
                                   spot_envelope_.destination_endpoint_rid);
    if (!state) {
        errno = ENOENT;
        return -1;
    }

    pending_spot_key_t key;
    key.source_class = spot_envelope_.source_class;
    key.source_rid = spot_envelope_.source_class == zmp_router_class
                       ? spot_envelope_.source_endpoint_rid
                       : spot_envelope_.source_node_rid;
    key.source_spot_rid = spot_envelope_.source_class == zmp_spot_class
                            ? spot_envelope_.source_endpoint_rid
                            : std::string ();
    key.request_seq = rr_envelope_.request_seq;

    pending_reply_t pending;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        std::map<pending_spot_key_t, pending_reply_t>::iterator it =
          state->pending_replies.find (key);
        if (it != state->pending_replies.end ()) {
            pending = it->second;
            state->pending_sequences.erase (key.request_seq);
            state->pending_replies.erase (it);
            found = true;
        }
    }

    if (!found)
        return 0;

    int callback_errno = 0;
    zlink_msg_t *callback_parts = rr_envelope_.payload_parts;
    size_t callback_part_count = rr_envelope_.payload_part_count;
    if (zlink::request_reply::decode_reply_completion (
          rr_envelope_.message_type, rr_envelope_.payload_parts,
          rr_envelope_.payload_part_count, &callback_errno, &callback_parts,
          &callback_part_count)
        != 0) {
        return -1;
    }

    zlink::request_reply::complete_reply_callback (
      pending.handler, callback_errno, callback_parts, callback_part_count,
      pending.userdata);
    return 0;
}

int deliver_reply_to_router (const std::string &router_rid_,
                             const zlink::request_reply::parsed_envelope_t
                               &rr_envelope_)
{
    std::shared_ptr<local_router_spot_state_t> state =
      find_router_state_by_rid (router_rid_);
    if (!state) {
        errno = ENOENT;
        return -1;
    }

    pending_reply_t pending;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        std::map<uint64_t, pending_reply_t>::iterator it =
          state->pending_replies.find (rr_envelope_.request_seq);
        if (it != state->pending_replies.end ()) {
            pending = it->second;
            state->pending_sequences.erase (rr_envelope_.request_seq);
            state->pending_replies.erase (it);
            found = true;
        }
    }

    if (!found)
        return 0;

    int callback_errno = 0;
    zlink_msg_t *callback_parts = rr_envelope_.payload_parts;
    size_t callback_part_count = rr_envelope_.payload_part_count;
    if (zlink::request_reply::decode_reply_completion (
          rr_envelope_.message_type, rr_envelope_.payload_parts,
          rr_envelope_.payload_part_count, &callback_errno, &callback_parts,
          &callback_part_count)
        != 0) {
        return -1;
    }

    zlink::request_reply::complete_reply_callback (
      pending.handler, callback_errno, callback_parts, callback_part_count,
      pending.userdata);
    return 0;
}

int dispatch_local_reply (std::vector<zlink_msg_t> *combined_)
{
    if (!combined_) {
        errno = EFAULT;
        return -1;
    }

    parsed_spot_envelope_t spot_envelope;
    zlink::request_reply::parsed_envelope_t rr_envelope;
    if (!parse_combined_local_message (combined_, &spot_envelope, &rr_envelope))
        return -1;

    if (spot_envelope.destination_class == zmp_spot_class)
        return deliver_reply_to_spot (spot_envelope, rr_envelope);

    return deliver_reply_to_router (spot_envelope.destination_endpoint_rid,
                                    rr_envelope);
}

int synthesize_local_error_reply (const parsed_spot_envelope_t &request_envelope_,
                                  uint64_t request_seq_,
                                  int errnum_)
{
    zlink_msg_t errno_part;
    zlink_msg_init (&errno_part);

    unsigned char errbuf[4];
    zlink::request_reply::encode_u32_be (static_cast<uint32_t> (errnum_),
                                         errbuf);
    if (zlink_msg_init_size (&errno_part, sizeof (errbuf)) != 0)
        return -1;
    memcpy (zlink_msg_data (&errno_part), errbuf, sizeof (errbuf));

    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          request_envelope_.destination_class,
          request_envelope_.destination_node_rid,
          request_envelope_.destination_endpoint_rid,
          request_envelope_.source_class, request_envelope_.source_node_rid,
          request_envelope_.source_endpoint_rid,
          zlink::request_reply::error_reply_type,
          request_seq_, &errno_part, 1, &combined)
        != 0) {
        const int saved_errno = errno;
        zlink::request_reply::consume_send_frame (&errno_part);
        errno = saved_errno;
        return -1;
    }

    const int rc = dispatch_local_reply (&combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return rc;
}

int dispatch_spot_request_to_spot (const parsed_spot_envelope_t &spot_envelope_,
                                   const zlink::request_reply::parsed_envelope_t
                                     &rr_envelope_)
{
    std::shared_ptr<local_spot_request_reply_state_t> state =
      find_spot_state_by_identity (spot_envelope_.destination_node_rid,
                                   spot_envelope_.destination_endpoint_rid);
    if (!state) {
        // Local short-circuit still preserves wire semantics by building the
        // same request envelope and completing the requester through a
        // protocol-level error reply instead of a direct local failure.
        return synthesize_local_error_reply (spot_envelope_,
                                             rr_envelope_.request_seq,
                                             ENOENT);
    }

    zlink_routing_id_t source_rid;
    zlink_routing_id_t source_spot_rid;
    routing_id_from_string (
      spot_envelope_.source_class == zmp_router_class
        ? spot_envelope_.source_endpoint_rid
        : spot_envelope_.source_node_rid,
      &source_rid);
    routing_id_from_string (
      spot_envelope_.source_class == zmp_spot_class
        ? spot_envelope_.source_endpoint_rid
        : std::string (),
      &source_spot_rid);

    dispatch_spot_message (state.get (), &source_rid, &source_spot_rid,
                           rr_envelope_.request_seq, rr_envelope_.payload_parts,
                           rr_envelope_.payload_part_count);
    return 0;
}

int dispatch_spot_request_to_router (
  const std::string &router_rid_,
  const parsed_spot_envelope_t &spot_envelope_,
  const zlink::request_reply::parsed_envelope_t &rr_envelope_)
{
    std::shared_ptr<local_router_spot_state_t> state =
      find_router_state_by_rid (router_rid_);
    if (!state) {
        return synthesize_local_error_reply (spot_envelope_,
                                             rr_envelope_.request_seq,
                                             ENOENT);
    }

    zlink_router_spot_handler_fn handler = NULL;
    void *handler_userdata = NULL;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        handler = state->handler;
        handler_userdata = state->handler_userdata;
    }

    zlink_routing_id_t source_node_rid;
    zlink_routing_id_t source_spot_rid;
    routing_id_from_string (spot_envelope_.source_node_rid, &source_node_rid);
    routing_id_from_string (spot_envelope_.source_endpoint_rid,
                            &source_spot_rid);
    if (handler) {
        handler (&source_node_rid, &source_spot_rid, rr_envelope_.request_seq,
                 rr_envelope_.payload_parts, rr_envelope_.payload_part_count,
                 handler_userdata);
        return 0;
    }

    dispatch_router_spot_message (state.get (), &source_node_rid,
                                  &source_spot_rid, rr_envelope_.request_seq,
                                  rr_envelope_.payload_parts,
                                  rr_envelope_.payload_part_count);
    return 0;
}

int dispatch_local_request (const std::string &router_rid_,
                            std::vector<zlink_msg_t> *combined_)
{
    parsed_spot_envelope_t spot_envelope;
    zlink::request_reply::parsed_envelope_t rr_envelope;
    if (!parse_combined_local_message (combined_, &spot_envelope, &rr_envelope))
        return -1;

    if (spot_envelope.destination_class == zmp_spot_class)
        return dispatch_spot_request_to_spot (spot_envelope, rr_envelope);

    return dispatch_spot_request_to_router (router_rid_, spot_envelope,
                                            rr_envelope);
}

void erase_spot_pending_request (
  const std::shared_ptr<local_spot_request_reply_state_t> &state_,
  const pending_spot_key_t &key_)
{
    std::lock_guard<std::mutex> lock (state_->mutex);
    state_->pending_sequences.erase (key_.request_seq);
    state_->pending_replies.erase (key_);
}

int dispatch_local_built_message (uint8_t source_class_,
                                  const std::string &source_node_rid_,
                                  const std::string &source_endpoint_rid_,
                                  uint8_t destination_class_,
                                  const std::string &destination_node_rid_,
                                  const std::string &destination_endpoint_rid_,
                                  uint8_t message_type_,
                                  uint64_t request_seq_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_)
{
    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          source_class_, source_node_rid_, source_endpoint_rid_,
          destination_class_, destination_node_rid_, destination_endpoint_rid_,
          message_type_, request_seq_, parts_, part_count_, &combined)
        != 0) {
        return -1;
    }

    const int rc = dispatch_local_reply (&combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return rc;
}

int start_spot_request_common (void *spot_,
                               uint8_t destination_class_,
                               const std::string &destination_node_rid_,
                               const std::string &destination_endpoint_rid_,
                               uint8_t pending_source_class_,
                               const std::string &pending_source_rid_,
                               const std::string &pending_source_spot_rid_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               uint32_t timeout_ms_,
                               zlink_reply_handler_fn handler_,
                               void *userdata_)
{
    routing_pair_t source_identity;
    if (!resolve_spot_identity (spot_, &source_identity))
        return -1;

    std::shared_ptr<local_spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    pending_spot_key_t key;
    uint32_t resolved_timeout_ms = zlink::request_reply::default_timeout_ms;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        const uint64_t request_seq =
          allocate_request_seq (&state->next_request_seq, state->pending_sequences);
        if (request_seq == 0)
            return -1;

        key.source_class = pending_source_class_;
        key.source_rid = pending_source_rid_;
        key.source_spot_rid = pending_source_spot_rid_;
        key.request_seq = request_seq;
        pending_reply_t pending = {key, handler_, userdata_};
        state->pending_sequences.insert (request_seq);
        state->pending_replies[key] = pending;
        resolved_timeout_ms = zlink::request_reply::resolve_timeout_ms (
          timeout_ms_, state->default_timeout_ms);
    }

    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          zmp_spot_class, source_identity.node_rid, source_identity.spot_rid,
          destination_class_, destination_node_rid_, destination_endpoint_rid_,
          zlink::request_reply::request_type, key.request_seq, parts_,
          part_count_, &combined)
        != 0) {
        erase_spot_pending_request (state, key);
        return -1;
    }

    if (dispatch_local_request (destination_class_ == zmp_router_class
                                  ? destination_endpoint_rid_
                                  : std::string (),
                                &combined)
        != 0) {
        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (&combined);
        erase_spot_pending_request (state, key);
        errno = saved_errno;
        return -1;
    }

    zlink::request_reply::close_built_parts (&combined);
    schedule_spot_timeout (state, key, resolved_timeout_ms);
    return 0;
}

int start_spot_request_to_spot (void *spot_,
                                const zlink_routing_id_t *dest_node_rid_,
                                const zlink_routing_id_t *dest_spot_rid_,
                                zlink_msg_t *parts_,
                                size_t part_count_,
                                uint32_t timeout_ms_,
                                zlink_reply_handler_fn handler_,
                                void *userdata_)
{
    if (!has_valid_routing_id (dest_node_rid_) || !has_valid_routing_id (dest_spot_rid_)
        || !handler_) {
        errno = EINVAL;
        return -1;
    }

    return start_spot_request_common (
      spot_, zmp_spot_class, routing_id_key (dest_node_rid_),
      routing_id_key (dest_spot_rid_), zmp_spot_class,
      routing_id_key (dest_node_rid_), routing_id_key (dest_spot_rid_), parts_,
      part_count_, timeout_ms_, handler_, userdata_);
}

int start_spot_request_to_router (void *spot_,
                                  const zlink_routing_id_t *peer_rid_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_,
                                  uint32_t timeout_ms_,
                                  zlink_reply_handler_fn handler_,
                                  void *userdata_)
{
    if (!has_valid_routing_id (peer_rid_) || !handler_) {
        errno = EINVAL;
        return -1;
    }

    return start_spot_request_common (
      spot_, zmp_router_class, std::string (), routing_id_key (peer_rid_),
      zmp_router_class, routing_id_key (peer_rid_), std::string (), parts_,
      part_count_, timeout_ms_, handler_, userdata_);
}

int start_router_request_to_spot (void *router_,
                                  const zlink_routing_id_t *dest_node_rid_,
                                  const zlink_routing_id_t *dest_spot_rid_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_,
                                  uint32_t timeout_ms_,
                                  zlink_reply_handler_fn handler_,
                                  void *userdata_)
{
    if (!handler_ || !has_valid_routing_id (dest_node_rid_)
        || !has_valid_routing_id (dest_spot_rid_)) {
        errno = EINVAL;
        return -1;
    }

    zlink_routing_id_t router_rid;
    memset (&router_rid, 0, sizeof (router_rid));
    if (zlink_get_routing_id (router_, &router_rid) != 0 || router_rid.size == 0)
        return -1;

    std::shared_ptr<local_router_spot_state_t> state =
      find_or_create_router_state (router_);
    state->router_rid = routing_id_key (&router_rid);

    uint64_t request_seq = 0;
    uint32_t resolved_timeout_ms = zlink::request_reply::default_timeout_ms;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        request_seq =
          allocate_request_seq (&state->next_request_seq, state->pending_sequences);
        if (request_seq == 0)
            return -1;

        pending_spot_key_t key;
        key.source_class = zmp_spot_class;
        key.source_rid = routing_id_key (dest_node_rid_);
        key.source_spot_rid = routing_id_key (dest_spot_rid_);
        key.request_seq = request_seq;
        pending_reply_t pending = {key, handler_, userdata_};
        state->pending_sequences.insert (request_seq);
        state->pending_replies[request_seq] = pending;
        resolved_timeout_ms = zlink::request_reply::resolve_timeout_ms (
          timeout_ms_, state->default_timeout_ms);
    }

    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          zmp_router_class, std::string (), state->router_rid, zmp_spot_class,
          routing_id_key (dest_node_rid_), routing_id_key (dest_spot_rid_),
          zlink::request_reply::request_type, request_seq, parts_, part_count_,
          &combined)
        != 0) {
        std::lock_guard<std::mutex> lock (state->mutex);
        state->pending_sequences.erase (request_seq);
        state->pending_replies.erase (request_seq);
        return -1;
    }

    if (dispatch_local_request (std::string (), &combined) != 0) {
        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (&combined);
        std::lock_guard<std::mutex> lock (state->mutex);
        state->pending_sequences.erase (request_seq);
        state->pending_replies.erase (request_seq);
        errno = saved_errno;
        return -1;
    }

    zlink::request_reply::close_built_parts (&combined);
    schedule_router_timeout (state, request_seq, resolved_timeout_ms);
    return 0;
}

int dispatch_local_direct_to_spot (uint8_t source_class_,
                                   const std::string &source_node_rid_,
                                   const std::string &source_endpoint_rid_,
                                   const std::string &dest_node_rid_,
                                   const std::string &dest_spot_rid_,
                                   zlink_msg_t *parts_,
                                   size_t part_count_)
{
    std::shared_ptr<local_spot_request_reply_state_t> state =
      find_spot_state_by_identity (dest_node_rid_, dest_spot_rid_);
    if (!state) {
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        errno = 0;
        return 0;
    }

    zlink_routing_id_t source_rid;
    zlink_routing_id_t spot_rid;
    routing_id_from_string (
      source_class_ == zmp_router_class ? source_endpoint_rid_ : source_node_rid_,
      &source_rid);
    routing_id_from_string (
      source_class_ == zmp_spot_class ? source_endpoint_rid_ : std::string (),
      &spot_rid);

    dispatch_spot_message (state.get (), &source_rid,
                           spot_rid.size > 0 ? &spot_rid : NULL, 0, parts_,
                           part_count_);
    errno = 0;
    return 0;
}
}

int zlink_spot_request_spot (void *spot_,
                             const zlink_routing_id_t *dest_node_rid_,
                             const zlink_routing_id_t *dest_spot_rid_,
                             zlink_msg_t *parts_,
                             size_t part_count_,
                             uint32_t timeout_ms_,
                             zlink_reply_handler_fn handler_,
                             void *userdata_)
{
    if (validate_request_parts (parts_, part_count_) != 0)
        return -1;
    return start_spot_request_to_spot (spot_, dest_node_rid_, dest_spot_rid_,
                                       parts_, part_count_, timeout_ms_,
                                       handler_, userdata_);
}

int zlink_spot_send_spot (void *spot_,
                          const zlink_routing_id_t *dest_node_rid_,
                          const zlink_routing_id_t *dest_spot_rid_,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          zlink_send_flags_t flags_)
{
    LIBZLINK_UNUSED (flags_);
    if (validate_request_parts (parts_, part_count_) != 0)
        return -1;
    if (!has_valid_routing_id (dest_node_rid_) || !has_valid_routing_id (dest_spot_rid_)) {
        errno = EINVAL;
        return -1;
    }
    if (validate_recv_flags (flags_) != 0)
        return -1;

    routing_pair_t source_identity;
    if (!resolve_spot_identity (spot_, &source_identity))
        return -1;

    return dispatch_local_direct_to_spot (
      zmp_spot_class, source_identity.node_rid, source_identity.spot_rid,
      routing_id_key (dest_node_rid_), routing_id_key (dest_spot_rid_), parts_,
      part_count_);
}

int zlink_spot_send_router (void *spot_,
                            const zlink_routing_id_t *peer_rid_,
                            zlink_msg_t *parts_,
                            size_t part_count_,
                            zlink_send_flags_t flags_)
{
    if (validate_request_parts (parts_, part_count_) != 0)
        return -1;
    if (!has_valid_routing_id (peer_rid_)) {
        errno = EINVAL;
        return -1;
    }
    if (validate_recv_flags (flags_) != 0)
        return -1;

    routing_pair_t source_identity;
    if (!resolve_spot_identity (spot_, &source_identity))
        return -1;

    std::shared_ptr<local_router_spot_state_t> state =
      find_router_state_by_rid (routing_id_key (peer_rid_));
    if (!state) {
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        errno = ENOENT;
        return -1;
    }

    zlink_routing_id_t source_node_rid;
    zlink_routing_id_t source_spot_rid;
    routing_id_from_string (source_identity.node_rid, &source_node_rid);
    routing_id_from_string (source_identity.spot_rid, &source_spot_rid);
    dispatch_router_spot_message (state.get (), &source_node_rid,
                                  &source_spot_rid, 0, parts_, part_count_);
    errno = 0;
    return 0;
}

int zlink_spot_request_router (void *spot_,
                               const zlink_routing_id_t *peer_rid_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               uint32_t timeout_ms_,
                               zlink_reply_handler_fn handler_,
                               void *userdata_)
{
    if (validate_request_parts (parts_, part_count_) != 0)
        return -1;
    return start_spot_request_to_router (spot_, peer_rid_, parts_, part_count_,
                                         timeout_ms_, handler_, userdata_);
}

int zlink_spot_reply_spot (void *spot_,
                           const zlink_routing_id_t *dest_node_rid_,
                           const zlink_routing_id_t *dest_spot_rid_,
                           uint64_t request_seq_,
                           zlink_msg_t *parts_,
                           size_t part_count_)
{
    if (validate_request_parts (parts_, part_count_) != 0)
        return -1;
    if (!has_valid_routing_id (dest_node_rid_) || !has_valid_routing_id (dest_spot_rid_)
        || request_seq_ == 0) {
        errno = EINVAL;
        return -1;
    }

    routing_pair_t source_identity;
    if (!resolve_spot_identity (spot_, &source_identity))
        return -1;

    return dispatch_local_built_message (
      zmp_spot_class, source_identity.node_rid, source_identity.spot_rid,
      zmp_spot_class, routing_id_key (dest_node_rid_),
      routing_id_key (dest_spot_rid_), zlink::request_reply::reply_type,
      request_seq_, parts_, part_count_);
}

int zlink_spot_reply_router (void *spot_,
                             const zlink_routing_id_t *peer_rid_,
                             uint64_t request_seq_,
                             zlink_msg_t *parts_,
                             size_t part_count_)
{
    if (validate_request_parts (parts_, part_count_) != 0)
        return -1;
    if (!has_valid_routing_id (peer_rid_) || request_seq_ == 0) {
        errno = EINVAL;
        return -1;
    }

    routing_pair_t source_identity;
    if (!resolve_spot_identity (spot_, &source_identity))
        return -1;

    return dispatch_local_built_message (
      zmp_spot_class, source_identity.node_rid, source_identity.spot_rid,
      zmp_router_class, std::string (), routing_id_key (peer_rid_),
      zlink::request_reply::reply_type, request_seq_, parts_, part_count_);
}

int zlink_spot_handler (void *spot_,
                        zlink_spot_handler_fn handler_,
                        void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    if (!as_spot_handle (spot_)) {
        errno = EFAULT;
        return -1;
    }
    if (spot_transition_to_callback_mode (as_spot_handle (spot_)) != 0)
        return -1;

    std::shared_ptr<local_spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    std::lock_guard<std::mutex> lock (state->mutex);
    if (state->request_handler || state->dispatch_event_handler) {
        spot_revert_callback_transition (as_spot_handle (spot_));
        errno = EBUSY;
        return -1;
    }

    state->request_handler = handler_;
    state->request_handler_userdata = userdata_;
    return 0;
}

int zlink_spot_dispatch_event_handler (
  void *spot_,
  zlink_spot_dispatch_event_handler_fn handler_,
  void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    if (!as_spot_handle (spot_)) {
        errno = EFAULT;
        return -1;
    }
    if (spot_transition_to_callback_mode (as_spot_handle (spot_)) != 0)
        return -1;

    std::shared_ptr<local_spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    std::lock_guard<std::mutex> lock (state->mutex);
    if (state->request_handler || state->dispatch_event_handler) {
        spot_revert_callback_transition (as_spot_handle (spot_));
        errno = EBUSY;
        return -1;
    }

    state->dispatch_event_handler = handler_;
    state->dispatch_event_handler_userdata = userdata_;
    return 0;
}

int zlink_spot_recv (void *spot_,
                     const zlink_routing_id_t **source_rid_out_,
                     const zlink_routing_id_t **spot_rid_out_,
                     uint64_t *request_seq_out_,
                     zlink_msg_t **parts_out_,
                     size_t *part_count_out_,
                     int flags_)
{
    if (!source_rid_out_ || !spot_rid_out_ || !request_seq_out_ || !parts_out_
        || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }
    if (validate_recv_flags (flags_) != 0)
        return -1;
    if (!as_spot_handle (spot_)) {
        errno = EFAULT;
        return -1;
    }
    if (spot_require_recv_model (as_spot_handle (spot_)) != 0)
        return -1;

    std::shared_ptr<local_spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    std::unique_lock<std::mutex> lock (state->mutex);
    if (state->request_handler || state->dispatch_event_handler) {
        errno = EBUSY;
        return -1;
    }

    if ((flags_ & ZLINK_DONTWAIT) != 0 && state->queued_messages.empty ()) {
        errno = EAGAIN;
        return -1;
    }

    while (state->queued_messages.empty ())
        state->recv_cv.wait (lock);

    queued_spot_message_t queued = std::move (state->queued_messages.front ());
    state->queued_messages.pop_front ();
    lock.unlock ();

    return export_queued_spot_message (&queued, source_rid_out_,
                                       spot_rid_out_, request_seq_out_,
                                       parts_out_, part_count_out_);
}

int zlink_router_request_spot (void *router_,
                               const zlink_routing_id_t *dest_node_rid_,
                               const zlink_routing_id_t *dest_spot_rid_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               uint32_t timeout_ms_,
                               zlink_reply_handler_fn handler_,
                               void *userdata_)
{
    if (validate_request_parts (parts_, part_count_) != 0)
        return -1;

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket || handle.socket->socket_type () != ZLINK_CORE_SOCKET_ROUTER) {
        errno = EINVAL;
        return -1;
    }

    return start_router_request_to_spot (router_, dest_node_rid_, dest_spot_rid_,
                                         parts_, part_count_, timeout_ms_,
                                         handler_, userdata_);
}

int zlink_router_reply_spot (void *router_,
                             const zlink_routing_id_t *dest_node_rid_,
                             const zlink_routing_id_t *dest_spot_rid_,
                             uint64_t request_seq_,
                             zlink_msg_t *parts_,
                             size_t part_count_)
{
    if (validate_request_parts (parts_, part_count_) != 0)
        return -1;
    if (!has_valid_routing_id (dest_node_rid_) || !has_valid_routing_id (dest_spot_rid_)
        || request_seq_ == 0) {
        errno = EINVAL;
        return -1;
    }

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket || handle.socket->socket_type () != ZLINK_CORE_SOCKET_ROUTER) {
        errno = EINVAL;
        return -1;
    }

    zlink_routing_id_t router_rid;
    memset (&router_rid, 0, sizeof (router_rid));
    if (zlink_get_routing_id (router_, &router_rid) != 0 || router_rid.size == 0)
        return -1;

    return dispatch_local_built_message (
      zmp_router_class, std::string (), routing_id_key (&router_rid),
      zmp_spot_class, routing_id_key (dest_node_rid_),
      routing_id_key (dest_spot_rid_), zlink::request_reply::reply_type,
      request_seq_, parts_, part_count_);
}

int zlink_router_send_spot (void *router_,
                            const zlink_routing_id_t *dest_node_rid_,
                            const zlink_routing_id_t *dest_spot_rid_,
                            zlink_msg_t *parts_,
                            size_t part_count_,
                            zlink_send_flags_t flags_)
{
    if (validate_request_parts (parts_, part_count_) != 0)
        return -1;
    if (!has_valid_routing_id (dest_node_rid_) || !has_valid_routing_id (dest_spot_rid_)) {
        errno = EINVAL;
        return -1;
    }
    if (validate_recv_flags (flags_) != 0)
        return -1;

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket || handle.socket->socket_type () != ZLINK_CORE_SOCKET_ROUTER) {
        errno = EINVAL;
        return -1;
    }

    zlink_routing_id_t router_rid;
    memset (&router_rid, 0, sizeof (router_rid));
    if (zlink_get_routing_id (router_, &router_rid) != 0 || router_rid.size == 0)
        return -1;

    return dispatch_local_direct_to_spot (
      zmp_router_class, std::string (), routing_id_key (&router_rid),
      routing_id_key (dest_node_rid_), routing_id_key (dest_spot_rid_), parts_,
      part_count_);
}

int zlink_router_spot_handler (void *router_,
                               zlink_router_spot_handler_fn handler_,
                               void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket || handle.socket->socket_type () != ZLINK_CORE_SOCKET_ROUTER) {
        errno = EINVAL;
        return -1;
    }

    zlink_routing_id_t router_rid;
    memset (&router_rid, 0, sizeof (router_rid));
    if (zlink_get_routing_id (router_, &router_rid) != 0 || router_rid.size == 0)
        return -1;

    std::shared_ptr<local_router_spot_state_t> state =
      find_or_create_router_state (router_);
    std::lock_guard<std::mutex> lock (state->mutex);
    if (state->handler) {
        errno = EBUSY;
        return -1;
    }

    state->router_rid = routing_id_key (&router_rid);
    state->handler = handler_;
    state->handler_userdata = userdata_;
    return 0;
}

int zlink_router_spot_recv (void *router_,
                            const zlink_routing_id_t **source_node_rid_out_,
                            const zlink_routing_id_t **source_spot_rid_out_,
                            uint64_t *request_seq_out_,
                            zlink_msg_t **parts_out_,
                            size_t *part_count_out_,
                            int flags_)
{
    if (!source_node_rid_out_ || !source_spot_rid_out_ || !request_seq_out_
        || !parts_out_ || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }
    if (validate_recv_flags (flags_) != 0)
        return -1;

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket || handle.socket->socket_type () != ZLINK_CORE_SOCKET_ROUTER) {
        errno = EINVAL;
        return -1;
    }

    zlink_routing_id_t router_rid;
    memset (&router_rid, 0, sizeof (router_rid));
    if (zlink_get_routing_id (router_, &router_rid) != 0 || router_rid.size == 0)
        return -1;

    std::shared_ptr<local_router_spot_state_t> state =
      find_or_create_router_state (router_);
    state->router_rid = routing_id_key (&router_rid);

    std::unique_lock<std::mutex> lock (state->mutex);
    if (state->handler) {
        errno = EBUSY;
        return -1;
    }

    if ((flags_ & ZLINK_DONTWAIT) != 0 && state->queued_messages.empty ()) {
        errno = EAGAIN;
        return -1;
    }

    while (state->queued_messages.empty ())
        state->recv_cv.wait (lock);

    queued_spot_message_t queued = std::move (state->queued_messages.front ());
    state->queued_messages.pop_front ();
    lock.unlock ();

    return export_queued_spot_message (&queued, source_node_rid_out_,
                                       source_spot_rid_out_, request_seq_out_,
                                       parts_out_, part_count_out_);
}

void *zlink_spot_timer_new (void *spot_)
{
    if (!as_spot_handle (spot_)) {
        errno = EFAULT;
        return NULL;
    }

    std::unique_ptr<spot_timer_handle_t> timer (
      new (std::nothrow) spot_timer_handle_t (spot_));
    if (!timer) {
        errno = ENOMEM;
        return NULL;
    }

    {
        std::lock_guard<std::mutex> lock (g_spot_timers_mutex);
        g_spot_timers_by_owner[spot_].insert (timer.get ());
    }

    return timer.release ();
}

int zlink_spot_timer_destroy (void **timer_p_)
{
    if (!timer_p_ || !*timer_p_) {
        errno = EFAULT;
        return -1;
    }

    spot_timer_handle_t *timer = as_spot_timer_handle (*timer_p_);
    if (!timer) {
        errno = EFAULT;
        return -1;
    }

    {
        std::lock_guard<std::mutex> lock (timer->mutex);
        timer->destroyed = true;
    }
    stop_spot_timer_worker (timer, true);
    unregister_spot_timer (timer);
    delete timer;
    *timer_p_ = NULL;
    return 0;
}

int zlink_spot_timer_start (void *timer_,
                            uint64_t interval_ns_,
                            uint64_t repeat_count_)
{
    spot_timer_handle_t *timer = as_spot_timer_handle (timer_);
    if (!timer || interval_ns_ == 0) {
        errno = EINVAL;
        return -1;
    }

    stop_spot_timer_worker (timer, true);
    {
        std::lock_guard<std::mutex> lock (timer->mutex);
        timer->destroyed = false;
        timer->stop_requested = false;
        timer->running = true;
        timer->interval_ns = interval_ns_;
        timer->repeat_count = repeat_count_;
        timer->next_fire_count = 1;
        timer->fired_counts.clear ();
    }

    timer->worker = std::thread (run_spot_timer_loop, timer);
    return 0;
}

int zlink_spot_timer_stop (void *timer_)
{
    spot_timer_handle_t *timer = as_spot_timer_handle (timer_);
    if (!timer) {
        errno = EFAULT;
        return -1;
    }

    stop_spot_timer_worker (timer, true);
    return 0;
}

int zlink_spot_timer_handler (void *timer_,
                              zlink_spot_timer_handler_fn handler_,
                              void *userdata_)
{
    spot_timer_handle_t *timer = as_spot_timer_handle (timer_);
    if (!timer || !handler_) {
        errno = EINVAL;
        return -1;
    }

    std::lock_guard<std::mutex> lock (timer->mutex);
    if (timer->recv_in_progress) {
        errno = EBUSY;
        return -1;
    }
    if (timer->receive_callback_active) {
        errno = EBUSY;
        return -1;
    }

    timer->receive_callback_active = true;
    timer->handler = handler_;
    timer->handler_userdata = userdata_;
    return 0;
}

int zlink_spot_timer_recv (void *timer_,
                           uint64_t *fire_count_out_,
                           int flags_)
{
    spot_timer_handle_t *timer = as_spot_timer_handle (timer_);
    if (!timer || !fire_count_out_) {
        errno = EFAULT;
        return -1;
    }
    if (validate_recv_flags (flags_) != 0)
        return -1;

    std::unique_lock<std::mutex> lock (timer->mutex);
    if (timer->receive_callback_active) {
        errno = EBUSY;
        return -1;
    }

    timer->recv_in_progress = true;
    const bool dontwait = (flags_ & ZLINK_DONTWAIT) != 0;
    while (timer->fired_counts.empty ()) {
        if (!timer->running) {
            timer->recv_in_progress = false;
            errno = EAGAIN;
            return -1;
        }
        if (dontwait) {
            timer->recv_in_progress = false;
            errno = EAGAIN;
            return -1;
        }
        timer->recv_cv.wait (lock);
    }

    *fire_count_out_ = timer->fired_counts.front ();
    timer->fired_counts.pop_front ();
    timer->recv_in_progress = false;
    return 0;
}

extern "C" int zlink_spot_request_reply_set_default_timeout (
  void *spot_,
  const void *optval_,
  size_t optvallen_)
{
    if (!as_spot_handle (spot_)) {
        errno = EINVAL;
        return -1;
    }
    if (!optval_ || optvallen_ != sizeof (int)) {
        errno = EINVAL;
        return -1;
    }

    int timeout_ms = 0;
    memcpy (&timeout_ms, optval_, sizeof (timeout_ms));
    if (timeout_ms < 0) {
        errno = EINVAL;
        return -1;
    }

    std::shared_ptr<local_spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    std::lock_guard<std::mutex> lock (state->mutex);
    state->default_timeout_ms = static_cast<uint32_t> (timeout_ms);
    return 0;
}

extern "C" int zlink_spot_request_reply_get_default_timeout (
  void *spot_,
  void *optval_,
  size_t *optvallen_)
{
    if (!as_spot_handle (spot_)) {
        errno = EINVAL;
        return -1;
    }
    if (!optval_ || !optvallen_ || *optvallen_ < sizeof (int)) {
        errno = EINVAL;
        return -1;
    }

    std::shared_ptr<local_spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    int timeout_ms = 0;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        timeout_ms = static_cast<int> (state->default_timeout_ms);
    }

    memcpy (optval_, &timeout_ms, sizeof (timeout_ms));
    *optvallen_ = sizeof (timeout_ms);
    return 0;
}

extern "C" void zlink_spot_request_reply_cleanup_spot (void *spot_)
{
    if (!as_spot_handle (spot_))
        return;

    std::vector<spot_timer_handle_t *> timers_to_destroy;
    {
        std::lock_guard<std::mutex> lock (g_spot_timers_mutex);
        std::map<void *, std::set<spot_timer_handle_t *> >::iterator tit =
          g_spot_timers_by_owner.find (spot_);
        if (tit != g_spot_timers_by_owner.end ()) {
            timers_to_destroy.assign (tit->second.begin (), tit->second.end ());
            g_spot_timers_by_owner.erase (tit);
        }
    }
    for (size_t i = 0; i < timers_to_destroy.size (); ++i) {
        spot_timer_handle_t *timer = timers_to_destroy[i];
        {
            std::lock_guard<std::mutex> lock (timer->mutex);
            timer->destroyed = true;
        }
        stop_spot_timer_worker (timer, true);
        delete timer;
    }

    std::lock_guard<std::mutex> lock (g_local_spot_states_mutex);
    local_spot_state_map_t::iterator it = g_local_spot_states.find (spot_);
    if (it != g_local_spot_states.end ()) {
        std::lock_guard<std::mutex> state_lock (it->second->mutex);
        for (std::deque<queued_spot_message_t>::iterator qit =
               it->second->queued_messages.begin ();
             qit != it->second->queued_messages.end (); ++qit) {
            for (size_t i = 0; i < qit->parts.size (); ++i)
                zlink_msg_close (&qit->parts[i]);
        }
        g_local_spot_states.erase (it);
    }
}

extern "C" void zlink_spot_request_reply_cleanup_router (void *router_)
{
    const socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket)
        return;

    std::lock_guard<std::mutex> lock (g_local_spot_states_mutex);
    local_router_state_map_t::iterator it = g_local_router_states.find (router_);
    if (it != g_local_router_states.end ()) {
        std::lock_guard<std::mutex> state_lock (it->second->mutex);
        for (std::deque<queued_spot_message_t>::iterator qit =
               it->second->queued_messages.begin ();
             qit != it->second->queued_messages.end (); ++qit) {
            for (size_t i = 0; i < qit->parts.size (); ++i)
                zlink_msg_close (&qit->parts[i]);
        }
        g_local_router_states.erase (it);
    }
}
