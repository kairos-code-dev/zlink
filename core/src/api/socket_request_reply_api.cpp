/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <thread>

#include "api/request_reply_protocol_internal.hpp"
#include "api/socket_api_internal.hpp"
#include "api/service_api_internal.hpp"
#include "core/recv_internal.hpp"
#include "core/recv_tls_view.hpp"
#include "sockets/socket_base.hpp"

namespace
{
struct pending_key_t
{
    std::string peer_rid;
    uint64_t request_seq;

    bool operator< (const pending_key_t &other_) const
    {
        if (request_seq != other_.request_seq)
            return request_seq < other_.request_seq;
        return peer_rid < other_.peer_rid;
    }
};

struct pending_request_t
{
    pending_key_t key;
    zlink_reply_handler_fn handler;
    void *userdata;
};

struct queued_router_message_t
{
    zlink_routing_id_t peer_rid;
    uint64_t request_seq;
    std::vector<zlink_msg_t> parts;

    queued_router_message_t () : request_seq (0)
    {
        memset (&peer_rid, 0, sizeof (peer_rid));
    }
};

struct socket_request_reply_state_t
{
    explicit socket_request_reply_state_t (zlink::socket_base_t *socket_,
                                           int socket_type_) :
        socket (socket_),
        socket_type (socket_type_),
        default_timeout_ms (zlink::request_reply::default_timeout_ms),
        next_request_seq (1),
        internal_dispatch_installed (false),
        router_handler (NULL),
        router_handler_userdata (NULL)
    {
    }

    zlink::socket_base_t *socket;
    int socket_type;
    std::mutex mutex;
    uint32_t default_timeout_ms;
    uint64_t next_request_seq;
    std::set<uint64_t> pending_sequences;
    std::map<pending_key_t, pending_request_t> pending_requests;
    bool internal_dispatch_installed;
    std::condition_variable recv_cv;
    std::deque<queued_router_message_t> queued_messages;
    zlink_router_handler_fn router_handler;
    void *router_handler_userdata;
};

typedef std::map<zlink::socket_base_t *, std::shared_ptr<socket_request_reply_state_t> >
  socket_request_reply_state_map_t;

std::mutex g_socket_request_reply_states_mutex;
socket_request_reply_state_map_t g_socket_request_reply_states;
thread_local zlink_routing_id_t g_router_recv_source_rid;

bool has_valid_routing_id (const zlink_routing_id_t *peer_rid_);

int validate_request_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if ((!parts_ && part_count_ > 0) || part_count_ == 0) {
        errno = EFAULT;
        return -1;
    }

    return 0;
}

