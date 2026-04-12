// SPDX-License-Identifier: MPL-2.0

package zlink

/*
#include "zlink.h"
*/
import "C"

import (
	"fmt"
	"reflect"
	"syscall"
)

type ZlinkError interface {
	error
	Code() int
	InternalErrno() int
}

type SubmitError struct {
	Result        SubmitResult
	internalErrno int
}

type RequestError struct {
	Result        RequestResult
	internalErrno int
}

type RecvError struct {
	Result        RecvResult
	internalErrno int
}

type HandlerError struct {
	Result        HandlerResult
	internalErrno int
}

type CloseError struct {
	Result        CloseResult
	internalErrno int
}

type BindError struct {
	Result        BindResult
	internalErrno int
}

type ConnectError struct {
	Result        ConnectResult
	internalErrno int
}

type ConfigError struct {
	Result        ConfigResult
	internalErrno int
}

func (e *SubmitError) Error() string {
	return formatError("submit", int(e.Result), e.internalErrno)
}

func (e *SubmitError) Code() int { return int(e.Result) }

func (e *SubmitError) InternalErrno() int { return e.internalErrno }

func (e *SubmitError) Unwrap() error { return errnoToError(e.internalErrno) }

func (e *RequestError) Error() string {
	return formatError("request", int(e.Result), e.internalErrno)
}

func (e *RequestError) Code() int { return int(e.Result) }

func (e *RequestError) InternalErrno() int { return e.internalErrno }

func (e *RequestError) Unwrap() error { return errnoToError(e.internalErrno) }

func (e *RecvError) Error() string {
	return formatError("recv", int(e.Result), e.internalErrno)
}

func (e *RecvError) Code() int { return int(e.Result) }

func (e *RecvError) InternalErrno() int { return e.internalErrno }

func (e *RecvError) Unwrap() error { return errnoToError(e.internalErrno) }

func (e *HandlerError) Error() string {
	return formatError("handler", int(e.Result), e.internalErrno)
}

func (e *HandlerError) Code() int { return int(e.Result) }

func (e *HandlerError) InternalErrno() int { return e.internalErrno }

func (e *HandlerError) Unwrap() error { return errnoToError(e.internalErrno) }

func (e *CloseError) Error() string {
	return formatError("close", int(e.Result), e.internalErrno)
}

func (e *CloseError) Code() int { return int(e.Result) }

func (e *CloseError) InternalErrno() int { return e.internalErrno }

func (e *CloseError) Unwrap() error { return errnoToError(e.internalErrno) }

func (e *BindError) Error() string {
	return formatError("bind", int(e.Result), e.internalErrno)
}

func (e *BindError) Code() int { return int(e.Result) }

func (e *BindError) InternalErrno() int { return e.internalErrno }

func (e *BindError) Unwrap() error { return errnoToError(e.internalErrno) }

func (e *ConnectError) Error() string {
	return formatError("connect", int(e.Result), e.internalErrno)
}

func (e *ConnectError) Code() int { return int(e.Result) }

func (e *ConnectError) InternalErrno() int { return e.internalErrno }

func (e *ConnectError) Unwrap() error { return errnoToError(e.internalErrno) }

func (e *ConfigError) Error() string {
	return formatError("config", int(e.Result), e.internalErrno)
}

func (e *ConfigError) Code() int { return int(e.Result) }

func (e *ConfigError) InternalErrno() int { return e.internalErrno }

func (e *ConfigError) Unwrap() error { return errnoToError(e.internalErrno) }

func formatError(kind string, code int, internalErrno int) string {
	if internalErrno != 0 {
		return fmt.Sprintf("%s error (%d): %s", kind, code, C.GoString(C.zlink_strerror(C.int(internalErrno))))
	}
	return fmt.Sprintf("%s error (%d)", kind, code)
}

func errnoToError(errno int) error {
	if errno == 0 {
		return nil
	}
	return syscall.Errno(errno)
}

func currentErrno() int {
	return int(C.zlink_errno())
}

func errnoOrIO() int {
	if errno := currentErrno(); errno != 0 {
		return errno
	}
	return int(C.EIO)
}

func submitErrorFromResult(result any) error {
	if resultCodeInt(result) == int(SubmitOK) {
		return nil
	}
	return &SubmitError{Result: SubmitResult(resultCodeInt(result)), internalErrno: errnoOrIO()}
}

func requestErrorFromResult(result any) error {
	if resultCodeInt(result) == int(RequestOK) {
		return nil
	}
	return &RequestError{Result: RequestResult(resultCodeInt(result)), internalErrno: errnoOrIO()}
}

func recvErrorFromResult(result any) error {
	if resultCodeInt(result) == int(RecvOK) {
		return nil
	}
	return &RecvError{Result: RecvResult(resultCodeInt(result)), internalErrno: errnoOrIO()}
}

