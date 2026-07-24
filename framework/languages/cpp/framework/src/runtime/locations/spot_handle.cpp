/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/locations/spot_handle_state.hpp"

namespace zlink::framework
{

spot_id_t spot_handle_t::spot_id () const noexcept
{
    if (!_state) {
        return spot_id_t{};
    }
    const auto address = _state->snapshot ();
    return spot_id_t (address.spot_id);
}

} // namespace zlink::framework
