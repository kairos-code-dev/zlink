/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_node_batch_option_policy.hpp"

#include <string.h>

namespace zlink
{
namespace
{
bool copy_bool_option_value_local (const void *optval_,
                                   size_t optvallen_,
                                   bool *out_)
{
    if (!optval_ || !out_ || optvallen_ == 0)
        return false;

    if (optvallen_ == sizeof (uint8_t)) {
        uint8_t value = 0;
        memcpy (&value, optval_, sizeof (value));
        *out_ = value != 0;
        return true;
    }
    if (optvallen_ == sizeof (int)) {
        int value = 0;
        memcpy (&value, optval_, sizeof (value));
        *out_ = value != 0;
        return true;
    }
    return false;
}

bool validate_positive_int_option_value_local (const void *optval_,
                                               size_t optvallen_,
                                               int min_value_,
                                               int *out_)
{
    if (!optval_ || !out_ || optvallen_ == 0 || optvallen_ > sizeof (int))
        return false;

    int value = 0;
    memcpy (&value, optval_, optvallen_);
    if (value < min_value_)
        return false;
    *out_ = value;
    return true;
}
}

spot_node_batch_option_binding_t::spot_node_batch_option_binding_t () :
    option (0),
    bool_field (NULL),
    int_field (NULL),
    int_min_value (0),
    is_bool (false)
{
}

spot_node_batch_option_binding_t::spot_node_batch_option_binding_t (
  int option_,
  bool spot_node_batch_config_t::*bool_field_,
  int spot_node_batch_config_t::*int_field_,
  int int_min_value_,
  bool is_bool_) :
    option (option_),
    bool_field (bool_field_),
    int_field (int_field_),
    int_min_value (int_min_value_),
    is_bool (is_bool_)
{
}

const spot_node_batch_option_binding_t *
resolve_spot_node_batch_option_binding (int option_)
{
    static const spot_node_batch_option_binding_t bindings[] = {
      spot_node_batch_option_binding_t (
        ZLINK_SPOT_NODE_OPT_PEER_BATCH_ENABLE,
        &spot_node_batch_config_t::enabled, NULL, 0, true),
      spot_node_batch_option_binding_t (
        ZLINK_SPOT_NODE_OPT_PEER_BATCH_DELAY_MS, NULL,
        &spot_node_batch_config_t::delay_ms, 0, false),
      spot_node_batch_option_binding_t (
        ZLINK_SPOT_NODE_OPT_PEER_BATCH_MAX_MESSAGES, NULL,
        &spot_node_batch_config_t::max_messages, 1, false),
      spot_node_batch_option_binding_t (
        ZLINK_SPOT_NODE_OPT_PEER_BATCH_MAX_BYTES, NULL,
        &spot_node_batch_config_t::max_bytes, 1, false),
      spot_node_batch_option_binding_t (
        ZLINK_SPOT_NODE_OPT_PEER_BATCH_BYPASS_BYTES, NULL,
        &spot_node_batch_config_t::bypass_bytes, 1, false),
      spot_node_batch_option_binding_t (
        ZLINK_SPOT_NODE_OPT_PEER_UNBATCH_MAX_MESSAGES_PER_TURN, NULL,
        &spot_node_batch_config_t::unbatch_max_messages_per_turn, 1, false),
      spot_node_batch_option_binding_t (
        ZLINK_SPOT_NODE_OPT_PEER_UNBATCH_MAX_BYTES_PER_TURN, NULL,
        &spot_node_batch_config_t::unbatch_max_bytes_per_turn, 1, false),
    };

    for (size_t i = 0; i < sizeof (bindings) / sizeof (bindings[0]); ++i) {
        if (bindings[i].option == option_)
            return &bindings[i];
    }
    return NULL;
}

bool apply_spot_node_batch_option_value (
  spot_node_batch_config_t *config_,
  const spot_node_batch_option_binding_t *binding_,
  const void *optval_,
  size_t optvallen_)
{
    if (!config_ || !binding_)
        return false;

    if (binding_->is_bool) {
        bool value = false;
        if (!copy_bool_option_value_local (optval_, optvallen_, &value))
            return false;
        config_->*(binding_->bool_field) = value;
        return true;
    }

    int value = 0;
    if (!validate_positive_int_option_value_local (optval_, optvallen_,
                                                   binding_->int_min_value,
                                                   &value)) {
        return false;
    }
    config_->*(binding_->int_field) = value;
    return true;
}

bool read_spot_node_batch_option_value (
  const spot_node_batch_config_t &config_,
  const spot_node_batch_option_binding_t *binding_,
  int *value_out_)
{
    if (!binding_ || !value_out_)
        return false;

    if (binding_->is_bool) {
        *value_out_ = config_.*(binding_->bool_field) ? 1 : 0;
        return true;
    }

    *value_out_ = config_.*(binding_->int_field);
    return true;
}
}
