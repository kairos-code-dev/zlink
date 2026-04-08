/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "core/message_envelope.hpp"

#include <string.h>
#include <vector>

#include "core/msg.hpp"
#include "core/recv_internal.hpp"
#include "sockets/socket_base.hpp"

namespace
{
enum : uint8_t
{
    rr_envelope_version = 1,
    metadata_envelope_version = 1
};

const unsigned char rr_envelope_magic[] = {'Z', 'R', 'R', 'P'};
const size_t rr_envelope_size = sizeof (rr_envelope_magic) + 1 + 1 + 8;
const unsigned char metadata_envelope_magic[] = {'Z', 'M', 'D', 'T'};
const size_t metadata_envelope_prefix_size =
  sizeof (metadata_envelope_magic) + 1;

bool is_request_reply_type (uint8_t type_)
{
    return type_ == ZLINK_MSG_TYPE_REQUEST || type_ == ZLINK_MSG_TYPE_REPLY;
}

void encode_u64_be (uint64_t value_, unsigned char *out_)
{
    for (int i = 7; i >= 0; --i) {
        out_[i] = static_cast<unsigned char> (value_ & 0xffu);
        value_ >>= 8;
    }
}

uint64_t decode_u64_be (const unsigned char *in_)
{
    uint64_t value = 0;
    for (size_t i = 0; i < 8; ++i)
        value = (value << 8) | in_[i];
    return value;
}

bool frame_has_more (const zlink_msg_t &msg_)
{
    return (reinterpret_cast<const zlink::msg_t *> (&msg_)->flags ()
            & zlink::msg_t::more)
           != 0;
}

bool try_decode_request_reply_envelope (const zlink_msg_t &frame_,
                                        uint8_t *type_out_,
                                        uint64_t *correlation_id_out_)
{
    if (!frame_has_more (frame_))
        return false;

    if (zlink_msg_size (&frame_) != rr_envelope_size)
        return false;

    const unsigned char *data = static_cast<const unsigned char *> (
      zlink_msg_data (&const_cast<zlink_msg_t &> (frame_)));
    if (memcmp (data, rr_envelope_magic, sizeof (rr_envelope_magic)) != 0)
        return false;
    if (data[sizeof (rr_envelope_magic)] != rr_envelope_version)
        return false;

    const uint8_t type = data[sizeof (rr_envelope_magic) + 1];
    if (!is_request_reply_type (type))
        return false;

    if (type_out_)
        *type_out_ = type;
    if (correlation_id_out_)
        *correlation_id_out_ =
          decode_u64_be (data + sizeof (rr_envelope_magic) + 2);
    return true;
}

bool try_decode_user_metadata_envelope (const zlink_msg_t &frame_,
                                        const unsigned char **encoded_out_,
                                        size_t *encoded_size_out_)
{
    if (!encoded_out_ || !encoded_size_out_ || !frame_has_more (frame_))
        return false;

    if (zlink_msg_size (&frame_) < metadata_envelope_prefix_size)
        return false;

    const unsigned char *data = static_cast<const unsigned char *> (
      zlink_msg_data (&const_cast<zlink_msg_t &> (frame_)));
    if (memcmp (data, metadata_envelope_magic, sizeof (metadata_envelope_magic))
        != 0)
        return false;
    if (data[sizeof (metadata_envelope_magic)] != metadata_envelope_version)
        return false;

    *encoded_out_ = data + metadata_envelope_prefix_size;
    *encoded_size_out_ =
      zlink_msg_size (&frame_) - metadata_envelope_prefix_size;
    return true;
}

void set_request_reply_info (zlink_msg_t *msg_,
                             uint8_t type_,
                             uint64_t correlation_id_)
{
    if (!msg_)
        return;
    zlink::msg_t *msg = reinterpret_cast<zlink::msg_t *> (msg_);
    if (!msg->check ())
        return;
    msg->set_request_info (type_, correlation_id_);
}
}

bool zlink::message_has_request_reply_envelope (const msg_t &msg_)
{
    return is_request_reply_type (msg_.request_type ());
}

bool zlink::message_has_user_metadata_envelope (const msg_t &msg_)
{
    return msg_.has_user_metadata ();
}

