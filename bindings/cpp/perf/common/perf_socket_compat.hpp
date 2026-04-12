#ifndef ZLINK_CPP_PERF_SOCKET_COMPAT_HPP
#define ZLINK_CPP_PERF_SOCKET_COMPAT_HPP

#include <zlink.hpp>

#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

namespace zlink
{

class socket_t : public base_socket_t
{
  public:
    socket_t () noexcept : base_socket_t () {}

    socket_t (context_t &ctx_, socket_type type_) : base_socket_t (ctx_, type_) {}

    static socket_t adopt (void *socket_)
    {
        socket_t socket;
        socket.reset_handle (socket_, true);
        return socket;
    }

    static socket_t wrap (void *socket_)
    {
        socket_t socket;
        socket.reset_handle (socket_, false);
        return socket;
    }

    using socket_handle_t::close;
    using socket_handle_t::handle;
    using socket_handle_t::valid;

    using base_socket_t::bind;
    using base_socket_t::connect;
    using base_socket_t::disconnect;
    using base_socket_t::on_receive;
    using base_socket_t::on_send_ready;
    using base_socket_t::on_subscribe;
    using base_socket_t::publish;
    using base_socket_t::receive;
    using base_socket_t::send;
    using base_socket_t::set_subscription;
    using base_socket_t::subscribe;
    using base_socket_t::subscription_at;
    using base_socket_t::subscription_event;
    using base_socket_t::unbind;
    using base_socket_t::unset_subscription;

    template<typename T>
    int set_option (socket_option_key_t<T> key_, const T &value_)
    {
        return base_socket_t::set_option (key_, value_);
    }

    int set_option (socket_option_key_t<std::string> key_,
                    const std::string &value_)
    {
        return base_socket_t::set_option (key_, value_);
    }

    template<typename T>
    int set (socket_option_key_t<T> key_, const T &value_)
    {
        return set_option (key_, value_);
    }

    template<typename T>
    int set (router_option_key_t<T> key_, const T &value_)
    {
        return base_socket_t::set_router_option (key_, value_);
    }

    template<typename T>
    int set (dealer_option_key_t<T> key_, const T &value_)
    {
        return base_socket_t::set_dealer_option (key_, value_);
    }

    template<typename T>
    int set (pub_option_key_t<T> key_, const T &value_)
    {
        return base_socket_t::set_pub_option (key_, value_);
    }

    template<typename T>
    int set (sub_option_key_t<T> key_, const T &value_)
    {
        return base_socket_t::set_sub_option (key_, value_);
    }

    template<typename T>
    int set (stream_option_key_t<T> key_, const T &value_)
    {
        return base_socket_t::set_stream_option (key_, value_);
    }

    template<typename T>
    int get_option (socket_option_key_t<T> key_, T *value_) const
    {
        return base_socket_t::get_option (key_, value_);
    }

    int get_option (socket_option_key_t<std::string> key_,
                    std::string &value_) const
    {
        return base_socket_t::get_option (key_, value_);
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
        return base_socket_t::get_router_option (key_, &value_);
    }

    int get (router_option_key_t<std::string> key_, std::string &value_) const
    {
        return base_socket_t::get_router_option (key_, value_);
    }

    template<typename T>
    int get (pub_option_key_t<T> key_, T &value_) const
    {
        return base_socket_t::get_pub_option (key_, &value_);
    }

    int get (pub_option_key_t<std::string> key_, std::string &value_) const
    {
        return base_socket_t::get_pub_option (key_, value_);
    }

    template<typename T>
    int get (sub_option_key_t<T> key_, T &value_) const
    {
        return base_socket_t::get_sub_option (key_, &value_);
    }

    template<typename T>
    int get (stream_option_key_t<T> key_, T &value_) const
    {
        return base_socket_t::get_stream_option (key_, &value_);
    }

    int set_routing_id (const void *data_, size_t size_)
    {
        return base_socket_t::set_routing_id_raw (data_, size_);
    }

    int set_routing_id (const std::string &routing_id_)
    {
        return base_socket_t::set_routing_id_raw (routing_id_);
    }

    int get_routing_id (routing_id_t &routing_id_) const
    {
        return base_socket_t::get_routing_id_raw (routing_id_);
    }

    int get_routing_id (std::string &routing_id_) const
    {
        return base_socket_t::get_routing_id_raw (routing_id_);
    }

    int send (const void *data_,
              size_t size_,
              send_flag flags_ = send_flag::none)
    {
        message_t part = message_t::from_bytes (data_, size_);
        if (!part.valid ())
            return -1;
        const int rc = base_socket_t::send (part, flags_);
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
        routing_id_t rid;
        if (routing_id_from (routing_id_, &rid) != 0)
            return -1;
        message_t part = message_t::from_bytes (data_, size_);
        if (!part.valid ())
            return -1;
        const int rc = base_socket_t::send (rid, part, flags_);
        return rc == 0 ? static_cast<int> (size_) : -1;
    }

    int recv (message_t &part_, recv_flag flags_ = recv_flag::none)
    {
        return detail::recv_single_part (handle (), NULL, flags_, part_);
    }

    int recv (std::vector<message_t> &parts_, recv_flag flags_ = recv_flag::none)
    {
        return detail::recv_parts (handle (), NULL, flags_, parts_);
    }

    int recv (routing_id_t &source_rid_out_,
              message_t &part_,
              recv_flag flags_ = recv_flag::none)
    {
        return detail::recv_single_part (
          handle (), routing_id_native (source_rid_out_), flags_, part_);
    }

    int try_send (send_result_t &result_,
                  const routing_id_t &target_rid_,
                  message_t &part_)
    {
        return base_socket_t::try_send (result_, target_rid_, part_);
    }

    int try_send (send_result_t &result_,
                  const routing_id_t &target_rid_,
                  std::vector<message_t> &parts_)
    {
        return base_socket_t::try_send (result_, target_rid_, parts_);
    }

    int recv (zlink_routing_id_t &source_rid_out_,
              message_t &part_,
              recv_flag flags_ = recv_flag::none)
    {
        std::memset (&source_rid_out_, 0, sizeof (source_rid_out_));
        return detail::recv_single_part (
          handle (), &source_rid_out_, flags_, part_);
    }

    int recv (void *data_, size_t size_, recv_flag flags_ = recv_flag::none)
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
};

} // namespace zlink

namespace perf {

// Lightweight RAII wrapper that creates a zlink::socket_t and
// closes it on destruction.  Shared by single and multi benchmarks.
class socket_guard_t
{
  public:
    socket_guard_t () : _sock () {}
    socket_guard_t (zlink::context_t &ctx_, zlink::socket_type type_)
        : _sock (ctx_, type_)
    {
    }

    zlink::socket_t &sock () { return _sock; }
    bool valid () const { return _sock.handle () != NULL; }

  private:
    zlink::socket_t _sock;
};

} // namespace perf

#endif
