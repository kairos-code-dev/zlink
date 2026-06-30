/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <mutex>
#include <string>
#include <utility>

namespace zlink::framework::e2e::registry_messaging::registry
{

class fault_state_t
{
  public:
    void set_mode (std::string mode)
    {
        std::lock_guard lock (_mutex);
        _mode = std::move (mode);
    }

    std::string mode () const
    {
        std::lock_guard lock (_mutex);
        return _mode;
    }

  private:
    mutable std::mutex _mutex;
    std::string _mode = "none";
};

} // namespace zlink::framework::e2e::registry_messaging::registry
