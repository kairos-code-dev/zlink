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

type errorDetails struct {
	InternalErrno int
}

type SubmitError struct {
	Result SubmitResult
	errorDetails
	internalErrno int
}

type RequestError struct {
	Result RequestResult
	errorDetails
	internalErrno int
}

type RecvError struct {
	Result RecvResult
	errorDetails
	internalErrno int
}

type HandlerError struct {
	Result HandlerResult
	errorDetails
	internalErrno int
}

type CloseError struct {
	Result CloseResult
	errorDetails
	internalErrno int
}

type BindError struct {
	Result BindResult
	errorDetails
	internalErrno int
}

type ConnectError struct {
	Result ConnectResult
	errorDetails
	internalErrno int
}

type ConfigError struct {
	Result ConfigResult
	errorDetails
	internalErrno int
}

func (e *SubmitError) Error() string {
	return formatError("submit", int(e.Result), e.ensureInternalErrno())
}

func (e *SubmitError) Code() int { return int(e.Result) }

func (e *SubmitError) InternalErrno() int { return e.ensureInternalErrno() }

func (e *SubmitError) Unwrap() error { return errnoToError(e.ensureInternalErrno()) }

func (e *RequestError) Error() string {
	return formatError("request", int(e.Result), e.ensureInternalErrno())
}

func (e *RequestError) Code() int { return int(e.Result) }

func (e *RequestError) InternalErrno() int { return e.ensureInternalErrno() }

func (e *RequestError) Unwrap() error { return errnoToError(e.ensureInternalErrno()) }

func (e *RecvError) Error() string {
	return formatError("recv", int(e.Result), e.ensureInternalErrno())
}

func (e *RecvError) Code() int { return int(e.Result) }

func (e *RecvError) InternalErrno() int { return e.ensureInternalErrno() }

func (e *RecvError) Unwrap() error { return errnoToError(e.ensureInternalErrno()) }

func (e *HandlerError) Error() string {
	return formatError("handler", int(e.Result), e.ensureInternalErrno())
}

func (e *HandlerError) Code() int { return int(e.Result) }

func (e *HandlerError) InternalErrno() int { return e.ensureInternalErrno() }

func (e *HandlerError) Unwrap() error { return errnoToError(e.ensureInternalErrno()) }

func (e *CloseError) Error() string {
	return formatError("close", int(e.Result), e.ensureInternalErrno())
}

func (e *CloseError) Code() int { return int(e.Result) }

func (e *CloseError) InternalErrno() int { return e.ensureInternalErrno() }

func (e *CloseError) Unwrap() error { return errnoToError(e.ensureInternalErrno()) }

func (e *BindError) Error() string {
	return formatError("bind", int(e.Result), e.ensureInternalErrno())
}

func (e *BindError) Code() int { return int(e.Result) }

func (e *BindError) InternalErrno() int { return e.ensureInternalErrno() }

func (e *BindError) Unwrap() error { return errnoToError(e.ensureInternalErrno()) }

func (e *ConnectError) Error() string {
	return formatError("connect", int(e.Result), e.ensureInternalErrno())
}

func (e *ConnectError) Code() int { return int(e.Result) }

func (e *ConnectError) InternalErrno() int { return e.ensureInternalErrno() }

func (e *ConnectError) Unwrap() error { return errnoToError(e.ensureInternalErrno()) }

func (e *ConfigError) Error() string {
	return formatError("config", int(e.Result), e.ensureInternalErrno())
}

func (e *ConfigError) Code() int { return int(e.Result) }

func (e *ConfigError) InternalErrno() int { return e.ensureInternalErrno() }

func (e *ConfigError) Unwrap() error { return errnoToError(e.ensureInternalErrno()) }

func (e *SubmitError) ensureInternalErrno() int {
	if e == nil {
		return 0
	}
	if e.errorDetails.InternalErrno == 0 && e.internalErrno != 0 {
		e.errorDetails.InternalErrno = e.internalErrno
	}
	return e.errorDetails.InternalErrno
}

func (e *RequestError) ensureInternalErrno() int {
	if e == nil {
		return 0
	}
	if e.errorDetails.InternalErrno == 0 && e.internalErrno != 0 {
		e.errorDetails.InternalErrno = e.internalErrno
	}
	return e.errorDetails.InternalErrno
}

func (e *RecvError) ensureInternalErrno() int {
	if e == nil {
		return 0
	}
	if e.errorDetails.InternalErrno == 0 && e.internalErrno != 0 {
		e.errorDetails.InternalErrno = e.internalErrno
	}
	return e.errorDetails.InternalErrno
}

func (e *HandlerError) ensureInternalErrno() int {
	if e == nil {
		return 0
	}
	if e.errorDetails.InternalErrno == 0 && e.internalErrno != 0 {
		e.errorDetails.InternalErrno = e.internalErrno
	}
	return e.errorDetails.InternalErrno
}

func (e *CloseError) ensureInternalErrno() int {
	if e == nil {
		return 0
	}
	if e.errorDetails.InternalErrno == 0 && e.internalErrno != 0 {
		e.errorDetails.InternalErrno = e.internalErrno
	}
	return e.errorDetails.InternalErrno
}

