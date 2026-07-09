/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <chrono>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#ifdef __linux__
#include <pthread.h>
#endif
#include <thread>
#include <vector>

#include "api/socket/request_reply_protocol_internal.hpp"
#include "api/spot/request_reply/service_spot_request_reply_internal.hpp"

namespace
{
using zlink::spot_reqrep_internal::pending_reply_t;
using zlink::spot_reqrep_internal::pending_spot_key_t;
using zlink::spot_reqrep_internal::router_spot_request_reply_state_t;
using zlink::spot_reqrep_internal::spot_request_reply_state_t;

const std::chrono::milliseconds spot_timeout_reaper_idle_wait (100);

std::mutex g_spot_timeout_reaper_mutex;
std::condition_variable g_spot_timeout_reaper_cv;
std::thread g_spot_timeout_reaper_thread;
bool g_spot_timeout_reaper_started = false;
bool g_spot_timeout_reaper_stopping = false;
uint64_t g_spot_timeout_reaper_next_deadline_ns = 0;
std::atomic<uint64_t> g_spot_timeout_reaper_next_deadline_hint_ns (0);

void run_spot_timeout_reaper ();

struct spot_timeout_reaper_shutdown_t
{
    ~spot_timeout_reaper_shutdown_t ()
    {
        {
            std::lock_guard<std::mutex> lock (g_spot_timeout_reaper_mutex);
            g_spot_timeout_reaper_stopping = true;
            g_spot_timeout_reaper_cv.notify_all ();
        }
        if (g_spot_timeout_reaper_thread.joinable ()) {
            g_spot_timeout_reaper_thread.join ();
        }
    }
};

spot_timeout_reaper_shutdown_t g_spot_timeout_reaper_shutdown;

std::chrono::nanoseconds ns_until_deadline (uint64_t deadline_ns_)
{
    const uint64_t now_ns = zlink::request_timeout::monotonic_now_ns ();
    if (deadline_ns_ <= now_ns)
        return std::chrono::nanoseconds (0);
    return std::chrono::nanoseconds (deadline_ns_ - now_ns);
}

void update_spot_timeout_reaper_deadline (uint64_t deadline_ns_)
{
    if (deadline_ns_ == 0)
        return;

    const uint64_t hinted_deadline =
      g_spot_timeout_reaper_next_deadline_hint_ns.load (std::memory_order_acquire);
    if (hinted_deadline != 0 && hinted_deadline <= deadline_ns_
        && hinted_deadline > zlink::request_timeout::monotonic_now_ns ()) {
        return;
    }

    std::lock_guard<std::mutex> lock (g_spot_timeout_reaper_mutex);
    if (!g_spot_timeout_reaper_started) {
        g_spot_timeout_reaper_thread = std::thread (run_spot_timeout_reaper);
        g_spot_timeout_reaper_started = true;
    }
    if (g_spot_timeout_reaper_next_deadline_ns == 0
        || deadline_ns_ < g_spot_timeout_reaper_next_deadline_ns) {
        g_spot_timeout_reaper_next_deadline_ns = deadline_ns_;
        g_spot_timeout_reaper_next_deadline_hint_ns.store (deadline_ns_, std::memory_order_release);
        g_spot_timeout_reaper_cv.notify_all ();
    } else if (g_spot_timeout_reaper_next_deadline_ns != 0) {
        g_spot_timeout_reaper_next_deadline_hint_ns.store (g_spot_timeout_reaper_next_deadline_ns,
                                                           std::memory_order_release);
    }
}

void run_spot_timeout_reaper ()
{
#ifdef __linux__
    pthread_setname_np (pthread_self (), "zlink-spot-time");
#endif
    std::unique_lock<std::mutex> lock (g_spot_timeout_reaper_mutex);
    while (!g_spot_timeout_reaper_stopping) {
        if (g_spot_timeout_reaper_next_deadline_ns == 0) {
            g_spot_timeout_reaper_cv.wait_for (lock, spot_timeout_reaper_idle_wait);
        } else {
            g_spot_timeout_reaper_cv.wait_for (
              lock, ns_until_deadline (g_spot_timeout_reaper_next_deadline_ns));
        }
        if (g_spot_timeout_reaper_stopping)
            break;
        lock.unlock ();

        const uint64_t now_ns = zlink::request_timeout::monotonic_now_ns ();
        std::vector<std::shared_ptr<spot_request_reply_state_t>> states =
          zlink::spot_reqrep_internal::snapshot_spot_states ();
        uint64_t next_deadline_ns = 0;
        for (size_t i = 0; i < states.size (); ++i) {
            const std::shared_ptr<spot_request_reply_state_t> &state = states[i];
            if (!state)
                continue;

            std::vector<pending_reply_t> expired;
            {
                std::lock_guard<std::mutex> state_lock (state->mutex);
                for (std::unordered_map<
                       pending_spot_key_t, pending_reply_t,
                       zlink::spot_reqrep_internal::pending_spot_key_hash_t>::iterator it =
                       state->requests.pending_replies.begin ();
                     it != state->requests.pending_replies.end ();) {
                    if (it->second.deadline_ns == 0 || it->second.deadline_ns > now_ns) {
                        if (it->second.deadline_ns != 0
                            && (next_deadline_ns == 0
                                || it->second.deadline_ns < next_deadline_ns)) {
                            next_deadline_ns = it->second.deadline_ns;
                        }
                        ++it;
                        continue;
                    }

                    expired.push_back (it->second);
                    state->requests.pending_sequences.erase (it->first.request_seq);
                    it = state->requests.pending_replies.erase (it);
                }
            }

            for (size_t j = 0; j < expired.size (); ++j) {
                (void) zlink::spot_reqrep_internal::queue_spot_reply_completion (
                  state, expired[j].handler, expired[j].userdata, ETIMEDOUT, NULL, 0);
            }
        }

        lock.lock ();
        g_spot_timeout_reaper_next_deadline_ns = next_deadline_ns;
        g_spot_timeout_reaper_next_deadline_hint_ns.store (next_deadline_ns,
                                                           std::memory_order_release);
    }
}

void ensure_spot_timeout_reaper_started ()
{
    std::lock_guard<std::mutex> lock (g_spot_timeout_reaper_mutex);
    if (g_spot_timeout_reaper_started)
        return;
    g_spot_timeout_reaper_thread = std::thread (run_spot_timeout_reaper);
    g_spot_timeout_reaper_started = true;
}

struct router_spot_timeout_callback_ctx_t
{
    std::shared_ptr<router_spot_request_reply_state_t> state;
    uint64_t request_seq;
};

void on_router_spot_request_timeout (void *userdata_)
{
    std::unique_ptr<router_spot_timeout_callback_ctx_t> ctx (
      static_cast<router_spot_timeout_callback_ctx_t *> (userdata_));
    if (!ctx.get () || !ctx->state)
        return;

    pending_reply_t pending;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock (ctx->state->mutex);
        std::unordered_map<uint64_t, pending_reply_t>::iterator it =
          ctx->state->requests.pending_replies.find (ctx->request_seq);
        if (it == ctx->state->requests.pending_replies.end ())
            return;
        pending = it->second;
        ctx->state->requests.pending_sequences.erase (ctx->request_seq);
        ctx->state->requests.pending_replies.erase (it);
        found = true;
    }

