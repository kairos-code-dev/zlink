// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include <stdlib.h>
#include "zlink.h"
*/
import "C"

import (
	"runtime/cgo"
	"strings"
	"sync"
	"time"
	"unsafe"
)

type spotCore struct {
	handle          unsafe.Pointer
	closed          bool
	subscribeHandle cgo.Handle
	sendReadyHandle cgo.Handle
	dispatchHandle  cgo.Handle
	owner           *SpotNode
	mu              sync.Mutex
}

func (s *spotCore) raw() unsafe.Pointer {
	if s == nil {
		return nil
	}
	return s.handle
}

func (s *spotCore) Close() error {
	if s == nil {
		return nil
	}
	s.mu.Lock()
	if s.closed {
		s.mu.Unlock()
		return nil
	}
	handle := s.handle
	if err := closeErrorFromResult(C.zlink_spot_destroy(&handle)); err != nil {
		s.mu.Unlock()
		return err
	}
	subscribeHandle := s.subscribeHandle
	sendReadyHandle := s.sendReadyHandle
	dispatchHandle := s.dispatchHandle
	owner := s.owner
	s.subscribeHandle = 0
	s.sendReadyHandle = 0
	s.dispatchHandle = 0
	s.closed = true
	s.handle = nil
	s.owner = nil
	s.mu.Unlock()
	if subscribeHandle != 0 {
		releaseCallbackHandle(subscribeHandle)
	}
	if sendReadyHandle != 0 {
		releaseCallbackHandle(sendReadyHandle)
	}
	if dispatchHandle != 0 {
		releaseCallbackHandle(dispatchHandle)
	}
	if owner != nil {
		owner.unregisterSpot(s)
	}
	return nil
}

func (s *spotCore) setOption(option C.zlink_option_t, ptr unsafe.Pointer, size C.size_t) error {
	if s == nil {
		return &ConfigError{Result: ConfigInvalidHandle, nativeErrno: int(C.EFAULT)}
	}
	return setNativeOption(s.handle, s.closed, "spot is closed", option, ptr, size)
}

func (s *spotCore) setIntOption(option C.zlink_option_t, value int32) error {
	if s == nil {
		return &ConfigError{Result: ConfigInvalidHandle, nativeErrno: int(C.EFAULT)}
	}
	return setNativeIntOption(s.handle, s.closed, "spot is closed", option, value)
}

func (s *spotCore) setDurationOption(option C.zlink_option_t, value time.Duration) error {
	if s == nil {
		return &ConfigError{Result: ConfigInvalidHandle, nativeErrno: int(C.EFAULT)}
	}
	return setNativeDurationOption(s.handle, s.closed, "spot is closed", option, value)
}

func (s *spotCore) withCString(value string, fn func(*C.char) error) error {
	if s == nil || s.closed {
		return &ConfigError{Result: ConfigInvalidHandle, nativeErrno: int(C.EFAULT)}
	}
	if strings.IndexByte(value, 0) >= 0 {
		return &ConfigError{Result: ConfigInvalidArgument, nativeErrno: int(C.EINVAL)}
	}
	cstr := C.CString(value)
	defer C.free(unsafe.Pointer(cstr))
	return fn(cstr)
}
