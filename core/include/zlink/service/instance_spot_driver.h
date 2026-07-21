/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_SERVICE_INSTANCE_SPOT_DRIVER_H_INCLUDED
#define ZLINK_SERVICE_INSTANCE_SPOT_DRIVER_H_INCLUDED

#include <zlink/service/spot.h>

/* zlink.h intentionally removes this declaration macro after building its
   ordinary API closure. Restore it when this explicit driver header follows
   zlink.h, then remove only the definition restored here. */
#ifndef ZLINK_EXPORT
#define ZLINK_INSTANCE_SPOT_DRIVER_RESTORED_EXPORT 1
#if defined ZLINK_NO_EXPORT || defined ZLINK_STATIC
#define ZLINK_EXPORT
#elif defined _WIN32
#if defined DLL_EXPORT
#define ZLINK_EXPORT __declspec (dllexport)
#else
#define ZLINK_EXPORT __declspec (dllimport)
#endif
#elif defined __SUNPRO_C || defined __SUNPRO_CC
#define ZLINK_EXPORT __global
#elif (defined __GNUC__ && __GNUC__ >= 4) || defined __INTEL_COMPILER
#define ZLINK_EXPORT __attribute__ ((visibility ("default")))
#else
#define ZLINK_EXPORT
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Framework runtime driver SPI for Instance Spot placement and activation.
   Contract: core/doc/spec/core/service/03-spot.md */

#define ZLINK_INSTANCE_SPOT_TYPE_MAX 255u
#define ZLINK_INSTANCE_SPOT_OWNER_ID_MAX 255u
#define ZLINK_INSTANCE_SPOT_CONTRACT_ID_MAX 255u

typedef enum zlink_instance_spot_claim_role_t {
  ZLINK_INSTANCE_SPOT_CLAIM_INVALID  = 0,
  ZLINK_INSTANCE_SPOT_CLAIM_LEADER   = 1,
  ZLINK_INSTANCE_SPOT_CLAIM_FOLLOWER = 2
} zlink_instance_spot_claim_role_t;

typedef enum zlink_instance_spot_operation_kind_t {
  ZLINK_INSTANCE_SPOT_OPERATION_INVALID = 0,
  ZLINK_INSTANCE_SPOT_OPERATION_SEND    = 1,
  ZLINK_INSTANCE_SPOT_OPERATION_REQUEST = 2
} zlink_instance_spot_operation_kind_t;

typedef struct zlink_instance_spot_placement_t {
  zlink_routing_id_t node_rid;
  uint64_t node_generation;
  zlink_routing_id_t spot_rid;
  const char *instance_spot_type;
  size_t instance_spot_type_size;
  const char *message_contract_id;
  size_t message_contract_id_size;
} zlink_instance_spot_placement_t;

typedef struct zlink_instance_spot_activation_token_t {
  uint64_t opaque[4];
} zlink_instance_spot_activation_token_t;

typedef struct zlink_instance_spot_activation_data_t {
  zlink_routing_id_t spot_rid;
  zlink_instance_spot_operation_kind_t operation_kind;
  char instance_spot_type[ZLINK_INSTANCE_SPOT_TYPE_MAX + 1];
  char message_contract_id[ZLINK_INSTANCE_SPOT_CONTRACT_ID_MAX + 1];
  zlink_instance_spot_activation_token_t token;
} zlink_instance_spot_activation_data_t;

typedef struct zlink_instance_spot_claim_result_t {
  zlink_instance_spot_claim_role_t role;
  void *leader_spot;
  uint64_t leader_spot_generation;
} zlink_instance_spot_claim_result_t;

ZLINK_EXPORT zlink_submit_result_t zlink_spot_send_to_instance_placement(
  void *spot,
  const zlink_instance_spot_placement_t *placement,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_request_to_instance_placement(
  void *spot,
  const zlink_instance_spot_placement_t *placement,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_mesh_operation_id_t *operation_id_out,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);

ZLINK_EXPORT zlink_config_result_t zlink_instance_spot_activation_claim_owner(
  zlink_instance_spot_activation_token_t *token,
  const char *location_owner_id,
  size_t location_owner_id_size,
  zlink_instance_spot_claim_result_t *result_out);

ZLINK_EXPORT zlink_config_result_t zlink_instance_spot_activation_mark_ready(
  zlink_instance_spot_activation_token_t *token,
  uint32_t owner_lease_valid_for_ms);

ZLINK_EXPORT zlink_config_result_t zlink_instance_spot_activation_redirect(
  zlink_instance_spot_activation_token_t *token,
  const zlink_routing_id_t *target_node_rid,
  const zlink_routing_id_t *target_spot_rid,
  uint64_t target_spot_generation);

ZLINK_EXPORT zlink_config_result_t zlink_instance_spot_activation_abort(
  zlink_instance_spot_activation_token_t *token,
  zlink_request_result_t terminal_result,
  int32_t failure_errno);

ZLINK_EXPORT zlink_config_result_t zlink_instance_spot_begin_close(void *spot);

ZLINK_EXPORT zlink_config_result_t zlink_instance_spot_renew_owner_admission(
  void *spot,
  uint32_t owner_lease_valid_for_ms);

#ifdef __cplusplus
}
#endif

#ifdef ZLINK_INSTANCE_SPOT_DRIVER_RESTORED_EXPORT
#undef ZLINK_EXPORT
#undef ZLINK_INSTANCE_SPOT_DRIVER_RESTORED_EXPORT
#endif

#endif
