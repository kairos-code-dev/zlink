/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_NATIVE_PARTS_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_NATIVE_PARTS_HPP_INCLUDED

#include "../../Contracts/Core/types.hpp"
#include "../../Contracts/Errors/error.hpp"
#include "../../Contracts/Messaging/message.hpp"

#include <cerrno>
#include <cstring>
#include <functional>
#include <iterator>
#include <string>
#include <type_traits>
#include <vector>

extern "C" int zlink_socket_request_progress_internal (void *socket_);

namespace zlink
{

namespace detail
{

inline void request_progress_socket (void *socket_) noexcept
{
    (void) zlink_socket_request_progress_internal (socket_);
}

inline void close_message_array (zlink_msg_t *parts_, size_t part_count_) noexcept
{
    if (!parts_)
        return;
    zlink_multipart_close (parts_, part_count_);
}

inline void close_native_parts (std::vector<zlink_msg_t> &parts_,
                                size_t start_index_ = 0) noexcept
{
    if (start_index_ >= parts_.size ())
        return;

    for (size_t i = start_index_; i < parts_.size (); ++i)
        (void) zlink_msg_close (&parts_[i]);
}

inline bool is_common_string_option (compat::options::socket_option option_) noexcept
{
    switch (option_) {
    case compat::options::socket_option::last_endpoint:
    case compat::options::socket_option::bindtodevice:
    case compat::options::socket_option::tls_cert:
    case compat::options::socket_option::tls_key:
    case compat::options::socket_option::tls_ca:
    case compat::options::socket_option::tls_hostname:
    case compat::options::socket_option::tls_password:
        return true;
    default:
        return false;
    }
}

template<typename Getter, typename Option>
inline int get_string_option (Getter getter_,
                              void *handle_,
                              Option option_,
                              size_t initial_capacity_,
                              std::string &value_)
{
    size_t cap = initial_capacity_;
    const size_t max_cap = 64u * 1024u;

    while (cap <= max_cap) {
        std::vector<char> buffer (cap);
        size_t size = cap;
        const int rc = getter_ (handle_, option_, buffer.data (), &size);
        if (rc == 0) {
            const size_t bounded = size <= buffer.size () ? size : buffer.size ();
            size_t out_size = bounded;
            if (out_size > 0 && buffer[out_size - 1] == '\0')
                --out_size;
            value_.assign (buffer.data (), out_size);
            return 0;
        }

        if (errno != EINVAL || cap == max_cap)
            return -1;

        cap *= 2u;
        if (cap > max_cap)
            cap = max_cap;
    }

    errno = EINVAL;
    return -1;
}

inline int move_parts_to_native (std::vector<message_t> &parts_,
                                 std::vector<zlink_msg_t> &native_)
{
    native_.clear ();
    native_.resize (parts_.size ());

    size_t moved = 0;
    for (; moved < parts_.size (); ++moved) {
        if (!parts_[moved].valid ()) {
            errno = EINVAL;
            break;
        }
        detail::move_to_native(parts_[moved], &native_[moved]);
        if (parts_[moved].valid ())
            break;
    }

    if (moved == parts_.size ())
        return 0;

    for (size_t i = 0; i < moved; ++i) {
        parts_[i].init ();
        if (parts_[i].valid ())
            (void) zlink_msg_move (detail::native_handle(parts_[i]), &native_[i]);
        (void) zlink_msg_close (&native_[i]);
    }

    native_.clear ();
    return -1;
}

inline void restore_parts_from_native (std::vector<message_t> &parts_,
                                       std::vector<zlink_msg_t> &native_,
                                       size_t start_index_ = 0) noexcept
{
    const size_t count =
      native_.size () < parts_.size () ? native_.size () : parts_.size ();
    for (size_t i = start_index_; i < count; ++i) {
        parts_[i].init ();
        if (parts_[i].valid ())
            (void) zlink_msg_move (detail::native_handle(parts_[i]), &native_[i]);
        (void) zlink_msg_close (&native_[i]);
    }
    native_.clear ();
}

inline int assign_parts_from_native (zlink_msg_t *parts_native_,
                                     size_t part_count_,
                                     std::vector<message_t> &parts_)
{
    parts_.clear ();
    parts_.resize (part_count_);
    for (size_t i = 0; i < part_count_; ++i) {
        if (zlink_msg_move (detail::native_handle(parts_[i]), &parts_native_[i]) != 0) {
            parts_.clear ();
            close_message_array (parts_native_, part_count_);
            return -1;
        }
    }
    close_message_array (parts_native_, part_count_);
    return 0;
}

inline int recv_result_from_errno (int err_) noexcept
{
    switch (err_) {
    case EAGAIN:
        return ZLINK_RECV_NO_DATA;
    case EBUSY:
        return ZLINK_RECV_BUSY;
    case EFAULT:
        return ZLINK_RECV_INVALID_HANDLE;
    case ENOTSUP:
#if defined(EOPNOTSUPP) && EOPNOTSUPP != ENOTSUP
    case EOPNOTSUPP:
#endif
        return ZLINK_RECV_NOT_SUPPORTED;
    default:
        return ZLINK_RECV_INTERNAL_ERROR;
    }
}

inline int recv_result_from_rc (int rc_) noexcept
{
    return rc_ == 0 ? ZLINK_RECV_OK : recv_result_from_errno (errno);
}

inline int assign_parts_from_native (std::vector<zlink_msg_t> &parts_native_,
                                     std::vector<message_t> &parts_)
{
    parts_.clear ();
    parts_.resize (parts_native_.size ());
    for (size_t i = 0; i < parts_native_.size (); ++i) {
        if (zlink_msg_move (detail::native_handle(parts_[i]), &parts_native_[i]) != 0) {
            parts_.clear ();
            close_native_parts (parts_native_, i);
            parts_native_.clear ();
            return -1;
        }
    }
    parts_native_.clear ();
    return 0;
}

inline std::vector<message_t>
take_parts_from_native (zlink_msg_t *parts_, size_t part_count_)
{
    std::vector<message_t> parts;
    parts.resize (part_count_);
    for (size_t i = 0; i < part_count_; ++i)
        (void) zlink_msg_move (detail::native_handle (parts[i]), &parts_[i]);
    close_message_array (parts_, part_count_);
    return parts;
}

inline int recv_router_parts (void *router_,
                              recv_flags_t flags_,
                              const zlink_routing_id_t **source_node_rid_out_,
                              const zlink_routing_id_t **source_spot_rid_out_,
                              uint64_t *request_seq_out_,
                              std::vector<message_t> &parts_)
{
    if (!source_node_rid_out_ || !source_spot_rid_out_ || !request_seq_out_) {
        errno = EFAULT;
        return ZLINK_RECV_INVALID_HANDLE;
    }

    *source_node_rid_out_ = NULL;
    *source_spot_rid_out_ = NULL;
    *request_seq_out_ = 0;
    parts_.clear ();

    for (;;) {
        zlink_msg_t part;
        if (zlink_msg_init (&part) != 0)
            return recv_result_from_errno (errno);

        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        const zlink_recv_result_t rc = zlink_router_recv_part (
          router_, source_node_rid_out_, source_spot_rid_out_,
          request_seq_out_, &part, &has_more,
          static_cast<zlink_recv_flags_t> (flags_));
        if (rc != ZLINK_RECV_OK) {
            (void) zlink_msg_close (&part);
            return rc;
        }

        message_t message;
        if (zlink_msg_move (detail::native_handle (message), &part) != 0) {
            const int saved_errno = errno;
            (void) zlink_msg_close (&part);
            errno = saved_errno != 0 ? saved_errno : EFAULT;
            return recv_result_from_errno (errno);
        }
        parts_.push_back (std::move (message));
        if (has_more == ZLINK_PART_FINAL)
            return ZLINK_RECV_OK;
    }
}

template<typename SubmitFn>
inline int submit_native_parts (std::vector<zlink_msg_t> &parts_native_,
                                size_t &failed_index_out_,
                                SubmitFn submit_)
{
    failed_index_out_ = 0;
    if (parts_native_.empty ()) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }

