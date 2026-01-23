/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_ZMP_METADATA_HPP_INCLUDED__
#define __ZMQ_ZMP_METADATA_HPP_INCLUDED__

#include <limits.h>
#include <string.h>
#include <string>
#include <vector>

#include "utils/err.hpp"
#include "protocol/metadata.hpp"
#include "core/options.hpp"
#include "protocol/wire.hpp"

namespace zmq
{
namespace zmp_metadata
{
static inline const char *socket_type_string (int socket_type_)
{
    switch (socket_type_) {
        case ZMQ_PAIR:
            return "PAIR";
        case ZMQ_PUB:
            return "PUB";
        case ZMQ_SUB:
            return "SUB";
        case ZMQ_DEALER:
            return "DEALER";
        case ZMQ_ROUTER:
            return "ROUTER";
        case ZMQ_XPUB:
            return "XPUB";
        case ZMQ_XSUB:
            return "XSUB";
        case ZMQ_STREAM:
            return "STREAM";
        default:
            zmq_assert (false);
            return NULL;
    }
}

static inline void append_u32 (std::vector<unsigned char> &buf_,
                               uint32_t value_)
{
    const size_t offset = buf_.size ();
    buf_.resize (offset + 4);
    put_uint32 (&buf_[offset], value_);
}

static inline void append_property (std::vector<unsigned char> &buf_,
                                    const char *name_,
                                    const void *value_,
                                    size_t value_len_)
{
    const size_t name_len = strlen (name_);
    zmq_assert (name_len <= UCHAR_MAX);
    buf_.push_back (static_cast<unsigned char> (name_len));
    buf_.insert (buf_.end (), name_, name_ + name_len);
    zmq_assert (value_len_ <= 0x7FFFFFFF);
    append_u32 (buf_, static_cast<uint32_t> (value_len_));
    if (value_len_ > 0) {
        const unsigned char *src =
          static_cast<const unsigned char *> (value_);
        buf_.insert (buf_.end (), src, src + value_len_);
    }
}

static inline void add_basic_properties (const options_t &options_,
                                         std::vector<unsigned char> &buf_)
{
    const char *socket_type = socket_type_string (options_.type);
    append_property (buf_, "Socket-Type", socket_type, strlen (socket_type));

    if (options_.type == ZMQ_DEALER || options_.type == ZMQ_ROUTER) {
        append_property (buf_, "Identity", options_.routing_id,
                         options_.routing_id_size);
    }
}

static inline int parse (const unsigned char *ptr_,
                         size_t length_,
                         metadata_t::dict_t &out_)
{
    size_t bytes_left = length_;
    while (bytes_left > 1) {
        const size_t name_length = static_cast<size_t> (*ptr_);
        ptr_ += 1;
        bytes_left -= 1;
        if (bytes_left < name_length)
            break;

        const std::string name (
          reinterpret_cast<const char *> (ptr_), name_length);
        ptr_ += name_length;
        bytes_left -= name_length;
        if (bytes_left < 4)
            break;

        const size_t value_length =
          static_cast<size_t> (get_uint32 (ptr_));
        ptr_ += 4;
        bytes_left -= 4;
        if (bytes_left < value_length)
            break;

        const char *value = reinterpret_cast<const char *> (ptr_);
        ptr_ += value_length;
        bytes_left -= value_length;

        out_[name] = std::string (value, value_length);
    }

    if (bytes_left > 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}
}
}

#endif
