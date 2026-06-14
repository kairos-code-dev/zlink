// SPDX-License-Identifier: MPL-2.0

package native

/*
#include "zlink.h"
*/
import "C"

import (
	"errors"
	"fmt"
	"reflect"
	"syscall"
)

type ZlinkError interface {
	error
	Code() int
	NativeErrno() int
}

type errorDetails struct {
	NativeErrno int
}

type SubmitError struct {
	Result SubmitResult
	errorDetails
	nativeErrno int
}

type RequestError struct {
	Result RequestResult
	errorDetails
	nativeErrno int
}

type RecvError struct {
	Result RecvResult
	errorDetails
	nativeErrno int
}

// isNoData reports whether err is a RecvError carrying the no-data result —
// the signal a non-blocking receive raises when nothing was available.
func isNoData(err error) bool {
	var recvErr *RecvError
	return errors.As(err, &recvErr) && recvErr.Result == RecvNoData
}

type HandlerError struct {
	Result HandlerResult
	errorDetails
	nativeErrno int
}

type CloseError struct {
	Result CloseResult
	errorDetails
	nativeErrno int
}

type BindError struct {
	Result BindResult
	errorDetails
	nativeErrno int
}

type ConnectError struct {
	Result ConnectResult
	errorDetails
	nativeErrno int
}

type ConfigError struct {
	Result ConfigResult
	errorDetails
	nativeErrno int
}

func (e *SubmitError) Error() string {
	return formatError("submit", int(e.Result), e.ensureNativeErrno())
}

func (e *SubmitError) Code() int { return int(e.Result) }

func (e *SubmitError) NativeErrno() int { return e.ensureNativeErrno() }

func (e *SubmitError) Unwrap() error { return errnoToError(e.ensureNativeErrno()) }

func (e *RequestError) Error() string {
	return formatError("request", int(e.Result), e.ensureNativeErrno())
}

func (e *RequestError) Code() int { return int(e.Result) }

func (e *RequestError) NativeErrno() int { return e.ensureNativeErrno() }

func (e *RequestError) Unwrap() error { return errnoToError(e.ensureNativeErrno()) }

func (e *RecvError) Error() string {
	return formatError("recv", int(e.Result), e.ensureNativeErrno())
}

func (e *RecvError) Code() int { return int(e.Result) }

func (e *RecvError) NativeErrno() int { return e.ensureNativeErrno() }

func (e *RecvError) Unwrap() error { return errnoToError(e.ensureNativeErrno()) }

func (e *HandlerError) Error() string {
	return formatError("handler", int(e.Result), e.ensureNativeErrno())
}

func (e *HandlerError) Code() int { return int(e.Result) }

func (e *HandlerError) NativeErrno() int { return e.ensureNativeErrno() }

func (e *HandlerError) Unwrap() error { return errnoToError(e.ensureNativeErrno()) }

func (e *CloseError) Error() string {
	return formatError("close", int(e.Result), e.ensureNativeErrno())
}

func (e *CloseError) Code() int { return int(e.Result) }

func (e *CloseError) NativeErrno() int { return e.ensureNativeErrno() }

func (e *CloseError) Unwrap() error { return errnoToError(e.ensureNativeErrno()) }

func (e *BindError) Error() string {
	return formatError("bind", int(e.Result), e.ensureNativeErrno())
}

func (e *BindError) Code() int { return int(e.Result) }

func (e *BindError) NativeErrno() int { return e.ensureNativeErrno() }

func (e *BindError) Unwrap() error { return errnoToError(e.ensureNativeErrno()) }

func (e *ConnectError) Error() string {
	return formatError("connect", int(e.Result), e.ensureNativeErrno())
}

func (e *ConnectError) Code() int { return int(e.Result) }

func (e *ConnectError) NativeErrno() int { return e.ensureNativeErrno() }

func (e *ConnectError) Unwrap() error { return errnoToError(e.ensureNativeErrno()) }

func (e *ConfigError) Error() string {
	return formatError("config", int(e.Result), e.ensureNativeErrno())
}

func (e *ConfigError) Code() int { return int(e.Result) }

func (e *ConfigError) NativeErrno() int { return e.ensureNativeErrno() }

func (e *ConfigError) Unwrap() error { return errnoToError(e.ensureNativeErrno()) }

