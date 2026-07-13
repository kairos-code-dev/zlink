/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/locations/spot_handle_state.hpp"

namespace zlink::framework
{

spot_rid_t spot_handle_t::spot_rid () const noexcept
{
    if (!_state) {
        return spot_rid_t{};
    }
    const auto address = _state->snapshot ();
    return spot_rid_t::from_string (address.spot_rid.to_string ());
}

} // namespace zlink::framework
