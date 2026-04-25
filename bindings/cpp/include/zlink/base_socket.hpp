/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_BASE_SOCKET_HPP_INCLUDED
#define ZLINK_CPP_BASE_SOCKET_HPP_INCLUDED

#include "context.hpp"
#include "message.hpp"
#include "monitor.hpp"
#include "service_monitor.hpp"
#include "socket_handle.hpp"
#include "types.hpp"

#include <cerrno>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace zlink
{
class socket_base_t;
}

struct socket_handle_t
{
    zlink::socket_base_t *socket;
};

#if defined(__cplusplus)
extern "C" {
#endif

int zlink_socket_subscribe_recv_internal (void *s_,
                                          zlink_routing_id_t *source_rid_out_,
                                          zlink_msg_t **parts_out_,
                                          size_t *part_count_out_,
                                          char *topic_out_,
                                          size_t *topic_len_out_,
                                          zlink_send_flags_t flags_);

int zlink_socket_recv_internal (void *socket_,
                                zlink_routing_id_t *source_rid_out_,
                                zlink_msg_t **parts_out_,
                                size_t *part_count_out_,
                                zlink_send_flags_t flags_);

int zlink_socket_request_progress_internal (void *socket_);

int zlink_spot_request_progress_internal (void *spot_);

#if defined(__cplusplus)
}
#endif

namespace zlink
{
namespace socket_reqrep_internal
{
int recv_router_message_direct ( ::socket_handle_t handle_,
                                 const zlink_routing_id_t **source_node_rid_out_,
                                 const zlink_routing_id_t **source_spot_rid_out_,
                                 uint64_t *request_seq_out_,
                                 zlink_msg_t **parts_out_,
                                 size_t *part_count_out_,
                                 int flags_);
}
}

