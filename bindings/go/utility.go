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

type SocketTarget interface {
	raw() unsafe.Pointer
}

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

func Proxy(frontend SocketTarget, backend SocketTarget, capture SocketTarget) error {
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

func ProxySteerable(frontend SocketTarget, backend SocketTarget, capture SocketTarget, control SocketTarget) error {
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

func socketHandle(socket SocketTarget) (unsafe.Pointer, error) {
	if socket == nil {
		return nil, validationError("socket must not be nil")
	}
	return socket.raw(), nil
}
