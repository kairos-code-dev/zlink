#ifndef ZLINK_CPP_PERF_SOCKET_COMPAT_HPP
#define ZLINK_CPP_PERF_SOCKET_COMPAT_HPP

#include <zlink.hpp>

#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace zlink
{

namespace perf_detail
{

inline void close_message_array (zlink_msg_t *parts_, size_t part_count_) noexcept
{
    if (!parts_)
        return;
    zlink_multipart_close (parts_, part_count_);
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
        if (parts_[moved].move_to (&native_[moved]) != 0)
            break;
    }

    if (moved == parts_.size ())
        return 0;

    for (size_t i = 0; i < moved; ++i) {
        if (parts_[i].init () == 0)
            (void) zlink_msg_move (parts_[i].handle (), &native_[i]);
        (void) zlink_msg_close (&native_[i]);
    }

    native_.clear ();
    return -1;
}

inline void restore_parts_from_native (std::vector<message_t> &parts_,
                                       std::vector<zlink_msg_t> &native_) noexcept
{
    const size_t count =
      native_.size () < parts_.size () ? native_.size () : parts_.size ();
    for (size_t i = 0; i < count; ++i) {
        if (parts_[i].init () == 0)
            (void) zlink_msg_move (parts_[i].handle (), &native_[i]);
        (void) zlink_msg_close (&native_[i]);
    }
    native_.clear ();
}

inline int assign_parts_from_native (zlink_msg_t *parts_native_,
                                     size_t part_count_,
                                     std::vector<message_t> &parts_)
{
    std::vector<message_t> tmp;
    tmp.resize (part_count_);
    for (size_t i = 0; i < part_count_; ++i) {
        if (zlink_msg_move (tmp[i].handle (), &parts_native_[i]) != 0) {
            close_message_array (parts_native_, part_count_);
            return -1;
        }
    }

    parts_.swap (tmp);
    return 0;
}

inline int recv_parts (void *socket_,
                       zlink_routing_id_t *source_rid_out_,
                       recv_flag flags_,
                       std::vector<message_t> &parts_)
{
    zlink_msg_t *native_parts = NULL;
    size_t native_part_count = 0;
    const int rc = zlink_recv (
      socket_, source_rid_out_, &native_parts, &native_part_count,
      static_cast<zlink_send_flags_t> (flags_));
    if (rc != 0)
        return rc;

    return assign_parts_from_native (native_parts, native_part_count, parts_);
}

inline int recv_single_part (void *socket_,
                             zlink_routing_id_t *source_rid_out_,
                             recv_flag flags_,
                             message_t &part_)
{
    std::vector<message_t> parts;
    if (recv_parts (socket_, source_rid_out_, flags_, parts) != 0)
        return -1;

    if (parts.size () != 1) {
        errno = EMSGSIZE;
        return -1;
    }

    part_ = std::move (parts[0]);
    return 0;
}

} // namespace perf_detail

class socket_t
{
  public:
    socket_t () : _socket (NULL), _own (false) {}

    socket_t (context_t &ctx_, socket_type type_)
        : _socket (
            zlink_socket (ctx_.handle (),
                          static_cast<zlink_socket_type_t> (type_))),
          _own (true)
    {
    }

    ~socket_t () { (void) close (); }

    socket_t (socket_t &&other) noexcept
        : _socket (other._socket), _own (other._own)
    {
        other._socket = NULL;
        other._own = false;
    }

    socket_t &operator= (socket_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        (void) close ();
        _socket = other._socket;
        _own = other._own;
        other._socket = NULL;
        other._own = false;
        return *this;
    }

    socket_t (const socket_t &) = delete;
    socket_t &operator= (const socket_t &) = delete;

    static socket_t adopt (void *socket_)
    {
        socket_t socket;
        socket._socket = socket_;
        socket._own = true;
        return socket;
    }

    static socket_t wrap (void *socket_)
    {
        socket_t socket;
        socket._socket = socket_;
        socket._own = false;
        return socket;
    }

    bool valid () const noexcept { return _socket != NULL; }
    void *handle () noexcept { return _socket; }
    const void *handle () const noexcept { return _socket; }

    int bind (const std::string &endpoint_)
    {
        return zlink_bind (_socket, endpoint_.c_str ());
    }

    int connect (const std::string &endpoint_)
    {
        return zlink_connect (_socket, endpoint_.c_str ());
    }

    int unbind (const std::string &endpoint_)
    {
        return zlink_unbind (_socket, endpoint_.c_str ());
    }

    int disconnect (const std::string &endpoint_)
    {
        return zlink_disconnect (_socket, endpoint_.c_str ());
    }

