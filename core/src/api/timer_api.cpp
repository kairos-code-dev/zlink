/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <thread>

#include "api/service_api_internal.hpp"
#include "api/timer_api_internal.hpp"
#include "core/signaler.hpp"

namespace
{
struct scheduler_state_t;
}

struct timer_handle_t
{
    enum backend_kind_t
    {
        backend_global_scheduler = 1,
        backend_spot_node_scheduler = 2
    };

    explicit timer_handle_t (backend_kind_t backend_,
                             void *owner_spot_ = NULL,
                             void *owner_node_ = NULL) :
        tag (0x74696d72),
        backend (backend_),
        owner_spot (owner_spot_),
        owner_node (owner_node_),
        scheduler (NULL),
        destroyed (false),
        running (false),
        receive_callback_active (false),
        recv_in_progress (false),
        stop_requested (false),
        signal_pending (false),
        poller_refs (0),
        scheduler_busy_refs (0),
        scheduler_registered (false),
        scheduled_deadline_ns (0),
        interval_ns (0),
        repeat_count (0),
        next_fire_count (1),
        handler (NULL),
        handler_userdata (NULL)
    {
    }

    bool check_tag () const { return tag == 0x74696d72; }

    uint32_t tag;
    backend_kind_t backend;
    void *owner_spot;
    void *owner_node;
    scheduler_state_t *scheduler;
    std::mutex mutex;
    std::condition_variable cv;
    std::condition_variable recv_cv;
    zlink::signaler_t signaler;
    bool destroyed;
    bool running;
    bool receive_callback_active;
    bool recv_in_progress;
    bool stop_requested;
    bool signal_pending;
    int poller_refs;
    int scheduler_busy_refs;
    bool scheduler_registered;
    uint64_t scheduled_deadline_ns;
    uint64_t interval_ns;
    uint64_t repeat_count;
    uint64_t next_fire_count;
    std::deque<uint64_t> fired_counts;
    zlink_timer_handler_fn handler;
    void *handler_userdata;
};

namespace
{
struct scheduler_state_t
{
    explicit scheduler_state_t () :
        started (false),
        shutdown_requested (false),
        active_timers (0)
    {
    }

