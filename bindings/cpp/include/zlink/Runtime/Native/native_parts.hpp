/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_NATIVE_PARTS_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_NATIVE_PARTS_HPP_INCLUDED

#include "native_message_parts.hpp"
#include "native_options.hpp"

#include <cstring>
#include <iterator>
#include <vector>

namespace zlink
{
namespace detail
{

inline void request_progress_socket (void *socket_) noexcept
{
    void *poller = zlink_poller_new ();
    if (!poller)
        return;
    if (zlink_poller_add (poller, socket_, NULL, ZLINK_POLLCOMPLETION)
        == ZLINK_CONFIG_OK) {
        zlink_poller_event_t event;
        (void) zlink_poller_wait (poller, &event, 1, 0, NULL);
        (void) zlink_poller_remove (poller, socket_);
    }
    (void) zlink_poller_destroy (&poller);
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

} // namespace detail
} // namespace zlink

#endif