int export_queued_router_message (queued_router_message_t *queued_,
                                  const zlink_routing_id_t **peer_rid_out_,
                                  uint64_t *request_seq_out_,
                                  zlink_msg_t **parts_out_,
                                  size_t *part_count_out_)
{
    if (!queued_ || !peer_rid_out_ || !request_seq_out_ || !parts_out_
        || !part_count_out_) {
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

    *peer_rid_out_ = &queued_->peer_rid;
    *request_seq_out_ = queued_->request_seq;
    return zlink::recv_tls_view::commit (parts_out_, part_count_out_);
}

void close_router_raw_parts (std::vector<zlink_msg_t> *parts_)
{
    if (!parts_)
        return;

    for (size_t i = 0; i < parts_->size (); ++i)
        zlink_msg_close (&(*parts_)[i]);
}

bool router_raw_part_has_more (const zlink_msg_t *part_)
{
    if (!part_)
        return false;

    const zlink::msg_t *msg =
      reinterpret_cast<const zlink::msg_t *> (part_);
    if (!msg->check ())
        return false;

    return (msg->flags () & zlink::msg_t::more) != 0;
}

int recv_router_message_direct (socket_handle_t handle_,
                                const zlink_routing_id_t **peer_rid_out_,
                                uint64_t *request_seq_out_,
                                zlink_msg_t **parts_out_,
                                size_t *part_count_out_,
                                int flags_)
{
    if (!handle_.socket || !peer_rid_out_ || !request_seq_out_ || !parts_out_
        || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }

    zlink::msg_t current;
    const int current_init_rc = current.init ();
    errno_assert (current_init_rc == 0);
    zlink_routing_id_t source_rid;
    memset (&source_rid, 0, sizeof (source_rid));
    if (handle_.socket->recv_routed (&current, &source_rid, flags_) != 0) {
        return -1;
    }

    std::vector<zlink_msg_t> raw_parts;
    while (true) {
        raw_parts.push_back (zlink_msg_t ());
        zlink_msg_init (&raw_parts.back ());
        if (reinterpret_cast<zlink::msg_t *> (&raw_parts.back ())
              ->move (current)
            != 0) {
            close_router_raw_parts (&raw_parts);
            const int close_rc = current.close ();
            errno_assert (close_rc == 0);
            errno = EFAULT;
            return -1;
        }

        if (!router_raw_part_has_more (&raw_parts.back ()))
            break;

        zlink::msg_t next;
        const int next_init_rc = next.init ();
        errno_assert (next_init_rc == 0);
        zlink_routing_id_t next_source_rid;
        memset (&next_source_rid, 0, sizeof (next_source_rid));
        if (handle_.socket->recv_routed (&next, &next_source_rid, ZLINK_DONTWAIT)
            != 0) {
            const int close_rc = next.close ();
            errno_assert (close_rc == 0);
            close_router_raw_parts (&raw_parts);
            return -1;
        }
        if (current.move (next) != 0) {
            const int close_rc = next.close ();
            errno_assert (close_rc == 0);
            close_router_raw_parts (&raw_parts);
            errno = EFAULT;
            return -1;
        }
    }

    if (zlink::recv_tls_view::begin (parts_out_, part_count_out_) != 0) {
        close_router_raw_parts (&raw_parts);
        return -1;
    }

    zlink::request_reply::parsed_envelope_t envelope;
    const bool parsed = zlink::request_reply::parse_envelope (
      raw_parts.data (), raw_parts.size (), &envelope);
    const size_t start_index =
      parsed ? zlink::request_reply::control_part_count : 0;

    if (parsed) {
        *request_seq_out_ = envelope.request_seq;
        for (size_t i = 0; i < start_index; ++i)
            zlink_msg_close (&raw_parts[i]);
    } else
        *request_seq_out_ = 0;

    for (size_t i = start_index; i < raw_parts.size (); ++i) {
        if (zlink::recv_tls_view::push (&raw_parts[i]) != 0) {
            const int saved_errno = errno;
            zlink::recv_tls_view::abort ();
            for (size_t j = i; j < raw_parts.size (); ++j)
                zlink_msg_close (&raw_parts[j]);
            errno = saved_errno;
            return -1;
        }
    }

    g_router_recv_source_rid = source_rid;
    *peer_rid_out_ = &g_router_recv_source_rid;
    return zlink::recv_tls_view::commit (parts_out_, part_count_out_);
}

int queue_router_message (socket_request_reply_state_t *state_,
                          const zlink_routing_id_t *peer_rid_,
                          uint64_t request_seq_,
                          zlink_msg_t *parts_,
                          size_t part_count_)
{
    if (!state_ || !parts_) {
        errno = EFAULT;
        return -1;
    }

    queued_router_message_t queued;
    if (has_valid_routing_id (peer_rid_))
        queued.peer_rid = *peer_rid_;
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
    return 0;
}

void dispatch_router_message (socket_request_reply_state_t *state_,
                              const zlink_routing_id_t *peer_rid_,
                              uint64_t request_seq_,
                              zlink_msg_t *parts_,
                              size_t part_count_)
{
    zlink_router_handler_fn handler = NULL;
    void *handler_userdata = NULL;
    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        handler = state_->router_handler;
        handler_userdata = state_->router_handler_userdata;
    }

    if (handler) {
        handler (peer_rid_, request_seq_, parts_, part_count_, handler_userdata);
        return;
    }

    if (queue_router_message (state_, peer_rid_, request_seq_, parts_, part_count_)
        != 0) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
    }
}

bool has_valid_routing_id (const zlink_routing_id_t *peer_rid_)
{
    return peer_rid_ && peer_rid_->size > 0
           && peer_rid_->size <= sizeof (peer_rid_->data);
}

int validate_socket_type (void *socket_, int expected_type_)
{
    const socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;

    if (socket_type (handle) != expected_type_) {
        errno = EINVAL;
        return -1;
    }

    return 0;
}

std::string routing_id_key (const zlink_routing_id_t *peer_rid_)
{
    if (!has_valid_routing_id (peer_rid_))
        return std::string ();

    return std::string (reinterpret_cast<const char *> (peer_rid_->data),
                        peer_rid_->size);
}

