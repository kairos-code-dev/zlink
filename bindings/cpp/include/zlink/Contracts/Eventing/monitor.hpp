/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Errors/errors.hpp"
#include "events.hpp"
#include "status.hpp"

#include <functional>
#include <memory>
#include <optional>

namespace zlink
{

class base_socket_t;
class monitor_handle_t;
namespace detail
{
struct monitor_access_t;
} // namespace detail

class monitor_handle_t
{
  public:
    monitor_handle_t ();

    static monitor_handle_t open (
      const base_socket_t &socket_, monitor_event events_ = monitor_event::all);

    ~monitor_handle_t ();

    monitor_handle_t (monitor_handle_t &&other) noexcept;

    monitor_handle_t &operator= (monitor_handle_t &&other) noexcept;

    monitor_handle_t (const monitor_handle_t &) = delete;
    monitor_handle_t &operator= (const monitor_handle_t &) = delete;

    bool valid () const noexcept;

    void set_event_handler (std::function<void(const monitor_event_t &)> handler_);

    static void ignore_event (const monitor_event_t &) noexcept {}

    std::optional<monitor_event_t> recv (
      recv_flags_t flags_ = recv_flags_t::none);

    monitor_status_t status () const;

    void close ();

  private:
    void close_noexcept () noexcept;

    friend class base_socket_t;
    friend struct detail::monitor_access_t;

    struct impl;
    std::unique_ptr<impl> _impl;
};

} // namespace zlink
