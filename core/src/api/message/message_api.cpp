/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <zlink.h>

#include "api/core/config_result_internal.hpp"
#include "core/msg.hpp"
#include "core/recv_tls_view.hpp"

zlink_config_result_t zlink_msg_init (zlink_msg_t *msg_)
{
    return zlink::config_result_internal::from_rc (
      (reinterpret_cast<zlink::msg_t *> (msg_))->init ());
}

zlink_config_result_t zlink_msg_init_size (zlink_msg_t *msg_, size_t size_)
{
    return zlink::config_result_internal::from_rc (
      (reinterpret_cast<zlink::msg_t *> (msg_))->init_size (size_));
}

zlink_config_result_t zlink_msg_init_buffer (zlink_msg_t *msg_, const void *buf_, size_t size_)
{
    return zlink::config_result_internal::from_rc (
      (reinterpret_cast<zlink::msg_t *> (msg_))->init_buffer (buf_, size_));
}

zlink_config_result_t
zlink_msg_init_data (zlink_msg_t *msg_, void *data_, size_t size_, zlink_free_fn *ffn_, void *hint_)
{
    return zlink::config_result_internal::from_rc (
      (reinterpret_cast<zlink::msg_t *> (msg_))->init_data (data_, size_, ffn_, hint_));
}

zlink_config_result_t zlink_msg_close (zlink_msg_t *msg_)
{
    return zlink::config_result_internal::from_rc (
      (reinterpret_cast<zlink::msg_t *> (msg_))->close ());
}

zlink_config_result_t zlink_msg_move (zlink_msg_t *dest_, zlink_msg_t *src_)
{
    return zlink::config_result_internal::from_rc (
      (reinterpret_cast<zlink::msg_t *> (dest_))->move (*reinterpret_cast<zlink::msg_t *> (src_)));
}

zlink_config_result_t zlink_msg_copy (zlink_msg_t *dest_, zlink_msg_t *src_)
{
    return zlink::config_result_internal::from_rc (
      (reinterpret_cast<zlink::msg_t *> (dest_))->copy (*reinterpret_cast<zlink::msg_t *> (src_)));
}

zlink_config_result_t zlink_msg_adopt (zlink_msg_t *dest_, zlink_msg_t *src_)
{
    if (!dest_ || !src_) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }

    zlink::msg_t *dest = reinterpret_cast<zlink::msg_t *> (dest_);
    zlink::msg_t *src = reinterpret_cast<zlink::msg_t *> (src_);
    if (unlikely (!src->check ())) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }

    *dest = *src;
    return zlink::config_result_internal::from_rc (src->init ());
}

void *zlink_msg_data (zlink_msg_t *msg_)
{
    return (reinterpret_cast<zlink::msg_t *> (msg_))->data ();
}

size_t zlink_msg_size (const zlink_msg_t *msg_)
{
    return ((zlink::msg_t *) msg_)->size ();
}

int zlink_msg_refcnt (const zlink_msg_t *msg_, zlink_config_result_t *error_out_)
{
    if (!msg_) {
        errno = EFAULT;
        if (error_out_)
            *error_out_ = ZLINK_CONFIG_INVALID_HANDLE;
        return -1;
    }
    const zlink::msg_t *msg = reinterpret_cast<const zlink::msg_t *> (msg_);
    if (error_out_)
        *error_out_ = ZLINK_CONFIG_OK;
    return static_cast<int> (msg->refcnt_value ());
}

const char *zlink_msg_gets (const zlink_msg_t *msg_, const char *property_)
{
    LIBZLINK_UNUSED (msg_);
    LIBZLINK_UNUSED (property_);
    errno = EINVAL;
    return NULL;
}

void zlink_multipart_close (zlink_msg_t *parts_, size_t part_count_)
{
    if (!parts_)
        return;

    if (zlink::recv_tls_view::release_closed_prefix (parts_, part_count_))
        return;

    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}