    std::mutex mutex;
    std::condition_variable cv;
    std::multimap<uint64_t, timer_handle_t *> schedule;
    std::thread worker;
    bool started;
    bool shutdown_requested;
    size_t active_timers;
};

typedef std::map<void *, std::shared_ptr<scheduler_state_t> > spot_scheduler_map_t;

std::shared_ptr<scheduler_state_t> g_global_scheduler (new scheduler_state_t ());
std::mutex g_spot_scheduler_map_mutex;
spot_scheduler_map_t g_spot_schedulers;

uint64_t monotonic_now_ns ()
{
    return static_cast<uint64_t> (
      std::chrono::duration_cast<std::chrono::nanoseconds> (
        std::chrono::steady_clock::now ().time_since_epoch ())
        .count ());
}

void drain_timer_signal_locked (timer_handle_t *timer_)
{
    if (!timer_ || !timer_->signal_pending)
        return;

    while (timer_->signaler.recv_failable () == 0) {
    }
    timer_->signal_pending = false;
}

void ensure_timer_signal_locked (timer_handle_t *timer_)
{
    if (!timer_ || timer_->signal_pending || timer_->fired_counts.empty ())
        return;

    timer_->signaler.send ();
    timer_->signal_pending = true;
}

void remove_timer_registration_locked (timer_handle_t *timer_)
{
    if (!timer_ || !timer_->scheduler || !timer_->scheduler_registered)
        return;

    scheduler_state_t *scheduler = timer_->scheduler;
    const uint64_t deadline = timer_->scheduled_deadline_ns;
    const std::multimap<uint64_t, timer_handle_t *>::iterator begin =
      scheduler->schedule.lower_bound (deadline);
    const std::multimap<uint64_t, timer_handle_t *>::iterator end =
      scheduler->schedule.upper_bound (deadline);
    for (std::multimap<uint64_t, timer_handle_t *>::iterator it = begin;
         it != end; ++it) {
        if (it->second == timer_) {
            scheduler->schedule.erase (it);
            break;
        }
    }

    timer_->scheduler_registered = false;
    timer_->scheduled_deadline_ns = 0;
}

void schedule_timer_locked (timer_handle_t *timer_, uint64_t deadline_ns_)
{
    remove_timer_registration_locked (timer_);
    timer_->scheduler_registered = true;
    timer_->scheduled_deadline_ns = deadline_ns_;
    timer_->scheduler->schedule.insert (std::make_pair (deadline_ns_, timer_));
}

void scheduler_fire_timer (timer_handle_t *timer_)
{
    zlink_timer_handler_fn handler = NULL;
    void *handler_userdata = NULL;
    void *owner_spot = NULL;
    uint64_t fire_count = 0;
    bool notify_spot = false;
    bool reschedule = false;
    uint64_t next_deadline_ns = 0;
    scheduler_state_t *scheduler = NULL;

    {
        std::lock_guard<std::mutex> lock (timer_->mutex);
        scheduler = timer_->scheduler;
        if (!timer_->destroyed && timer_->running && !timer_->stop_requested) {
            fire_count = timer_->next_fire_count++;
            handler = timer_->handler;
            handler_userdata = timer_->handler_userdata;
            owner_spot = timer_->owner_spot;

            if (handler) {
                timer_->receive_callback_active = true;
            } else {
                timer_->fired_counts.push_back (fire_count);
                ensure_timer_signal_locked (timer_);
                timer_->recv_cv.notify_one ();
                notify_spot = owner_spot != NULL;
            }

            if (timer_->repeat_count > 0) {
                --timer_->repeat_count;
                if (timer_->repeat_count == 0)
                    timer_->running = false;
            }

            if (timer_->running && !timer_->destroyed && !timer_->stop_requested) {
                next_deadline_ns = monotonic_now_ns () + timer_->interval_ns;
                reschedule = true;
            }
        }
    }

    if (handler)
        handler (timer_, fire_count, handler_userdata);

    if (notify_spot)
        zlink_spot_notify_dispatch_event (
          owner_spot, ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE);

    std::unique_lock<std::mutex> scheduler_lock (scheduler->mutex);
    std::unique_lock<std::mutex> timer_lock (timer_->mutex);
    if (reschedule && !timer_->destroyed && timer_->running
        && !timer_->stop_requested) {
        schedule_timer_locked (timer_, next_deadline_ns);
        scheduler->cv.notify_all ();
    }
    if (timer_->scheduler_busy_refs > 0)
        --timer_->scheduler_busy_refs;
    timer_->cv.notify_all ();
}

void run_scheduler_loop (std::shared_ptr<scheduler_state_t> scheduler_)
{
    for (;;) {
        timer_handle_t *timer = NULL;
        {
            std::unique_lock<std::mutex> scheduler_lock (scheduler_->mutex);
            while (scheduler_->schedule.empty () && !scheduler_->shutdown_requested)
                scheduler_->cv.wait (scheduler_lock);

            if (scheduler_->shutdown_requested && scheduler_->schedule.empty ()) {
                scheduler_->started = false;
                scheduler_->shutdown_requested = false;
                break;
            }

            for (;;) {
                if (scheduler_->schedule.empty ()) {
                    timer = NULL;
                    break;
                }

                const uint64_t now_ns = monotonic_now_ns ();
                const std::multimap<uint64_t, timer_handle_t *>::iterator next =
                  scheduler_->schedule.begin ();
                if (next->first > now_ns) {
                    scheduler_->cv.wait_for (
                      scheduler_lock,
                      std::chrono::nanoseconds (next->first - now_ns));
                    continue;
                }

                timer = next->second;
                scheduler_->schedule.erase (next);
                {
                    std::lock_guard<std::mutex> timer_lock (timer->mutex);
                    timer->scheduler_registered = false;
                    timer->scheduled_deadline_ns = 0;
                    ++timer->scheduler_busy_refs;
                }
                break;
            }
        }

        if (!timer)
            continue;

        scheduler_fire_timer (timer);
    }
}

void ensure_scheduler_started (const std::shared_ptr<scheduler_state_t> &scheduler_)
{
    std::lock_guard<std::mutex> lock (scheduler_->mutex);
    if (scheduler_->started)
        return;
    scheduler_->shutdown_requested = false;
    scheduler_->worker = std::thread (run_scheduler_loop, scheduler_);
    scheduler_->worker.detach ();
    scheduler_->started = true;
}

std::shared_ptr<scheduler_state_t> resolve_spot_scheduler (void *owner_node_)
{
    std::lock_guard<std::mutex> lock (g_spot_scheduler_map_mutex);
    spot_scheduler_map_t::iterator it = g_spot_schedulers.find (owner_node_);
    if (it != g_spot_schedulers.end ())
        return it->second;

    std::shared_ptr<scheduler_state_t> scheduler (new scheduler_state_t ());
    g_spot_schedulers[owner_node_] = scheduler;
    return scheduler;
}

std::shared_ptr<scheduler_state_t> scheduler_for_timer (timer_handle_t *timer_)
{
    if (!timer_)
        return std::shared_ptr<scheduler_state_t> ();

    if (timer_->backend == timer_handle_t::backend_spot_node_scheduler)
        return resolve_spot_scheduler (timer_->owner_node);
    return g_global_scheduler;
}

void stop_timer_scheduler (timer_handle_t *timer_)
{
    if (!timer_ || !timer_->scheduler)
        return;

    scheduler_state_t *scheduler = timer_->scheduler;
    std::unique_lock<std::mutex> scheduler_lock (scheduler->mutex);
    std::unique_lock<std::mutex> timer_lock (timer_->mutex);
    timer_->stop_requested = true;
    timer_->running = false;
    remove_timer_registration_locked (timer_);
    timer_lock.unlock ();
    scheduler_lock.unlock ();
    scheduler->cv.notify_all ();
    timer_->recv_cv.notify_all ();
}
}