int send_request_reply_message (void *socket_handle_,
                                const zlink_routing_id_t *peer_rid_,
                                zlink_msg_t *parts_,
                                size_t part_count_,
                                uint8_t message_type_,
                                uint64_t request_seq_)
{
    if (!socket_handle_ || !parts_ || part_count_ == 0 || request_seq_ == 0) {
        errno = EINVAL;
        return -1;
    }

    const bool routed = has_valid_routing_id (peer_rid_);
    const size_t total_part_count =
      zlink::request_reply::control_part_count + part_count_;
    std::vector<zlink_msg_t> combined (total_part_count);
    for (size_t i = 0; i < total_part_count; ++i)
        zlink_msg_init (&combined[i]);

    unsigned char protocol_id = zlink::request_reply::protocol_id;
    unsigned char version = zlink::request_reply::version;
    unsigned char type = message_type_;
    unsigned char seq_buf[8];
    zlink::request_reply::encode_u64_be (request_seq_, seq_buf);

    if (zlink::request_reply::init_control_part (&combined[0], &protocol_id, 1)
          != 0
        || zlink::request_reply::init_control_part (&combined[1], &version, 1)
             != 0
        || zlink::request_reply::init_control_part (&combined[2], &type, 1)
             != 0
        || zlink::request_reply::init_control_part (&combined[3], seq_buf,
                                                    sizeof (seq_buf))
             != 0) {
        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (&combined);
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        errno = saved_errno;
        return -1;
    }

    for (size_t i = 0; i < part_count_; ++i) {
        if (zlink_msg_move (&combined[zlink::request_reply::control_part_count
                                      + i],
                            &parts_[i])
            != 0) {
            const int saved_errno = errno;
            zlink::request_reply::close_built_parts (&combined);
            zlink::request_reply::consume_send_frames_from (parts_, i,
                                                            part_count_);
            errno = saved_errno;
            return -1;
        }
    }

    const int rc =
      routed ? zlink_send_rid (socket_handle_, peer_rid_, &combined[0],
                               total_part_count, 0)
             : zlink_send (socket_handle_, &combined[0], total_part_count, 0);
    if (rc != 0)
        return -1;

    errno = 0;
    return 0;
}

std::shared_ptr<socket_request_reply_state_t>
find_or_create_request_reply_state (socket_handle_t handle_)
{
    std::lock_guard<std::mutex> lock (g_socket_request_reply_states_mutex);
    socket_request_reply_state_map_t::iterator it =
      g_socket_request_reply_states.find (handle_.socket);
    if (it != g_socket_request_reply_states.end ())
        return it->second;

    std::shared_ptr<socket_request_reply_state_t> state (
      new socket_request_reply_state_t (handle_.socket, socket_type (handle_)));
    g_socket_request_reply_states[handle_.socket] = state;
    return state;
}

std::shared_ptr<socket_request_reply_state_t>
find_request_reply_state (socket_handle_t handle_)
{
    std::lock_guard<std::mutex> lock (g_socket_request_reply_states_mutex);
    socket_request_reply_state_map_t::iterator it =
      g_socket_request_reply_states.find (handle_.socket);
    if (it == g_socket_request_reply_states.end ())
        return std::shared_ptr<socket_request_reply_state_t> ();
    return it->second;
}

uint64_t allocate_request_seq (socket_request_reply_state_t *state_)
{
    if (!state_) {
        errno = EFAULT;
        return 0;
    }

    const uint64_t start = state_->next_request_seq == 0 ? 1 : state_->next_request_seq;
    uint64_t candidate = start;

    do {
        if (candidate == 0)
            candidate = 1;

        if (state_->pending_sequences.count (candidate) == 0) {
            uint64_t next = candidate + 1;
            if (next == 0)
                next = 1;
            state_->next_request_seq = next;
            return candidate;
        }

        ++candidate;
        if (candidate == 0)
            candidate = 1;
    } while (candidate != start);

    errno = EBUSY;
    return 0;
}

