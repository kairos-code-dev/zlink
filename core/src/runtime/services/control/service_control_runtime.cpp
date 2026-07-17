/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/control/service_control_runtime.hpp"

#include "core/ctx.hpp"
#include "utils/clock.hpp"

#include <limits.h>
#include <vector>

namespace zlink
{
service_control_runtime_t::service_control_runtime_t (ctx_t *ctx_, const char *thread_name_) :
    _ctx (ctx_),
    _thread_name (thread_name_ ? thread_name_ : "service-ctrl"),
    _next_task_id (1),
    _active_task_id (0),
    _running (false),
    _stopping (false)
{
    zlink_assert (_ctx);
}

service_control_runtime_t::~service_control_runtime_t ()
{
    stop ();
}

bool service_control_runtime_t::start ()
{
    scoped_lock_t lock (_sync);
    if (_running)
        return true;

    _stopping = false;
    _active_task_id = 0;
    _ctx->start_thread (_thread, run, this, _thread_name.c_str ());
    _running = true;
    return true;
}

void service_control_runtime_t::stop ()
{
    {
        scoped_lock_t lock (_sync);
        if (!_running)
            return;
        _stopping = true;
        _cv.broadcast ();
    }

    _thread.stop ();

    scoped_lock_t lock (_sync);
    _tasks.clear ();
    _schedule.clear ();
    _active_task_id = 0;
    _running = false;
    _stopping = false;
}

uint64_t service_control_runtime_t::add_periodic_task (service_control_task_fn *fn_,
                                                       void *arg_,
                                                       uint32_t interval_ms_,
                                                       bool run_immediately_)
{
    if (!fn_ || interval_ms_ == 0) {
        errno = EINVAL;
        return 0;
    }

    zlink::clock_t clock;
    const uint64_t now = clock.now_ms ();

    scoped_lock_t lock (_sync);
    if (!_running || _stopping) {
        errno = ETERM;
        return 0;
    }

    task_entry_t task;
    task.id = _next_task_id++;
    if (task.id == 0)
        task.id = _next_task_id++;
    task.fn = fn_;
    task.arg = arg_;
    task.interval_ms = interval_ms_;
    task.next_run_ms = run_immediately_ ? now : now + interval_ms_;

    std::pair<std::map<uint64_t, task_entry_t>::iterator, bool> inserted =
      _tasks.insert (std::make_pair (task.id, task));
    try {
        schedule_task_locked (&inserted.first->second);
    }
    catch (const std::bad_alloc &) {
        //  Strong rollback: a task entry without a schedule slot would be
        //  invisible to the loop but still claim the ID.
        _tasks.erase (inserted.first);
        errno = ENOMEM;
        return 0;
    }
    _cv.broadcast ();
    return task.id;
}

int service_control_runtime_t::remove_task (uint64_t task_id_)
{
    if (task_id_ == 0)
        return 0;

    scoped_lock_t lock (_sync);
    if (_thread.get_started () && _thread.is_current_thread () && _active_task_id == task_id_) {
        std::map<uint64_t, task_entry_t>::iterator it = _tasks.find (task_id_);
        if (it != _tasks.end ()) {
            deschedule_task_locked (&it->second);
            _tasks.erase (it);
        }
        return 0;
    }
    std::map<uint64_t, task_entry_t>::iterator it = _tasks.find (task_id_);
    if (it != _tasks.end ()) {
        deschedule_task_locked (&it->second);
        _tasks.erase (it);
    }
    while (_active_task_id == task_id_)
        _cv.wait (&_sync, -1);
    return 0;
}

int service_control_runtime_t::wakeup_task (uint64_t task_id_)
{
    if (task_id_ == 0)
        return 0;

    scoped_lock_t lock (_sync);
    std::map<uint64_t, task_entry_t>::iterator it = _tasks.find (task_id_);
    if (it == _tasks.end ()) {
        errno = EINVAL;
        return -1;
    }
    deschedule_task_locked (&it->second);
    it->second.next_run_ms = 0;
    schedule_task_locked (&it->second);
    _cv.broadcast ();
    return 0;
}

bool service_control_runtime_t::is_current_thread () const
{
    return _thread.get_started () && _thread.is_current_thread ();
}

void service_control_runtime_t::run (void *arg_)
{
    service_control_runtime_t *self = static_cast<service_control_runtime_t *> (arg_);
    self->loop ();
}

void service_control_runtime_t::schedule_task_locked (task_entry_t *task_)
{
    if (!task_)
        return;

    deschedule_task_locked (task_);
    task_->schedule_it = _schedule.insert (std::make_pair (task_->next_run_ms, task_->id));
    task_->scheduled = true;
}

void service_control_runtime_t::deschedule_task_locked (task_entry_t *task_)
{
    if (!task_ || !task_->scheduled)
        return;

    _schedule.erase (task_->schedule_it);
    task_->scheduled = false;
}

void service_control_runtime_t::loop ()
{
    zlink::clock_t clock;

    while (true) {
        std::vector<due_call_t> due_calls;

        {
            scoped_lock_t lock (_sync);
            while (due_calls.empty () && !_stopping) {
                const uint64_t now = clock.now_ms ();

                while (!_schedule.empty ()) {
                    const std::multimap<uint64_t, uint64_t>::iterator next = _schedule.begin ();
                    if (next->first > now)
                        break;

                    const uint64_t task_id = next->second;
                    _schedule.erase (next);

                    std::map<uint64_t, task_entry_t>::iterator task_it = _tasks.find (task_id);
                    if (task_it == _tasks.end ())
                        continue;

                    task_entry_t &task = task_it->second;
                    task.scheduled = false;

                    task.next_run_ms = now + task.interval_ms;
                    schedule_task_locked (&task);

                    due_call_t call;
                    call.task_id = task.id;
                    call.fn = task.fn;
                    call.arg = task.arg;
                    due_calls.push_back (call);
                }

                if (!due_calls.empty () || _stopping)
                    break;

                if (_schedule.empty ()) {
                    _cv.wait (&_sync, -1);
                } else {
                    const uint64_t next_deadline = _schedule.begin ()->first;
                    const int wait_ms =
                      next_deadline <= now
                        ? 0
                        : static_cast<int> (std::min<uint64_t> (next_deadline - now,
                                                                static_cast<uint64_t> (INT_MAX)));
                    _cv.wait (&_sync, wait_ms);
                }
            }

            if (_stopping)
                break;
        }

        for (size_t i = 0; i < due_calls.size (); ++i) {
            due_call_t call = due_calls[i];
            {
                scoped_lock_t lock (_sync);
                if (_tasks.find (call.task_id) == _tasks.end ())
                    continue;
                _active_task_id = call.task_id;
            }

            call.fn (call.arg);

            {
                scoped_lock_t lock (_sync);
                if (_active_task_id == call.task_id) {
                    _active_task_id = 0;
                    _cv.broadcast ();
                }
            }
        }
    }

    scoped_lock_t lock (_sync);
    _active_task_id = 0;
    _cv.broadcast ();
}
}
