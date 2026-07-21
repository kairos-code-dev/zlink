/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/messaging/async_submit_runtime.hpp"

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <vector>

namespace zlink::framework::runtime::messaging
{
namespace
{

thread_local submit_attempt_context_t last_attempt_context;
thread_local bool has_attempt_context = false;

void clear_attempt_context () noexcept
{
    last_attempt_context = {};
    has_attempt_context = false;
}

submit_attempt_context_t take_attempt_context ()
{
    if (!has_attempt_context) {
        return {"*", nullptr, 0, std::chrono::seconds (1), 1024};
    }
    auto result = std::move (last_attempt_context);
    clear_attempt_context ();
    return result;
}

bool is_backpressured (const result_t<void> &result) noexcept
{
    return !result && result.error_kind () == framework_error_kind_t::worker_queue_full;
}

result_t<void> invoke_attempt (const std::function<result_t<void> ()> &submit,
                               submit_attempt_context_t &context)
{
    clear_attempt_context ();
    try {
        auto result = submit ();
        context = take_attempt_context ();
        return result;
    }
    catch (...) {
        (void) take_attempt_context ();
        throw;
    }
}

struct pending_entry_t
{
    std::uint64_t id = 0;
    std::uint64_t owner_epoch = 0;
    submit_attempt_context_t context;
    std::chrono::steady_clock::time_point deadline;
    std::function<result_t<void> ()> submit;
    std::shared_ptr<detail::task_completion_source_t<submit_result_t>> completion;
};

class async_submit_runtime_t
{
  public:
    async_submit_runtime_t () : _timer ([this] { run_timer (); }) {}

    ~async_submit_runtime_t ()
    {
        std::vector<pending_entry_t> pending;
        {
            std::lock_guard lock (_mutex);
            _stopping = true;
            pending.assign (std::make_move_iterator (_pending.begin ()),
                            std::make_move_iterator (_pending.end ()));
            _pending.clear ();
            pending.insert (pending.end (), std::make_move_iterator (_ready.begin ()),
                            std::make_move_iterator (_ready.end ()));
            _ready.clear ();
        }
        _changed.notify_all ();
        if (_timer.joinable ()) {
            _timer.join ();
        }
        for (auto &entry : pending) {
            entry.completion->complete (result_t<submit_result_t>::success (
              {submit_status_t::shutdown}));
        }
    }

    task_t<submit_result_t> submit (std::function<result_t<void> ()> submit)
    {
        if (!submit) {
            return task_t<submit_result_t> (result_t<submit_result_t>::failure (
              framework_error_kind_t::request_protocol_error,
              "one-way call is not bound to a submit operation"));
        }

        submit_attempt_context_t context;
        result_t<void> first = result_t<void>::failure (
          framework_error_kind_t::request_failed, "one-way submit failed");
        try {
            first = invoke_attempt (submit, context);
        }
        catch (const framework_exception_t &error) {
            return task_t<submit_result_t> (
              detail::result_access_t::failure<submit_result_t> (error));
        }
        catch (const std::exception &error) {
            return task_t<submit_result_t> (result_t<submit_result_t>::failure (
              framework_error_kind_t::request_failed, error.what ()));
        }
        catch (...) {
            return task_t<submit_result_t> (result_t<submit_result_t>::failure (
              framework_error_kind_t::request_failed, "one-way submit failed"));
        }

        record_attempt (context.target);
        if (!is_backpressured (first)) {
            return task_t<submit_result_t> (detail::one_way_submit_result (first));
        }

        const auto timeout = context.timeout > std::chrono::milliseconds::zero ()
                               ? context.timeout
                               : std::chrono::seconds (1);
        auto completion =
          std::make_shared<detail::task_completion_source_t<submit_result_t>> ();
        auto task = completion->task ();
        bool full = false;
        {
            std::lock_guard lock (_mutex);
            if (_stopping
                || (context.owner != nullptr
                    && (_shutdown_owners.contains (context.owner)
                        || _owner_epochs[context.owner] != context.owner_epoch))) {
                completion->complete (result_t<submit_result_t>::success (
                  {submit_status_t::shutdown}));
                return task;
            }
            const auto owner_pending = _reservations.find (context.owner);
            const auto owner_pending_count = owner_pending == _reservations.end ()
                                               ? 0
                                               : owner_pending->second;
            if (context.capacity == 0 || owner_pending_count >= context.capacity) {
                full = true;
            } else {
                ++_reservations[context.owner];
                _pending.push_back ({++_next_id, context.owner_epoch,
                                     std::move (context),
                                     std::chrono::steady_clock::now () + timeout,
                                     std::move (submit), completion});
            }
        }
        if (full) {
            completion->complete (result_t<submit_result_t>::success (
              {submit_status_t::backpressured}));
        } else {
            _changed.notify_all ();
        }
        return task;
    }