void schedule_request_timeout (
  std::shared_ptr<socket_request_reply_state_t> state_,
  const pending_key_t &key_,
  uint32_t timeout_ms_)
{
    std::thread ([state_, key_, timeout_ms_]() {
        const uint32_t sleep_slice_ms = 10;
        uint32_t waited_ms = 0;
        while (waited_ms < timeout_ms_) {
            {
                std::lock_guard<std::mutex> lock (state_->mutex);
                if (state_->pending_requests.find (key_)
                    == state_->pending_requests.end ()) {
                    return;
                }
            }

            const uint32_t remaining_ms = timeout_ms_ - waited_ms;
            const uint32_t step_ms =
              remaining_ms < sleep_slice_ms ? remaining_ms : sleep_slice_ms;
            std::this_thread::sleep_for (std::chrono::milliseconds (step_ms));
            waited_ms += step_ms;
        }

        pending_request_t pending;
        bool found = false;
        {
            std::lock_guard<std::mutex> lock (state_->mutex);
            std::map<pending_key_t, pending_request_t>::iterator it =
              state_->pending_requests.find (key_);
            if (it == state_->pending_requests.end ())
                return;
            pending = it->second;
            state_->pending_sequences.erase (key_.request_seq);
            state_->pending_requests.erase (it);
            found = true;
        }

        if (found)
            zlink::request_reply::complete_reply_callback (
              pending.handler, ETIMEDOUT, NULL, 0, pending.userdata);
    }).detach ();
}

void socket_request_reply_dispatch (const zlink_routing_id_t *source_rid_,
                                    zlink_msg_t *parts_,
                                    size_t part_count_,
                                    void *userdata_)
{
    socket_request_reply_state_t *state =
      static_cast<socket_request_reply_state_t *> (userdata_);
    if (!state) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        return;
    }

    zlink::request_reply::parsed_envelope_t envelope;
    if (!zlink::request_reply::parse_envelope (parts_, part_count_, &envelope)) {
        dispatch_router_message (state, source_rid_, 0, parts_, part_count_);
        return;
    }

    if (envelope.message_type == zlink::request_reply::request_type) {
        if (state->socket_type == ZLINK_CORE_SOCKET_ROUTER
            && has_valid_routing_id (source_rid_)) {
            dispatch_router_message (state, source_rid_, envelope.request_seq,
                                     envelope.payload_parts,
                                     envelope.payload_part_count);
        } else {
            zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        }
        return;
    }

    pending_key_t key;
    key.request_seq = envelope.request_seq;
    if (state->socket_type == ZLINK_CORE_SOCKET_ROUTER
        && has_valid_routing_id (source_rid_)) {
        key.peer_rid = routing_id_key (source_rid_);
    }

    pending_request_t pending;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        std::map<pending_key_t, pending_request_t>::iterator it =
          state->pending_requests.find (key);
        if (it != state->pending_requests.end ()) {
            pending = it->second;
            state->pending_sequences.erase (key.request_seq);
            state->pending_requests.erase (it);
            found = true;
        }
    }

    if (!found) {
        if (state->socket_type == ZLINK_CORE_SOCKET_ROUTER
            && has_valid_routing_id (source_rid_)) {
            dispatch_router_message (state, source_rid_, envelope.request_seq,
                                     envelope.payload_parts,
                                     envelope.payload_part_count);
        } else {
            zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        }
        return;
    }

    int callback_errno = 0;
    zlink_msg_t *callback_parts = envelope.payload_parts;
    size_t callback_part_count = envelope.payload_part_count;
    if (zlink::request_reply::decode_reply_completion (
          envelope.message_type, envelope.payload_parts,
          envelope.payload_part_count, &callback_errno, &callback_parts,
          &callback_part_count)
        != 0) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        return;
    }

    zlink::request_reply::complete_reply_callback (
      pending.handler, callback_errno, callback_parts, callback_part_count,
      pending.userdata);
    zlink::request_reply::close_request_reply_parts (parts_, part_count_);
}

int ensure_internal_dispatch_installed (
  const std::shared_ptr<socket_request_reply_state_t> &state_)
{
    if (!state_ || !state_->socket) {
        errno = EFAULT;
        return -1;
    }

    std::lock_guard<std::mutex> lock (state_->mutex);
    if (state_->internal_dispatch_installed)
        return 0;

    if (state_->socket->socket_msg_dispatch_active ()) {
        errno = EBUSY;
        return -1;
    }

    if (state_->socket->socket_set_msg_handler_with_userdata (
          &socket_request_reply_dispatch, NULL, state_.get ())
        != 0)
        return -1;

    state_->internal_dispatch_installed = true;
    return 0;
}

