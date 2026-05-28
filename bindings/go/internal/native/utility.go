// SPDX-License-Identifier: MPL-2.0

package native

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
		return false, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	cstr := C.CString(capability)
	defer C.free(unsafe.Pointer(cstr))
	return bool(C.zlink_has(cstr)), nil
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
	return configErrorFromResult(ConfigResult(C.zlink_proxy(frontendHandle, backendHandle, captureHandle)))
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
	return configErrorFromResult(ConfigResult(C.zlink_proxy_steerable(frontendHandle, backendHandle, captureHandle, controlHandle)))
}

func socketHandle(socket SocketTarget) (unsafe.Pointer, error) {
	if socket == nil {
		return nil, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	return socket.raw(), nil
}

func Sleep(seconds int) {
	C.zlink_sleep(C.int(seconds))
}

func MultipartClose(parts []*Message) {
	for _, part := range parts {
		if part != nil {
			_ = part.Close()
		}
	}
}

type Stopwatch struct {
	handle unsafe.Pointer
}

func NewStopwatch() *Stopwatch {
	handle := C.zlink_stopwatch_start()
	if handle == nil {
		return nil
	}
	return &Stopwatch{handle: handle}
}

func (s *Stopwatch) Intermediate() uint64 {
	if s == nil || s.handle == nil {
		return 0
	}
	return uint64(C.zlink_stopwatch_intermediate(s.handle))
}

func (s *Stopwatch) Stop() uint64 {
	if s == nil || s.handle == nil {
		return 0
	}
	handle := s.handle
	s.handle = nil
	return uint64(C.zlink_stopwatch_stop(handle))
}
