/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Actors/customer_actor.hpp"
#include "../../../../Shared/Contracts/messages.hpp"

#include <string>
#include <unordered_map>
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

    bool join (const customer_actor_t &actor, const delivery_spot_join_req_t &request)
    {
        if (request.delivery_id != _delivery_id || request.customer_id != actor.actor_id ()) {
            return false;
        }
        _customers[actor.actor_id ()] = actor.snapshot ();
        return true;
    }

    void record (const delivery_status_req_t &status) { _history.push_back (status); }

  private:
    std::string _delivery_id;
    std::unordered_map<std::string, actor_ref_snapshot_t> _customers;
    std::vector<delivery_status_req_t> _history;
};

} // namespace zlink::samples::deliverydispatch
