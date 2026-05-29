/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_NATIVE_PARTS_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_NATIVE_PARTS_HPP_INCLUDED

#include "native_message_parts.hpp"
#include "native_options.hpp"
#include "../Core/routing_id_access.hpp"

#include <cstring>
#include <functional>
#include <memory>
#include <vector>

namespace zlink
{
namespace detail
{

class request_progress_poller_t
{
  public:
    explicit request_progress_poller_t (void *handle_) noexcept
        : poller (zlink_poller_new ()), registered (false)
    {
        if (!poller || !handle_)
            return;
        registered =
          zlink_poller_add (poller, handle_, nullptr, ZLINK_POLLCOMPLETION)
          == ZLINK_CONFIG_OK;
    }

    ~request_progress_poller_t ()
    {
        if (poller)
            (void) zlink_poller_destroy (&poller);
    }

    request_progress_poller_t (const request_progress_poller_t &) = delete;
    request_progress_poller_t &
    operator= (const request_progress_poller_t &) = delete;

    void poll_once () noexcept
    {
        if (!registered)
            return;
        zlink_poller_event_t event;
        (void) zlink_poller_wait (poller, &event, 1, 0, nullptr);
    }

  private:
    void *poller;
    bool registered;
};

inline std::function<void()> make_request_progress_callback (void *handle_)
{
    auto progress = std::make_shared<request_progress_poller_t> (handle_);
    return [progress]() { progress->poll_once (); };
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

    *source_node_rid_out_ = nullptr;
    *source_spot_rid_out_ = nullptr;
    *request_seq_out_ = 0;
    parts_.clear ();

    for (;;) {
        zlink_msg_t part;
        if (zlink_msg_init (&part) != 0)
            return recv_result_from_errno (errno);

        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        const zlink_recv_result_t rc = zlink_router_recv_part (
          router_, source_node_rid_out_, source_spot_rid_out_, request_seq_out_,
          &part, &has_more,
          static_cast<zlink_recv_flags_t> (static_cast<int> (flags_)));
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

struct recv_envelope_t
{
    routing_id_t source_rid;
    routing_id_t source_spot_rid;
    bool has_request_seq;
    uint64_t request_seq;
    std::optional<message_t> single_part;
    std::vector<message_t> parts;

    void reset () noexcept
    {
        source_rid = zlink::detail::unchecked_empty_routing_id ();
        source_spot_rid = zlink::detail::unchecked_empty_routing_id ();
        has_request_seq = false;
        request_seq = 0;
        single_part.reset ();
        parts.clear ();
    }

    recv_envelope_t () :
        source_rid (zlink::detail::unchecked_empty_routing_id ()),
        source_spot_rid (zlink::detail::unchecked_empty_routing_id ()),
        has_request_seq (false),
        request_seq (0),
        single_part (),
        parts ()
    {
    }
};

inline int drain_router_remaining_parts (void *router_, recv_flags_t flags_)
{
    zlink_part_flag_t has_more = ZLINK_PART_MORE;
    while (has_more != ZLINK_PART_FINAL) {
        zlink_msg_t ignored_part;
        if (zlink_msg_init (&ignored_part) != 0)
            return recv_result_from_errno (errno);

        const zlink_routing_id_t *ignored_source_rid = nullptr;
        const zlink_routing_id_t *ignored_spot_rid = nullptr;
        uint64_t ignored_request_seq = 0;
        const int rc = zlink_router_recv_part (
          router_, &ignored_source_rid, &ignored_spot_rid,
          &ignored_request_seq, &ignored_part, &has_more,
          static_cast<zlink_recv_flags_t> (static_cast<int> (flags_)));
        (void) zlink_msg_close (&ignored_part);
        if (rc != ZLINK_RECV_OK)
            return rc;
    }
    return ZLINK_RECV_OK;
}

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

inline int
recv_envelope (void *socket_, recv_flags_t flags_, recv_envelope_t &envelope_)
{
    envelope_.reset ();

    if (socket_uses_router_recv (socket_)) {
        const zlink_routing_id_t *source_rid = nullptr;
        const zlink_routing_id_t *source_spot_rid = nullptr;
        uint64_t request_seq = 0;
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        message_t first_msg;
        if (!first_msg.valid ()) {
            errno = EFAULT;
            return -1;
        }

        const int first_rc = zlink_router_recv_part (
          socket_, &source_rid, &source_spot_rid, &request_seq,
          detail::native_handle (first_msg), &has_more,
          static_cast<zlink_recv_flags_t> (static_cast<int> (flags_)));
        if (first_rc != ZLINK_RECV_OK)
            return -1;

        if (source_rid && source_rid->size > 0)
            envelope_.source_rid =
              zlink::detail::native_routing_id (*source_rid);
        if (source_spot_rid && source_spot_rid->size > 0)
            envelope_.source_spot_rid =
              zlink::detail::native_routing_id (*source_spot_rid);
        if (request_seq != 0) {
            envelope_.has_request_seq = true;
            envelope_.request_seq = request_seq;
        }

        if (has_more == ZLINK_PART_FINAL) {
            envelope_.single_part.emplace (std::move (first_msg));
            return 0;
        }

        envelope_.parts.reserve (2);
        envelope_.parts.emplace_back (std::move (first_msg));
        for (;;) {
            message_t next_msg;
            if (!next_msg.valid ()) {
                errno = EFAULT;
                return -1;
            }

            has_more = ZLINK_PART_FINAL;
            const int rc = zlink_router_recv_part (
              socket_, &source_rid, &source_spot_rid, &request_seq,
              detail::native_handle (next_msg), &has_more,
              static_cast<zlink_recv_flags_t> (static_cast<int> (flags_)));
            if (rc != ZLINK_RECV_OK)
                return -1;

            envelope_.parts.emplace_back (std::move (next_msg));
            if (has_more == ZLINK_PART_FINAL)
                return 0;
        }
    } else {
        const zlink_routing_id_t *source_rid = nullptr;
        zlink_msg_t first_part;
        if (zlink_msg_init (&first_part) != 0)
            return -1;

        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        const int first_rc = zlink_recv_part (
          socket_, &source_rid, &first_part, &has_more,
          static_cast<zlink_recv_flags_t> (static_cast<int> (flags_)));
        if (first_rc != ZLINK_RECV_OK) {
            (void) zlink_msg_close (&first_part);
            return first_rc;
        }

        message_t first_msg;
        if (zlink_msg_move (detail::native_handle (first_msg), &first_part)
            != 0) {
            (void) zlink_msg_close (&first_part);
            return -1;
        }

        if (has_more == ZLINK_PART_FINAL) {
            envelope_.single_part.emplace (std::move (first_msg));
            if (source_rid && source_rid->size > 0)
                envelope_.source_rid =
                  zlink::detail::native_routing_id (*source_rid);
            return 0;
        }

        envelope_.parts.reserve (2);
        envelope_.parts.emplace_back (std::move (first_msg));
        while (has_more != ZLINK_PART_FINAL) {
            zlink_msg_t next_part;
            if (zlink_msg_init (&next_part) != 0)
                return -1;

            const int rc = zlink_recv_part (
              socket_, &source_rid, &next_part, &has_more,
              static_cast<zlink_recv_flags_t> (static_cast<int> (flags_)));
            if (rc != ZLINK_RECV_OK) {
                (void) zlink_msg_close (&next_part);
                return rc;
            }

            message_t next_msg;
            if (zlink_msg_move (detail::native_handle (next_msg), &next_part)
                != 0) {
                (void) zlink_msg_close (&next_part);
                return -1;
            }
            envelope_.parts.emplace_back (std::move (next_msg));
        }

        if (source_rid && source_rid->size > 0)
            envelope_.source_rid =
              zlink::detail::native_routing_id (*source_rid);
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
            *source_rid_out_ =
              *zlink::detail::routing_id_native (envelope.source_rid);
    }
    if (envelope.single_part.has_value ()) {
        parts_.clear ();
        parts_.push_back (std::move (*envelope.single_part));
    } else {
        parts_ = std::move (envelope.parts);
    }
    return 0;
}

inline int recv_single_part (void *socket_,
                             zlink_routing_id_t *source_rid_out_,
                             recv_flags_t flags_,
                             message_t &part_)
{
    if (socket_uses_router_recv (socket_)) {
        const zlink_routing_id_t *source_rid = nullptr;
        const zlink_routing_id_t *source_spot_rid = nullptr;
        uint64_t request_seq = 0;
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        message_t received_part;
        if (!received_part.valid ()) {
            errno = EFAULT;
            return -1;
        }

        const int rc = zlink_router_recv_part (
          socket_, &source_rid, &source_spot_rid, &request_seq,
          detail::native_handle (received_part), &has_more,
          static_cast<zlink_recv_flags_t> (static_cast<int> (flags_)));
        if (rc != ZLINK_RECV_OK)
            return rc;

        if (source_rid_out_) {
            if (source_rid && source_rid->size > 0)
                *source_rid_out_ = *source_rid;
            else
                std::memset (source_rid_out_, 0, sizeof (*source_rid_out_));
        }

        if (has_more != ZLINK_PART_FINAL) {
            const int drain_rc = drain_router_remaining_parts (socket_, flags_);
            if (drain_rc != ZLINK_RECV_OK)
                return drain_rc;
            errno = EMSGSIZE;
            return -1;
        }

        if ((source_spot_rid && source_spot_rid->size > 0)
            || request_seq != 0) {
            errno = EPROTO;
            return -1;
        }

        part_ = std::move (received_part);
        return 0;
    }

    const zlink_routing_id_t *source_rid = nullptr;
    zlink_msg_t part_native;
    if (zlink_msg_init (&part_native) != 0)
        return -1;

    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    const int rc = zlink_recv_part (
      socket_, &source_rid, &part_native, &has_more,
      static_cast<zlink_recv_flags_t> (static_cast<int> (flags_)));
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

template <typename NativeSend>
inline int send_single_no_wait_result (send_result_t &result_,
                                       message_t &part_,
                                       NativeSend send_)
{
    if (!part_.valid ()) {
        errno = EINVAL;
        return -1;
    }

    const int rc = send_ (detail::native_handle (part_), ZLINK_PART_FINAL);
    if (rc == 0) {
        detail::mark_sent (part_);
        result_ = send_result_t::sent;
        return 0;
    }

    const int err = errno;
    if (detail::classify_nonblocking_send_errno (err, result_))
        return 0;
    errno = err;
    return -1;
}

template <typename NativeSend>
inline int send_parts_no_wait_result (send_result_t &result_,
                                      std::vector<message_t> &parts_,
                                      NativeSend send_)
{
    std::vector<zlink_msg_t> native_parts;
    if (detail::move_parts_to_native (parts_, native_parts) != 0)
        return -1;

    size_t failed_index = 0;
    const int rc = detail::submit_native_parts (
      native_parts, failed_index,
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
          return send_ (part_out_, part_flag_);
      });
    if (rc == 0) {
        result_ = send_result_t::sent;
        return 0;
    }

    const int err = errno;
    if (detail::classify_nonblocking_send_errno (err, result_)) {
        if (result_ != send_result_t::sent)
            detail::restore_parts_from_native (parts_, native_parts,
                                               failed_index);
        return 0;
    }

    detail::restore_parts_from_native (parts_, native_parts, failed_index);
    errno = err;
    return -1;
}

inline std::function<bool (std::vector<message_t> &, send_flags_t)>
make_routed_send_fn (void *socket_, const routing_id_t &target_rid_)
{
    return [socket_, target_rid_] (std::vector<message_t> &send_parts_,
                                   send_flags_t flags_) {
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (send_parts_, native) != 0)
            throw last_error ();

        size_t failed_index = 0;
        const submit_result_t result =
          static_cast<submit_result_t> (detail::submit_native_parts (
            native, failed_index,
            [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
                return zlink_send_part_rid (
                  socket_, zlink::detail::routing_id_native (target_rid_),
                  part_out_,
                  static_cast<zlink_send_flags_t> (static_cast<int> (flags_)),
                  part_flag_);
            }));
        if (result == submit_result_t::ok)
            return true;

        detail::restore_parts_from_native (send_parts_, native, failed_index);
        if (flags_ == send_flags_t::dontwait
            && result == submit_result_t::backpressured)
            return false;
        throw submit_error_t (result, zlink_errno ());
    };
}

} // namespace detail
} // namespace zlink

#endif
