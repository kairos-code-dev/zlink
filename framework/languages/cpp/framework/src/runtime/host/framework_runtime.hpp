/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "runtime/dispatch/offload_executor.hpp"

#include <memory>

namespace zlink
{
class context_t;
class router_socket_t;
class dealer_socket_t;
class stream_socket_t;
namespace service
{
class discovery_t;
class registry_t;
class spot_node_t;
} // namespace service
} // namespace zlink

namespace zlink::framework::runtime
{

class framework_runtime_t
{
  public:
    framework_runtime_t ();
    ~framework_runtime_t ();

    framework_runtime_t (const framework_runtime_t &) = delete;
    framework_runtime_t &operator= (const framework_runtime_t &) = delete;

    bool owns_native_context () const noexcept;
    zlink::router_socket_t &channel_router ();
    zlink::dealer_socket_t &channel_dealer ();
    zlink::stream_socket_t &stream_socket ();
    zlink::service::discovery_t &discovery ();
    zlink::service::registry_t &registry ();
    zlink::service::spot_node_t &spot_node ();
    void drain ();
    offload_executor_t &offload_executor () noexcept;

  private:
    std::unique_ptr<zlink::context_t> _context;
    std::unique_ptr<zlink::router_socket_t> _router;
    std::unique_ptr<zlink::dealer_socket_t> _dealer;
    std::unique_ptr<zlink::stream_socket_t> _stream;
    std::unique_ptr<zlink::service::discovery_t> _discovery;
    std::unique_ptr<zlink::service::registry_t> _registry;
    std::unique_ptr<zlink::service::spot_node_t> _spot_node;
    offload_executor_t _offload;
};

} // namespace zlink::framework::runtime