    int close () noexcept
    {
        if (!_socket) {
            _own = false;
            return 0;
        }

        if (!_own) {
            _socket = NULL;
            return 0;
        }

        void *socket = _socket;
        const int rc = zlink_close (socket);
        if (rc == 0) {
            _socket = NULL;
            _own = false;
        }
        return rc;
    }

    template<typename T>
    int set (socket_option_key_t<T> key_, const T &value_)
    {
        return set_option (key_, value_);
    }

    template<typename T>
    int set (router_option_key_t<T> key_, const T &value_)
    {
        return set_router_option (key_.option, value_);
    }

    template<typename T>
    int set (dealer_option_key_t<T> key_, const T &value_)
    {
        return set_dealer_option (key_.option, value_);
    }

    template<typename T>
    int set (pub_option_key_t<T> key_, const T &value_)
    {
        return set_pub_option (key_.option, value_);
    }

    template<typename T>
    int set (sub_option_key_t<T> key_, const T &value_)
    {
        return set_sub_option (key_.option, value_);
    }

    template<typename T>
    int set (stream_option_key_t<T> key_, const T &value_)
    {
        return set_stream_option (key_.option, value_);
    }

    template<typename T>
    int get (socket_option_key_t<T> key_, T &value_) const
    {
        return get_option (key_, &value_);
    }

    int get (socket_option_key_t<std::string> key_, std::string &value_) const
    {
        return get_option (key_, value_);
    }

    template<typename T>
    int get (router_option_key_t<T> key_, T &value_) const
    {
        return get_router_option (key_.option, &value_);
    }

    int get (router_option_key_t<std::string> key_, std::string &value_) const
    {
        return get_router_option (key_.option, value_);
    }

    template<typename T>
    int get (dealer_option_key_t<T> key_, T &value_) const
    {
        return get_dealer_option (key_.option, &value_);
    }

    template<typename T>
    int get (pub_option_key_t<T> key_, T &value_) const
    {
        return get_pub_option (key_.option, &value_);
    }

    int get (pub_option_key_t<std::string> key_, std::string &value_) const
    {
        return get_pub_option (key_.option, value_);
    }

    template<typename T>
    int get (sub_option_key_t<T> key_, T &value_) const
    {
        return get_sub_option (key_.option, &value_);
    }

    template<typename T>
    int get (stream_option_key_t<T> key_, T &value_) const
    {
        return get_stream_option (key_.option, &value_);
    }

    int send (message_t &part_,
              send_flag flags_ = send_flag::none)
    {
        std::vector<message_t> parts (1);
        parts[0] = std::move (part_);
        const int rc = send (parts, flags_);
        if (rc != 0)
            part_ = std::move (parts[0]);
        return rc;
    }

    int send (std::vector<message_t> &parts_,
              send_flag flags_ = send_flag::none)
    {
        std::vector<zlink_msg_t> native_parts;
        if (perf_detail::move_parts_to_native (parts_, native_parts) != 0)
            return -1;

        const int rc = zlink_send (
          _socket, native_parts.empty () ? NULL : &native_parts[0],
          native_parts.size (), static_cast<zlink_send_flags_t> (flags_));
        if (rc != 0)
            perf_detail::restore_parts_from_native (parts_, native_parts);
        return rc;
    }

    int send (const zlink_routing_id_t &target_rid_,
              message_t &part_,
              send_flag flags_ = send_flag::none)
    {
        std::vector<message_t> parts (1);
        parts[0] = std::move (part_);
        const int rc = send (target_rid_, parts, flags_);
        if (rc != 0)
            part_ = std::move (parts[0]);
        return rc;
    }

    int send (const zlink_routing_id_t &target_rid_,
              std::vector<message_t> &parts_,
              send_flag flags_ = send_flag::none)
    {
        std::vector<zlink_msg_t> native_parts;
        if (perf_detail::move_parts_to_native (parts_, native_parts) != 0)
            return -1;

        const int rc = zlink_send_rid (
          _socket, &target_rid_, native_parts.empty () ? NULL : &native_parts[0],
          native_parts.size (), static_cast<zlink_send_flags_t> (flags_));
        if (rc != 0)
            perf_detail::restore_parts_from_native (parts_, native_parts);
        return rc;
    }

    int send (const void *data_,
              size_t size_,
              send_flag flags_ = send_flag::none)
    {
        message_t part = message_t::from_bytes (data_, size_);
        if (!part.valid ())
            return -1;
        const int rc = send (part, flags_);
        return rc == 0 ? static_cast<int> (size_) : -1;
    }

    int send (const char *data_,
              size_t size_,
              send_flag flags_ = send_flag::none)
    {
        return send (static_cast<const void *> (data_), size_, flags_);
    }

