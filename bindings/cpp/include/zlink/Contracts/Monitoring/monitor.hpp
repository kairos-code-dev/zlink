/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_MONITOR_HPP_INCLUDED
#define ZLINK_CPP_MONITOR_HPP_INCLUDED

#include "../Errors/error.hpp"
#include "../Core/types.hpp"

namespace zlink
{

class base_socket_t;
class monitor_handle_t;
namespace detail
{
inline void *native_handle (base_socket_t &socket_) noexcept;
inline const void *native_handle (const base_socket_t &socket_) noexcept;
inline void *native_handle (monitor_handle_t &monitor_) noexcept;
inline const void *native_handle (const monitor_handle_t &monitor_) noexcept;
} // namespace detail

class monitor_handle_t
{
  public:
    monitor_handle_t () : _monitor (NULL) {}

    template<typename SocketLike>
    static monitor_handle_t open (const SocketLike &socket_,
                                  monitor_event events_ = monitor_event::all)
    {
        zlink_socket_monitor_open_options_t options;
        options.events =
          static_cast<zlink_socket_monitor_event_mask_t> (events_);
        void *monitor = zlink_socket_monitor_open (
          const_cast<void *> (detail::native_handle (socket_)), &options);
        if (!monitor)
            throw config_error_t (
              config_result_t::invalid_handle, zlink_errno ());
        return monitor_handle_t (monitor);
    }

    ~monitor_handle_t () { close_noexcept (); }

    monitor_handle_t (monitor_handle_t &&other) noexcept
        : _monitor (other._monitor), _event_handler (other._event_handler),
          _event_userdata (other._event_userdata)
    {
        other._monitor = NULL;
        other._event_handler = NULL;
        other._event_userdata = NULL;
    }

    monitor_handle_t &operator= (monitor_handle_t &&other) noexcept
    {
        if (this == &other)
            return *this;
        close_noexcept ();
        _monitor = other._monitor;
        _event_handler = other._event_handler;
        _event_userdata = other._event_userdata;
        other._monitor = NULL;
        other._event_handler = NULL;
        other._event_userdata = NULL;
        return *this;
    }

    monitor_handle_t (const monitor_handle_t &) = delete;
    monitor_handle_t &operator= (const monitor_handle_t &) = delete;

    bool valid () const noexcept { return _monitor != NULL; }

    void on_event (std::function<void(const monitor_event_t &)> handler_)
    {
        _event_function_handler = std::move (handler_);
        detail::throw_if_failed<handler_error_t> (
          static_cast<handler_result_t> (
            zlink_socket_monitor_handler (
              _monitor, &monitor_handle_t::event_function_trampoline, this)));
        _event_handler = NULL;
        _event_userdata = NULL;
    }

    static void ignore_event (const monitor_event_t &) noexcept {}

    std::optional<monitor_event_t> recv (
      recv_flags_t flags_ = recv_flags_t::none)
    {
        zlink_monitor_event_t event;
        const recv_result_t result = static_cast<recv_result_t> (
          zlink_socket_monitor_recv (
            _monitor, &event, static_cast<zlink_recv_flags_t> (flags_)));
        if (result == recv_result_t::no_data && flags_ == recv_flags_t::dontwait)
            return std::nullopt;
        if (result != recv_result_t::ok)
            throw recv_error_t (result, zlink_errno ());
        return std::optional<monitor_event_t> (monitor_event_t (event));
    }

    monitor_status_t status () const
    {
        zlink_monitor_status_t snapshot;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_monitor_status (_monitor, &snapshot)));
        return monitor_status_t (snapshot);
    }

    void close ()
    {
        if (!_monitor)
            return;
        void *monitor = _monitor;
        const close_result_t result =
          static_cast<close_result_t> (zlink_monitor_close (&monitor));
        if (result != close_result_t::ok)
            throw close_error_t (result, zlink_errno ());
        _monitor = NULL;
        _event_handler = NULL;
        _event_userdata = NULL;
        _event_function_handler = nullptr;
    }

  private:
    explicit monitor_handle_t (void *monitor_) : _monitor (monitor_) {}

    void close_noexcept () noexcept
    {
        if (!_monitor)
            return;
        void *monitor = _monitor;
        (void) zlink_monitor_close (&monitor);
        _monitor = NULL;
        _event_handler = NULL;
        _event_userdata = NULL;
        _event_function_handler = nullptr;
    }

    friend class base_socket_t;
    friend void *detail::native_handle (monitor_handle_t &monitor_) noexcept;
    friend const void *
    detail::native_handle (const monitor_handle_t &monitor_) noexcept;

    static void event_trampoline (const zlink_monitor_event_t *event_,
                                  void *userdata_)
    {
        monitor_handle_t *self = static_cast<monitor_handle_t *> (userdata_);
        if (!self || !self->_event_handler || !event_)
            return;
        monitor_event_t event (*event_);
        self->_event_handler (&event, self->_event_userdata);
    }

    static void event_function_trampoline (const zlink_monitor_event_t *event_,
                                           void *userdata_)
    {
        monitor_handle_t *self = static_cast<monitor_handle_t *> (userdata_);
        if (!self || !self->_event_function_handler || !event_)
            return;
        const monitor_event_t event (*event_);
        self->_event_function_handler (event);
    }

    void *_monitor;
    monitor_event_handler_fn _event_handler = NULL;
    void *_event_userdata = NULL;
    std::function<void(const monitor_event_t &)> _event_function_handler;
};

namespace detail
{
inline void *native_handle (monitor_handle_t &monitor_) noexcept
{
    return monitor_._monitor;
}

inline const void *native_handle (const monitor_handle_t &monitor_) noexcept
{
    return monitor_._monitor;
}
} // namespace detail

} // namespace zlink

#endif
