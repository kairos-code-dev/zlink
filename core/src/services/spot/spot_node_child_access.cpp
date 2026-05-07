/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_node_child_access.hpp"

#include "services/spot/spot_node.hpp"

zlink::spot_runtime_t *zlink::spot_node_child_access_t::runtime (
  spot_node_t *node_)
{
    return node_ ? node_->runtime () : NULL;
}

int zlink::spot_node_child_access_t::set_pub_option (spot_node_t *node_,
                                                     int option_,
                                                     const void *optval_,
                                                     size_t optvallen_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    return node_->set_pub_option (option_, optval_, optvallen_);
}

bool zlink::spot_node_child_access_t::is_shutting_down (
  const spot_node_t *node_)
{
    return node_ && node_->is_shutting_down ();
}

void zlink::spot_node_child_access_t::remove_spot_pub (spot_node_t *node_,
                                                       spot_pub_t *pub_)
{
    if (node_)
        node_->remove_spot_pub (pub_);
}

void zlink::spot_node_child_access_t::remove_spot_sub (spot_node_t *node_,
                                                       spot_sub_t *sub_)
{
    if (node_)
        node_->remove_spot_sub (sub_);
}

int zlink::spot_node_child_access_t::destroy_attachment (
  spot_node_t *node_,
  uint64_t attachment_id_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    return node_->destroy_attachment (attachment_id_);
}

int zlink::spot_node_child_access_t::destroy_attachment_async (
  spot_node_t *node_,
  uint64_t attachment_id_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    return node_->destroy_attachment_async (attachment_id_);
}

void zlink::spot_node_child_access_t::submit_pub_summary (
  spot_node_t *node_,
  spot_pub_t *pub_,
  uint16_t state_,
  int error_code_)
{
    if (node_)
        node_->submit_pub_summary (pub_, state_, error_code_);
}

void zlink::spot_node_child_access_t::submit_sub_summary (
  spot_node_t *node_,
  spot_sub_t *sub_,
  uint16_t state_,
  int error_code_)
{
    if (node_)
        node_->submit_sub_summary (sub_, state_, error_code_);
}

int zlink::spot_node_child_access_t::send_subscription_update (
  spot_node_t *node_,
  const std::string &raw_filter_,
  bool subscribe_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    return node_->send_subscription_update (raw_filter_, subscribe_);
}

int zlink::spot_node_child_access_t::send_ready_ack_update (
  spot_node_t *node_,
  const std::string &target_endpoint_,
  const std::string &raw_filter_,
  const std::string &ack_source_id_,
  bool subscribe_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    return node_->send_ready_ack_update (
      target_endpoint_, raw_filter_, ack_source_id_, subscribe_);
}

void zlink::spot_node_child_access_t::schedule_subscription_replay (
  spot_node_t *node_)
{
    if (node_)
        node_->schedule_subscription_replay ();
}

void zlink::spot_node_child_access_t::note_local_sub_filters_changed (
  spot_node_t *node_,
  bool had_filters_,
  bool has_filters_)
{
    if (node_)
        node_->note_local_sub_filters_changed (had_filters_, has_filters_);
}

bool zlink::spot_node_child_access_t::has_active_peers (
  const spot_node_t *node_)
{
    return node_ && node_->has_active_peers ();
}

void zlink::spot_node_child_access_t::notify_subscription_forwarded (
  spot_node_t *node_,
  const std::string &raw_filter_)
{
    if (node_)
        node_->notify_subscription_forwarded (raw_filter_);
}

void zlink::spot_node_child_access_t::mark_subject_changed (
  spot_node_t *node_,
  const std::string &subject_,
  uint32_t subject_kind_)
{
    if (!node_)
        return;

    char prefix[16];
    snprintf (prefix, sizeof (prefix), "%u:", subject_kind_);
    scoped_lock_t node_lock (node_->_sync);
    node_->_summary_state.subject_last_changed_ms[std::string (prefix)
                                                  + subject_] =
      zlink::clock_t ().now_ms ();
    node_->_summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
}