namespace zlink
{

namespace detail
{

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

inline bool is_common_string_option (socket_option option_) noexcept
{
    switch (option_) {
    case socket_option::last_endpoint:
    case socket_option::bindtodevice:
    case socket_option::tls_cert:
    case socket_option::tls_key:
    case socket_option::tls_ca:
    case socket_option::tls_hostname:
    case socket_option::tls_password:
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
        parts_[moved].move_to (&native_[moved]);
        if (parts_[moved].valid ())
            break;
    }

    if (moved == parts_.size ())
        return 0;

    for (size_t i = 0; i < moved; ++i) {
        parts_[i].init ();
        if (parts_[i].valid ())
            (void) zlink_msg_move (parts_[i].handle (), &native_[i]);
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
            (void) zlink_msg_move (parts_[i].handle (), &native_[i]);
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
        if (zlink_msg_move (parts_[i].handle (), &parts_native_[i]) != 0) {
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
        if (zlink_msg_move (parts_[i].handle (), &parts_native_[i]) != 0) {
            parts_.clear ();
            close_native_parts (parts_native_, i);
            parts_native_.clear ();
            return -1;
        }
    }
    parts_native_.clear ();
    return 0;
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

    recv_envelope_t () : source_rid (), source_spot_rid (), has_request_seq (false),
                         request_seq (0), parts ()
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
    envelope_ = recv_envelope_t ();

    if (socket_uses_router_recv (socket_)) {
        const zlink_routing_id_t *source_rid = NULL;
        const zlink_routing_id_t *source_spot_rid = NULL;
        uint64_t request_seq = 0;
        zlink_msg_t *parts_native = NULL;
        size_t part_count = 0;
        const ::socket_handle_t handle = {
          static_cast<socket_base_t *> (socket_)};
        const int rc = socket_reqrep_internal::recv_router_message_direct (
          handle, &source_rid, &source_spot_rid, &request_seq, &parts_native,
          &part_count, static_cast<int> (flags_));
        if (recv_result_from_rc (rc) != ZLINK_RECV_OK)
            return recv_result_from_rc (rc);
        if (assign_parts_from_native (parts_native, part_count, envelope_.parts) != 0)
            return rc;

        if (source_rid && source_rid->size > 0)
            envelope_.source_rid = routing_id_t (*source_rid);
        if (source_spot_rid && source_spot_rid->size > 0)
            envelope_.source_spot_rid = routing_id_t (*source_spot_rid);
        if (request_seq != 0) {
            envelope_.has_request_seq = true;
            envelope_.request_seq = request_seq;
        }
    } else {
        if (socket_uses_stream_recv (socket_)) {
            zlink_routing_id_t source_rid;
            std::memset (&source_rid, 0, sizeof (source_rid));
            zlink_msg_t *parts_native = NULL;
            size_t part_count = 0;
            const int rc = zlink_socket_recv_internal (
              socket_, &source_rid, &parts_native, &part_count,
              static_cast<zlink_send_flags_t> (flags_));
            if (recv_result_from_rc (rc) != ZLINK_RECV_OK)
                return recv_result_from_rc (rc);
            if (assign_parts_from_native (parts_native, part_count, envelope_.parts)
                != 0)
                return rc;
            if (source_rid.size > 0)
                envelope_.source_rid = routing_id_t (source_rid);
        } else {
            const zlink_routing_id_t *source_rid = NULL;
            const int rc = collect_parts_from_recv (
              [&] (zlink_msg_t *part_out_, zlink_part_flag_t *has_more_out_, bool) {
                  return zlink_recv_part (
                    socket_, &source_rid, part_out_, has_more_out_,
                    static_cast<zlink_recv_flags_t> (flags_));
              },
              envelope_.parts);
            if (rc != 0)
                return rc;

            if (source_rid && source_rid->size > 0)
                envelope_.source_rid = routing_id_t (*source_rid);
        }
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
        if (envelope.source_rid.empty ())
            std::memset (source_rid_out_, 0, sizeof (*source_rid_out_));
        else
            *source_rid_out_ = envelope.source_rid.native ();
    }
    parts_ = std::move (envelope.parts);
    return 0;
}

inline int recv_single_part (void *socket_,
                             zlink_routing_id_t *source_rid_out_,
                             recv_flags_t flags_,
                             message_t &part_)
{
    recv_envelope_t envelope;
    const int rc = recv_envelope (socket_, flags_, envelope);
    if (rc != 0)
        return rc;

    if (source_rid_out_) {
        if (envelope.source_rid.empty ())
            std::memset (source_rid_out_, 0, sizeof (*source_rid_out_));
        else
            *source_rid_out_ = envelope.source_rid.native ();
    }

    if (envelope.parts.size () != 1) {
        errno = EMSGSIZE;
        return -1;
    }

    part_ = std::move (envelope.parts[0]);
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

class base_socket_t : public socket_handle_t
{
  public:
    bool valid () const noexcept { return socket_handle_t::valid (); }

    void bind (const std::string &endpoint_)
    {
        const int rc = zlink_bind (handle (), endpoint_.c_str ());
        if (rc != 0)
            throw bind_error_t (
              detail::bind_result_from_errno (zlink_errno ()), zlink_errno ());
    }

    void connect (const std::string &endpoint_)
    {
        const int rc = zlink_connect (handle (), endpoint_.c_str ());
        if (rc != 0)
            throw connect_error_t (
              detail::connect_result_from_errno (zlink_errno ()), zlink_errno ());
    }

    void unbind (const std::string &endpoint_)
    {
        const int rc = zlink_unbind (handle (), endpoint_.c_str ());
        if (rc != 0)
            throw connect_error_t (
              detail::connect_result_from_errno (zlink_errno ()), zlink_errno ());
    }

    void disconnect (const std::string &endpoint_)
    {
        const int rc = zlink_disconnect (handle (), endpoint_.c_str ());
        if (rc != 0)
            throw connect_error_t (
              detail::connect_result_from_errno (zlink_errno ()), zlink_errno ());
    }

    void disconnect_rid (const routing_id_t &peer_rid_)
    {
        const zlink_routing_id_t native = peer_rid_.native ();
        const int rc = zlink_disconnect_rid (handle (), &native);
        if (rc != 0)
            throw connect_error_t (
              static_cast<connect_result_t> (rc), zlink_errno ());
    }

    monitor_handle_t
    monitor_handle (monitor_event events_ = monitor_event::all) const
    {
        return monitor_handle_t::open (
          *this, events_);
    }

    common_socket_options_t options () { return common_socket_options_t (handle ()); }

    void set_tls_server (const std::string &cert_,
                         const std::string &key_,
                         bool require_client_cert_ = false)
    {
        const int rc = zlink_set_tls_server (
          handle (), cert_.c_str (), key_.c_str (),
          require_client_cert_ ? 1 : 0);
        if (rc != 0)
            throw config_error_t (
              detail::config_result_from_errno (zlink_errno ()), zlink_errno ());
    }

    void set_tls_client (const std::string &ca_cert_,
                         const std::string &hostname_,
                         bool trust_system_ = false)
    {
        const char *ca = ca_cert_.empty () ? NULL : ca_cert_.c_str ();
        const char *hostname =
          hostname_.empty () ? NULL : hostname_.c_str ();
        const int rc = zlink_set_tls_client (
          handle (), ca, hostname, trust_system_ ? 1 : 0);
        if (rc != 0)
            throw config_error_t (
              detail::config_result_from_errno (zlink_errno ()), zlink_errno ());
    }

  protected:
    template<typename DiscoveryT>
    ZLINK_CPP_NODISCARD int attach_discovery (DiscoveryT &discovery_)
    {
        return zlink_socket_attach_discovery (handle (), discovery_.handle ());
    }

    base_socket_t () noexcept {}

    base_socket_t (context_t &ctx_, socket_type type_)
        : socket_handle_t (
            zlink_socket (ctx_.handle (),
                          static_cast<zlink_socket_type_t> (type_)),
            true)
    {
    }

    ZLINK_CPP_NODISCARD int send (message_t &part_,
                                  send_flags_t flags_ = send_flags_t::none)
    {
        if (!part_.valid ()) {
            errno = EINVAL;
            return -1;
        }

        zlink_msg_t native_part;
        part_.move_to (&native_part);
        if (part_.valid ())
            return -1;

        const int rc = zlink_send_part (
          handle (), &native_part, static_cast<zlink_send_flags_t> (flags_),
          ZLINK_PART_FINAL);
        if (rc != 0) {
            part_.init ();
            if (part_.valid ())
                (void) zlink_msg_move (part_.handle (), &native_part);
            (void) zlink_msg_close (&native_part);
        }
        return rc;
    }

    ZLINK_CPP_NODISCARD int send (std::vector<message_t> &parts_,
                                  send_flags_t flags_ = send_flags_t::none)
    {
        std::vector<zlink_msg_t> native_parts;
        if (detail::move_parts_to_native (parts_, native_parts) != 0)
            return -1;

        size_t failed_index = 0;
        const int rc = detail::submit_native_parts (
          native_parts, failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
              return zlink_send_part (
                handle (), part_out_, static_cast<zlink_send_flags_t> (flags_),
                part_flag_);
          });
        if (rc != 0)
            detail::restore_parts_from_native (parts_, native_parts, failed_index);
        return rc;
    }

    ZLINK_CPP_NODISCARD int send (const routing_id_t &target_rid_,
                                  message_t &part_,
                                  send_flags_t flags_ = send_flags_t::none)
    {
        if (!part_.valid ()) {
            errno = EINVAL;
            return -1;
        }

        zlink_msg_t native_part;
        part_.move_to (&native_part);
        if (part_.valid ())
            return -1;

        const int rc = zlink_send_part_rid (
          handle (), routing_id_native (target_rid_), &native_part,
          static_cast<zlink_send_flags_t> (flags_), ZLINK_PART_FINAL);
        if (rc != 0) {
            part_.init ();
            if (part_.valid ())
                (void) zlink_msg_move (part_.handle (), &native_part);
            (void) zlink_msg_close (&native_part);
        }
        return rc;
    }

    ZLINK_CPP_NODISCARD int
    send (const routing_id_t &target_rid_,
          std::vector<message_t> &parts_,
          send_flags_t flags_ = send_flags_t::none)
    {
        std::vector<zlink_msg_t> native_parts;
        if (detail::move_parts_to_native (parts_, native_parts) != 0)
            return -1;

        size_t failed_index = 0;
        const int rc = detail::submit_native_parts (
          native_parts, failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
              return zlink_send_part_rid (
                handle (), routing_id_native (target_rid_), part_out_,
                static_cast<zlink_send_flags_t> (flags_), part_flag_);
          });
        if (rc != 0)
            detail::restore_parts_from_native (parts_, native_parts, failed_index);
        return rc;
    }

  protected:
    ZLINK_CPP_NODISCARD int send_no_wait_result (send_result_t &result_,
                                      message_t &part_)
    {
        if (!part_.valid ()) {
            errno = EINVAL;
            return -1;
        }

        zlink_msg_t native_part;
        part_.move_to (&native_part);
        if (part_.valid ())
            return -1;

        const int rc =
          zlink_send_part (handle (), &native_part, ZLINK_DONTWAIT, ZLINK_PART_FINAL);
        if (rc == 0) {
            result_ = send_result_t::sent;
            return 0;
        }

        const int err = errno;
        if (detail::classify_nonblocking_send_errno (err, result_)) {
            if (result_ != send_result_t::sent) {
                part_.init ();
                if (part_.valid ())
                    (void) zlink_msg_move (part_.handle (), &native_part);
                (void) zlink_msg_close (&native_part);
            }
            return 0;
        }

        part_.init ();
        if (part_.valid ())
            (void) zlink_msg_move (part_.handle (), &native_part);
        (void) zlink_msg_close (&native_part);
        errno = err;
        return -1;
    }

    ZLINK_CPP_NODISCARD int
    send_no_wait_result (send_result_t &result_, std::vector<message_t> &parts_)
    {
        std::vector<zlink_msg_t> native_parts;
        if (detail::move_parts_to_native (parts_, native_parts) != 0)
            return -1;

        size_t failed_index = 0;
        const int rc = detail::submit_native_parts (
          native_parts, failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
              return zlink_send_part (
                handle (), part_out_, ZLINK_DONTWAIT, part_flag_);
          });
        if (rc == 0) {
            result_ = send_result_t::sent;
            return 0;
        }

        const int err = errno;
        if (detail::classify_nonblocking_send_errno (err, result_)) {
            if (result_ != send_result_t::sent)
                detail::restore_parts_from_native (parts_, native_parts, failed_index);
            return 0;
        }

        detail::restore_parts_from_native (parts_, native_parts, failed_index);
        errno = err;
        return -1;
    }