    void notify (const std::string &target, const void *owner)
    {
        pending_entry_t entry;
        {
            std::lock_guard lock (_mutex);
            const auto match = std::find_if (
              _pending.begin (), _pending.end (), [&] (const pending_entry_t &candidate) {
                  return candidate.context.owner == owner
                         && (candidate.context.target == target
                             || candidate.context.target == "*");
              });
            if (match == _pending.end ()) {
                const auto key = std::make_pair (owner, target);
                const auto retrying = _retrying.find (key);
                if (retrying != _retrying.end ()) {
                    ++_credits[retrying->second];
                }
                return;
            }
            entry = std::move (*match);
            _pending.erase (match);
            _ready.push_back (std::move (entry));
        }
        /* Socket callbacks forbid reentrant send. The runtime thread consumes
         * this single retry credit after the callback has returned. */
        _changed.notify_all ();
    }

    void notify (const void *owner)
    {
        pending_entry_t entry;
        {
            std::lock_guard lock (_mutex);
            const auto match = std::find_if (
              _pending.begin (), _pending.end (), [&] (const pending_entry_t &candidate) {
                  return candidate.context.owner == owner;
              });
            if (match == _pending.end ()) {
                const auto retrying = std::find_if (
                  _retrying.begin (), _retrying.end (), [&] (const auto &candidate) {
                      return candidate.first.first == owner;
                  });
                if (retrying != _retrying.end ()) {
                    ++_credits[retrying->second];
                }
                return;
            }
            entry = std::move (*match);
            _pending.erase (match);
            _ready.push_back (std::move (entry));
        }
        _changed.notify_all ();
    }

    void shutdown_owner (const void *owner) noexcept
    {
        if (owner == nullptr) {
            return;
        }
        std::vector<pending_entry_t> stopped;
        {
            std::lock_guard lock (_mutex);
            ++_owner_epochs[owner];
            _shutdown_owners.insert (owner);
            for (auto it = _pending.begin (); it != _pending.end ();) {
                if (it->context.owner == owner) {
                    release_reservation_locked (*it);
                    stopped.push_back (std::move (*it));
                    it = _pending.erase (it);
                } else {
                    ++it;
                }
            }
            for (auto it = _ready.begin (); it != _ready.end ();) {
                if (it->context.owner == owner) {
                    release_reservation_locked (*it);
                    stopped.push_back (std::move (*it));
                    it = _ready.erase (it);
                } else {
                    ++it;
                }
            }
        }
        for (auto &entry : stopped) {
            entry.completion->complete (result_t<submit_result_t>::success (
              {submit_status_t::shutdown}));
        }
        _changed.notify_all ();
    }

    void activate_owner (const void *owner)
    {
        if (owner == nullptr) {
            return;
        }
        std::lock_guard lock (_mutex);
        _shutdown_owners.erase (owner);
    }

    std::size_t pending_count () const
    {
        std::lock_guard lock (_mutex);
        std::size_t count = 0;
        for (const auto &[_, reservations] : _reservations) {
            count += reservations;
        }
        return count;
    }

