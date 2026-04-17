/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <string.h>

#include "api/config_result_internal.hpp"
#include "api/routing_id_internal.hpp"

zlink_config_result_t zlink_routing_id_from_u32 (
  uint32_t value_,
  uint8_t *buf_,
  size_t buf_cap_,
  zlink_routing_id_t *out_)
{
    if (!out_ || !buf_) {
        errno = EFAULT;
        return zlink::config_result_internal::from_errno (errno);
    }
    if (buf_cap_ < sizeof (uint32_t)) {
        errno = EINVAL;
        return zlink::config_result_internal::from_errno (errno);
    }

    zlink::put_uint32 (buf_, value_);
    if (!zlink::routing_id_internal::assign_view (out_, buf_, sizeof (uint32_t))) {
        errno = EINVAL;
        return zlink::config_result_internal::from_errno (errno);
    }
    errno = 0;
    return ZLINK_CONFIG_OK;
}

zlink_config_result_t zlink_routing_id_to_u32 (const zlink_routing_id_t *rid_,
                                              uint32_t *value_out_)
{
    if (!rid_ || !value_out_) {
        errno = EFAULT;
        return zlink::config_result_internal::from_errno (errno);
    }

    uint32_t value = 0;
    if (!zlink::routing_id_internal::to_u32 (rid_, &value)) {
        errno = EINVAL;
        return zlink::config_result_internal::from_errno (errno);
    }

    *value_out_ = value;
    errno = 0;
    return ZLINK_CONFIG_OK;
}

zlink_config_result_t zlink_routing_id_from_text (const char *text_,
                                                  uint8_t *buf_,
                                                  size_t buf_cap_,
                                                  zlink_routing_id_t *out_)
{
    if (!out_ || !text_ || !buf_) {
        errno = EFAULT;
        return zlink::config_result_internal::from_errno (errno);
    }

    const size_t text_len = strlen (text_);
    if (text_len == 0 || text_len > buf_cap_
        || text_len > zlink::routing_id_internal::owned_capacity ()) {
        errno = EINVAL;
        return zlink::config_result_internal::from_errno (errno);
    }

    memcpy (buf_, text_, text_len);
    if (!zlink::routing_id_internal::assign_view (
          out_, reinterpret_cast<const uint8_t *> (buf_), text_len)) {
        errno = EINVAL;
        return zlink::config_result_internal::from_errno (errno);
    }

    if (!zlink::routing_id_internal::is_valid_utf8_text (out_)) {
        errno = EINVAL;
        return zlink::config_result_internal::from_errno (errno);
    }

    errno = 0;
    return ZLINK_CONFIG_OK;
}

zlink_config_result_t zlink_routing_id_from_bytes (const void *data_,
                                                   size_t size_,
                                                   uint8_t *buf_,
                                                   size_t buf_cap_,
                                                   zlink_routing_id_t *out_)
{
    if (!out_ || !buf_) {
        errno = EFAULT;
        return zlink::config_result_internal::from_errno (errno);
    }
    if (size_ > zlink::routing_id_internal::owned_capacity ()
        || size_ > buf_cap_
        || (size_ != 0 && !data_)) {
        errno = EINVAL;
        return zlink::config_result_internal::from_errno (errno);
    }

    if (size_ != 0)
        memcpy (buf_, data_, size_);
    if (!zlink::routing_id_internal::assign_view (
          out_, reinterpret_cast<const uint8_t *> (buf_), size_)) {
        errno = EINVAL;
        return zlink::config_result_internal::from_errno (errno);
    }

    errno = 0;
    return ZLINK_CONFIG_OK;
}

zlink_config_result_t zlink_routing_id_to_text (const zlink_routing_id_t *rid_,
                                                char *out_,
                                                size_t *out_len_)
{
    if (!rid_ || !out_len_) {
        errno = EFAULT;
        return zlink::config_result_internal::from_errno (errno);
    }

    if (!zlink::routing_id_internal::is_valid_utf8_text (rid_)) {
        errno = EINVAL;
        return zlink::config_result_internal::from_errno (errno);
    }

    const size_t needed = rid_->size + 1;
    if (out_ && *out_len_ >= needed) {
        const uint8_t *bytes = zlink::routing_id_internal::bytes (rid_);
        if (rid_->size != 0 && !bytes) {
            errno = EINVAL;
            return zlink::config_result_internal::from_errno (errno);
        }
        memcpy (out_, bytes, rid_->size);
        out_[rid_->size] = '\0';
    } else if (out_ != NULL) {
        *out_len_ = needed;
        errno = EINVAL;
        return zlink::config_result_internal::from_errno (errno);
    }

    *out_len_ = needed;
    errno = 0;
    return ZLINK_CONFIG_OK;
}

zlink_config_result_t zlink_routing_id_to_hex (const zlink_routing_id_t *rid_,
                                               char *out_,
                                               size_t *out_len_)
{
    if (!rid_ || !out_len_) {
        errno = EFAULT;
        return zlink::config_result_internal::from_errno (errno);
    }

    const size_t needed = rid_->size * 2 + 1;
    if (out_ && *out_len_ >= needed) {
        const size_t written = zlink::routing_id_internal::to_hex (rid_, out_,
                                                                    *out_len_);
        if (written == 0) {
            errno = EINVAL;
            return zlink::config_result_internal::from_errno (errno);
        }
    } else if (out_ != NULL) {
        *out_len_ = needed;
        errno = EINVAL;
        return zlink::config_result_internal::from_errno (errno);
    }

    *out_len_ = needed;
    errno = 0;
    return ZLINK_CONFIG_OK;
}
