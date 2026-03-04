/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SERVICES_REGISTRY_HPP_INCLUDED
#define ZLINK_CPP_SERVICES_REGISTRY_HPP_INCLUDED

#include "../context.hpp"
#include "../types.hpp"
#include <type_traits>

namespace zlink
{
namespace service
{

/**
 * @brief Service registry wrapper for endpoint and peer announcements.
 */
class registry_t
{
  public:
    /**
     * @brief Create registry bound to a context.
     * @param ctx_ Context wrapper.
     */
    explicit registry_t (context_t &ctx_)
        : _reg (zlink_registry_new (ctx_.handle ()))
    {
    }

    /**
     * @brief Destroy registry handle.
     */
    ~registry_t () { destroy (); }

    registry_t (registry_t &&other) noexcept : _reg (other._reg)
    {
        other._reg = NULL;
    }

    registry_t &operator= (registry_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        destroy ();
        _reg = other._reg;
        other._reg = NULL;
        return *this;
    }

    registry_t (const registry_t &) = delete;
    registry_t &operator= (const registry_t &) = delete;

    /**
     * @brief Configure registry publish/router endpoints.
     * @param pub_ Registry PUB endpoint.
     * @param router_ Registry ROUTER endpoint.
     * @return 0 on success, -1 on failure.
     */
    int set_endpoints (const std::string &pub_, const std::string &router_)
    {
        return zlink_registry_set_endpoints (
          _reg, pub_.c_str (), router_.c_str ());
    }

    /**
     * @brief Set explicit registry node identifier.
     * @param id_ Node id.
     * @return 0 on success, -1 on failure.
     */
    int set_id (uint32_t id_) { return zlink_registry_set_id (_reg, id_); }

    /**
     * @brief Add static peer registry pub endpoint.
     * @param pub_ Peer PUB endpoint.
     * @return 0 on success, -1 on failure.
     */
    int add_peer (const std::string &pub_)
    {
        return zlink_registry_add_peer (_reg, pub_.c_str ());
    }

    /**
     * @brief Configure heartbeat interval and timeout.
     * @param ivl_ms_ Heartbeat interval in milliseconds.
     * @param timeout_ms_ Failure timeout in milliseconds.
     * @return 0 on success, -1 on failure.
     */
    int set_heartbeat (uint32_t ivl_ms_, uint32_t timeout_ms_)
    {
        return zlink_registry_set_heartbeat (_reg, ivl_ms_, timeout_ms_);
    }

    /**
     * @brief Configure peer broadcast interval.
     * @param ivl_ms_ Interval in milliseconds.
     * @return 0 on success, -1 on failure.
     */
    int set_broadcast_interval (uint32_t ivl_ms_)
    {
        return zlink_registry_set_broadcast_interval (_reg, ivl_ms_);
    }

    /**
     * @brief Set socket option on an internal registry socket role.
     * @param role_ Internal socket role.
     * @param option_ Socket option key.
     * @param value_ Option value buffer.
     * @param len_ Buffer length in bytes.
     * @return 0 on success, -1 on failure.
     */
    int set_sockopt (registry_socket_role role_,
                     socket_option option_,
                     const void *value_,
                     size_t len_)
    {
        return zlink_registry_setsockopt (
          _reg, static_cast<int> (role_), static_cast<int> (option_), value_, len_);
    }

    /**
     * @brief Set typed non-string socket option on internal registry socket.
     * @param role_ Internal socket role.
     * @param key_ Typed socket option key.
     * @param value_ Typed option value.
     * @return 0 on success, -1 on failure.
     */
    template<typename T>
    typename std::enable_if<!std::is_same<T, std::string>::value, int>::type
    set_sockopt (registry_socket_role role_,
                 socket_option_key_t<T> key_,
                 const T &value_)
    {
        return set_sockopt (role_, key_.option, &value_, sizeof (value_));
    }

    /**
     * @brief Set typed string socket option on internal registry socket.
     * @param role_ Internal socket role.
     * @param key_ Typed socket option key.
     * @param value_ Option value bytes.
     * @return 0 on success, -1 on failure.
     */
    int set_sockopt (registry_socket_role role_,
                     socket_option_key_t<std::string> key_,
                     const std::string &value_)
    {
        return set_sockopt (role_, key_.option, value_.data (), value_.size ());
    }

    /**
     * @brief Start registry background processing.
     * @return 0 on success, -1 on failure.
     */
    int start () { return zlink_registry_start (_reg); }

    /**
     * @brief Explicitly destroy registry handle.
     * @return 0 on success, -1 on failure.
     */
    int destroy ()
    {
        if (!_reg)
            return 0;

        void *tmp = _reg;
        _reg = NULL;
        return zlink_registry_destroy (&tmp);
    }

    /**
     * @brief Access raw native registry handle.
     * @return Native handle pointer.
     */
    void *handle () const { return _reg; }

  private:
    void *_reg;
};

} // namespace service
} // namespace zlink

#endif