func handlerErrorFromResult(result any) error {
	if resultCodeInt(result) == int(HandlerOK) {
		return nil
	}
	return &HandlerError{Result: HandlerResult(resultCodeInt(result)), internalErrno: errnoOrIO()}
}

func closeErrorFromResult(result any) error {
	if resultCodeInt(result) == int(CloseOK) {
		return nil
	}
	return &CloseError{Result: CloseResult(resultCodeInt(result)), internalErrno: errnoOrIO()}
}

func bindErrorFromResult(result any) error {
	if resultCodeInt(result) == int(BindOK) {
		return nil
	}
	return &BindError{Result: BindResult(resultCodeInt(result)), internalErrno: errnoOrIO()}
}

func connectErrorFromResult(result any) error {
	if resultCodeInt(result) == int(ConnectOK) {
		return nil
	}
	return &ConnectError{Result: ConnectResult(resultCodeInt(result)), internalErrno: errnoOrIO()}
}

func configErrorFromResult(result any) error {
	if resultCodeInt(result) == int(ConfigOK) {
		return nil
	}
	return &ConfigError{Result: ConfigResult(resultCodeInt(result)), internalErrno: errnoOrIO()}
}

func validationError(format string, args ...any) error {
	return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
}

func stateError(format string, args ...any) error {
	return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
}

func configErrorFromErrno(errno int) error {
	switch errno {
	case 0:
		return nil
	case int(C.EFAULT), int(C.ENOTSOCK), int(C.EDESTADDRREQ):
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: errno}
	case int(C.EINVAL), int(C.EMSGSIZE), int(C.EAFNOSUPPORT), int(C.ENAMETOOLONG):
		return &ConfigError{Result: ConfigInvalidArgument, internalErrno: errno}
	case int(C.ENOTSUP):
		return &ConfigError{Result: ConfigNotSupported, internalErrno: errno}
	default:
		return &ConfigError{Result: ConfigInvalidArgument, internalErrno: errno}
	}
}

func closeErrorFromErrno(errno int) error {
	switch errno {
	case 0:
		return nil
	case int(C.EBUSY):
		return &CloseError{Result: CloseBusy, internalErrno: errno}
	case int(C.ESHUTDOWN):
		return &CloseError{Result: CloseShutdown, internalErrno: errno}
	case int(C.EFAULT):
		return &CloseError{Result: CloseInvalidHandle, internalErrno: errno}
	default:
		return &CloseError{Result: CloseInvalidHandle, internalErrno: errno}
	}
}

func bindErrorFromErrno(errno int) error {
	switch errno {
	case 0:
		return nil
	case int(C.EINVAL):
		return &BindError{Result: BindInvalidArgument, internalErrno: errno}
	case int(C.EADDRINUSE):
		return &BindError{Result: BindAddrInUse, internalErrno: errno}
	case int(C.ENOTSUP):
		return &BindError{Result: BindNotSupported, internalErrno: errno}
	default:
		return &BindError{Result: BindInvalidHandle, internalErrno: errno}
	}
}

func connectErrorFromErrno(errno int) error {
	switch errno {
	case 0:
		return nil
	case int(C.EINVAL):
		return &ConnectError{Result: ConnectInvalidArgument, internalErrno: errno}
	case int(C.ENOTSUP):
		return &ConnectError{Result: ConnectNotSupported, internalErrno: errno}
	default:
		return &ConnectError{Result: ConnectInvalidHandle, internalErrno: errno}
	}
}

func recvErrorFromErrno(errno int) error {
	switch errno {
	case 0:
		return nil
	case int(C.EAGAIN):
		return &RecvError{Result: RecvNoData, internalErrno: errno}
	case int(C.EBUSY):
		return &RecvError{Result: RecvBusy, internalErrno: errno}
	case int(C.ETERM):
		return &RecvError{Result: RecvTerminated, internalErrno: errno}
	case int(C.EFAULT):
		return &RecvError{Result: RecvInvalidHandle, internalErrno: errno}
	case int(C.ENOTSUP):
		return &RecvError{Result: RecvNotSupported, internalErrno: errno}
	default:
		return &RecvError{Result: RecvTerminated, internalErrno: errno}
	}
}

func resultCodeInt(value any) int {
	if value == nil {
		return 0
	}
	v := reflect.ValueOf(value)
	switch v.Kind() {
	case reflect.Int, reflect.Int8, reflect.Int16, reflect.Int32, reflect.Int64:
		return int(v.Int())
	case reflect.Uint, reflect.Uint8, reflect.Uint16, reflect.Uint32, reflect.Uint64, reflect.Uintptr:
		return int(v.Uint())
	default:
		return 0
	}
}
