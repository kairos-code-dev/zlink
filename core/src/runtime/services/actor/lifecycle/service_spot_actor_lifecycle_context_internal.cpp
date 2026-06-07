/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/actor/lifecycle/service_spot_actor_lifecycle_context_internal.hpp"

#include "services/actor/validation/service_spot_actor_validation_internal.hpp"
#include "services/spot/runtime/spot_handle.hpp"

#include <string.h>

namespace zlink
{
namespace spot_actor_lifecycle
{

context_t::context_t () : active (false), spot (NULL)
{
    memset (&previous_actor, 0, sizeof (previous_actor));
    memset (&current_actor, 0, sizeof (current_actor));
}

namespace
{
context_t &context ()
{
    static thread_local context_t value;
    return value;
}

bool same_logical_spot (const spot_handle_t *lhs_, const spot_handle_t *rhs_)
{
    if (!lhs_ || !rhs_)
        return false;
    if (lhs_ == rhs_)
        return true;
    return lhs_->logical_state && lhs_->logical_state == rhs_->logical_state;
}
}

handler_scope_t::handler_scope_t (spot_handle_t *spot_,
                                  const zlink_spot_actor_lifecycle_info_t &info_) :
    _previous (context ())
{
    context_t &current = context ();
    current.active = true;
    current.spot = spot_;
    current.previous_actor = info_.previous_actor;
    current.current_actor = info_.current_actor;
}

handler_scope_t::~handler_scope_t ()
{
    context () = _previous;
}

bool reenters_same_actor (const zlink_actor_ref_t *actor_ref_)
{
    if (!actor_ref_)
        return false;

    const context_t &current = context ();
    return current.active
           && (spot_actor_internal::same_actor_ref_identity (*actor_ref_, current.previous_actor)
               || spot_actor_internal::same_actor_ref_identity (*actor_ref_,
                                                                current.current_actor));
}

bool reenters_same_spot (const spot_handle_t *spot_)
{
    const context_t &current = context ();
    return current.active && same_logical_spot (spot_, current.spot);
}

}
}
