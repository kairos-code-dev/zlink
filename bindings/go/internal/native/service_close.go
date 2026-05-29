// SPDX-License-Identifier: MPL-2.0

package native

/*
#include "zlink.h"
*/
import "C"

func (d *Discovery) Close() error {
	if d == nil || d.closed {
		return nil
	}
	handle := d.handle
	if err := checkRC(C.zlink_discovery_destroy(&handle)); err != nil {
		return err
	}
	d.closed = true
	d.handle = nil
	return nil
}

func (r *Registry) Close() error {
	if r == nil || r.closed {
		return nil
	}
	handle := r.handle
	if err := checkRC(C.zlink_registry_destroy(&handle)); err != nil {
		return err
	}
	r.closed = true
	r.handle = nil
	return nil
}

func (c *RegistryQueryClient) Close() error {
	if c == nil || c.closed {
		return nil
	}
	handle := c.handle
	if err := checkRC(C.zlink_registry_query_client_destroy(&handle)); err != nil {
		return err
	}
	c.closed = true
	c.handle = nil
	return nil
}
