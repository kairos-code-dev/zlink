/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include <zlink/framework/contracts/spots/spot.hpp>

#include <utility>

namespace zlink::framework
{

node_rid_t::node_rid_t (std::string value) : _value (std::move (value)) {}

node_rid_t node_rid_t::from_string (std::string value)
{
    return node_rid_t (std::move (value));
}

std::string_view node_rid_t::value () const noexcept
{
    return _value;
}

bool node_rid_t::empty () const noexcept
{
    return _value.empty ();
}

spot_rid_t::spot_rid_t (std::string value) : _value (std::move (value)) {}

spot_rid_t spot_rid_t::from_string (std::string value)
{
    return spot_rid_t (std::move (value));
}

std::string_view spot_rid_t::value () const noexcept
{
    return _value;
}

bool spot_rid_t::empty () const noexcept
{
    return _value.empty ();
}

} // namespace zlink::framework