timer_handle_t *as_timer_handle (void *timer_)
{
    timer_handle_t *timer = static_cast<timer_handle_t *> (timer_);
    if (!timer || !timer->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    return timer;
}

int timer_handle_signaler_fd (timer_handle_t *timer_, zlink_fd_t *fd_out_)
{
    if (!timer_ || !fd_out_) {
        errno = EFAULT;
        return -1;
    }

    const zlink::fd_t fd = timer_->signaler.get_fd ();
    if (fd == zlink::retired_fd) {
        errno = EFAULT;
        return -1;
    }
    *fd_out_ = static_cast<zlink_fd_t> (fd);
    return 0;
}

int timer_handle_acquire_poller_ref (timer_handle_t *timer_)
{
    if (!timer_) {
        errno = EFAULT;
        return -1;
    }

    std::lock_guard<std::mutex> lock (timer_->mutex);
    if (timer_->receive_callback_active || timer_->poller_refs > 0) {
        errno = EBUSY;
        return -1;
    }
    ++timer_->poller_refs;
    return 0;
}

void timer_handle_release_poller_ref (timer_handle_t *timer_)
{
    if (!timer_)
        return;

    std::lock_guard<std::mutex> lock (timer_->mutex);
    if (timer_->poller_refs > 0)
        --timer_->poller_refs;
}

void *zlink_timer_new (void)
{
    std::unique_ptr<timer_handle_t> timer (new (std::nothrow) timer_handle_t (
      timer_handle_t::backend_global_scheduler));
    if (!timer) {
        errno = ENOMEM;
        return NULL;
    }
    if (!timer->signaler.valid ()) {
        errno = EFAULT;
        return NULL;
    }

    timer->scheduler = g_global_scheduler.get ();
    {
        std::lock_guard<std::mutex> lock (g_global_scheduler->mutex);
        ++g_global_scheduler->active_timers;
    }
    ensure_scheduler_started (g_global_scheduler);
    return timer.release ();
}

void *zlink_spot_timer_new (void *spot_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot || !spot->node) {
        errno = EFAULT;
        return NULL;
    }

    std::shared_ptr<scheduler_state_t> scheduler =
      resolve_spot_scheduler (spot->node);
    ensure_scheduler_started (scheduler);

    std::unique_ptr<timer_handle_t> timer (new (std::nothrow) timer_handle_t (
      timer_handle_t::backend_spot_node_scheduler, spot_, spot->node));
    if (!timer) {
        errno = ENOMEM;
        return NULL;
    }
    if (!timer->signaler.valid ()) {
        errno = EFAULT;
        return NULL;
    }

    timer->scheduler = scheduler.get ();
    {
        std::lock_guard<std::mutex> lock (scheduler->mutex);
        ++scheduler->active_timers;
    }
    return timer.release ();
}