    ZLINK_CPP_NODISCARD int
    send_no_wait_result (send_result_t &result_,
              const routing_id_t &target_rid_,
              message_t &part_)
    {
        if (!part_.valid ()) {
            errno = EINVAL;
            return -1;
        }

        zlink_msg_t native_part;
        part_.move_to (&native_part);
        if (part_.valid ())
            return -1;

        const int rc = zlink_send_part_rid (
          handle (), routing_id_native (target_rid_), &native_part,
          ZLINK_DONTWAIT, ZLINK_PART_FINAL);
        if (rc == 0) {
            result_ = send_result_t::sent;
            return 0;
        }

        const int err = errno;
        if (detail::classify_nonblocking_send_errno (err, result_)) {
            if (result_ != send_result_t::sent) {
                part_.init ();
                if (part_.valid ())
                    (void) zlink_msg_move (part_.handle (), &native_part);
                (void) zlink_msg_close (&native_part);
            }
            return 0;
        }

        part_.init ();
        if (part_.valid ())
            (void) zlink_msg_move (part_.handle (), &native_part);
        (void) zlink_msg_close (&native_part);
        errno = err;
        return -1;
    }

    ZLINK_CPP_NODISCARD int
    send_no_wait_result (send_result_t &result_,
              const routing_id_t &target_rid_,
              std::vector<message_t> &parts_)
    {
        std::vector<zlink_msg_t> native_parts;
        if (detail::move_parts_to_native (parts_, native_parts) != 0)
            return -1;

        size_t failed_index = 0;
        const int rc = detail::submit_native_parts (
          native_parts, failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
              return zlink_send_part_rid (
                handle (), routing_id_native (target_rid_), part_out_,
                ZLINK_DONTWAIT, part_flag_);
          });
        if (rc == 0) {
            result_ = send_result_t::sent;
            return 0;
        }

