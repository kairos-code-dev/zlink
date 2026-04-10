// SPDX-License-Identifier: MPL-2.0

package zlink

/*
#include "zlink.h"
*/
import "C"

import (
	"fmt"
)

func classifyNonBlockingSendErr() (SendResult, error) {
	code := int(C.zlink_errno())
	switch code {
	case int(C.EAGAIN):
		return SendResultBackpressured, nil
	case int(C.ENOTCONN), int(C.EHOSTUNREACH):
		return SendResultNotReady, nil
	default:
		return 0, lastError()
	}
}

type ErrorKind string

const (
	ErrorKindNative     ErrorKind = "native"
	ErrorKindValidation ErrorKind = "validation"
	ErrorKindState      ErrorKind = "state"
)

type ZlinkError struct {
	Kind    ErrorKind
	Code    int
	Message string
}

func (e *ZlinkError) Error() string {
	if e == nil {
		return "<nil>"
	}
	if e.Code != 0 {
		return fmt.Sprintf("%s error (%d): %s", e.Kind, e.Code, e.Message)
	}
	return fmt.Sprintf("%s error: %s", e.Kind, e.Message)
}

func validationError(format string, args ...any) error {
	return &ZlinkError{
		Kind:    ErrorKindValidation,
		Message: fmt.Sprintf(format, args...),
	}
}

func stateError(format string, args ...any) error {
	return &ZlinkError{
		Kind:    ErrorKindState,
		Message: fmt.Sprintf(format, args...),
	}
}

func lastError() error {
	code := int(C.zlink_errno())
	return &ZlinkError{
		Kind:    ErrorKindNative,
		Code:    code,
		Message: C.GoString(C.zlink_strerror(C.int(code))),
	}
}

func checkRC(rc C.int) error {
	if rc == 0 {
		return nil
	}
	return lastError()
}

func isNativeErrorCode(err error, code int) bool {
	zerr, ok := err.(*ZlinkError)
	return ok && zerr.Kind == ErrorKindNative && zerr.Code == code
}
