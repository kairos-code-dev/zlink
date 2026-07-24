/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/spots/spot_identity.hpp>

#include <memory>
#include <utility>

namespace zlink::framework
{

namespace detail
{
struct spot_handle_access_t;
}

/* Messaging lookup handle for a spot. Callers resolve once and keep this
 * opaque handle; the framework keeps the internal address snapshot current
 * from location changes. A request refreshes and retries once only when the
 * target is known not to have handled the first attempt. One-way sends are
 * never retried. Lifecycle flows that need generations read location rows
 * through the store/runtime surfaces instead. */
class spot_handle_t final
{
  public:
    spot_id_t spot_id () const noexcept;

  private:
    spot_handle_t () = default;

    struct state_t;
    std::shared_ptr<state_t> _state;

    friend class spot_handle_resolver_t;
    friend class actor_spot_handle_resolver_t;
    friend struct detail::spot_handle_access_t;
};

namespace detail
{

struct spot_handle_access_t
{
    static spot_handle_t create (std::shared_ptr<spot_handle_t::state_t> state)
    {
        spot_handle_t handle;
        handle._state = std::move (state);
        return handle;
    }

    /* Instantiated only where the runtime state definition is complete. */
    template <typename... Args> static spot_handle_t make (Args &&...args)
    {
        return create (std::make_shared<spot_handle_t::state_t> (std::forward<Args> (args)...));
    }

    static const std::shared_ptr<spot_handle_t::state_t> &
    state (const spot_handle_t &handle) noexcept
    {
        return handle._state;
    }
};

} // namespace detail

} // namespace zlink::framework