        const int err = errno;
        if (detail::classify_nonblocking_send_errno (err, result_)) {
            if (result_ != send_result_t::sent)
                detail::restore_parts_from_native (parts_, native_parts, failed_index);
            return 0;
        }

        detail::restore_parts_from_native (parts_, native_parts, failed_index);
        errno = err;
        return -1;
    }

    ZLINK_CPP_NODISCARD int
    receive (received_t &received_, recv_flags_t flags_ = recv_flags_t::none)
    {
        detail::recv_envelope_t envelope;
        const int rc = detail::recv_envelope (handle (), flags_, envelope);
        if (rc != 0)
            return rc;

        received_ = received_t (
          envelope.source_rid.empty ()
            ? std::nullopt
            : std::optional<routing_id_t> (envelope.source_rid),
          envelope.source_spot_rid.empty ()
            ? std::nullopt
            : std::optional<routing_id_t> (envelope.source_spot_rid),
          envelope.has_request_seq
            ? std::optional<uint64_t> (envelope.request_seq)
            : std::nullopt,
          std::move (envelope.parts));
        return 0;
    }

    ZLINK_CPP_NODISCARD int publish (const std::string &topic_id_,
                                     message_t &part_,
                                     send_flags_t flags_ = send_flags_t::none)
    {
        validate_no_embedded_null (topic_id_, "topic");
        if (!part_.valid ()) {
            errno = EINVAL;
            return -1;
        }

        zlink_msg_t native_part;
        part_.move_to (&native_part);
        if (part_.valid ())
            return -1;

        const int rc = zlink_publish_part (
          handle (), topic_id_.c_str (), &native_part,
          static_cast<zlink_send_flags_t> (flags_), ZLINK_PART_FINAL);
        if (rc != 0) {
            part_.init ();
            if (part_.valid ())
                (void) zlink_msg_move (part_.handle (), &native_part);
            (void) zlink_msg_close (&native_part);
        }
        return rc;
    }