func (e *SubmitError) ensureNativeErrno() int {
	if e == nil {
		return 0
	}
	if e.errorDetails.NativeErrno == 0 && e.nativeErrno != 0 {
		e.errorDetails.NativeErrno = e.nativeErrno
	}
	return e.errorDetails.NativeErrno
}

func (e *RequestError) ensureNativeErrno() int {
	if e == nil {
		return 0
	}
	if e.errorDetails.NativeErrno == 0 && e.nativeErrno != 0 {
		e.errorDetails.NativeErrno = e.nativeErrno
	}
	return e.errorDetails.NativeErrno
}

func (e *RecvError) ensureNativeErrno() int {
	if e == nil {
		return 0
	}
	if e.errorDetails.NativeErrno == 0 && e.nativeErrno != 0 {
		e.errorDetails.NativeErrno = e.nativeErrno
	}
	return e.errorDetails.NativeErrno
}

func (e *HandlerError) ensureNativeErrno() int {
	if e == nil {
		return 0
	}
	if e.errorDetails.NativeErrno == 0 && e.nativeErrno != 0 {
		e.errorDetails.NativeErrno = e.nativeErrno
	}
	return e.errorDetails.NativeErrno
}

func (e *CloseError) ensureNativeErrno() int {
	if e == nil {
		return 0
	}
	if e.errorDetails.NativeErrno == 0 && e.nativeErrno != 0 {
		e.errorDetails.NativeErrno = e.nativeErrno
	}
	return e.errorDetails.NativeErrno
}

func (e *BindError) ensureNativeErrno() int {
	if e == nil {
		return 0
	}
	if e.errorDetails.NativeErrno == 0 && e.nativeErrno != 0 {
		e.errorDetails.NativeErrno = e.nativeErrno
	}
	return e.errorDetails.NativeErrno
}

func (e *ConnectError) ensureNativeErrno() int {
	if e == nil {
		return 0
	}
	if e.errorDetails.NativeErrno == 0 && e.nativeErrno != 0 {
		e.errorDetails.NativeErrno = e.nativeErrno
	}
	return e.errorDetails.NativeErrno
}

func (e *ConfigError) ensureNativeErrno() int {
	if e == nil {
		return 0
	}
	if e.errorDetails.NativeErrno == 0 && e.nativeErrno != 0 {
		e.errorDetails.NativeErrno = e.nativeErrno
	}
	return e.errorDetails.NativeErrno
}