    for (size_t i = 0; i < parts_native_.size (); ++i) {
        const zlink_part_flag_t part_flag =
          i + 1 < parts_native_.size () ? ZLINK_PART_MORE : ZLINK_PART_FINAL;
        const int rc = submit_ (&parts_native_[i], part_flag, i + 1 == parts_native_.size ());
        if (rc != ZLINK_SUBMIT_OK) {
            failed_index_out_ = i;
            return rc;
        }
    }

    return ZLINK_SUBMIT_OK;
}

template<typename RecvFn>
inline int collect_parts_from_recv (RecvFn recv_, std::vector<message_t> &parts_out_)
{
    std::vector<zlink_msg_t> native_parts;

    for (;;) {
        native_parts.emplace_back ();
        zlink_msg_t &native_part = native_parts.back ();
        if (zlink_msg_init (&native_part) != 0) {
            native_parts.pop_back ();
            close_native_parts (native_parts);
            return -1;
        }

        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        const int rc = recv_ (&native_part, &has_more, native_parts.size () == 1u);
        if (rc != ZLINK_RECV_OK) {
            (void) zlink_msg_close (&native_part);
            native_parts.pop_back ();
            close_native_parts (native_parts);
            return rc;
        }

        if (!has_more)
            break;
    }

    return assign_parts_from_native (native_parts, parts_out_);
}

struct recv_envelope_t
{
    routing_id_t source_rid;
    routing_id_t source_spot_rid;
    bool has_request_seq;
    uint64_t request_seq;
    std::vector<message_t> parts;

