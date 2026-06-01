/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace zlink::framework::runtime
{

class offload_executor_t
{
public:
  explicit offload_executor_t (std::size_t worker_count = 1);
  ~offload_executor_t ();

  offload_executor_t (const offload_executor_t &) = delete;
  offload_executor_t &operator= (const offload_executor_t &) = delete;

  void submit (std::function<void ()> work);
  void drain ();
  bool drained () const;

private:
  void worker_loop ();

  mutable std::mutex _mutex;
  std::condition_variable _ready;
  std::condition_variable _empty;
  std::queue<std::function<void ()>> _queue;
  std::vector<std::thread> _workers;
  bool _stopping = false;
  std::size_t _active = 0;
};

} // namespace zlink::framework::runtime
