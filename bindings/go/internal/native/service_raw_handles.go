// SPDX-License-Identifier: MPL-2.0

package native

import "unsafe"

func (d *Discovery) raw() unsafe.Pointer {
	if d == nil {
		return nil
	}
	return d.handle
}

func (r *Registry) raw() unsafe.Pointer {
	if r == nil {
		return nil
	}
	return r.handle
}

func (c *RegistryQueryClient) raw() unsafe.Pointer {
	if c == nil {
		return nil
	}
	return c.handle
}