    std::size_t attempt_count (const std::string &target) const
    {
        std::lock_guard lock (_mutex);
        const auto found = _attempts.find (target);
        return found == _attempts.end () ? 0 : found->second;
    }

    std::uint64_t owner_epoch (const void *owner) const
    {
        std::lock_guard lock (_mutex);
        const auto found = _owner_epochs.find (owner);
        return found == _owner_epochs.end () ? 0 : found->second;
    }

    void reset_for_tests ()
    {
        std::vector<pending_entry_t> stopped;
        {
            std::lock_guard lock (_mutex);
            stopped.assign (std::make_move_iterator (_pending.begin ()),
                            std::make_move_iterator (_pending.end ()));
            _pending.clear ();
            stopped.insert (stopped.end (), std::make_move_iterator (_ready.begin ()),
                            std::make_move_iterator (_ready.end ()));
            _ready.clear ();
            _attempts.clear ();
            _shutdown_owners.clear ();
            _credits.clear ();
            _retrying.clear ();
            _reservations.clear ();
            _owner_epochs.clear ();
        }
        for (auto &entry : stopped) {
            entry.completion->complete (result_t<submit_result_t>::success (
              {submit_status_t::shutdown}));
        }
        _changed.notify_all ();
    }

  private:
    void release_reservation_locked (const pending_entry_t &entry)
    {
        const auto found = _reservations.find (entry.context.owner);
        if (found == _reservations.end ()) {
            return;
        }
        if (found->second <= 1) {
            _reservations.erase (found);
        } else {
            --found->second;
        }
    }

    void finish_terminal_locked (const pending_entry_t &entry)
    {
        const auto key = std::make_pair (entry.context.owner, entry.context.target);
        const auto retrying = _retrying.find (key);
        if (retrying != _retrying.end () && retrying->second == entry.id) {
            _retrying.erase (retrying);
        }
        _credits.erase (entry.id);
        release_reservation_locked (entry);
    }

    void record_attempt (const std::string &target)
    {
        std::lock_guard lock (_mutex);
        ++_attempts[target];
    }

    void retry_once (pending_entry_t entry)
    {
        const auto retry_key =
          std::make_pair (entry.context.owner, entry.context.target);
        const auto finish_retry = [this, &entry] {
            std::lock_guard lock (_mutex);
            finish_terminal_locked (entry);
        };
        if (std::chrono::steady_clock::now () >= entry.deadline) {
            finish_retry ();
            entry.completion->complete (result_t<submit_result_t>::success (
              {submit_status_t::timed_out}));
            return;
        }

        submit_attempt_context_t retry_context;
        result_t<void> result = result_t<void>::failure (
          framework_error_kind_t::request_failed, "one-way submit failed");
        try {
            result = invoke_attempt (entry.submit, retry_context);
        }
        catch (const framework_exception_t &error) {
            finish_retry ();
            entry.completion->complete (
              detail::result_access_t::failure<submit_result_t> (error));
            return;
        }
        catch (const std::exception &error) {
            finish_retry ();
            entry.completion->complete (result_t<submit_result_t>::failure (
              framework_error_kind_t::request_failed, error.what ()));
            return;
        }
        catch (...) {
            finish_retry ();
            entry.completion->complete (result_t<submit_result_t>::failure (
              framework_error_kind_t::request_failed, "one-way submit failed"));
            return;
        }
        record_attempt (entry.context.target);
        if (!is_backpressured (result)) {
            finish_retry ();
            entry.completion->complete (detail::one_way_submit_result (result));
            return;
        }

        /* The logical destination is fixed by the first admission attempt.
         * A retry may observe a new physical route, but it cannot change the
         * operation identity or extend its deadline. */
        bool stopped = false;
        {
            std::lock_guard lock (_mutex);
            const auto retrying = _retrying.find (retry_key);
            if (retrying != _retrying.end () && retrying->second == entry.id) {
                _retrying.erase (retrying);
            }
            stopped = _stopping
                      || (entry.context.owner != nullptr
                          && (_shutdown_owners.contains (entry.context.owner)
                              || _owner_epochs[entry.context.owner]
                                   != entry.owner_epoch));
            if (!stopped) {
                const auto credit = _credits.find (entry.id);
                if (credit != _credits.end () && credit->second > 0) {
                    if (--credit->second == 0) {
                        _credits.erase (credit);
                    }
                    _ready.push_back (std::move (entry));
                } else {
                    _pending.push_back (std::move (entry));
                }
            } else {
                _credits.erase (entry.id);
                release_reservation_locked (entry);
            }
        }
        if (stopped) {
            entry.completion->complete (result_t<submit_result_t>::success (
              {submit_status_t::shutdown}));
        }
        _changed.notify_all ();
    }