    if (found) {
        (void) zlink::spot_reqrep_internal::queue_router_reply_completion (
          ctx->state, pending.handler, pending.userdata, ETIMEDOUT, NULL, 0);
    }
}
}

int zlink::spot_reqrep_internal::register_spot_pending_request (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  const pending_spot_key_t &key_,
  uint32_t timeout_ms_,
  zlink_reply_handler_fn handler_,
  void *userdata_)
{
    if (!state_ || !handler_ || key_.request_seq == 0) {
        errno = EINVAL;
        return -1;
    }

    pending_reply_t pending;
    pending.handler = handler_;
    pending.userdata = userdata_;
    pending.deadline_ns = 0;

    const uint32_t resolved_timeout_ms =
      zlink::request_reply::resolve_timeout_ms (timeout_ms_, state_->requests.default_timeout_ms);
    if (resolved_timeout_ms > 0) {
        pending.deadline_ns = zlink::request_timeout::deadline_after_ms (resolved_timeout_ms);
        ensure_spot_timeout_reaper_started ();
    }

    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        state_->requests.pending_sequences.insert (key_.request_seq);
        state_->requests.pending_replies[key_] = pending;
    }
    if (pending.deadline_ns != 0)
        update_spot_timeout_reaper_deadline (pending.deadline_ns);
    return 0;
}

int zlink::spot_reqrep_internal::register_router_spot_pending_request (
  const std::shared_ptr<router_spot_request_reply_state_t> &state_,
  uint64_t request_seq_,
  const pending_spot_key_t &key_,
  uint32_t timeout_ms_,
  zlink_reply_handler_fn handler_,
  void *userdata_)
{
    LIBZLINK_UNUSED (key_);

    if (!state_ || !handler_ || request_seq_ == 0) {
        errno = EINVAL;
        return -1;
    }

    pending_reply_t pending;
    pending.handler = handler_;
    pending.userdata = userdata_;
    pending.deadline_ns = 0;

    const uint32_t resolved_timeout_ms =
      zlink::request_reply::resolve_timeout_ms (timeout_ms_, state_->requests.default_timeout_ms);
    if (zlink::request_reply_runtime::schedule_timeout_task<router_spot_timeout_callback_ctx_t> (
          resolved_timeout_ms, &on_router_spot_request_timeout,
          [&] (router_spot_timeout_callback_ctx_t &ctx_) {
              ctx_.state = state_;
              ctx_.request_seq = request_seq_;
          },
          &pending.timeout_task)
        != 0) {
        return -1;
    }

    std::lock_guard<std::mutex> lock (state_->mutex);
    state_->requests.pending_sequences.insert (request_seq_);
    state_->requests.pending_replies[request_seq_] = pending;
    return 0;
}

void zlink::spot_reqrep_internal::erase_spot_pending_request (
  const std::shared_ptr<spot_request_reply_state_t> &state_, const pending_spot_key_t &key_)
{
    if (!state_)
        return;

    std::lock_guard<std::mutex> lock (state_->mutex);
    state_->requests.pending_sequences.erase (key_.request_seq);
    std::unordered_map<pending_spot_key_t, pending_reply_t,
                       zlink::spot_reqrep_internal::pending_spot_key_hash_t>::iterator it =
      state_->requests.pending_replies.find (key_);
    if (it == state_->requests.pending_replies.end ())
        return;
    zlink::request_timeout::cancel (it->second.timeout_task);
    state_->requests.pending_replies.erase (it);
    notify_spot_timeout_reaper_state_changed ();
}

void zlink::spot_reqrep_internal::notify_spot_timeout_reaper_state_changed ()
{
    std::lock_guard<std::mutex> lock (g_spot_timeout_reaper_mutex);
    if (!g_spot_timeout_reaper_started)
        return;
    g_spot_timeout_reaper_next_deadline_ns = 0;
    g_spot_timeout_reaper_next_deadline_hint_ns.store (0, std::memory_order_release);
    g_spot_timeout_reaper_cv.notify_all ();
}
