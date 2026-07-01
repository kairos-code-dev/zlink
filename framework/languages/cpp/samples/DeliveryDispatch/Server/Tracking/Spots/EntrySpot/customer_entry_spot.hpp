/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Actors/customer_actor.hpp"

namespace zlink::samples::deliverydispatch
{

class customer_entry_spot_t
{
  public:
    bool join (const customer_actor_t &actor) const { return !actor.actor_id ().empty (); }
};

} // namespace zlink::samples::deliverydispatch