    void run_timer ()
    {
        for (;;) {
            std::vector<pending_entry_t> expired;
            std::optional<pending_entry_t> ready;
            {
                std::unique_lock lock (_mutex);
                if (_stopping) {
                    return;
                }
                if (!_ready.empty ()) {
                    ready.emplace (std::move (_ready.front ()));
                    _ready.pop_front ();
                } else if (_pending.empty ()) {
                    _changed.wait (lock, [&] {
                        return _stopping || !_pending.empty () || !_ready.empty ();
                    });
                } else {
                    const auto next = std::min_element (
                      _pending.begin (), _pending.end (), [] (const auto &left,
                                                              const auto &right) {
                          return left.deadline < right.deadline;
                      });
                    _changed.wait_until (lock, next->deadline);
                }
                if (_stopping) {
                    return;
                }
                if (!ready && !_ready.empty ()) {
                    ready.emplace (std::move (_ready.front ()));
                    _ready.pop_front ();
                }
                if (ready) {
                    _retrying[std::make_pair (ready->context.owner,
                                              ready->context.target)] = ready->id;
                }
                const auto now = std::chrono::steady_clock::now ();
                for (auto it = _pending.begin (); it != _pending.end ();) {
                    if (it->deadline <= now) {
                        release_reservation_locked (*it);
                        expired.push_back (std::move (*it));
                        it = _pending.erase (it);
                    } else {
                        ++it;
                    }
                }
            }
            if (ready) {
                retry_once (std::move (*ready));
            }
            for (auto &entry : expired) {
                entry.completion->complete (result_t<submit_result_t>::success (
                  {submit_status_t::timed_out}));
            }
        }
    }

    mutable std::mutex _mutex;
    std::condition_variable _changed;
    std::deque<pending_entry_t> _pending;
    std::deque<pending_entry_t> _ready;
    std::map<std::string, std::size_t> _attempts;
    std::map<std::uint64_t, std::size_t> _credits;
    std::map<std::pair<const void *, std::string>, std::uint64_t> _retrying;
    std::map<const void *, std::size_t> _reservations;
    std::map<const void *, std::uint64_t> _owner_epochs;
    std::set<const void *> _shutdown_owners;
    std::uint64_t _next_id = 0;
    bool _stopping = false;
    std::thread _timer;
};

class logical_multicast_executor_t
{
  public:
    logical_multicast_executor_t ()
    {
        const auto count = std::max<std::size_t> (
          2, std::thread::hardware_concurrency ());
        _available = count;
        _workers.reserve (count);
        for (std::size_t index = 0; index < count; ++index) {
            _workers.emplace_back ([this] { run (); });
        }
    }

    ~logical_multicast_executor_t ()
    {
        {
            std::lock_guard lock (_mutex);
            _stopping = true;
        }
        _changed.notify_all ();
        for (auto &worker : _workers) {
            if (worker.joinable ()) {
                worker.join ();
            }
        }
    }

