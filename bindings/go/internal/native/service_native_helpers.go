// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdlib.h>
#include "zlink.h"
*/
import "C"

import (
	"errors"
	"syscall"
	"unsafe"
)

func withDiscoveryCString(d *Discovery, value string, fn func(*C.char) error) error {
	if d == nil || d.closed {
		return stateError("discovery is closed")
	}
	if err := validateEndpointString(value); err != nil {
		return err
	}
	cstr := C.CString(value)
	defer C.free(unsafe.Pointer(cstr))
	return fn(cstr)
}

func checkRC(result any) error {
	code := resultCodeInt(result)
	if code == 0 {
		return nil
	}
	switch {
	case code >= 100 && code < 200:
		return requestErrorFromResult(RequestResult(code))
	case code >= 200 && code < 300:
		return recvErrorFromResult(RecvResult(code))
	case code >= 300 && code < 400:
		return handlerErrorFromResult(HandlerResult(code))
	case code >= 400 && code < 500:
		return closeErrorFromResult(CloseResult(code))
	case code >= 500 && code < 600:
		return bindErrorFromResult(BindResult(code))
	case code >= 600 && code < 700:
		return connectErrorFromResult(ConnectResult(code))
	case code >= 700 && code < 800:
		return configErrorFromResult(ConfigResult(code))
	default:
		return configErrorFromErrno(currentErrno())
	}
}

func lastError() error {
	return configErrorFromErrno(currentErrno())
}

func isNativeErrorCode(err error, code int) bool {
	return errors.Is(err, syscall.Errno(code))
}

func withRegistryCString(r *Registry, value string, fn func(*C.char) error) error {
	if r == nil || r.closed {
		return stateError("registry is closed")
	}
	if err := validateEndpointString(value); err != nil {
		return err
	}
	cstr := C.CString(value)
	defer C.free(unsafe.Pointer(cstr))
	return fn(cstr)
}

func withRegistryQueryCString(c *RegistryQueryClient, value string, fn func(*C.char) error) error {
	if c == nil || c.closed {
		return stateError("registry query client is closed")
	}
	if err := validateEndpointString(value); err != nil {
		return err
	}
	cstr := C.CString(value)
	defer C.free(unsafe.Pointer(cstr))
	return fn(cstr)
}
