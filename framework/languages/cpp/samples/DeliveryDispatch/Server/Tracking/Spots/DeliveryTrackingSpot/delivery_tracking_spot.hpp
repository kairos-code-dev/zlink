/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../../../../Shared/Contracts/messages.hpp"

#include <string>
#include <vector>

namespace zlink::samples::deliverydispatch
{

class delivery_tracking_spot_t
{
  public:
    explicit delivery_tracking_spot_t (std::string delivery_id) :
        _delivery_id (std::move (delivery_id))
    {
    }

    const std::string &delivery_id () const { return _delivery_id; }

    void record (const delivery_status_changed_req_t &status) { _history.push_back (status); }

  private:
    std::string _delivery_id;
    std::vector<delivery_status_changed_req_t> _history;
};

} // namespace zlink::samples::deliverydispatch