func (e *BindError) ensureInternalErrno() int {
	if e == nil {
		return 0
	}
	if e.errorDetails.InternalErrno == 0 && e.internalErrno != 0 {
		e.errorDetails.InternalErrno = e.internalErrno
	}
	return e.errorDetails.InternalErrno
}

func (e *ConnectError) ensureInternalErrno() int {
	if e == nil {
		return 0
	}
	if e.errorDetails.InternalErrno == 0 && e.internalErrno != 0 {
		e.errorDetails.InternalErrno = e.internalErrno
	}
	return e.errorDetails.InternalErrno
}

func (e *ConfigError) ensureInternalErrno() int {
	if e == nil {
		return 0
	}
	if e.errorDetails.InternalErrno == 0 && e.internalErrno != 0 {
		e.errorDetails.InternalErrno = e.internalErrno
	}
	return e.errorDetails.InternalErrno
}

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

func fallbackSubmitErrno(result SubmitResult) int {
	switch result {
	case SubmitBackpressured, SubmitNotAdmitted:
		return int(C.EAGAIN)
	case SubmitNotConnected, SubmitNotFound:
		return int(C.ENOTCONN)
	case SubmitTerminated:
		return int(C.ETERM)
	case SubmitInvalidHandle:
		return int(C.EFAULT)
	case SubmitInvalidArgument:
		return int(C.EINVAL)
	case SubmitNotSupported:
		return int(C.ENOTSUP)
	case SubmitInvalidState, SubmitThreadViolation:
		return int(C.EBUSY)
	case SubmitOutOfMemory:
		return int(C.ENOMEM)
	default:
		return int(C.EIO)
	}
}

func fallbackRequestErrno(result RequestResult) int {
	switch result {
	case RequestTimedOut:
		return int(C.ETIMEDOUT)
	case RequestNotFound:
		return int(C.ENOTCONN)
	case RequestTerminated:
		return int(C.ETERM)
	default:
		return int(C.EPROTO)
	}
}

func fallbackRecvErrno(result RecvResult) int {
	switch result {
	case RecvNoData:
		return int(C.EAGAIN)
	case RecvBusy:
		return int(C.EBUSY)
	case RecvTerminated:
		return int(C.ETERM)
	case RecvInvalidHandle:
		return int(C.EFAULT)
	case RecvNotSupported:
		return int(C.ENOTSUP)
	case RecvInternalError:
		return int(C.EINTR)
	default:
		return int(C.EIO)
	}
}

func submitErrorFromResult(result any) error {
	resultCode := SubmitResult(resultCodeInt(result))
	if resultCode == SubmitOK {
		return nil
	}
	errno := currentErrno()
	if errno == 0 {
		errno = fallbackSubmitErrno(resultCode)
	}
	return &SubmitError{Result: resultCode, errorDetails: errorDetails{InternalErrno: errno}, internalErrno: errno}
}

func requestErrorFromResult(result any) error {
	resultCode := RequestResult(resultCodeInt(result))
	if resultCode == RequestOK {
		return nil
	}
	errno := currentErrno()
	if errno == 0 {
		errno = fallbackRequestErrno(resultCode)
	}
	return &RequestError{Result: resultCode, errorDetails: errorDetails{InternalErrno: errno}, internalErrno: errno}
}

func recvErrorFromResult(result any) error {
	resultCode := RecvResult(resultCodeInt(result))
	if resultCode == RecvOK {
		return nil
	}
	errno := currentErrno()
	if errno == 0 {
		errno = fallbackRecvErrno(resultCode)
	}
	return &RecvError{Result: resultCode, errorDetails: errorDetails{InternalErrno: errno}, internalErrno: errno}
}

func handlerErrorFromResult(result any) error {
	if resultCodeInt(result) == int(HandlerOK) {
		return nil
	}
	errno := errnoOrIO()
	return &HandlerError{Result: HandlerResult(resultCodeInt(result)), errorDetails: errorDetails{InternalErrno: errno}, internalErrno: errno}
}

func closeErrorFromResult(result any) error {
	if resultCodeInt(result) == int(CloseOK) {
		return nil
	}
	errno := errnoOrIO()
	return &CloseError{Result: CloseResult(resultCodeInt(result)), errorDetails: errorDetails{InternalErrno: errno}, internalErrno: errno}
}

func bindErrorFromResult(result any) error {
	if resultCodeInt(result) == int(BindOK) {
		return nil
	}
	errno := errnoOrIO()
	return &BindError{Result: BindResult(resultCodeInt(result)), errorDetails: errorDetails{InternalErrno: errno}, internalErrno: errno}
}

func connectErrorFromResult(result any) error {
	if resultCodeInt(result) == int(ConnectOK) {
		return nil
	}
	errno := errnoOrIO()
	return &ConnectError{Result: ConnectResult(resultCodeInt(result)), errorDetails: errorDetails{InternalErrno: errno}, internalErrno: errno}
}

func configErrorFromResult(result any) error {
	if resultCodeInt(result) == int(ConfigOK) {
		return nil
	}
	errno := errnoOrIO()
	return &ConfigError{Result: ConfigResult(resultCodeInt(result)), errorDetails: errorDetails{InternalErrno: errno}, internalErrno: errno}
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
	case int(C.EINTR):
		return &RecvError{Result: RecvInternalError, internalErrno: errno}
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
