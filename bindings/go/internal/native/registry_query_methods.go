// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include <stdlib.h>
#include "zlink.h"
*/
import "C"

func (c *RegistryQueryClient) Connect(endpoint string) error {
	return withRegistryQueryCString(c, endpoint, func(cstr *C.char) error {
		return checkRC(C.zlink_registry_query_client_connect(c.raw(), cstr))
	})
}

func (c *RegistryQueryClient) Topology(filter *RegistryTopologyFilter) ([]RegistryTopologyEntry, error) {
	if c == nil || c.closed {
		return nil, stateError("registry query client is closed")
	}
	var rawFilter *C.zlink_registry_topology_filter_t
	var cfilter C.zlink_registry_topology_filter_t
	if filter != nil {
		if err := validateRegistryTopologyFilter(*filter); err != nil {
			return nil, err
		}
		cfilter = filter.toC()
		rawFilter = &cfilter
	}
	return queryRegistryTopology(func(entries *C.zlink_registry_topology_entry_t, count *C.size_t) error {
		return checkRC(C.zlink_registry_query_client_topology(c.raw(), rawFilter, entries, count))
	})
}
