/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SERVICES_SPOT_NODE_OPS_HPP_INCLUDED
#define ZLINK_CPP_SERVICES_SPOT_NODE_OPS_HPP_INCLUDED

#include "spot.hpp"

namespace zlink
{
namespace service
{

inline spot_t spot_node_t::create_spot ()
{
    return spot_t (*this);
}

inline spot_t spot_node_t::entry_spot ()
{
    void *handle = NULL;
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_spot_node_entry_spot (_node, &handle)));
    return spot_t (handle);
}

inline std::optional<spot_t> spot_node_t::spot_lookup (
  const routing_id_t &spot_rid_)
{
    void *handle = NULL;
    const config_result_t rc = static_cast<config_result_t> (
      zlink_spot_node_spot_lookup (
        _node, zlink::detail::routing_id_native (spot_rid_), &handle));
    if (rc == config_result_t::not_found)
        return std::nullopt;
    detail::throw_if_failed<config_error_t> (rc);
    return std::optional<spot_t> (spot_t (handle));
}


} // namespace service
} // namespace zlink

#endif