int zlink::encode_request_reply_envelope (const msg_t &src_, msg_t *dst_)
{
    if (!dst_) {
        errno = EFAULT;
        return -1;
    }

    const uint8_t type = src_.request_type ();
    if (!is_request_reply_type (type)) {
        errno = EINVAL;
        return -1;
    }

    if (dst_->init_size (rr_envelope_size) != 0)
        return -1;

    unsigned char *out = static_cast<unsigned char *> (dst_->data ());
    memcpy (out, rr_envelope_magic, sizeof (rr_envelope_magic));
    out[sizeof (rr_envelope_magic)] = rr_envelope_version;
    out[sizeof (rr_envelope_magic) + 1] = type;
    encode_u64_be (src_.request_correlation_id (),
                   out + sizeof (rr_envelope_magic) + 2);
    dst_->set_flags (zlink::msg_t::command);
    return 0;
}

int zlink::encode_user_metadata_envelope (const msg_t &src_, msg_t *dst_)
{
    if (!dst_) {
        errno = EFAULT;
        return -1;
    }

    if (!src_.has_user_metadata ()) {
        errno = EINVAL;
        return -1;
    }

    const size_t metadata_size = src_.user_metadata_encoded_size ();
    if (dst_->init_size (metadata_envelope_prefix_size + metadata_size) != 0)
        return -1;

    unsigned char *out = static_cast<unsigned char *> (dst_->data ());
    memcpy (out, metadata_envelope_magic, sizeof (metadata_envelope_magic));
    out[sizeof (metadata_envelope_magic)] = metadata_envelope_version;
    if (src_.encode_user_metadata (out + metadata_envelope_prefix_size,
                                   metadata_size)
        != 0) {
        const int saved_errno = errno;
        (void) dst_->close ();
        errno = saved_errno;
        return -1;
    }
    dst_->set_flags (zlink::msg_t::command);
    return 0;
}

int zlink::strip_internal_message_envelopes (socket_base_t *socket_,
                                             zlink_msg_t *msg_)
{
    if (!socket_ || !msg_) {
        errno = EFAULT;
        return -1;
    }

    uint8_t accumulated_request_type = ZLINK_MSG_TYPE_DATA;
    uint64_t accumulated_correlation_id = 0;
    std::vector<unsigned char> accumulated_metadata;

    while (true) {
        uint8_t request_type = ZLINK_MSG_TYPE_DATA;
        uint64_t correlation_id = 0;
        bool recognized = false;

        if (try_decode_request_reply_envelope (*msg_, &request_type,
                                               &correlation_id)) {
            recognized = true;
        } else {
            const unsigned char *encoded_metadata = NULL;
            size_t encoded_metadata_size = 0;
            if (try_decode_user_metadata_envelope (
                  *msg_, &encoded_metadata, &encoded_metadata_size)) {
                try {
                    accumulated_metadata.assign (
                      encoded_metadata,
                      encoded_metadata + encoded_metadata_size);
                } catch (const std::bad_alloc &) {
                    errno = ENOMEM;
                    return -1;
                }
                recognized = true;
            }
        }

        if (!recognized)
            return 0;

        zlink_msg_close (msg_);
        zlink_msg_init (msg_);
        zlink_msg_t payload;
        zlink_msg_init (&payload);
        if (zlink::recv_followup_msg_socket (socket_, &payload) < 0) {
            zlink_msg_close (&payload);
            return -1;
        }

        if (zlink_msg_move (msg_, &payload) != 0) {
            zlink_msg_close (&payload);
            errno = EFAULT;
            return -1;
        }

        if (request_type != ZLINK_MSG_TYPE_DATA) {
            accumulated_request_type = request_type;
            accumulated_correlation_id = correlation_id;
        }
        if (accumulated_request_type != ZLINK_MSG_TYPE_DATA)
            set_request_reply_info (msg_, accumulated_request_type,
                                    accumulated_correlation_id);
        if (!accumulated_metadata.empty ()) {
            msg_t *payload_msg = reinterpret_cast<msg_t *> (msg_);
            if (payload_msg->decode_user_metadata (&accumulated_metadata[0],
                                                   accumulated_metadata.size ())
                != 0)
                return -1;
        }
    }
}