    int send (const std::string &routing_id_,
              const void *data_,
              size_t size_,
              send_flag flags_ = send_flag::none)
    {
        message_t part = message_t::from_bytes (data_, size_);
        if (!part.valid ())
            return -1;
        zlink_routing_id_t rid;
        if (routing_id_from (routing_id_, &rid) != 0)
            return -1;
        const int rc = send (rid, part, flags_);
        return rc == 0 ? static_cast<int> (size_) : -1;
    }

    int recv (message_t &part_,
              recv_flag flags_ = recv_flag::none)
    {
        return perf_detail::recv_single_part (_socket, NULL, flags_, part_);
    }

    int recv (std::vector<message_t> &parts_,
              recv_flag flags_ = recv_flag::none)
    {
        return perf_detail::recv_parts (_socket, NULL, flags_, parts_);
    }

    int recv (zlink_routing_id_t &source_rid_,
              message_t &part_,
              recv_flag flags_ = recv_flag::none)
    {
        std::memset (&source_rid_, 0, sizeof (source_rid_));
        return perf_detail::recv_single_part (_socket, &source_rid_, flags_, part_);
    }

    int recv (void *data_,
              size_t size_,
              recv_flag flags_ = recv_flag::none)
    {
        if (!data_) {
            errno = EINVAL;
            return -1;
        }

        message_t part;
        const int rc = recv (part, flags_);
        if (rc != 0)
            return -1;
        if (part.size () != size_) {
            errno = EMSGSIZE;
            return -1;
        }

        if (size_ > 0)
            std::memcpy (data_, part.data (), size_);
        return static_cast<int> (size_);
    }

    int receive (received_t &received_,
                 recv_flag flags_ = recv_flag::none)
    {
        received_.routing_id = empty_routing_id ();
        return perf_detail::recv_parts (
          _socket, routing_id_native (received_.routing_id), flags_,
          received_.parts);
    }

    int recv_handler (zlink_socket_msg_handler_fn handler_,
                      void *userdata_ = NULL)
    {
        return zlink_recv_handler (_socket, handler_, userdata_);
    }

    int on_receive (zlink_socket_msg_handler_fn handler_,
                    void *userdata_ = NULL)
    {
        return recv_handler (handler_, userdata_);
    }

    int subscribe_handler (zlink_subscribe_handler_fn handler_,
                           void *userdata_ = NULL)
    {
        return zlink_subscribe_handler (_socket, handler_, userdata_);
    }

    int on_subscribe (zlink_subscribe_handler_fn handler_,
                      void *userdata_ = NULL)
    {
        return subscribe_handler (handler_, userdata_);
    }

    int send_ready_handler (zlink_send_ready_handler_fn handler_,
                            void *userdata_ = NULL)
    {
        return zlink_send_ready_handler (_socket, handler_, userdata_);
    }

    int on_send_ready (zlink_send_ready_handler_fn handler_,
                       void *userdata_ = NULL)
    {
        return send_ready_handler (handler_, userdata_);
    }

    int set_routing_id (const void *data_, size_t size_)
    {
        return zlink_set_routing_id (_socket, data_, size_);
    }

    int set_routing_id (const std::string &routing_id_)
    {
        return set_routing_id (routing_id_.data (), routing_id_.size ());
    }

    int get_routing_id (zlink_routing_id_t &routing_id_) const
    {
        std::memset (&routing_id_, 0, sizeof (routing_id_));
        return zlink_get_routing_id (_socket, &routing_id_);
    }

    int get_routing_id (std::string &routing_id_) const
    {
        zlink_routing_id_t native_rid;
        if (get_routing_id (native_rid) != 0)
            return -1;
        routing_id_ = routing_id_to_string (native_rid);
        return 0;
    }

    int set_subscription (const std::string &filter_)
    {
        return zlink_set_subscription (_socket, filter_.c_str ());
    }

    int unset_subscription (const std::string &filter_)
    {
        return zlink_unset_subscription (_socket, filter_.c_str ());
    }

    int set_option (socket_option option_, const void *value_, size_t size_)
    {
        return zlink_set_option (
          _socket, static_cast<zlink_option_t> (option_), value_, size_);
    }

    template<typename T>
    typename std::enable_if<!std::is_same<T, std::string>::value, int>::type
    set_option (socket_option option_, const T &value_)
    {
        return set_option (option_, &value_, sizeof (value_));
    }

    int set_option (socket_option option_, const std::string &value_)
    {
        if (!perf_detail::is_common_string_option (option_)) {
            errno = EINVAL;
            return -1;
        }
        return set_option (option_, value_.data (), value_.size ());
    }

