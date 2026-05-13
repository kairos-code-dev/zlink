/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_DISCOVERY_REGISTRY_QUERY_ACCESS_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_REGISTRY_QUERY_ACCESS_HPP_INCLUDED__

#include <zlink.h>

namespace zlink
{
class ctx_t;

struct registry_query_access_t
{
    static void *create (ctx_t *ctx_);
    static int connect (void *client_, const char *endpoint_);
    static int topology_query (
      void *client_,
      const zlink_registry_topology_filter_t *filter_,
      zlink_registry_topology_entry_t *entries_,
      size_t *count_);
    static int destroy (void **client_p_);
};
}

#endif
