/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Infrastructure/scenario_state.hpp"

#include <zlink/framework.hpp>

namespace zlink::framework::e2e::registration_codec::server
{

class scoped_lifecycle_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<scoped_dependency_t, singleton_dependency_t>;
    using request_type = scoped_lifecycle_req_t;
    using reply_type = scoped_lifecycle_reply_t;

    scoped_lifecycle_handler_t (scoped_dependency_t &scoped, singleton_dependency_t &singleton) :
        _scoped (scoped), _singleton (singleton)
    {
    }

    scoped_lifecycle_reply_t handle (const scoped_lifecycle_req_t &)
    {
        return {.scoped_id = _scoped.id,
                .singleton_id = _singleton.id,
                .destroyed_before = scoped_dependency_destroyed.load ()};
    }

  private:
    scoped_dependency_t &_scoped;
    singleton_dependency_t &_singleton;
};

class scoped_lifecycle_stats_handler_t
{
  public:
    using request_type = scoped_lifecycle_stats_req_t;
    using reply_type = scoped_lifecycle_stats_reply_t;

    scoped_lifecycle_stats_reply_t handle (const scoped_lifecycle_stats_req_t &)
    {
        return {.destroyed_count = scoped_dependency_destroyed.load ()};
    }
};

} // namespace zlink::framework::e2e::registration_codec::server