    template<typename T>
    typename std::enable_if<!std::is_same<T, std::string>::value, int>::type
    set_option (socket_option_key_t<T> key_, const T &value_)
    {
        return set_option (key_.option, value_);
    }

    int set_option (socket_option_key_t<std::string> key_,
                    const std::string &value_)
    {
        return set_option (key_.option, value_);
    }

    int get_option (socket_option option_, void *value_, size_t *size_) const
    {
        return zlink_get_option (
          _socket, static_cast<zlink_option_t> (option_), value_, size_);
    }

    template<typename T>
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

    template<typename T>
    typename std::enable_if<!std::is_same<T, std::string>::value, int>::type
    get_option (socket_option_key_t<T> key_, T *value_) const
    {
        return get_option (key_.option, value_);
    }

    int get_option (socket_option option_, std::string &value_) const
    {
        return perf_detail::get_string_option (
          [](void *socket_, socket_option option_, void *value_, size_t *size_) {
              return zlink_get_option (
                socket_, static_cast<zlink_option_t> (option_), value_, size_);
          },
          _socket, option_, option_ == socket_option::last_endpoint ? 1024u : 512u,
          value_);
    }

    int get_option (socket_option_key_t<std::string> key_,
                    std::string &value_) const
    {
        return get_option (key_.option, value_);
    }

    int set_router_option (router_option option_,
                           const void *value_,
                           size_t size_)
    {
        return zlink_set_router_option (
          _socket, static_cast<zlink_router_option_t> (option_), value_, size_);
    }

    template<typename T>
    int set_router_option (router_option option_, const T &value_)
    {
        return set_router_option (option_, &value_, sizeof (value_));
    }

    template<typename T>
    int set_dealer_option (dealer_option option_, const T &value_)
    {
        return zlink_set_dealer_option (
          _socket, static_cast<zlink_dealer_option_t> (option_), &value_,
          sizeof (value_));
    }

    template<typename T>
    int set_pub_option (pub_option option_, const T &value_)
    {
        return zlink_set_pub_option (
          _socket, static_cast<zlink_pub_option_t> (option_), &value_,
          sizeof (value_));
    }

    template<typename T>
    int set_sub_option (sub_option option_, const T &value_)
    {
        return zlink_set_sub_option (
          _socket, static_cast<zlink_sub_option_t> (option_), &value_,
          sizeof (value_));
    }

    template<typename T>
    int set_stream_option (stream_option option_, const T &value_)
    {
        return zlink_set_stream_option (
          _socket, static_cast<zlink_stream_option_t> (option_), &value_,
          sizeof (value_));
    }

    template<typename T>
    int get_router_option (router_option option_, T *value_) const
    {
        size_t size = sizeof (*value_);
        return zlink_get_router_option (
          _socket, static_cast<zlink_router_option_t> (option_), value_, &size);
    }

    int get_router_option (router_option option_, std::string &value_) const
    {
        return perf_detail::get_string_option (
          [](void *socket_, router_option option_, void *value_, size_t *size_) {
              return zlink_get_router_option (
                socket_, static_cast<zlink_router_option_t> (option_), value_,
                size_);
          },
          _socket, option_, 256u, value_);
    }

    template<typename T>
    int get_dealer_option (dealer_option option_, T *value_) const
    {
        size_t size = sizeof (*value_);
        return zlink_get_dealer_option (
          _socket, static_cast<zlink_dealer_option_t> (option_), value_, &size);
    }

    template<typename T>
    int get_pub_option (pub_option option_, T *value_) const
    {
        size_t size = sizeof (*value_);
        return zlink_get_pub_option (
          _socket, static_cast<zlink_pub_option_t> (option_), value_, &size);
    }

    int get_pub_option (pub_option option_, std::string &value_) const
    {
        return perf_detail::get_string_option (
          [](void *socket_, pub_option option_, void *value_, size_t *size_) {
              return zlink_get_pub_option (
                socket_, static_cast<zlink_pub_option_t> (option_), value_, size_);
          },
          _socket, option_, 256u, value_);
    }

    template<typename T>
    int get_sub_option (sub_option option_, T *value_) const
    {
        size_t size = sizeof (*value_);
        return zlink_get_sub_option (
          _socket, static_cast<zlink_sub_option_t> (option_), value_, &size);
    }

    template<typename T>
    int get_stream_option (stream_option option_, T *value_) const
    {
        size_t size = sizeof (*value_);
        return zlink_get_stream_option (
          _socket, static_cast<zlink_stream_option_t> (option_), value_, &size);
    }

  private:
    void *_socket;
    bool _own;
};

} // namespace zlink

#endif
