/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace zlink::framework::runtime
{

class offload_executor_t
{
  public:
    explicit offload_executor_t (std::size_t worker_count = 1,
                                 std::size_t max_queue_length = 0,
                                 std::string thread_name = "zlink-offload");
    offload_executor_t (std::size_t min_worker_count,
                        std::size_t max_worker_count,
                        std::size_t max_queue_length,
                        std::chrono::milliseconds idle_timeout,
                        std::string thread_name = "zlink-offload");
    ~offload_executor_t ();

    offload_executor_t (const offload_executor_t &) = delete;
    offload_executor_t &operator= (const offload_executor_t &) = delete;

    bool try_submit (std::function<void ()> work);
    void submit (std::function<void ()> work);
    void drain ();
    bool drained () const;
    std::size_t live_worker_count () const;

  private:
    void worker_loop ();
    void start_worker_locked ();

    mutable std::mutex _mutex;
    std::condition_variable _ready;
    std::condition_variable _empty;
    std::queue<std::function<void ()>> _queue;
    std::vector<std::thread> _workers;
    std::size_t _min_worker_count = 1;
    std::size_t _max_worker_count = 1;
    std::size_t _max_queue_length = 0;
    std::chrono::milliseconds _idle_timeout{0};
    std::string _thread_name;
    bool _stopping = false;
    std::size_t _active = 0;
    std::size_t _live_workers = 0;
    std::size_t _idle_workers = 0;
};

} // namespace zlink::framework::runtime
