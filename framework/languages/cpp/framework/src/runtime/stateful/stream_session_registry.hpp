/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "runtime/stateful/stateful_object_runtime.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace zlink::framework::runtime::stateful
{

struct stream_connection_t
{
    std::string connection_id;
    std::uint64_t connection_generation = 0;

    friend bool operator== (const stream_connection_t &,
                            const stream_connection_t &) = default;
};

struct stream_binding_t
{
    stream_connection_t connection;
    std::uint64_t binding_generation = 0;
    object_ref_t actor;

    friend bool operator== (const stream_binding_t &,
                            const stream_binding_t &) = default;
};

struct stream_dispatch_t
{
    stream_binding_t binding;
    std::uint64_t inbound_sequence = 0;
};

class stream_session_registry_t
{
  public:
    using authority_resolver_t =
      std::function<std::optional<object_ref_t> (const std::string &)>;

    explicit stream_session_registry_t (authority_resolver_t resolver);

    stream_connection_t open (std::string connection_id);
    bool close (const stream_connection_t &connection);
    std::pair<stateful_error_t, stream_binding_t> bind (
      const stream_connection_t &connection,
      const object_ref_t &actor);
    stateful_error_t unbind (const stream_binding_t &binding);
    std::pair<stateful_error_t, std::optional<stream_dispatch_t>>
    admit_inbound (const stream_binding_t &binding);
    bool is_current (const stream_binding_t &binding) const;

  private:
    struct connection_state_t
    {
        stream_connection_t connection;
        std::uint64_t next_binding_generation = 1;
        std::uint64_t next_inbound_sequence = 1;
        std::optional<stream_binding_t> binding;
    };

    static bool exact_actor (const object_ref_t &left,
                             const object_ref_t &right);

    authority_resolver_t _resolver;
    mutable std::mutex _mutex;
    std::map<std::string, connection_state_t> _connections;
    std::map<std::string, std::uint64_t> _last_connection_generation;
};

} // namespace zlink::framework::runtime::stateful
