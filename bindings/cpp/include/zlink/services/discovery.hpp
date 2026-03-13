/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SERVICES_DISCOVERY_HPP_INCLUDED
#define ZLINK_CPP_SERVICES_DISCOVERY_HPP_INCLUDED

#include "../context.hpp"
#include "../types.hpp"
#include <cerrno>
#include <type_traits>

namespace zlink
{
namespace service
{

/**
 * @brief Service discovery client wrapper.
 */
class discovery_t
{
  public:
    /**
     * @brief Create a typed discovery client.
     * @param ctx_ Context wrapper.
     * @param service_type_ Service type to discover.
     */
    discovery_t (context_t &ctx_, service_type service_type_)
        : _disc (zlink_discovery_new_typed (
            ctx_.handle (), static_cast<uint16_t> (service_type_))),
          _last_error (0)
    {
        if (!_disc)
            _last_error = errno != 0 ? errno : EFAULT;
    }

    /**
     * @brief Destroy discovery handle.
     */
    ~discovery_t () { (void) destroy (); }

    discovery_t (discovery_t &&other) noexcept
        : _disc (other._disc), _last_error (other._last_error)
    {
        other._disc = NULL;
        other._last_error = 0;
    }

    discovery_t &operator= (discovery_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        (void) destroy ();
        _disc = other._disc;
        _last_error = other._last_error;
        other._disc = NULL;
        other._last_error = 0;
        return *this;
    }

    discovery_t (const discovery_t &) = delete;
    discovery_t &operator= (const discovery_t &) = delete;

    /**
     * @brief Check whether discovery handle was created.
     * @return `true` when handle is valid.
     */
    bool valid () const noexcept { return _disc != NULL; }

    /**
     * @brief Return constructor-time initialization error.
     * @return Error number, or 0 when initialized successfully.
     */
    int last_error () const noexcept { return _last_error; }

    /**
     * @brief Connect to registry PUB endpoint.
     * @param pub_ Registry PUB endpoint.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int connect_registry (const std::string &pub_)
    {
        return zlink_discovery_connect_registry (_disc, pub_.c_str ());
    }

    /**
     * @brief Get number of known receivers for a service.
     * @param service_ Service name.
     * @return Receiver count, or -1 on failure.
     */
    ZLINK_CPP_NODISCARD int receiver_count (const std::string &service_)
    {
        return zlink_discovery_receiver_count (_disc, service_.c_str ());
    }

    /**
     * @brief Check whether a service currently has providers.
     * @param service_ Service name.
     * @return 1 when available, 0 when unavailable, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int service_available (const std::string &service_)
    {
        return zlink_discovery_service_available (_disc, service_.c_str ());
    }

    /**
     * @brief Set socket option on discovery internal socket.
     * @param role_ Internal socket role.
     * @param option_ Socket option key.
     * @param value_ Option value buffer.
     * @param len_ Buffer length in bytes.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    set_sockopt (discovery_socket_role role_,
                 socket_option option_,
                 const void *value_,
                 size_t len_)
    {
        (void) role_;
        (void) option_;
        (void) value_;
        (void) len_;
        errno = ENOTSUP;
        return -1;
    }

    /**
     * @brief Set typed non-string socket option on discovery internal socket.
     * @param role_ Internal socket role.
     * @param key_ Typed socket option key.
     * @param value_ Typed option value.
     * @return 0 on success, -1 on failure.
     */
    template<typename T>
    ZLINK_CPP_NODISCARD
    typename std::enable_if<!std::is_same<T, std::string>::value, int>::type
    set_sockopt (discovery_socket_role role_,
                 socket_option_key_t<T> key_,
                 const T &value_)
    {
        return set_sockopt (role_, key_.option, &value_, sizeof (value_));
    }

    /**
     * @brief Set typed string socket option on discovery internal socket.
     * @param role_ Internal socket role.
     * @param key_ Typed socket option key.
     * @param value_ Option value bytes.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    set_sockopt (discovery_socket_role role_,
                 socket_option_key_t<std::string> key_,
                 const std::string &value_)
    {
        return set_sockopt (role_, key_.option, value_.data (), value_.size ());
    }

    /**
     * @brief Fetch receiver list for a service.
     * @param service_ Service name.
     * @param providers_ Output receiver array.
     * @param count_ In/out capacity and written count.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    get_receivers (const std::string &service_,
                   zlink_receiver_info_t *providers_,
                   size_t *count_)
    {
        return zlink_discovery_get_receivers (
          _disc, service_.c_str (), providers_, count_);
    }

    /**
     * @brief Explicitly destroy discovery handle.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int destroy ()
    {
        if (!_disc)
            return 0;

        void *tmp = _disc;
        _disc = NULL;
        return zlink_discovery_destroy (&tmp);
    }

    /**
     * @brief Access raw native discovery handle.
     * @return Native handle pointer.
     */
    void *handle () const { return _disc; }

  private:
    void *_disc;
    int _last_error;
};

} // namespace service
} // namespace zlink

#endif
