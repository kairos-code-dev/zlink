/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/mesh/mesh_node_host_service.hpp"

#include "runtime/channels/route_handler_registry.hpp"
#include "runtime/channels/channel_reply_writer.hpp"
#include "runtime/mesh/mesh_record_dispatcher.hpp"
#include "runtime/messaging/envelope_codec.hpp"
#include "runtime/spots/spot_route_packets.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string_view>
#include <utility>

namespace zlink::framework::runtime
{

namespace
{

void trace_mesh_host_stop (const char *stage)
{
    const char *value = std::getenv ("ZLINK_CPP_HOST_STOP_TRACE");
    if (value != nullptr && std::string_view (value) != "" && std::string_view (value) != "0")
        std::cerr << "zlink-cpp-host-stop stage=mesh-host-" << stage << std::endl;
}

void reject_application_request (
  const host::receive_record_t &record,
                                 std::vector<zlink::message_t> parts)
{
    const bool request = record.kind == host::record_kind_t::node_request
                         || record.kind == host::record_kind_t::channel_request
                         || record.kind == host::record_kind_t::spot_request
                         || record.kind == host::record_kind_t::actor_request;
    if (!request)
        return;
    messaging::message_parts_t encoded (std::move (parts));
    messaging::envelope_codec_t codec;
    const auto header = codec.decode_header (encoded);
    if (!header)
        return;
    detail::channel_reply_writer_t replies;
    auto reply = replies.reply_raw_envelope (
      replies.create_error_header (
        header.value ().channel_name, header.value (),
        framework_exception_t (framework_error_kind_t::request_rejected,
                               "MeshNode is draining and rejects new application work")),
      zlink::message_t::from (""));
    (void) host::reply (record.reply_token, reply.items ());
}

} // namespace

mesh_node_host_service_t::mesh_node_host_service_t (
  std::vector<std::shared_ptr<detail::mesh_node_builder_state_t>> registrations,
  serializer_registry_t &serializers,
  dispatch_options_t dispatch_options) :
    _registrations (std::move (registrations)),
    _serializers (&serializers),
    _dispatch_options (std::move (dispatch_options)),
    _application_dispatch (std::make_unique<offload_executor_t> (
      0, std::max<std::size_t> (2, std::thread::hardware_concurrency ()), 4096,
      std::chrono::milliseconds (100), "zlink-mesh-app"))
{
    detail::register_spot_route_packet_serializers (serializers);
    _nodes.reserve (_registrations.size ());
    for (const auto &registration : _registrations) {
        auto node = std::make_shared<detail::mesh_node_runtime_t> (registration);
        node->bind_serializers (serializers);
        _nodes.push_back (std::move (node));
    }
}

mesh_node_host_service_t::~mesh_node_host_service_t ()
{
    stop ();
}

void mesh_node_host_service_t::start (service_provider_t &services)
{
    _services = &services;
    _stop.store (false, std::memory_order_release);
    _accept_application_dispatch.store (true, std::memory_order_release);
    for (const auto &node : _nodes)
        node->start ();
    for (std::size_t index = 0; index < _nodes.size (); ++index) {
        const auto node = _nodes[index];
        const auto registration = _registrations[index];
        _threads.emplace_back ([this, node, registration] {
            while (!_stop.load (std::memory_order_acquire)) {
                const auto count = node->dispatch_ready (
                  [&] (const host::ready_record_t &owner,
                       const host::receive_record_t &record,
                       std::vector<zlink::message_t> parts) {
                      detail::spot_node_runtime_t spot_runtime (registration->spot_state);
                      const bool transfer_dispatch =
                        owner.owner_kind == host::owner_kind_t::actor
                        && (record.kind == host::record_kind_t::actor_send
                            || record.kind == host::record_kind_t::actor_request)
                        && spot_runtime.actor_transfer_in_progress (
                          owner.actor.actor_id ());
                      auto run_direct = [this] (auto &&work) {
                          {
                              std::lock_guard lock (_dispatch_gate_mutex);
                              ++_active_direct_dispatch;
                          }
                          try {
                              work ();
                          }
                          catch (...) {
                              {
                                  std::lock_guard lock (_dispatch_gate_mutex);
                                  --_active_direct_dispatch;
                              }
                              _dispatch_gate_changed.notify_all ();
                              throw;
                          }
                          {
                              std::lock_guard lock (_dispatch_gate_mutex);
                              --_active_direct_dispatch;
                          }
                          _dispatch_gate_changed.notify_all ();
                      };
                      if (transfer_dispatch) {
                          run_direct ([&] {
                              (void) spot_runtime.dispatch_mesh_record (
                                owner, record, parts, *_services, *_serializers);
                          });
                          return;
                      }
                      if (owner.domain == host::ready_domain_t::application) {
                          bool accepted = false;
                          {
                              std::lock_guard lock (_dispatch_gate_mutex);
                              if (_accept_application_dispatch.load (
                                    std::memory_order_relaxed)) {
                                  node->application_work_enqueued ();
                                  accepted = true;
                              }
                          }
                          if (!accepted) {
                              reject_application_request (record, std::move (parts));
                              return;
                          }
                          std::vector<std::vector<std::uint8_t>> owned_part_bytes;
                          owned_part_bytes.reserve (parts.size ());
                          for (const auto &part : parts)
                              owned_part_bytes.push_back (part.to_bytes ());
                          try {
                              _application_dispatch->submit (
                                [this, node, registration, owner, record,
                                 part_bytes = std::move (owned_part_bytes)] () mutable {
                                    node->application_work_started ();
                                    try {
                                        std::vector<zlink::message_t> owned_parts;
                                        owned_parts.reserve (part_bytes.size ());
                                        for (const auto &bytes : part_bytes) {
                                            owned_parts.emplace_back (bytes.size ());
                                            if (!bytes.empty ())
                                                std::memcpy (
                                                  owned_parts.back ().data (),
                                                  bytes.data (), bytes.size ());
                                        }
                                        detail::spot_node_runtime_t
                                          application_spot_runtime (
                                            registration->spot_state);
                                        if (!application_spot_runtime
                                               .dispatch_mesh_record (
                                                 owner, record, owned_parts,
                                                 *_services, *_serializers)) {
                                            detail::mesh_record_dispatcher_t dispatcher (
                                              *_services, *_serializers,
                                              registration->handlers,
                                              _dispatch_options);
                                            (void) dispatcher.dispatch (
                                              record, std::move (owned_parts));
                                        }
                                    }
                                    catch (...) {
                                        node->application_work_finished ();
                                        _dispatch_gate_changed.notify_all ();
                                        return;
                                    }
                                    node->application_work_finished ();
                                    _dispatch_gate_changed.notify_all ();
                                });
                          }
                          catch (...) {
                              node->application_work_started ();
                              node->application_work_finished ();
                              _dispatch_gate_changed.notify_all ();
                              throw;
                          }
                          return;
                      }
                      run_direct ([&] {
                          if (spot_runtime.dispatch_mesh_record (
                                owner, record, parts, *_services, *_serializers)) {
                              return;
                          }
                          detail::mesh_record_dispatcher_t dispatcher (
                            *_services, *_serializers, registration->handlers,
                            _dispatch_options);
                          (void) dispatcher.dispatch (record, std::move (parts));
                      });
                  });
                detail::spot_node_runtime_t maintenance (registration->spot_state);
                (void) maintenance.cleanup_expired_actor_admissions ();
                if (count == 0)
                    std::this_thread::sleep_for (std::chrono::milliseconds (1));
            }
        });
    }
}

void mesh_node_host_service_t::request_stop () noexcept
{
    seal_application_dispatch ();
}

void mesh_node_host_service_t::seal_application_dispatch () noexcept
{
    {
        std::lock_guard lock (_dispatch_gate_mutex);
        _accept_application_dispatch.store (false, std::memory_order_release);
    }
    _dispatch_gate_changed.notify_all ();
}

bool mesh_node_host_service_t::wait_for_accepted_callbacks_until (
  std::chrono::steady_clock::time_point deadline) noexcept
{
    std::unique_lock lock (_dispatch_gate_mutex);
    return _dispatch_gate_changed.wait_until (lock, deadline, [this] {
        if (_active_direct_dispatch != 0)
            return false;
        return std::all_of (_nodes.begin (), _nodes.end (), [] (const auto &node) {
            return node->pending_application_callbacks () == 0
                   && node->active_application_callbacks () == 0;
        });
    });
}

void mesh_node_host_service_t::stop () noexcept
{
    request_stop ();
    if (_application_dispatch)
        _application_dispatch->drain ();
    trace_mesh_host_stop ("application-drained");
    _stop.store (true, std::memory_order_release);
    trace_mesh_host_stop ("pump-join-begin");
    for (auto &thread : _threads) {
        if (thread.joinable ())
            thread.join ();
    }
    trace_mesh_host_stop ("pump-join-end");
    _threads.clear ();
    for (auto &node : _nodes) {
        trace_mesh_host_stop ("node-stop-begin");
        node->stop ();
        trace_mesh_host_stop ("node-stop-end");
    }
}

std::vector<std::shared_ptr<detail::mesh_node_runtime_t>>
mesh_node_host_service_t::nodes () const
{
    return _nodes;
}

zlink::submit_result_t mesh_node_host_service_t::submit_local_node_send (
  const std::shared_ptr<detail::mesh_node_runtime_t> &node,
  const std::vector<zlink::message_t> &parts)
{
    const auto found = std::find (_nodes.begin (), _nodes.end (), node);
    if (found == _nodes.end () || _services == nullptr || _serializers == nullptr)
        return zlink::submit_result_t::not_found;
    const auto index = static_cast<std::size_t> (std::distance (_nodes.begin (), found));
    const auto registration = _registrations[index];
    const auto source_rid = node->routing_id ();
    if (!source_rid)
        return zlink::submit_result_t::not_found;

    std::vector<std::vector<std::uint8_t>> owned_part_bytes;
    owned_part_bytes.reserve (parts.size ());
    for (const auto &part : parts)
        owned_part_bytes.push_back (part.to_bytes ());

    {
        std::lock_guard lock (_dispatch_gate_mutex);
        if (!_accept_application_dispatch.load (std::memory_order_relaxed))
            return zlink::submit_result_t::terminated;
        if (node->pending_application_callbacks () + node->active_application_callbacks ()
            >= node->max_pending ())
            return zlink::submit_result_t::backpressured;
        node->application_work_enqueued ();
    }

    try {
        _application_dispatch->submit (
          [this, node, registration, source_rid,
           part_bytes = std::move (owned_part_bytes)] () mutable {
              node->application_work_started ();
              try {
                  std::vector<zlink::message_t> owned_parts;
                  owned_parts.reserve (part_bytes.size ());
                  for (const auto &bytes : part_bytes) {
                      owned_parts.emplace_back (bytes.size ());
                      if (!bytes.empty ())
                          std::memcpy (owned_parts.back ().data (), bytes.data (), bytes.size ());
                  }
                  host::receive_record_t record;
                  record.kind = host::record_kind_t::node_send;
                  record.domain = host::ready_domain_t::application;
                  record.source_node_rid = *source_rid;
                  detail::mesh_record_dispatcher_t dispatcher (
                    *_services, *_serializers, registration->handlers, _dispatch_options);
                  (void) dispatcher.dispatch (record, std::move (owned_parts));
              }
              catch (...) {
                  node->application_work_finished ();
                  _dispatch_gate_changed.notify_all ();
                  return;
              }
              node->application_work_finished ();
              _dispatch_gate_changed.notify_all ();
          });
    }
    catch (...) {
        node->application_work_started ();
        node->application_work_finished ();
        _dispatch_gate_changed.notify_all ();
        return zlink::submit_result_t::backpressured;
    }
    return zlink::submit_result_t::ok;
}

} // namespace zlink::framework::runtime