    ZLINK_CPP_NODISCARD int publish (const std::string &topic_id_,
                                     std::vector<message_t> &parts_,
                                     send_flags_t flags_ = send_flags_t::none)
    {
        validate_no_embedded_null (topic_id_, "topic");
        std::vector<zlink_msg_t> native_parts;
        if (detail::move_parts_to_native (parts_, native_parts) != 0)
            return -1;

        size_t failed_index = 0;
        const int rc = detail::submit_native_parts (
          native_parts, failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
              return zlink_publish_part (
                handle (), topic_id_.c_str (), part_out_,
                static_cast<zlink_send_flags_t> (flags_), part_flag_);
          });
        if (rc != 0)
            detail::restore_parts_from_native (parts_, native_parts, failed_index);
        return rc;
    }

    ZLINK_CPP_NODISCARD int
    publish_no_wait_result (send_result_t &result_,
                 const std::string &topic_id_,
                 message_t &part_)
    {
        validate_no_embedded_null (topic_id_, "topic");
        if (!part_.valid ()) {
            errno = EINVAL;
            return -1;
        }

        zlink_msg_t native_part;
        part_.move_to (&native_part);
        if (part_.valid ())
            return -1;

        const int rc = zlink_publish_part (
          handle (), topic_id_.c_str (), &native_part, ZLINK_DONTWAIT,
          ZLINK_PART_FINAL);
        if (rc == 0) {
            result_ = send_result_t::sent;
            return 0;
        }

        const int err = errno;
        if (detail::classify_nonblocking_send_errno (err, result_)) {
            if (result_ != send_result_t::sent) {
                part_.init ();
                if (part_.valid ())
                    (void) zlink_msg_move (part_.handle (), &native_part);
                (void) zlink_msg_close (&native_part);
            }
            return 0;
        }

        part_.init ();
        if (part_.valid ())
            (void) zlink_msg_move (part_.handle (), &native_part);
        (void) zlink_msg_close (&native_part);
        errno = err;
        return -1;
    }

    ZLINK_CPP_NODISCARD int
    publish_no_wait_result (send_result_t &result_,
                 const std::string &topic_id_,
                 std::vector<message_t> &parts_)
    {
        std::vector<zlink_msg_t> native_parts;
        if (detail::move_parts_to_native (parts_, native_parts) != 0)
            return -1;

        size_t failed_index = 0;
        const int rc = detail::submit_native_parts (
          native_parts, failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
              return zlink_publish_part (
                handle (), topic_id_.c_str (), part_out_, ZLINK_DONTWAIT,
                part_flag_);
          });
        if (rc == 0) {
            result_ = send_result_t::sent;
            return 0;
        }

        const int err = errno;
        if (detail::classify_nonblocking_send_errno (err, result_)) {
            if (result_ != send_result_t::sent)
                detail::restore_parts_from_native (parts_, native_parts, failed_index);
            return 0;
        }

        detail::restore_parts_from_native (parts_, native_parts, failed_index);
        errno = err;
        return -1;
    }

  public:
    ZLINK_CPP_NODISCARD int set_subscription (const std::string &filter_)
    {
        validate_no_embedded_null (filter_, "filter");
        return zlink_set_subscription (handle (), filter_.c_str ());
    }

    ZLINK_CPP_NODISCARD int unset_subscription (const std::string &filter_)
    {
        validate_no_embedded_null (filter_, "filter");
        return zlink_unset_subscription (handle (), filter_.c_str ());
    }

    ZLINK_CPP_NODISCARD int
    subscription_at (size_t index_, std::string &filter_, bool *is_pattern_ = NULL)
    {
        size_t cap = 256;
        const size_t max_cap = 64u * 1024u;
        while (cap <= max_cap) {
            std::vector<char> buffer (cap);
            size_t size = cap;
            int pattern = 0;
            const int rc = zlink_subscription_at (
              handle (), index_, buffer.data (), &size, &pattern);
            if (rc == 0) {
                const size_t bounded = size <= buffer.size () ? size : buffer.size ();
                filter_.assign (buffer.data (), bounded);
                if (is_pattern_)
                    *is_pattern_ = pattern != 0;
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

    ZLINK_CPP_NODISCARD int
    subscribe (topic_message_t &message_, recv_flags_t flags_ = recv_flags_t::none)
    {
        routing_id_t source_rid;
        std::string topic;
        std::vector<message_t> parts;
        const int rc = subscribe (source_rid, topic, parts, flags_);
        if (rc != 0)
            return rc;
        message_ = topic_message_t (
          source_rid.empty () ? std::nullopt
                              : std::optional<routing_id_t> (source_rid),
          std::nullopt,
          std::move (topic), std::move (parts));
        return 0;
    }

    ZLINK_CPP_NODISCARD int
    subscribe (routing_id_t &source_rid_out_,
               std::string &topic_id_out_,
               std::vector<message_t> &parts_out_,
               recv_flags_t flags_ = recv_flags_t::none)
    {
        std::vector<char> topic_buffer (256);
        size_t topic_size = topic_buffer.size ();
        zlink_routing_id_t source_rid;
        std::memset (&source_rid, 0, sizeof (source_rid));
        zlink_msg_t *parts_native = NULL;
        size_t part_count = 0;
        const int rc = detail::recv_result_from_rc (
          zlink_socket_subscribe_recv_internal (
            handle (), &source_rid, &parts_native, &part_count,
            topic_buffer.data (), &topic_size,
            static_cast<zlink_send_flags_t> (flags_)));
        if (rc != ZLINK_RECV_OK)
            return rc;
        if (detail::assign_parts_from_native (parts_native, part_count, parts_out_)
            != 0) {
            return -1;
        }

        if (source_rid.size > 0)
            source_rid_out_ = routing_id_t (source_rid);
        else
            source_rid_out_ = routing_id_t ();

        const size_t bounded_topic =
          topic_size <= topic_buffer.size () ? topic_size : topic_buffer.size ();
        topic_id_out_.assign (topic_buffer.data (), bounded_topic);
        return 0;
    }

    ZLINK_CPP_NODISCARD int
    subscription_event (subscription_event_t &event_,
                        recv_flags_t flags_ = recv_flags_t::none)
    {
        event_.routing_id = std::nullopt;
        event_.service_name = std::nullopt;
        event_.topic.clear ();
        event_.subscribed = false;
        routing_id_t source_rid;
        const int rc = subscription_event (
          source_rid, event_.subscribed, event_.topic, flags_);
        if (rc != 0)
            return rc;
        if (!source_rid.empty ())
            event_.routing_id = source_rid;
        return 0;
    }

    ZLINK_CPP_NODISCARD int
    subscription_event (routing_id_t &source_rid_out_,
                        bool &subscribed_out_,
                        std::string &topic_id_out_,
                        recv_flags_t flags_ = recv_flags_t::none)
    {
        zlink_msg_t part;
        if (zlink_msg_init (&part) != 0)
            return -1;

        const zlink_routing_id_t *source_rid = NULL;
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        const int rc = zlink_recv_part (
          handle (), &source_rid, &part, &has_more,
          static_cast<zlink_recv_flags_t> (flags_));
        if (rc != 0) {
            (void) zlink_msg_close (&part);
            return rc;
        }

        if (source_rid && source_rid->size > 0)
            source_rid_out_ = routing_id_t (*source_rid);
        else
            source_rid_out_ = routing_id_t ();

        const unsigned char *data =
          static_cast<const unsigned char *> (zlink_msg_data (&part));
        const size_t size = zlink_msg_size (&part);
        if (has_more) {
            (void) zlink_msg_close (&part);
            errno = EMSGSIZE;
            return -1;
        }

        subscribed_out_ = size > 0 && data[0] != 0;
        topic_id_out_.assign (
          size > 1 ? reinterpret_cast<const char *> (data + 1) : "",
          size > 0 ? size - 1 : 0);
        (void) zlink_msg_close (&part);
        return 0;
    }

    ZLINK_CPP_NODISCARD int
    on_send_ready (zlink_send_ready_handler_fn handler_,
                   void *userdata_ = NULL)
    {
        return zlink_send_ready_handler (handle (), handler_, userdata_);
    }

  protected:
    ZLINK_CPP_NODISCARD int on_receive (zlink_socket_msg_handler_fn handler_,
                                        void *userdata_ = NULL)
    {
        return zlink_recv_handler (handle (), handler_, userdata_);
    }

    ZLINK_CPP_NODISCARD int
    on_packet (zlink_stream_packet_handler_fn handler_, void *userdata_ = NULL)
    {
        return zlink_stream_packet_handler (handle (), handler_, userdata_);
    }

    ZLINK_CPP_NODISCARD int
    set_option (socket_option option_, const void *value_, size_t size_)
    {
        return zlink_set_option (
          handle (), static_cast<zlink_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD
    typename std::enable_if<!std::is_same<T, std::string>::value, int>::type
    set_option (socket_option option_, const T &value_)
    {
        return set_option (option_, &value_, sizeof (value_));
    }

    ZLINK_CPP_NODISCARD int
    set_option (socket_option option_, const std::string &value_)
    {
        if (!detail::is_common_string_option (option_)) {
            errno = EINVAL;
            return -1;
        }
        return set_option (option_, value_.data (), value_.size ());
    }

    ZLINK_CPP_NODISCARD int
    get_option (socket_option option_, void *value_, size_t *size_) const
    {
        return zlink_get_option (
          const_cast<void *> (handle ()),
          static_cast<zlink_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD
    typename std::enable_if<!std::is_same<T, std::string>::value, int>::type
    get_option (socket_option option_, T *value_) const
    {
        if (!value_) {
            errno = EINVAL;
            return -1;
        }
        size_t size = sizeof (*value_);
        return get_option (option_, value_, &size);
    }

    ZLINK_CPP_NODISCARD int
    get_option (socket_option option_, std::string &value_) const
    {
        return detail::get_string_option (
          [](void *socket_, socket_option option_, void *value_, size_t *size_) {
              return zlink_get_option (
                socket_, static_cast<zlink_option_t> (option_), value_, size_);
          },
          const_cast<void *> (handle ()), option_,
          option_ == socket_option::last_endpoint ? 1024u : 512u, value_);
    }

    ZLINK_CPP_NODISCARD int set_routing_id_raw (const void *data_, size_t size_)
    {
        return zlink_set_routing_id (handle (), data_, size_);
    }

    ZLINK_CPP_NODISCARD int set_routing_id_raw (const std::string &routing_id_)
    {
        return set_routing_id_raw (routing_id_.data (), routing_id_.size ());
    }

    ZLINK_CPP_NODISCARD int
    get_routing_id_raw (routing_id_t &routing_id_) const
    {
        return zlink_get_routing_id (
          const_cast<void *> (handle ()), routing_id_native (routing_id_));
    }

    ZLINK_CPP_NODISCARD int get_routing_id_raw (std::string &routing_id_) const
    {
        routing_id_t native_rid;
        if (get_routing_id_raw (native_rid) != 0)
            return -1;
        routing_id_ = routing_id_to_string (native_rid);
        return 0;
    }

    ZLINK_CPP_NODISCARD int
    set_router_option (router_option option_, const void *value_, size_t size_)
    {
        return zlink_set_router_option (
          handle (), static_cast<zlink_router_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_router_option (router_option option_,
                                               const T &value_)
    {
        return set_router_option (option_, &value_, sizeof (value_));
    }

    ZLINK_CPP_NODISCARD int
    set_router_option (router_option_key_t<std::string> key_,
                       const std::string &value_)
    {
        return set_router_option (key_.option, value_.data (), value_.size ());
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_router_option (router_option_key_t<T> key_,
                                               const T &value_)
    {
        return set_router_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_router_option (router_option option_, void *value_, size_t *size_) const
    {
        return zlink_get_router_option (
          const_cast<void *> (handle ()),
          static_cast<zlink_router_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int
    get_router_option (router_option option_, T *value_) const
    {
        if (!value_) {
            errno = EINVAL;
            return -1;
        }
        size_t size = sizeof (*value_);
        return get_router_option (option_, value_, &size);
    }

    ZLINK_CPP_NODISCARD int
    get_router_option (router_option option_, std::string &value_) const
    {
        return detail::get_string_option (
          zlink_get_router_option, const_cast<void *> (handle ()),
          static_cast<zlink_router_option_t> (option_), 256u, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_router_option (router_option_key_t<std::string> key_,
                       std::string &value_) const
    {
        return get_router_option (key_.option, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int
    get_router_option (router_option_key_t<T> key_, T *value_) const
    {
        return get_router_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    set_dealer_option (dealer_option option_, const void *value_, size_t size_)
    {
        return zlink_set_dealer_option (
          handle (), static_cast<zlink_dealer_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_dealer_option (dealer_option option_,
                                               const T &value_)
    {
        return set_dealer_option (option_, &value_, sizeof (value_));
    }

    ZLINK_CPP_NODISCARD int
    set_dealer_option (dealer_option_key_t<std::string> key_,
                       const std::string &value_)
    {
        return set_dealer_option (key_.option, value_.data (), value_.size ());
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_dealer_option (dealer_option_key_t<T> key_,
                                               const T &value_)
    {
        return set_dealer_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    set_pub_option (pub_option option_, const void *value_, size_t size_)
    {
        return zlink_set_pub_option (
          handle (), static_cast<zlink_pub_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_pub_option (pub_option option_,
                                            const T &value_)
    {
        return set_pub_option (option_, &value_, sizeof (value_));
    }

    ZLINK_CPP_NODISCARD int
    set_pub_option (pub_option_key_t<std::string> key_,
                    const std::string &value_)
    {
        return set_pub_option (key_.option, value_.data (), value_.size ());
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_pub_option (pub_option_key_t<T> key_,
                                            const T &value_)
    {
        return set_pub_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_pub_option (pub_option option_, void *value_, size_t *size_) const
    {
        return zlink_get_pub_option (
          const_cast<void *> (handle ()),
          static_cast<zlink_pub_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int get_pub_option (pub_option option_, T *value_) const
    {
        if (!value_) {
            errno = EINVAL;
            return -1;
        }
        size_t size = sizeof (*value_);
        return get_pub_option (option_, value_, &size);
    }

    ZLINK_CPP_NODISCARD int
    get_pub_option (pub_option option_, std::string &value_) const
    {
        return detail::get_string_option (
          zlink_get_pub_option, const_cast<void *> (handle ()),
          static_cast<zlink_pub_option_t> (option_), 256u, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_pub_option (pub_option_key_t<std::string> key_,
                    std::string &value_) const
    {
        return get_pub_option (key_.option, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int get_pub_option (pub_option_key_t<T> key_,
                                            T *value_) const
    {
        return get_pub_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    set_sub_option (sub_option option_, const void *value_, size_t size_)
    {
        return zlink_set_sub_option (
          handle (), static_cast<zlink_sub_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_sub_option (sub_option option_, const T &value_)
    {
        return set_sub_option (option_, &value_, sizeof (value_));
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_sub_option (sub_option_key_t<T> key_,
                                            const T &value_)
    {
        return set_sub_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_sub_option (sub_option option_, void *value_, size_t *size_) const
    {
        return zlink_get_sub_option (
          const_cast<void *> (handle ()),
          static_cast<zlink_sub_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int get_sub_option (sub_option option_, T *value_) const
    {
        if (!value_) {
            errno = EINVAL;
            return -1;
        }
        size_t size = sizeof (*value_);
        return get_sub_option (option_, value_, &size);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int get_sub_option (sub_option_key_t<T> key_,
                                            T *value_) const
    {
        return get_sub_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    set_stream_option (stream_option option_, const void *value_, size_t size_)
    {
        return zlink_set_stream_option (
          handle (), static_cast<zlink_stream_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_stream_option (stream_option option_,
                                               const T &value_)
    {
        return set_stream_option (option_, &value_, sizeof (value_));
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_stream_option (stream_option_key_t<T> key_,
                                               const T &value_)
    {
        return set_stream_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_stream_option (stream_option option_, void *value_, size_t *size_) const
    {
        return zlink_get_stream_option (
          const_cast<void *> (handle ()),
          static_cast<zlink_stream_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int
    get_stream_option (stream_option option_, T *value_) const
    {
        if (!value_) {
            errno = EINVAL;
            return -1;
        }
        size_t size = sizeof (*value_);
        return get_stream_option (option_, value_, &size);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int
    get_stream_option (stream_option_key_t<T> key_, T *value_) const
    {
        return get_stream_option (key_.option, value_);
    }
};

} // namespace zlink

#endif
