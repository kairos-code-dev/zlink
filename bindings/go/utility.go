// SPDX-License-Identifier: MPL-2.0

package zlink

/*
#include <stdlib.h>
#include "zlink.h"
*/
import "C"

import (
	"strings"
	"unsafe"
)

func Has(capability string) (bool, error) {
	if strings.IndexByte(capability, 0) >= 0 {
		return false, validationError("string contains null byte")
	}
	cstr := C.CString(capability)
	defer C.free(unsafe.Pointer(cstr))
	rc := C.zlink_has(cstr)
	if rc == -1 {
		return false, lastError()
	}
	return rc != 0, nil
}

func Proxy(frontend any, backend any, capture any) error {
	frontendHandle, err := socketHandle(frontend)
	if err != nil {
		return err
	}
	backendHandle, err := socketHandle(backend)
	if err != nil {
		return err
	}
	var captureHandle unsafe.Pointer
	if capture != nil {
		captureHandle, err = socketHandle(capture)
		if err != nil {
			return err
		}
	}
	return checkRC(C.zlink_proxy(frontendHandle, backendHandle, captureHandle))
}

func ProxySteerable(frontend any, backend any, capture any, control any) error {
	frontendHandle, err := socketHandle(frontend)
	if err != nil {
		return err
	}
	backendHandle, err := socketHandle(backend)
	if err != nil {
		return err
	}
	var captureHandle unsafe.Pointer
	if capture != nil {
		captureHandle, err = socketHandle(capture)
		if err != nil {
			return err
		}
	}
	controlHandle, err := socketHandle(control)
	if err != nil {
		return err
	}
	return checkRC(C.zlink_proxy_steerable(frontendHandle, backendHandle, captureHandle, controlHandle))
}

func socketHandle(socket any) (unsafe.Pointer, error) {
	switch s := socket.(type) {
	case *PairSocket:
		return s.raw(), nil
	case *PubSocket:
		return s.raw(), nil
	case *SubSocket:
		return s.raw(), nil
	case *DealerSocket:
		return s.raw(), nil
	case *RouterSocket:
		return s.raw(), nil
	case *XPubSocket:
		return s.raw(), nil
	case *XSubSocket:
		return s.raw(), nil
	case *StreamSocket:
		return s.raw(), nil
	default:
		return nil, validationError("unsupported socket type %T", socket)
	}
}
