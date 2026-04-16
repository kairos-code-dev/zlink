/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_ROUTING_ID_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_ROUTING_ID_INTERNAL_HPP_INCLUDED__

#include <string.h>

#include "utils/precompiled.hpp"
#include "zlink.h"
#include "protocol/wire.hpp"

namespace zlink
{
namespace routing_id_internal
{
inline bool to_u32 (const zlink_routing_id_t *rid_, uint32_t *value_out_)
{
    if (!rid_ || !value_out_ || rid_->size != sizeof (*value_out_))
    {
        errno = EINVAL;
        return false;
    }

    if (rid_->size > sizeof (rid_->data)) {
        errno = EINVAL;
        return false;
    }

    *value_out_ = get_uint32 (rid_->data);
    errno = 0;
    return true;
}

inline bool to_u32 (const uint8_t *data_,
                    size_t size_,
                    uint32_t *value_out_)
{
    if (!data_ || !value_out_ || size_ != 4) {
        errno = EINVAL;
        return false;
    }
    *value_out_ = get_uint32 (data_);
    errno = 0;
    return true;
}

inline void from_u32 (zlink_routing_id_t *out_, uint32_t value_)
{
    if (!out_)
        return;

    memset (out_, 0, sizeof (*out_));
    put_uint32 (out_->data, value_);
    out_->size = sizeof (value_);
}

inline bool has_nul (const zlink_routing_id_t *rid_)
{
    if (!rid_ || rid_->size == 0)
        return false;
    for (size_t i = 0; i < rid_->size; ++i) {
        if (rid_->data[i] == 0)
            return true;
    }
    return false;
}

inline bool is_valid_utf8_text (const zlink_routing_id_t *rid_)
{
    if (!rid_ || rid_->size == 0)
        return false;

    size_t index = 0;
    const uint8_t *data = rid_->data;
    while (index < rid_->size) {
        const uint8_t byte = data[index];
        if (byte <= 0x7f) {
            if (byte == 0x00)
                return false;
            ++index;
            continue;
        }
        size_t n = 0;
        uint32_t codepoint = 0;
        if ((byte & 0xE0) == 0xC0) {
            n = 2;
            if (byte < 0xC2)
                return false;
            if (index + 1 >= rid_->size)
                return false;
            const uint8_t b1 = data[index + 1];
            if ((b1 & 0xC0) != 0x80)
                return false;
            codepoint = (byte & 0x1fu) << 6 | (b1 & 0x3fu);
            if (codepoint < 0x80)
                return false;
        }
        else if ((byte & 0xF0) == 0xE0) {
            n = 3;
            if (index + 2 >= rid_->size)
                return false;
            const uint8_t b1 = data[index + 1];
            const uint8_t b2 = data[index + 2];
            if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80)
                return false;
            if (byte == 0xE0 && b1 < 0xA0)
                return false;
            if (byte == 0xED && b1 > 0x9F)
                return false;
            codepoint = ((byte & 0x0fu) << 12)
                       | ((b1 & 0x3fu) << 6)
                       | (b2 & 0x3fu);
            if (codepoint < 0x800)
                return false;
        }
        else if ((byte & 0xF8) == 0xF0) {
            n = 4;
            if (index + 3 >= rid_->size)
                return false;
            const uint8_t b1 = data[index + 1];
            const uint8_t b2 = data[index + 2];
            const uint8_t b3 = data[index + 3];
            if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80
                || (b3 & 0xC0) != 0x80)
                return false;
            if (byte == 0xF0 && b1 < 0x90)
                return false;
            if (byte == 0xF4 && b1 > 0x8F)
                return false;
            if (byte > 0xF4)
                return false;
            codepoint = ((byte & 0x07u) << 18)
                       | ((b1 & 0x3fu) << 12)
                       | ((b2 & 0x3fu) << 6)
                       | (b3 & 0x3fu);
            if (codepoint < 0x10000 || codepoint > 0x10FFFF)
                return false;
        }
        else {
            return false;
        }
        index += n;
    }
    return true;
}

inline size_t to_hex (const zlink_routing_id_t *rid_,
                      char *out_,
                      size_t out_len_)
{
    static const char hex_chars[] = "0123456789abcdef";

    if (!rid_)
        return 0;

    const size_t needed = rid_->size * 2 + 1;
    if (!out_ || out_len_ < needed)
        return 0;

    for (size_t i = 0; i < rid_->size; ++i) {
        out_[i * 2] = hex_chars[(rid_->data[i] >> 4) & 0x0f];
        out_[i * 2 + 1] = hex_chars[rid_->data[i] & 0x0f];
    }
    out_[rid_->size * 2] = '\0';
    return needed;
}
} // namespace routing_id_internal
} // namespace zlink

#endif
