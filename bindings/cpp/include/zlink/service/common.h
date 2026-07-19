/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_SERVICE_COMMON_H_INCLUDED
#define ZLINK_SERVICE_COMMON_H_INCLUDED

#include <zlink/common.h>
#include <zlink/message/api.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Shared service identity types. ZLink Core 10.3.0.
   Contract: core/doc/spec/core/service/ */

#define ZLINK_ACTOR_ID_MAX 255u

typedef struct zlink_actor_ref_t {
  zlink_routing_id_t node_rid;
  char actor_id[ZLINK_ACTOR_ID_MAX + 1];
  uint64_t generation;
} zlink_actor_ref_t;

#ifdef __cplusplus
}
#endif

#endif