func formatError(kind string, code int, nativeErrno int) string {
	if nativeErrno != 0 {
		return fmt.Sprintf("%s error (%d): %s", kind, code, C.GoString(C.zlink_strerror(C.int(nativeErrno))))
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
		return int(C.ENOENT)
	case RequestTerminated:
		return int(C.ETERM)
	case RequestInternalError:
		return int(C.EIO)
	case RequestRejected:
		return int(C.ECONNREFUSED)
	case RequestConflict, RequestInvalidArgument, RequestInvalidState:
		return int(C.EINVAL)
	case RequestBusy:
		return int(C.EBUSY)
	case RequestNotConnected:
		return int(C.ENOTCONN)
	case RequestNotSupported:
		return int(C.ENOTSUP)
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
	return &SubmitError{Result: resultCode, errorDetails: errorDetails{NativeErrno: errno}, nativeErrno: errno}
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
	return &RequestError{Result: resultCode, errorDetails: errorDetails{NativeErrno: errno}, nativeErrno: errno}
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
	return &RecvError{Result: resultCode, errorDetails: errorDetails{NativeErrno: errno}, nativeErrno: errno}
}

func handlerErrorFromResult(result any) error {
	if resultCodeInt(result) == int(HandlerOK) {
		return nil
	}
	errno := errnoOrIO()
	return &HandlerError{Result: HandlerResult(resultCodeInt(result)), errorDetails: errorDetails{NativeErrno: errno}, nativeErrno: errno}
}

func closeErrorFromResult(result any) error {
	if resultCodeInt(result) == int(CloseOK) {
		return nil
	}
	errno := errnoOrIO()
	return &CloseError{Result: CloseResult(resultCodeInt(result)), errorDetails: errorDetails{NativeErrno: errno}, nativeErrno: errno}
}

func bindErrorFromResult(result any) error {
	if resultCodeInt(result) == int(BindOK) {
		return nil
	}
	errno := errnoOrIO()
	return &BindError{Result: BindResult(resultCodeInt(result)), errorDetails: errorDetails{NativeErrno: errno}, nativeErrno: errno}
}

func connectErrorFromResult(result any) error {
	if resultCodeInt(result) == int(ConnectOK) {
		return nil
	}
	errno := errnoOrIO()
	return &ConnectError{Result: ConnectResult(resultCodeInt(result)), errorDetails: errorDetails{NativeErrno: errno}, nativeErrno: errno}
}

func configErrorFromResult(result any) error {
	if resultCodeInt(result) == int(ConfigOK) {
		return nil
	}
	errno := errnoOrIO()
	return &ConfigError{Result: ConfigResult(resultCodeInt(result)), errorDetails: errorDetails{NativeErrno: errno}, nativeErrno: errno}
}

func validationError(format string, args ...any) error {
	return &ConfigError{Result: ConfigInvalidArgument, nativeErrno: int(C.EINVAL)}
}

func stateError(format string, args ...any) error {
	return &ConfigError{Result: ConfigInvalidHandle, nativeErrno: int(C.EFAULT)}
}

func configErrorFromErrno(errno int) error {
	switch errno {
	case 0:
		return nil
	case int(C.EFAULT), int(C.ENOTSOCK), int(C.EDESTADDRREQ):
		return &ConfigError{Result: ConfigInvalidHandle, nativeErrno: errno}
	case int(C.EINVAL), int(C.EMSGSIZE), int(C.EAFNOSUPPORT), int(C.ENAMETOOLONG):
		return &ConfigError{Result: ConfigInvalidArgument, nativeErrno: errno}
	case int(C.ENOTSUP):
		return &ConfigError{Result: ConfigNotSupported, nativeErrno: errno}
	default:
		return &ConfigError{Result: ConfigInvalidArgument, nativeErrno: errno}
	}
}

func closeErrorFromErrno(errno int) error {
	switch errno {
	case 0:
		return nil
	case int(C.EBUSY):
		return &CloseError{Result: CloseBusy, nativeErrno: errno}
	case int(C.ESHUTDOWN):
		return &CloseError{Result: CloseShutdown, nativeErrno: errno}
	case int(C.EFAULT):
		return &CloseError{Result: CloseInvalidHandle, nativeErrno: errno}
	default:
		return &CloseError{Result: CloseInvalidHandle, nativeErrno: errno}
	}
}

func bindErrorFromErrno(errno int) error {
	switch errno {
	case 0:
		return nil
	case int(C.EINVAL):
		return &BindError{Result: BindInvalidArgument, nativeErrno: errno}
	case int(C.EADDRINUSE):
		return &BindError{Result: BindAddrInUse, nativeErrno: errno}
	case int(C.ENOTSUP):
		return &BindError{Result: BindNotSupported, nativeErrno: errno}
	default:
		return &BindError{Result: BindInvalidHandle, nativeErrno: errno}
	}
}

func connectErrorFromErrno(errno int) error {
	switch errno {
	case 0:
		return nil
	case int(C.EINVAL):
		return &ConnectError{Result: ConnectInvalidArgument, nativeErrno: errno}
	case int(C.ENOTSUP):
		return &ConnectError{Result: ConnectNotSupported, nativeErrno: errno}
	default:
		return &ConnectError{Result: ConnectInvalidHandle, nativeErrno: errno}
	}
}

func recvErrorFromErrno(errno int) error {
	switch errno {
	case 0:
		return nil
	case int(C.EAGAIN):
		return &RecvError{Result: RecvNoData, nativeErrno: errno}
	case int(C.EBUSY):
		return &RecvError{Result: RecvBusy, nativeErrno: errno}
	case int(C.ETERM):
		return &RecvError{Result: RecvTerminated, nativeErrno: errno}
	case int(C.EFAULT):
		return &RecvError{Result: RecvInvalidHandle, nativeErrno: errno}
	case int(C.ENOTSUP):
		return &RecvError{Result: RecvNotSupported, nativeErrno: errno}
	case int(C.EINTR):
		return &RecvError{Result: RecvInternalError, nativeErrno: errno}
	default:
		return &RecvError{Result: RecvTerminated, nativeErrno: errno}
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
