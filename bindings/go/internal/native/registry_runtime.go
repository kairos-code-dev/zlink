// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include <stdlib.h>
#include "zlink.h"
*/
import "C"

func newRegistry(ctx *Context) (*Registry, error) {
	if ctx == nil || ctx.closed {
		return nil, stateError("context is closed")
	}
	handle := C.zlink_registry_new(ctx.raw())
	if handle == nil {
		return nil, lastError()
	}
	return &Registry{handle: handle}, nil
}