    void reset () noexcept
    {
        source_rid = zlink::detail::unchecked_empty_routing_id ();
        source_spot_rid = zlink::detail::unchecked_empty_routing_id ();
        has_request_seq = false;
        request_seq = 0;
        parts.clear ();
    }

    recv_envelope_t ()
        : source_rid (zlink::detail::unchecked_empty_routing_id ()),
          source_spot_rid (zlink::detail::unchecked_empty_routing_id ()),
          has_request_seq (false),
          request_seq (0),
          parts ()
    {
    }
};

inline bool socket_uses_router_recv (void *socket_)
{
    if (!socket_)
        return false;

    int type = 0;
    size_t size = sizeof (type);
    if (zlink_get_option (socket_, ZLINK_OPT_TYPE, &type, &size) != 0)
        return false;

    return type == ZLINK_SOCKET_ROUTER || type == 6;
}

inline bool socket_uses_stream_recv (void *socket_)
{
    if (!socket_)
        return false;

    int type = 0;
    size_t size = sizeof (type);
    if (zlink_get_option (socket_, ZLINK_OPT_TYPE, &type, &size) != 0)
        return false;

    return type == ZLINK_SOCKET_STREAM;
}

inline int recv_envelope (void *socket_,
                          recv_flags_t flags_,
                          recv_envelope_t &envelope_)
{
    envelope_.reset ();

    if (socket_uses_router_recv (socket_)) {
        const zlink_routing_id_t *source_rid = NULL;
        const zlink_routing_id_t *source_spot_rid = NULL;
        uint64_t request_seq = 0;
        const int rc = recv_router_parts (
          socket_, flags_, &source_rid, &source_spot_rid, &request_seq,
          envelope_.parts);
        if (rc != ZLINK_RECV_OK)
            return -1;

        if (source_rid && source_rid->size > 0)
            envelope_.source_rid = zlink::detail::native_routing_id (*source_rid);
        if (source_spot_rid && source_spot_rid->size > 0)
            envelope_.source_spot_rid = zlink::detail::native_routing_id (*source_spot_rid);
        if (request_seq != 0) {
            envelope_.has_request_seq = true;
            envelope_.request_seq = request_seq;
        }
    } else {
        const zlink_routing_id_t *source_rid = NULL;
        zlink_msg_t first_part;
        if (zlink_msg_init (&first_part) != 0)
            return -1;

        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        const int first_rc = zlink_recv_part (
          socket_, &source_rid, &first_part, &has_more,
          static_cast<zlink_recv_flags_t> (flags_));
        if (first_rc != ZLINK_RECV_OK) {
            (void) zlink_msg_close (&first_part);
            return first_rc;
        }

        message_t first_msg;
        if (zlink_msg_move (detail::native_handle (first_msg), &first_part) != 0) {
            (void) zlink_msg_close (&first_part);
            return -1;
        }

        if (has_more == ZLINK_PART_FINAL) {
            envelope_.parts.emplace_back (std::move (first_msg));
            if (source_rid && source_rid->size > 0)
                envelope_.source_rid = zlink::detail::native_routing_id (*source_rid);
            return 0;
        }

        std::vector<message_t> remaining_parts;
        const int rc = collect_parts_from_recv (
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t *has_more_out_, bool) {
              return zlink_recv_part (
                socket_, &source_rid, part_out_, has_more_out_,
                static_cast<zlink_recv_flags_t> (flags_));
          },
          remaining_parts);
        if (rc != 0)
            return -1;

        envelope_.parts.reserve (remaining_parts.size () + 1);
        envelope_.parts.emplace_back (std::move (first_msg));
        envelope_.parts.insert (envelope_.parts.end (),
                               std::make_move_iterator (remaining_parts.begin ()),
                               std::make_move_iterator (remaining_parts.end ()));

        if (source_rid && source_rid->size > 0)
            envelope_.source_rid = zlink::detail::native_routing_id (*source_rid);
    }