    task_t<publish_result_t> submit (std::function<publish_result_t ()> work)
    {
        if (!work) {
            return task_t<publish_result_t> (result_t<publish_result_t>::failure (
              framework_error_kind_t::request_protocol_error,
              "logical multicast call is not bound to a publisher"));
        }
        auto completion =
          std::make_shared<detail::task_completion_source_t<publish_result_t>> ();
        auto task = completion->task ();
        {
            std::lock_guard lock (_mutex);
            if (_stopping) {
                completion->complete (result_t<publish_result_t>::success (
                  {submit_status_t::shutdown, {}}));
                return task;
            }
            if (_available == 0) {
                completion->complete (result_t<publish_result_t>::success (
                  {submit_status_t::backpressured, {}}));
                return task;
            }
            --_available;
            _jobs.emplace_back (std::move (work), completion);
        }
        _changed.notify_one ();
        return task;
    }

  private:
    void run ()
    {
        for (;;) {
            std::pair<std::function<publish_result_t ()>,
                      std::shared_ptr<detail::task_completion_source_t<publish_result_t>>> job;
            {
                std::unique_lock lock (_mutex);
                _changed.wait (lock, [&] { return _stopping || !_jobs.empty (); });
                if (_stopping && _jobs.empty ()) {
                    return;
                }
                job = std::move (_jobs.front ());
                _jobs.pop_front ();
            }
            try {
                job.second->complete (
                  result_t<publish_result_t>::success (job.first ()));
            }
            catch (const framework_exception_t &error) {
                job.second->complete (
                  detail::result_access_t::failure<publish_result_t> (error));
            }
            catch (const std::exception &error) {
                job.second->complete (result_t<publish_result_t>::failure (
                  framework_error_kind_t::request_failed, error.what ()));
            }
            catch (...) {
                job.second->complete (result_t<publish_result_t>::failure (
                  framework_error_kind_t::request_failed,
                  "logical multicast submit failed"));
            }
            {
                std::lock_guard lock (_mutex);
                ++_available;
            }
        }
    }

    std::mutex _mutex;
    std::condition_variable _changed;
    std::deque<std::pair<
      std::function<publish_result_t ()>,
      std::shared_ptr<detail::task_completion_source_t<publish_result_t>>>> _jobs;
    std::vector<std::thread> _workers;
    std::size_t _available = 0;
    bool _stopping = false;
};

async_submit_runtime_t &runtime ()
{
    static async_submit_runtime_t value;
    return value;
}

logical_multicast_executor_t &multicast_executor ()
{
    static logical_multicast_executor_t value;
    return value;
}

} // namespace

void note_submit_attempt (std::string target,
                          const void *owner,
                          std::chrono::milliseconds timeout,
                          std::size_t capacity)
{
    last_attempt_context = {
      std::move (target), owner, runtime ().owner_epoch (owner), timeout, capacity};
    has_attempt_context = true;
}

void notify_submit_ready (const std::string &target, const void *owner)
{
    runtime ().notify (target, owner);
}

void notify_submit_ready (const void *owner)
{
    runtime ().notify (owner);
}

void shutdown_submit_owner (const void *owner) noexcept
{
    runtime ().shutdown_owner (owner);
}

void activate_submit_owner (const void *owner)
{
    runtime ().activate_owner (owner);
}

std::size_t pending_submit_count_for_tests ()
{
    return runtime ().pending_count ();
}

std::size_t submit_attempt_count_for_tests (const std::string &target)
{
    return runtime ().attempt_count (target);
}

void reset_async_submit_runtime_for_tests ()
{
    runtime ().reset_for_tests ();
}

} // namespace zlink::framework::runtime::messaging

namespace zlink::framework::detail
{

task_t<submit_result_t>
submit_one_way_task (std::function<result_t<void> ()> submit)
{
    return runtime::messaging::runtime ().submit (std::move (submit));
}

task_t<publish_result_t>
submit_logical_multicast_task (std::function<publish_result_t ()> submit)
{
    return runtime::messaging::multicast_executor ().submit (std::move (submit));
}

} // namespace zlink::framework::detail
