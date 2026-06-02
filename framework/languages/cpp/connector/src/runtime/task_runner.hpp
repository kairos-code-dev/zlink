/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <functional>

namespace zlink::stream_connector::detail
{

class task_runner_t
{
public:
  void run (std::function<void ()> task) const { task (); }
};

} // namespace zlink::stream_connector::detail