    return 0;
}

inline int recv_parts (void *socket_,
                       zlink_routing_id_t *source_rid_out_,
                       recv_flags_t flags_,
                       std::vector<message_t> &parts_)
{
    recv_envelope_t envelope;
    const int rc = recv_envelope (socket_, flags_, envelope);
    if (rc != 0)
        return rc;

    if (source_rid_out_) {
        if (zlink::detail::routing_id_empty (envelope.source_rid))
            std::memset (source_rid_out_, 0, sizeof (*source_rid_out_));
        else
            *source_rid_out_ = *zlink::detail::routing_id_native (envelope.source_rid);
    }
    parts_ = std::move (envelope.parts);
    return 0;
}

inline int recv_single_part (void *socket_,
                             zlink_routing_id_t *source_rid_out_,
                             recv_flags_t flags_,
                             message_t &part_)
{
    if (socket_uses_router_recv (socket_)) {
        const zlink_routing_id_t *source_rid = NULL;
        const zlink_routing_id_t *source_spot_rid = NULL;
        uint64_t request_seq = 0;
        std::vector<message_t> parts;
        const int rc = recv_router_parts (
          socket_, flags_, &source_rid, &source_spot_rid, &request_seq, parts);
        if (rc != ZLINK_RECV_OK)
            return rc;

        if (source_rid_out_) {
            if (source_rid && source_rid->size > 0)
                *source_rid_out_ = *source_rid;
            else
                std::memset (source_rid_out_, 0, sizeof (*source_rid_out_));
        }

        if ((source_spot_rid && source_spot_rid->size > 0) || request_seq != 0
            || parts.size () != 1) {
            errno = parts.size () == 1 ? EPROTO : EMSGSIZE;
            return -1;
        }

        part_ = std::move (parts[0]);
        return 0;
    }

    const zlink_routing_id_t *source_rid = NULL;
    zlink_msg_t part_native;
    if (zlink_msg_init (&part_native) != 0)
        return -1;

    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    const int rc = zlink_recv_part (
      socket_, &source_rid, &part_native, &has_more,
      static_cast<zlink_recv_flags_t> (flags_));
    if (rc != 0) {
        (void) zlink_msg_close (&part_native);
        return rc;
    }

    if (has_more != ZLINK_PART_FINAL) {
        (void) zlink_msg_close (&part_native);
        errno = EMSGSIZE;
        return -1;
    }

    if (source_rid_out_) {
        if (source_rid && source_rid->size > 0)
            *source_rid_out_ = *source_rid;
        else
            std::memset (source_rid_out_, 0, sizeof (*source_rid_out_));
    }

    if (zlink_msg_move (detail::native_handle (part_), &part_native) != 0) {
        (void) zlink_msg_close (&part_native);
        return -1;
    }
    return 0;
}

inline send_result_t to_send_result (int result_) noexcept
{
    switch (result_) {
    case ZLINK_SUBMIT_OK:
        return send_result_t::sent;
    case ZLINK_SUBMIT_BACKPRESSURED:
        return send_result_t::backpressured;
    case ZLINK_SUBMIT_NOT_CONNECTED:
        return send_result_t::not_ready;
    default:
        return send_result_t::sent;
    }
}

inline bool classify_nonblocking_send_errno (int err_,
                                             send_result_t &result_) noexcept
{
    switch (err_) {
    case EAGAIN:
        result_ = send_result_t::backpressured;
        return true;
    case ENOTCONN:
    case EHOSTUNREACH:
        result_ = send_result_t::not_ready;
        return true;
    default:
        return false;
    }
}

} // namespace detail

} // namespace zlink

#endif
