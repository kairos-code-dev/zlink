/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/dispatch/offload_executor.hpp"
#include "runtime/execution/serial_execution_queue.hpp"

#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

int
main ()
{
  zlink::framework::runtime::offload_executor_t executor (2);

  std::vector<int> order;
  std::string failed_item;
  bool error_seen = false;
  zlink::framework::runtime::serial_execution_queue_t queue (
    executor,
    4,
    [&](const std::string &name, const std::exception_ptr &error) {
      failed_item = name;
      try {
        if (error) {
          std::rethrow_exception (error);
        }
      } catch (const std::runtime_error &ex) {
        error_seen = std::string (ex.what ()) == "boom";
      }
    });

  if (!queue.try_post ("first", [&] { order.push_back (1); }) ||
      !queue.try_post ("second", [&] { order.push_back (2); }) ||
      !queue.try_post ("third", [&] { order.push_back (3); })) {
    return 1;
  }
  queue.drain ();
  if (order != std::vector<int> { 1, 2, 3 } || queue.pending_count () != 0) {
    return 2;
  }

  queue.post ("fail", [] { throw std::runtime_error ("boom"); });
  queue.post ("after-fail", [&] { order.push_back (4); });
  queue.drain ();
  if (!error_seen || failed_item != "fail" ||
      order != std::vector<int> { 1, 2, 3, 4 }) {
    return 3;
  }

  queue.close ();
  if (!queue.closed () ||
      queue.try_post ("closed", [] {}) ||
      queue.pending_count () != 0) {
    return 4;
  }

  bool capacity_error = false;
  try {
    zlink::framework::runtime::serial_execution_queue_t invalid (executor, 0);
  } catch (const std::invalid_argument &) {
    capacity_error = true;
  }
  return capacity_error ? 0 : 5;
}
