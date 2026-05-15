/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/socket/request_completion_queue_internal.hpp"

#include <utility>

#include "api/socket/request_reply_protocol_internal.hpp"
#include "core/socket_poller.hpp"
#include "core/recv_internal.hpp"

namespace
{
void *&request_completion_owner_tls ()
{
    static thread_local void *owner = NULL;
    return owner;
}

int move_completion_parts (std::vector<zlink_msg_t> *dest_,
                           zlink_msg_t *parts_,
                           size_t part_count_)
{
    if (!dest_) {
        errno = EFAULT;
        return -1;
    }
    if (!parts_ && part_count_ > 0) {
        errno = EFAULT;
        return -1;
    }

    dest_->reserve (part_count_);
    for (size_t i = 0; i < part_count_; ++i) {
        zlink_msg_t moved;
        zlink_msg_init (&moved);
        if (zlink_msg_move (&moved, &parts_[i]) != 0) {
            zlink_msg_close (&moved);
            zlink::close_msg_frames (dest_);
            errno = EFAULT;
            return -1;
        }
        dest_->push_back (moved);
    }
    return 0;
}

void drain_signal_socket_nonblocking (zlink::socket_base_t *socket_)
{
    if (!socket_)
        return;

    while (true) {
        zlink::msg_t msg;
        if (msg.init () != 0)
            return;
        if (socket_->recv (&msg, ZLINK_DONTWAIT) != 0) {
            msg.close ();
            return;
        }
        msg.close ();
    }
}

class request_completion_callback_scope_t
{
  public:
    explicit request_completion_callback_scope_t (void *owner_handle_) :
        previous_owner (request_completion_owner_tls ())
    {
        request_completion_owner_tls () = owner_handle_;
    }

    ~request_completion_callback_scope_t ()
    {
        request_completion_owner_tls () = previous_owner;
    }

  private:
    void *previous_owner;
};
}

zlink::request_completion::queued_completion_t::queued_completion_t () :
    handler (NULL),
    userdata (NULL),
    errnum (0)
{
}

zlink::request_completion::queued_completion_t::~queued_completion_t ()
{
    zlink::close_msg_frames (&parts);
}

zlink::request_completion::queued_completion_t::queued_completion_t (
  queued_completion_t &&other_) noexcept :
    handler (other_.handler),
    userdata (other_.userdata),
    errnum (other_.errnum)
{
    parts.swap (other_.parts);
    other_.handler = NULL;
    other_.userdata = NULL;
    other_.errnum = 0;
}

zlink::request_completion::queued_completion_t &
zlink::request_completion::queued_completion_t::operator= (
  queued_completion_t &&other_) noexcept
{
    if (this == &other_)
        return *this;

    zlink::close_msg_frames (&parts);
    handler = other_.handler;
    userdata = other_.userdata;
    errnum = other_.errnum;
    parts.swap (other_.parts);
    other_.handler = NULL;
    other_.userdata = NULL;
    other_.errnum = 0;
    return *this;
}

zlink::request_completion::queue_state_t::queue_state_t () :
    owner_thread_valid (false),
    signal_pending (false)
{
}

int zlink::request_completion::ensure_signal_ready (queue_state_t *state_,
                                                    zlink::ctx_t *ctx_,
                                                    const char *prefix_)
{
    if (!state_ || !ctx_ || !prefix_) {
        errno = EFAULT;
        return -1;
    }
    return zlink::internal_pair_queue::ensure (ctx_, prefix_, &state_->signal);
}

int zlink::request_completion::enqueue (queue_state_t *state_,
                                        zlink::ctx_t *ctx_,
                                        const char *prefix_,
                                        zlink_reply_handler_fn handler_,
                                        void *userdata_,
                                        int errnum_,
                                        zlink_msg_t *parts_,
                                        size_t part_count_)
{
    if (!state_ || !ctx_ || !prefix_ || !handler_) {
        errno = EFAULT;
        return -1;
    }
    if (ensure_signal_ready (state_, ctx_, prefix_) != 0)
        return -1;

    queued_completion_t completion;
    completion.handler = handler_;
    completion.userdata = userdata_;
    completion.errnum = errnum_;
    if (move_completion_parts (&completion.parts, parts_, part_count_) != 0)
        return -1;

    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        const bool should_signal =
          state_->pending.empty () && !state_->signal_pending;
        state_->pending.push_back (std::move (completion));
        if (should_signal) {
            state_->signal_pending = true;
            static const unsigned char signal_byte = 0x7a;
            const int signal_rc = zlink::internal_pair_queue::send_buffer_frame (
              state_->signal.tx, &signal_byte, sizeof (signal_byte), 0);
            if (signal_rc != 0) {
                state_->signal_pending = false;
                return -1;
            }
        }
    }

    errno = 0;
    return 0;
}

