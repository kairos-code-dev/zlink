/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_DISPATCH_WORKER_POOL_HPP_INCLUDED__
#define __ZLINK_SPOT_DISPATCH_WORKER_POOL_HPP_INCLUDED__

#include "utils/macros.hpp"
#include "core/thread.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace zlink
{
class spot_dispatch_worker_pool_t
{
  public:
    spot_dispatch_worker_pool_t ();
    ~spot_dispatch_worker_pool_t ();

    int start (int min_workers_, int max_workers_);
    int update_limits (int min_workers_, int max_workers_);
    void stop ();
    int post (void *spot_);

    int min_workers () const;
    int max_workers () const;

  private:
    static void worker_entry (void *arg_);
    bool spawn_worker_locked ();
    void run_worker ();

    mutable std::mutex _mutex;
    std::condition_variable _cv;
    std::deque<void *> _ready;
    std::unordered_set<void *> _queued;
    std::unordered_set<void *> _active;
    std::unordered_set<void *> _dirty;
    std::vector<thread_t *> _threads;
    bool _stopping;
    int _min_workers;
    int _max_workers;
    int _live_workers;
    int _idle_workers;
};

int spot_dispatch_default_workers_min ();
int spot_dispatch_default_workers_max ();
}

#endif
