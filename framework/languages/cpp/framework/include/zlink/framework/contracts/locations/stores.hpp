/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework/contracts/dispatch/task.hpp>
#include <zlink/framework/contracts/locations/diagnostics.hpp>
#include <zlink/framework/contracts/locations/maintenance_stores.hpp>
#include <zlink/framework/contracts/locations/writes.hpp>

namespace zlink::framework
{

class location_store_t
{
  public:
    virtual ~location_store_t () = default;
    virtual task_t<location_write_result_t> update_mesh_node (
      mesh_node_descriptor_t descriptor,
      location_write_intent_t intent) = 0;
    virtual task_t<location_write_status_t> remove_mesh_node (
      mesh_node_descriptor_key_t key,
      location_owner_token_t owner) = 0;
    virtual task_t<location_page_t<mesh_node_descriptor_t>>
    list_mesh_nodes (std::string mesh_name,
                     location_page_request_t page = {}) = 0;
    virtual task_t<location_write_result_t> update_client_server (
      client_server_server_descriptor_t descriptor,
      location_write_intent_t intent) = 0;
    virtual task_t<location_write_status_t> remove_client_server (
      client_server_server_descriptor_key_t key,
      location_owner_token_t owner) = 0;
    virtual task_t<location_page_t<client_server_server_descriptor_t>>
    list_client_servers (std::string channel_name,
                         location_page_request_t page = {}) = 0;
    virtual task_t<location_write_result_t> update_fanout_publisher (
      fanout_publisher_descriptor_t descriptor,
      location_write_intent_t intent) = 0;
    virtual task_t<location_write_status_t> remove_fanout_publisher (
      fanout_publisher_descriptor_key_t key,
      location_owner_token_t owner) = 0;
    virtual task_t<location_page_t<fanout_publisher_descriptor_t>>
    list_fanout_publishers (std::string channel_name,
                            location_page_request_t page = {}) = 0;
    virtual task_t<owner_lease_claim_result_t> claim_owner_lease (
      std::string owner_id,
      std::chrono::milliseconds lease_ttl) = 0;
    virtual task_t<owner_lease_read_result_t> read_owner_lease (
      std::string owner_id) = 0;
    virtual task_t<owner_lease_renew_result_t> renew_owner_lease (
      location_owner_token_t token,
      std::chrono::milliseconds lease_ttl) = 0;
    virtual task_t<owner_lease_release_result_t> release_owner_lease (
      location_owner_token_t token) = 0;
    virtual task_t<authority_read_result_t> read_authority (
      authority_key_t key,
      std::stop_token cancellation = {}) = 0;
    virtual task_t<authority_compare_exchange_result_t>
    compare_exchange_authority (
      authority_key_t key,
      std::string expected_store_version,
      authority_mutation_t mutation,
      std::stop_token cancellation = {}) = 0;
    virtual task_t<authority_scan_result_t> list_authorities (
      std::string prefix,
      std::optional<authority_scan_cursor_t> cursor,
      std::size_t limit,
      std::stop_token cancellation = {}) = 0;
    virtual task_t<std::optional<creation_terminal_record_t>>
    read_creation_terminal (
      creation_operation_identity_t operation,
      std::stop_token cancellation = {}) = 0;
    virtual task_t<object_reserve_result_t> reserve (
      object_reserve_request_t request,
      std::stop_token cancellation = {}) = 0;
    virtual task_t<object_complete_creation_result_t> complete_creation (
      object_complete_creation_request_t request,
      std::stop_token cancellation = {}) = 0;
    virtual task_t<object_commit_result_t> commit (
      object_commit_request_t request,
      std::stop_token cancellation = {}) = 0;
    virtual task_t<object_abort_result_t> abort (
      object_abort_request_t request,
      std::stop_token cancellation = {}) = 0;
    virtual task_t<relocation_capacity_reserve_result_t>
    reserve_relocation_capacity (
      relocation_capacity_reserve_request_t request,
      std::stop_token cancellation = {}) = 0;
    virtual task_t<relocation_capacity_abort_result_t>
    abort_relocation_capacity (
      relocation_capacity_fence_t fence,
      std::stop_token cancellation = {}) = 0;
    virtual task_t<aggregate_prepare_result_t> prepare_aggregate (
      aggregate_prepare_request_t request,
      std::stop_token cancellation = {}) = 0;
    virtual task_t<aggregate_commit_result_t> commit_aggregate (
      aggregate_fence_t fence,
      std::stop_token cancellation = {}) = 0;
    virtual task_t<aggregate_abort_result_t> abort_aggregate (
      aggregate_fence_t fence,
      std::stop_token cancellation = {}) = 0;
    virtual task_t<std::int64_t> remove_all_by_owner (
      location_owner_token_t owner) = 0;
    virtual task_t<std::optional<std::uint64_t>> get_mesh_node_change_stamp (
      std::string mesh_name)
    {
        (void) mesh_name;
        return task_t<std::optional<std::uint64_t>> (
          result_t<std::optional<std::uint64_t>>::success (std::nullopt));
    }
};

} // namespace zlink::framework
