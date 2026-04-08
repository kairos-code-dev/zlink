/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_NODE_BATCH_OPTION_POLICY_HPP_INCLUDED__
#define __ZLINK_SPOT_NODE_BATCH_OPTION_POLICY_HPP_INCLUDED__

#include "services/spot/spot_data_plane_internal.hpp"

namespace zlink
{
struct spot_node_batch_option_binding_t
{
    spot_node_batch_option_binding_t ();
    spot_node_batch_option_binding_t (
      int option_,
      bool spot_node_batch_config_t::*bool_field_,
      int spot_node_batch_config_t::*int_field_,
      int int_min_value_,
      bool is_bool_);

    int option;
    bool spot_node_batch_config_t::*bool_field;
    int spot_node_batch_config_t::*int_field;
    int int_min_value;
    bool is_bool;
};

const spot_node_batch_option_binding_t *
resolve_spot_node_batch_option_binding (int option_);

bool apply_spot_node_batch_option_value (
  spot_node_batch_config_t *config_,
  const spot_node_batch_option_binding_t *binding_,
  const void *optval_,
  size_t optvallen_);

bool read_spot_node_batch_option_value (
  const spot_node_batch_config_t &config_,
  const spot_node_batch_option_binding_t *binding_,
  int *value_out_);
}

#endif
