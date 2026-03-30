#ifndef ZLINK_CPP_PERF_SOCKET_COMPAT_HPP
#define ZLINK_CPP_PERF_SOCKET_COMPAT_HPP

#include <zlink.hpp>
#include <zlink/socket.hpp>

#include <cstring>

namespace zlink
{

class socket_t : public detail::socket_t
{
  public:
    using detail::socket_t::recv;
    using detail::socket_t::send;

    socket_t () : detail::socket_t () {}

    socket_t (context_t &ctx_, socket_type type_)
        : detail::socket_t (ctx_, type_)
    {
    }

    socket_t (socket_t &&other) noexcept
        : detail::socket_t (std::move (other))
    {
    }

    socket_t &operator= (socket_t &&other) noexcept
    {
        detail::socket_t::operator= (std::move (other));
        return *this;
    }

    static socket_t adopt (void *socket_)
    {
        return socket_t (detail::socket_t::adopt (socket_));
    }

    static socket_t wrap (void *socket_)
    {
        return socket_t (detail::socket_t::wrap (socket_));
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

    int send (const void *data_,
              size_t size_,
              send_flag flags_ = send_flag::none)
    {
        message_t part = message_t::from_bytes (data_, size_);
        if (!part.valid ())
            return -1;
        const int rc = detail::socket_t::send (part, flags_);
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
        const int rc = detail::socket_t::send (rid, part, flags_);
        return rc == 0 ? static_cast<int> (size_) : -1;
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
        const int rc = detail::socket_t::recv (part, flags_);
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

  private:
    explicit socket_t (detail::socket_t &&socket_) noexcept
        : detail::socket_t (std::move (socket_))
    {
    }
};

} // namespace zlink

#endif