int zlink_timer_destroy (void **timer_p_)
{
    if (!timer_p_ || !*timer_p_) {
        errno = EFAULT;
        return -1;
    }

    timer_handle_t *timer = as_timer_handle (*timer_p_);
    if (!timer)
        return -1;

    scheduler_state_t *scheduler = timer->scheduler;
    std::unique_lock<std::mutex> scheduler_lock (scheduler->mutex);
    std::unique_lock<std::mutex> timer_lock (timer->mutex);
    if (timer->poller_refs > 0) {
        errno = EBUSY;
        return -1;
    }
    timer->destroyed = true;
    timer->running = false;
    timer->stop_requested = true;
    remove_timer_registration_locked (timer);
    while (timer->scheduler_busy_refs > 0)
        timer->cv.wait (timer_lock);
    timer_lock.unlock ();
    scheduler_lock.unlock ();
    scheduler->cv.notify_all ();
    timer->recv_cv.notify_all ();

    {
        std::lock_guard<std::mutex> lock (scheduler->mutex);
        if (scheduler->active_timers > 0)
            --scheduler->active_timers;
        if (scheduler->active_timers == 0 && scheduler->schedule.empty ())
            scheduler->shutdown_requested = true;
    }
    scheduler->cv.notify_all ();

    delete timer;
    *timer_p_ = NULL;
    return 0;
}

int zlink_timer_start (void *timer_,
                       uint64_t interval_ns_,
                       uint64_t repeat_count_)
{
    timer_handle_t *timer = as_timer_handle (timer_);
    if (!timer || interval_ns_ == 0) {
        errno = EINVAL;
        return -1;
    }

    scheduler_state_t *scheduler = timer->scheduler;
    std::unique_lock<std::mutex> scheduler_lock (scheduler->mutex);
    std::lock_guard<std::mutex> lock (timer->mutex);
    timer->destroyed = false;
    timer->stop_requested = false;
    timer->running = true;
    timer->interval_ns = interval_ns_;
    timer->repeat_count = repeat_count_;
    timer->next_fire_count = 1;
    timer->fired_counts.clear ();
    drain_timer_signal_locked (timer);
    schedule_timer_locked (timer, monotonic_now_ns () + interval_ns_);
    scheduler->cv.notify_all ();
    return 0;
}

int zlink_timer_stop (void *timer_)
{
    timer_handle_t *timer = as_timer_handle (timer_);
    if (!timer)
        return -1;

    stop_timer_scheduler (timer);
    return 0;
}

int zlink_timer_recv (void *timer_, uint64_t *fire_count_out_, int flags_)
{
    timer_handle_t *timer = as_timer_handle (timer_);
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
    drain_timer_signal_locked (timer);
    ensure_timer_signal_locked (timer);
    timer->recv_in_progress = false;
    return 0;
}

int zlink_timer_handler (void *timer_,
                         zlink_timer_handler_fn handler_,
                         void *userdata_)
{
    timer_handle_t *timer = as_timer_handle (timer_);
    if (!timer || !handler_) {
        errno = EINVAL;
        return -1;
    }

    std::lock_guard<std::mutex> lock (timer->mutex);
    if (timer->recv_in_progress || timer->poller_refs > 0
        || timer->receive_callback_active) {
        errno = EBUSY;
        return -1;
    }

    timer->receive_callback_active = true;
    timer->handler = handler_;
    timer->handler_userdata = userdata_;
    return 0;
}

extern "C" void zlink_timer_cleanup_spot (void *spot_)
{
    if (!spot_)
        return;

    std::lock_guard<std::mutex> lock (g_spot_scheduler_map_mutex);
    for (spot_scheduler_map_t::iterator sit = g_spot_schedulers.begin ();
         sit != g_spot_schedulers.end (); ++sit) {
        std::shared_ptr<scheduler_state_t> scheduler = sit->second;
        std::lock_guard<std::mutex> scheduler_lock (scheduler->mutex);
        for (std::multimap<uint64_t, timer_handle_t *>::iterator it =
               scheduler->schedule.begin ();
             it != scheduler->schedule.end (); ++it) {
            timer_handle_t *timer = it->second;
            std::lock_guard<std::mutex> timer_lock (timer->mutex);
            if (timer->owner_spot == spot_)
                timer->owner_spot = NULL;
        }
    }
}
