/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_DISCOVERY_ROUTE_LIMITS_INTERNAL_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_ROUTE_LIMITS_INTERNAL_HPP_INCLUDED__

#include <stddef.h>
#include <stdint.h>

typedef uint32_t zlink_route_kind_t;

static const zlink_route_kind_t ZLINK_ROUTE_KIND_INVALID = 0u;
static const size_t ZLINK_ROUTE_KEY_MAX = 256u;
static const size_t ZLINK_ROUTE_VALUE_MAX = 4096u;

#endif