int zlink::request_completion::drain (queue_state_t *state_,
                                      void *owner_handle_)
{
    if (!state_) {
        errno = EFAULT;
        return -1;
    }

    claim_owner_thread (state_);
    int drained = 0;
    while (true) {
        std::deque<queued_completion_t> batch;
        bool queue_empty = false;
        {
            std::lock_guard<std::mutex> lock (state_->mutex);
            // Always consume the signal byte and clear signal_pending whenever
            // we observe the queue here. Without this, a draining swap below
            // leaves signal_pending=true even after the byte has been observed
            // by the poller, so subsequent enqueue() calls take the
            // !signal_pending == false branch and skip waking up the fd.
            // Concretely: reply N+1, N+2, ... never raise the signaler until
            // the next drain comes around through the poller's timeout, which
            // caps throughput at 1 / poll_timeout per client.
            if (state_->signal_pending) {
                drain_signal_socket_nonblocking (state_->signal.rx);
                state_->signal_pending = false;
            }
            if (state_->pending.empty ()) {
                queue_empty = true;
            } else {
                batch.swap (state_->pending);
            }
        }

        if (queue_empty)
            break;

        for (std::deque<queued_completion_t>::iterator it = batch.begin ();
             it != batch.end (); ++it) {
            const request_completion_callback_scope_t scope (owner_handle_);
            zlink_msg_t *parts = it->parts.empty () ? NULL : &it->parts[0];
            zlink::request_reply::complete_reply_callback (
              it->handler, it->errnum, parts, it->parts.size (), it->userdata);
            ++drained;
        }
    }
    errno = 0;
    return drained;
}

void zlink::request_completion::close (queue_state_t *state_)
{
    if (!state_)
        return;

    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        state_->pending.clear ();
        state_->owner_thread_valid = false;
        state_->signal_pending = false;
    }
    zlink::internal_pair_queue::close (&state_->signal);
}

void zlink::request_completion::claim_owner_thread (queue_state_t *state_)
{
    if (!state_)
        return;

    std::lock_guard<std::mutex> lock (state_->mutex);
    state_->owner_thread = std::this_thread::get_id ();
    state_->owner_thread_valid = true;
}

bool zlink::request_completion::current_thread_is_owner (queue_state_t *state_)
{
    if (!state_)
        return false;

    std::lock_guard<std::mutex> lock (state_->mutex);
    return state_->owner_thread_valid
           && state_->owner_thread == std::this_thread::get_id ();
}

bool zlink::request_completion::has_pending (queue_state_t *state_)
{
    if (!state_)
        return false;

    std::lock_guard<std::mutex> lock (state_->mutex);
    return !state_->pending.empty ();
}

zlink::socket_base_t *
zlink::request_completion::signal_socket (queue_state_t *state_)
{
    return state_ ? state_->signal.rx : NULL;
}

bool zlink::request_completion::in_request_completion_callback (
  void *owner_handle_)
{
    return owner_handle_ != NULL
           && request_completion_owner_tls () == owner_handle_;
}

int zlink::request_completion::wait_input_or_signal (
  zlink::socket_base_t *input_,
  zlink::socket_base_t *signal_,
  long timeout_ms_,
  bool *input_ready_out_,
  bool *signal_ready_out_)
{
    if (!input_ || !input_ready_out_ || !signal_ready_out_) {
        errno = EFAULT;
        return -1;
    }

    *input_ready_out_ = false;
    *signal_ready_out_ = false;

    if (!signal_) {
        const int wait_rc =
          zlink::wait_socket_events_internal (input_, ZLINK_POLLIN, timeout_ms_);
        if (wait_rc < 0)
            return -1;
        if (wait_rc == 0)
            return 0;
        *input_ready_out_ = true;
        return 1;
    }

    zlink::socket_poller_t poller;
    if (poller.add (input_, NULL, ZLINK_POLLIN) != 0)
        return -1;
    if (poller.add (signal_, NULL, ZLINK_POLLIN) != 0)
        return -1;

    zlink::socket_poller_t::event_t events[2];
    const int rc = poller.wait (events, 2, timeout_ms_);
    if (rc <= 0)
        return rc;

    for (int i = 0; i < rc; ++i) {
        if (events[i].socket == input_)
            *input_ready_out_ = true;
        else if (events[i].socket == signal_)
            *signal_ready_out_ = true;
    }
    return rc;
}
