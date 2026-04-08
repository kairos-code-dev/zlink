/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <zlink.h>

#include "protocol/metadata.hpp"
#include "core/msg.hpp"
#include "core/recv_tls_view.hpp"

namespace
{
zlink::msg_t *validated_msg (zlink_msg_t *msg_)
{
    if (!msg_) {
        errno = EINVAL;
        return NULL;
    }

    zlink::msg_t *msg = reinterpret_cast<zlink::msg_t *> (msg_);
    if (!msg->check ()) {
        errno = EINVAL;
        return NULL;
    }

    return msg;
}

const zlink::msg_t *validated_msg (const zlink_msg_t *msg_)
{
    if (!msg_) {
        errno = EINVAL;
        return NULL;
    }

    const zlink::msg_t *msg = reinterpret_cast<const zlink::msg_t *> (msg_);
    if (!msg->check ()) {
        errno = EINVAL;
        return NULL;
    }

    return msg;
}
}

int zlink_msg_init (zlink_msg_t *msg_)
{
    return (reinterpret_cast<zlink::msg_t *> (msg_))->init ();
}

int zlink_msg_init_size (zlink_msg_t *msg_, size_t size_)
{
    return (reinterpret_cast<zlink::msg_t *> (msg_))->init_size (size_);
}

int zlink_msg_init_buffer (zlink_msg_t *msg_, const void *buf_, size_t size_)
{
    return (reinterpret_cast<zlink::msg_t *> (msg_))->init_buffer (buf_, size_);
}

int zlink_msg_init_data (
  zlink_msg_t *msg_, void *data_, size_t size_, zlink_free_fn *ffn_, void *hint_)
{
    return (reinterpret_cast<zlink::msg_t *> (msg_))
      ->init_data (data_, size_, ffn_, hint_);
}

int zlink_msg_close (zlink_msg_t *msg_)
{
    return (reinterpret_cast<zlink::msg_t *> (msg_))->close ();
}

int zlink_msg_move (zlink_msg_t *dest_, zlink_msg_t *src_)
{
    return (reinterpret_cast<zlink::msg_t *> (dest_))
      ->move (*reinterpret_cast<zlink::msg_t *> (src_));
}

int zlink_msg_copy (zlink_msg_t *dest_, zlink_msg_t *src_)
{
    return (reinterpret_cast<zlink::msg_t *> (dest_))
      ->copy (*reinterpret_cast<zlink::msg_t *> (src_));
}

int zlink_msg_set_request (zlink_msg_t *msg_, uint64_t correlation_id_)
{
    zlink::msg_t *msg = validated_msg (msg_);
    if (!msg)
        return -1;

    msg->set_request_info (ZLINK_MSG_TYPE_REQUEST, correlation_id_);
    errno = 0;
    return 0;
}

int zlink_msg_set_reply (zlink_msg_t *msg_, uint64_t correlation_id_)
{
    zlink::msg_t *msg = validated_msg (msg_);
    if (!msg)
        return -1;

    msg->set_request_info (ZLINK_MSG_TYPE_REPLY, correlation_id_);
    errno = 0;
    return 0;
}

int zlink_msg_get_request_info (const zlink_msg_t *msg_,
                                uint8_t *type_out_,
                                uint64_t *correlation_id_out_)
{
    const zlink::msg_t *msg = validated_msg (msg_);
    if (!msg)
        return -1;

    if (type_out_)
        *type_out_ = msg->request_type ();
    if (correlation_id_out_)
        *correlation_id_out_ = msg->request_correlation_id ();
    errno = 0;
    return 0;
}

int zlink_msg_set_metadata (zlink_msg_t *msg_,
                            uint16_t key_,
                            const void *value_,
                            size_t value_size_)
{
    zlink::msg_t *msg = validated_msg (msg_);
    if (!msg)
        return -1;

    if (key_ < ZLINK_MSG_METADATA_KEY_USER_MIN
        || (value_ != NULL && value_size_ > ZLINK_MSG_METADATA_VALUE_MAX)) {
        errno = EINVAL;
        return -1;
    }

    if (msg->set_user_metadata (key_, value_, value_ ? value_size_ : 0) != 0)
        return -1;

    errno = 0;
    return 0;
}

const void *zlink_msg_get_metadata (const zlink_msg_t *msg_,
                                    uint16_t key_,
                                    size_t *size_)
{
    const zlink::msg_t *msg =
      reinterpret_cast<const zlink::msg_t *> (msg_);
    if (!msg_ || !msg->check ())
        return NULL;

    return msg->get_user_metadata (key_, size_);
}

void *zlink_msg_data (zlink_msg_t *msg_)
{
    return (reinterpret_cast<zlink::msg_t *> (msg_))->data ();
}

size_t zlink_msg_size (const zlink_msg_t *msg_)
{
    return ((zlink::msg_t *) msg_)->size ();
}

int zlink_msg_refcnt (const zlink_msg_t *msg_)
{
    const zlink::msg_t *msg = reinterpret_cast<const zlink::msg_t *> (msg_);
    return static_cast<int> (msg->refcnt_value ());
}

const char *zlink_msg_gets (const zlink_msg_t *msg_, const char *property_)
{
    const zlink::metadata_t *metadata =
      reinterpret_cast<const zlink::msg_t *> (msg_)->metadata ();
    const char *value = NULL;
    if (metadata)
        value = metadata->get (std::string (property_));
    if (value)
        return value;

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