int start_request (socket_handle_t handle_,
                   const zlink_routing_id_t *peer_rid_,
                   zlink_msg_t *parts_,
                   size_t part_count_,
                   uint32_t timeout_ms_,
                   zlink_reply_handler_fn handler_,
                   void *userdata_)
{
    std::shared_ptr<socket_request_reply_state_t> state =
      find_or_create_request_reply_state (handle_);
    if (ensure_internal_dispatch_installed (state) != 0)
        return -1;

    pending_key_t key;
    pending_request_t pending;
    uint32_t resolved_timeout_ms = zlink::request_reply::default_timeout_ms;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        const uint64_t request_seq = allocate_request_seq (state.get ());
        if (request_seq == 0)
            return -1;

        key.request_seq = request_seq;
        if (handle_.socket->socket_type () == ZLINK_CORE_SOCKET_ROUTER
            && has_valid_routing_id (peer_rid_)) {
            key.peer_rid = routing_id_key (peer_rid_);
        }

        pending.key = key;
        pending.handler = handler_;
        pending.userdata = userdata_;
        state->pending_sequences.insert (request_seq);
        state->pending_requests[key] = pending;
        resolved_timeout_ms = zlink::request_reply::resolve_timeout_ms (
          timeout_ms_, state->default_timeout_ms);
    }

    const uint8_t message_type = zlink::request_reply::request_type;
    const int rc =
      send_request_reply_message (handle_.socket, peer_rid_, parts_, part_count_,
                                  message_type, key.request_seq);
    if (rc != 0) {
        std::lock_guard<std::mutex> lock (state->mutex);
        state->pending_sequences.erase (key.request_seq);
        state->pending_requests.erase (key);
        return -1;
    }

    schedule_request_timeout (
      state, key, resolved_timeout_ms);
    return 0;
}
}

int zlink_dealer_request (void *dealer_,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          uint32_t timeout_ms_,
                          zlink_reply_handler_fn handler_,
                          void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }
    if (validate_request_parts (parts_, part_count_) != 0)
        return -1;
    if (validate_socket_type (dealer_, ZLINK_CORE_SOCKET_DEALER) != 0)
        return -1;

    return start_request (as_socket_handle (dealer_), NULL, parts_, part_count_,
                          timeout_ms_, handler_, userdata_);
}

int zlink_router_request (void *router_,
                          const zlink_routing_id_t *peer_rid_,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          uint32_t timeout_ms_,
                          zlink_reply_handler_fn handler_,
                          void *userdata_)
{
    if (!handler_ || !has_valid_routing_id (peer_rid_)) {
        errno = EINVAL;
        return -1;
    }
    if (validate_request_parts (parts_, part_count_) != 0)
        return -1;
    if (validate_socket_type (router_, ZLINK_CORE_SOCKET_ROUTER) != 0)
        return -1;

    return start_request (as_socket_handle (router_), peer_rid_, parts_,
                          part_count_, timeout_ms_, handler_, userdata_);
}

int zlink_router_reply (void *router_,
                        const zlink_routing_id_t *peer_rid_,
                        uint64_t request_seq_,
                        zlink_msg_t *parts_,
                        size_t part_count_)
{
    if (!has_valid_routing_id (peer_rid_) || request_seq_ == 0) {
        errno = EINVAL;
        return -1;
    }
    if (validate_request_parts (parts_, part_count_) != 0)
        return -1;
    if (validate_socket_type (router_, ZLINK_CORE_SOCKET_ROUTER) != 0)
        return -1;

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket)
        return -1;

    return send_request_reply_message (router_, peer_rid_, parts_,
                                       part_count_,
                                       zlink::request_reply::reply_type,
                                       request_seq_);
}

int zlink_router_handler (void *router_,
                          zlink_router_handler_fn handler_,
                          void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }
    if (validate_socket_type (router_, ZLINK_CORE_SOCKET_ROUTER) != 0)
        return -1;

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket)
        return -1;

    std::shared_ptr<socket_request_reply_state_t> state =
      find_or_create_request_reply_state (handle);
    if (ensure_internal_dispatch_installed (state) != 0)
        return -1;

    std::lock_guard<std::mutex> lock (state->mutex);
    if (state->router_handler) {
        errno = EBUSY;
        return -1;
    }

    state->router_handler = handler_;
    state->router_handler_userdata = userdata_;
    errno = 0;
    return 0;
}

