/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_DEFAULTS_HPP_INCLUDED__
#define __ZLINK_SPOT_DEFAULTS_HPP_INCLUDED__

#include "core/internal_defs.hpp"

#include <zlink.h>

#include <stddef.h>
#include <string.h>

namespace zlink
{
struct spot_node_option_setting_t
{
    spot_node_option_setting_t () : enabled (false), value (0), size (0) {}

    bool enabled;
    int value;
    size_t size;
};

struct spot_node_pub_defaults_t
{
    spot_node_option_setting_t sndhwm;
    spot_node_option_setting_t sndtimeo;
    spot_node_option_setting_t linger;
    spot_node_option_setting_t nodrop;
    spot_node_option_setting_t sndbuf;
    spot_node_option_setting_t rcvbuf;
    spot_node_option_setting_t auto_hwm_msg_unit_bytes;
};

struct spot_node_sub_defaults_t
{
    spot_node_option_setting_t rcvhwm;
    spot_node_option_setting_t linger;
    spot_node_option_setting_t sndbuf;
    spot_node_option_setting_t rcvbuf;
    spot_node_option_setting_t rcvtimeo;
    spot_node_option_setting_t auto_hwm_msg_unit_bytes;
};

inline void copy_spot_option_setting (spot_node_option_setting_t *dst_,
                                      const void *optval_,
                                      size_t optvallen_)
{
    if (!dst_)
        return;

    dst_->enabled = true;
    dst_->value = 0;
    dst_->size = optvallen_;
    memcpy (&dst_->value, optval_, optvallen_);
}

inline bool is_spot_pub_default_option (int option_)
{
    switch (option_) {
        case ZLINK_SPOT_PUB_OPT_SNDHWM:
        case ZLINK_SPOT_PUB_OPT_SNDTIMEO:
        case ZLINK_SPOT_PUB_OPT_LINGER:
        case ZLINK_SPOT_PUB_OPT_NODROP:
        case ZLINK_SPOT_PUB_OPT_SNDBUF:
        case ZLINK_SPOT_PUB_OPT_RCVBUF:
        case ZLINK_SPOT_PUB_OPT_AUTO_HWM_MSG_UNIT_BYTES:
            return true;
        default:
            return false;
    }
}

inline bool is_spot_sub_default_option (int option_)
{
    switch (option_) {
        case ZLINK_SPOT_SUB_OPT_RCVHWM:
        case ZLINK_SPOT_SUB_OPT_LINGER:
        case ZLINK_SPOT_SUB_OPT_SNDBUF:
        case ZLINK_SPOT_SUB_OPT_RCVBUF:
        case ZLINK_SPOT_SUB_OPT_RCVTIMEO:
        case ZLINK_SPOT_SUB_OPT_AUTO_HWM_MSG_UNIT_BYTES:
            return true;
        default:
            return false;
    }
}

inline spot_node_option_setting_t *spot_pub_default_slot (
  spot_node_pub_defaults_t *defaults_, int option_)
{
    if (!defaults_)
        return NULL;

    switch (option_) {
        case ZLINK_SPOT_PUB_OPT_SNDHWM:
            return &defaults_->sndhwm;
        case ZLINK_SPOT_PUB_OPT_SNDTIMEO:
            return &defaults_->sndtimeo;
        case ZLINK_SPOT_PUB_OPT_LINGER:
            return &defaults_->linger;
        case ZLINK_SPOT_PUB_OPT_NODROP:
            return &defaults_->nodrop;
        case ZLINK_SPOT_PUB_OPT_SNDBUF:
            return &defaults_->sndbuf;
        case ZLINK_SPOT_PUB_OPT_RCVBUF:
            return &defaults_->rcvbuf;
        case ZLINK_SPOT_PUB_OPT_AUTO_HWM_MSG_UNIT_BYTES:
            return &defaults_->auto_hwm_msg_unit_bytes;
        default:
            return NULL;
    }
}

inline spot_node_option_setting_t *spot_sub_default_slot (
  spot_node_sub_defaults_t *defaults_, int option_)
{
    if (!defaults_)
        return NULL;

    switch (option_) {
        case ZLINK_SPOT_SUB_OPT_RCVHWM:
            return &defaults_->rcvhwm;
        case ZLINK_SPOT_SUB_OPT_LINGER:
            return &defaults_->linger;
        case ZLINK_SPOT_SUB_OPT_SNDBUF:
            return &defaults_->sndbuf;
        case ZLINK_SPOT_SUB_OPT_RCVBUF:
            return &defaults_->rcvbuf;
        case ZLINK_SPOT_SUB_OPT_RCVTIMEO:
            return &defaults_->rcvtimeo;
        case ZLINK_SPOT_SUB_OPT_AUTO_HWM_MSG_UNIT_BYTES:
            return &defaults_->auto_hwm_msg_unit_bytes;
        default:
            return NULL;
    }
}

inline bool store_spot_pub_default (spot_node_pub_defaults_t *defaults_,
                                    int option_,
                                    const void *optval_,
                                    size_t optvallen_)
{
    spot_node_option_setting_t *slot =
      spot_pub_default_slot (defaults_, option_);
    if (!slot)
        return false;
    copy_spot_option_setting (slot, optval_, optvallen_);
    return true;
}

inline bool store_spot_sub_default (spot_node_sub_defaults_t *defaults_,
                                    int option_,
                                    const void *optval_,
                                    size_t optvallen_)
{
    spot_node_option_setting_t *slot =
      spot_sub_default_slot (defaults_, option_);
    if (!slot)
        return false;
    copy_spot_option_setting (slot, optval_, optvallen_);
    return true;
}

template <typename Target>
inline int apply_spot_default_option (Target *target_,
                                      int option_,
                                      const spot_node_option_setting_t &setting_)
{
    if (!setting_.enabled)
        return 0;
    return target_->set_option (option_, &setting_.value, setting_.size);
}

template <typename Pub>
inline int apply_spot_pub_defaults (Pub *pub_,
                                    const spot_node_pub_defaults_t &defaults_)
{
    if (!pub_) {
        errno = EINVAL;
        return -1;
    }

    if (apply_spot_default_option (pub_, ZLINK_SPOT_PUB_OPT_SNDHWM,
                                   defaults_.sndhwm)
        != 0)
        return -1;
    if (apply_spot_default_option (pub_, ZLINK_SPOT_PUB_OPT_SNDTIMEO,
                                   defaults_.sndtimeo)
        != 0)
        return -1;
    if (apply_spot_default_option (pub_, ZLINK_SPOT_PUB_OPT_LINGER,
                                   defaults_.linger)
        != 0)
        return -1;
    if (apply_spot_default_option (pub_, ZLINK_SPOT_PUB_OPT_NODROP,
                                   defaults_.nodrop)
        != 0)
        return -1;
    if (apply_spot_default_option (pub_, ZLINK_SPOT_PUB_OPT_SNDBUF,
                                   defaults_.sndbuf)
        != 0)
        return -1;
    if (apply_spot_default_option (pub_, ZLINK_SPOT_PUB_OPT_RCVBUF,
                                   defaults_.rcvbuf)
        != 0)
        return -1;
    if (apply_spot_default_option (
          pub_, ZLINK_SPOT_PUB_OPT_AUTO_HWM_MSG_UNIT_BYTES,
          defaults_.auto_hwm_msg_unit_bytes)
        != 0)
        return -1;
    return 0;
}

template <typename Sub>
inline int apply_spot_sub_defaults (Sub *sub_,
                                    const spot_node_sub_defaults_t &defaults_)
{
    if (!sub_) {
        errno = EINVAL;
        return -1;
    }

    if (apply_spot_default_option (sub_, ZLINK_SPOT_SUB_OPT_RCVHWM,
                                   defaults_.rcvhwm)
        != 0)
        return -1;
    if (apply_spot_default_option (sub_, ZLINK_SPOT_SUB_OPT_LINGER,
                                   defaults_.linger)
        != 0)
        return -1;
    if (apply_spot_default_option (sub_, ZLINK_SPOT_SUB_OPT_SNDBUF,
                                   defaults_.sndbuf)
        != 0)
        return -1;
    if (apply_spot_default_option (sub_, ZLINK_SPOT_SUB_OPT_RCVBUF,
                                   defaults_.rcvbuf)
        != 0)
        return -1;
    if (apply_spot_default_option (sub_, ZLINK_SPOT_SUB_OPT_RCVTIMEO,
                                   defaults_.rcvtimeo)
        != 0)
        return -1;
    if (apply_spot_default_option (
          sub_, ZLINK_SPOT_SUB_OPT_AUTO_HWM_MSG_UNIT_BYTES,
          defaults_.auto_hwm_msg_unit_bytes)
        != 0)
        return -1;
    return 0;
}
}

#endif
