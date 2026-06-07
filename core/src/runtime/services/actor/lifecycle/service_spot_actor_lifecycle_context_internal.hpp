/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_SERVICE_SPOT_ACTOR_LIFECYCLE_CONTEXT_INTERNAL_HPP_INCLUDED
#define ZLINK_SERVICE_SPOT_ACTOR_LIFECYCLE_CONTEXT_INTERNAL_HPP_INCLUDED

#include "zlink.h"

struct spot_handle_t;

namespace zlink
{
namespace spot_actor_lifecycle
{

struct context_t
{
    context_t ();

    bool active;
    spot_handle_t *spot;
    zlink_actor_ref_t previous_actor;
    zlink_actor_ref_t current_actor;
};

class handler_scope_t
{
  public:
    handler_scope_t (spot_handle_t *spot_, const zlink_spot_actor_lifecycle_info_t &info_);
    ~handler_scope_t ();

  private:
    context_t _previous;
};

bool reenters_same_actor (const zlink_actor_ref_t *actor_ref_);
bool reenters_same_spot (const spot_handle_t *spot_);

}
}

#endif
