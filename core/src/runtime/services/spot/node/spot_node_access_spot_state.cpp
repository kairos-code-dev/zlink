/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/node/spot_node_access.hpp"

#include "services/spot/node/spot_node.hpp"

namespace zlink
{
int spot_node_access_t::try_register_spot_facade (spot_node_t *node_, spot_handle_t *spot_)
{
    if (!node_ || !spot_) {
        errno = EFAULT;
        return -1;
    }
    return node_->try_register_spot_facade (spot_);
}

void spot_node_access_t::unregister_spot_facade (spot_node_t *node_, spot_handle_t *spot_)
{
    if (node_ && spot_)
        node_->unregister_spot_facade (spot_);
}

bool spot_node_access_t::is_last_spot_facade_for_logical_state (spot_node_t *node_,
                                                                spot_handle_t *spot_)
{
    return node_ ? node_->is_last_spot_facade_for_logical_state (spot_) : true;
}

std::shared_ptr<spot_logical_state_t>
spot_node_access_t::create_user_spot_state (spot_node_t *node_)
{
    return node_ ? node_->create_user_spot_state () : std::shared_ptr<spot_logical_state_t> ();
}

std::shared_ptr<spot_logical_state_t> spot_node_access_t::entry_spot_state (spot_node_t *node_)
{
    return node_ ? node_->entry_spot_state () : std::shared_ptr<spot_logical_state_t> ();
}

std::shared_ptr<spot_logical_state_t>
spot_node_access_t::lookup_spot_state (spot_node_t *node_, const zlink_routing_id_t *spot_rid_)
{
    return node_ ? node_->lookup_spot_state (spot_rid_) : std::shared_ptr<spot_logical_state_t> ();
}

std::shared_ptr<spot_logical_state_t> spot_node_access_t::get_or_new_spot_state (
  spot_node_t *node_, const zlink_routing_id_t *spot_rid_, bool *created_out_)
{
    return node_ ? node_->get_or_new_spot_state (spot_rid_, created_out_)
                 : std::shared_ptr<spot_logical_state_t> ();
}

bool spot_node_access_t::publish_get_or_new_spot_state (
  spot_node_t *node_, const std::shared_ptr<spot_logical_state_t> &state_)
{
    return node_ ? node_->publish_get_or_new_spot_state (state_) : false;
}

void spot_node_access_t::cancel_get_or_new_spot_state (
  spot_node_t *node_, const std::shared_ptr<spot_logical_state_t> &state_)
{
    if (node_)
        node_->cancel_get_or_new_spot_state (state_);
}

void spot_node_access_t::remove_spot_state_if_unfacaded (
  spot_node_t *node_, const std::shared_ptr<spot_logical_state_t> &state_)
{
    if (node_)
        node_->remove_spot_state_if_unfacaded (state_);
}

int spot_node_access_t::update_spot_routing_id (spot_node_t *node_,
                                                spot_handle_t *spot_,
                                                const void *data_,
                                                size_t size_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    return node_->update_spot_routing_id (spot_, data_, size_);
}
}
