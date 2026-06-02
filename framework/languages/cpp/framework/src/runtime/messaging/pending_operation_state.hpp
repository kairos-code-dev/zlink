/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/channels/pending_operation.hpp>

#include <atomic>
#include <exception>
#include <memory>
#include <mutex>
#include <utility>

namespace zlink::framework::detail
{

struct pending_operation_state_t
{
  std::atomic_bool completed { false };
  std::atomic_bool cancelled { false };
  mutable std::mutex failure_gate;
  std::exception_ptr failure;

  bool try_complete () noexcept
  {
    bool expected = false;
    return completed.compare_exchange_strong (expected, true);
  }

  bool try_cancel () noexcept
  {
    if (!try_complete ()) {
      return false;
    }
    cancelled.store (true);
    return true;
  }

  bool try_fail (std::exception_ptr error) noexcept
  {
    if (!try_complete ()) {
      return false;
    }
    {
      std::lock_guard<std::mutex> lock (failure_gate);
      failure = std::move (error);
    }
    return true;
  }
};

} // namespace zlink::framework::detail
