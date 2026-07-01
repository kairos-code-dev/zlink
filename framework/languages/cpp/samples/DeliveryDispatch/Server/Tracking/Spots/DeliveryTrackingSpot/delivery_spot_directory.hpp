/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "delivery_tracking_spot.hpp"

#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace zlink::samples::deliverydispatch
{

class delivery_spot_directory_t
{
  public:
    delivery_tracking_spot_t &get_or_create (const std::string &delivery_id)
    {
        std::lock_guard<std::mutex> lock (_mutex);
        auto [it, _] = _spots.try_emplace (delivery_id, delivery_tracking_spot_t{delivery_id});
        return it->second;
    }

    delivery_tracking_spot_t &require (const std::string &delivery_id)
    {
        std::lock_guard<std::mutex> lock (_mutex);
        auto it = _spots.find (delivery_id);
        if (it == _spots.end ()) {
            throw std::runtime_error ("delivery spot '" + delivery_id + "' was not created");
        }
        return it->second;
    }

  private:
    std::mutex _mutex;
    std::unordered_map<std::string, delivery_tracking_spot_t> _spots;
};

} // namespace zlink::samples::deliverydispatch