int zlink_router_recv (void *router_,
                       const zlink_routing_id_t **peer_rid_out_,
                       uint64_t *request_seq_out_,
                       zlink_msg_t **parts_out_,
                       size_t *part_count_out_,
                       int flags_)
{
    if (!peer_rid_out_ || !request_seq_out_ || !parts_out_ || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }
    if (validate_recv_flags (flags_) != 0)
        return -1;
    if (validate_socket_type (router_, ZLINK_CORE_SOCKET_ROUTER) != 0)
        return -1;

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket)
        return -1;

    std::shared_ptr<socket_request_reply_state_t> state =
      find_request_reply_state (handle);
    if (!state)
        return recv_router_message_direct (handle, peer_rid_out_,
                                           request_seq_out_, parts_out_,
                                           part_count_out_, flags_);

    {
        std::unique_lock<std::mutex> lock (state->mutex);
        if (state->router_handler) {
            errno = EBUSY;
            return -1;
        }

        if (!state->internal_dispatch_installed
            && state->pending_requests.empty ()) {
            lock.unlock ();
            return recv_router_message_direct (handle, peer_rid_out_,
                                               request_seq_out_, parts_out_,
                                               part_count_out_, flags_);
        }

        if ((flags_ & ZLINK_DONTWAIT) != 0 && state->queued_messages.empty ()) {
            errno = EAGAIN;
            return -1;
        }

        while (state->queued_messages.empty ())
            state->recv_cv.wait (lock);

        queued_router_message_t queued =
          std::move (state->queued_messages.front ());
        state->queued_messages.pop_front ();
        lock.unlock ();

        return export_queued_router_message (&queued, peer_rid_out_,
                                             request_seq_out_, parts_out_,
                                             part_count_out_);
    }
}

extern "C" void zlink_socket_request_reply_cleanup (void *socket_)
{
    const socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return;

    bool stop_dispatch = false;
    {
        std::lock_guard<std::mutex> lock (g_socket_request_reply_states_mutex);
        socket_request_reply_state_map_t::iterator it =
          g_socket_request_reply_states.find (handle.socket);
        if (it != g_socket_request_reply_states.end ()) {
            std::lock_guard<std::mutex> state_lock (it->second->mutex);
            stop_dispatch = it->second->internal_dispatch_installed;
            for (std::deque<queued_router_message_t>::iterator qit =
                   it->second->queued_messages.begin ();
                 qit != it->second->queued_messages.end (); ++qit) {
                for (size_t i = 0; i < qit->parts.size (); ++i)
                    zlink_msg_close (&qit->parts[i]);
            }
            g_socket_request_reply_states.erase (it);
        }
    }

    if (stop_dispatch && handle.socket->socket_msg_dispatch_active ())
        (void) handle.socket->socket_msg_dispatch_stop ();
}

extern "C" int zlink_socket_request_reply_set_default_timeout (
  void *socket_,
  const void *optval_,
  size_t optvallen_)
{
    const socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket) {
        errno = EINVAL;
        return -1;
    }

    const int type = socket_type (handle);
    if (type != ZLINK_CORE_SOCKET_ROUTER && type != ZLINK_CORE_SOCKET_DEALER) {
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

    std::shared_ptr<socket_request_reply_state_t> state =
      find_or_create_request_reply_state (handle);
    std::lock_guard<std::mutex> lock (state->mutex);
    state->default_timeout_ms = static_cast<uint32_t> (timeout_ms);
    return 0;
}

extern "C" int zlink_socket_request_reply_get_default_timeout (
  void *socket_,
  void *optval_,
  size_t *optvallen_)
{
    const socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket) {
        errno = EINVAL;
        return -1;
    }

    const int type = socket_type (handle);
    if (type != ZLINK_CORE_SOCKET_ROUTER && type != ZLINK_CORE_SOCKET_DEALER) {
        errno = EINVAL;
        return -1;
    }
    if (!optval_ || !optvallen_ || *optvallen_ < sizeof (int)) {
        errno = EINVAL;
        return -1;
    }

    std::shared_ptr<socket_request_reply_state_t> state =
      find_or_create_request_reply_state (handle);
    int timeout_ms = 0;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        timeout_ms = static_cast<int> (state->default_timeout_ms);
    }

    memcpy (optval_, &timeout_ms, sizeof (timeout_ms));
    *optvallen_ = sizeof (timeout_ms);
    return 0;
}
