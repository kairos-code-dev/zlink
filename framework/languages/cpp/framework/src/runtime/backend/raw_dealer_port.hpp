/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "runtime/backend/raw_route_port.hpp"

namespace zlink
{
class dealer_socket_t;
}

namespace zlink::framework::detail::backend
{

class raw_dealer_port_t
{
  public:
    explicit raw_dealer_port_t (
      zlink::dealer_socket_t &socket,
      std::mutex *shared_socket_mutex = nullptr) noexcept;

    bool send (const raw_message_t &parts);
    bool request (const raw_message_t &parts,
                  std::chrono::milliseconds timeout,
                  raw_route_port_t::request_callback_t callback);
    std::optional<raw_message_t> try_receive ();
    void close () noexcept;

  private:
    zlink::dealer_socket_t *_socket;
    std::mutex _owned_socket_mutex;
    std::mutex *_socket_mutex;
};

} // namespace zlink::framework::detail::backend
