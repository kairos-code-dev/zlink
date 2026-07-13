/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../../../Shared/Contracts/messages.hpp"

#include <string>

namespace zlink::samples::deliverydispatch
{

class customer_actor_t
{
  public:
    explicit customer_actor_t (std::string actor_id) : _actor_id (std::move (actor_id)) {}

    const std::string &actor_id () const { return _actor_id; }

    actor_ref_snapshot_t snapshot () const
    {
        return {zlink::framework::node_rid_t::from_string ("tracking-spot"), _actor_id, 1};
    }

  private:
    std::string _actor_id;
};

} // namespace zlink::samples::deliverydispatch
