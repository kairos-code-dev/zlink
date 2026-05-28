// SPDX-License-Identifier: MPL-2.0

package errors

import impl "zlink.systems/zlink/internal/native"

type (
	ZlinkError    = impl.ZlinkError
	SubmitError   = impl.SubmitError
	RequestError  = impl.RequestError
	RecvError     = impl.RecvError
	HandlerError  = impl.HandlerError
	CloseError    = impl.CloseError
	BindError     = impl.BindError
	ConnectError  = impl.ConnectError
	ConfigError   = impl.ConfigError
	SubmitResult  = impl.SubmitResult
	RequestResult = impl.RequestResult
	RecvResult    = impl.RecvResult
	HandlerResult = impl.HandlerResult
	CloseResult   = impl.CloseResult
	BindResult    = impl.BindResult
	ConnectResult = impl.ConnectResult
	ConfigResult  = impl.ConfigResult
)

const (
	SubmitOK               = impl.SubmitOK
	SubmitBackpressured    = impl.SubmitBackpressured
	SubmitNotConnected     = impl.SubmitNotConnected
	SubmitNotFound         = impl.SubmitNotFound
	SubmitTerminated       = impl.SubmitTerminated
	SubmitInvalidHandle    = impl.SubmitInvalidHandle
	SubmitInvalidArgument  = impl.SubmitInvalidArgument
	SubmitNotSupported     = impl.SubmitNotSupported
	SubmitInvalidState     = impl.SubmitInvalidState
	SubmitThreadViolation  = impl.SubmitThreadViolation
	SubmitOutOfMemory      = impl.SubmitOutOfMemory
	SubmitSeqExhausted     = impl.SubmitSeqExhausted
	SubmitInternalError    = impl.SubmitInternalError
	SubmitNotAdmitted      = impl.SubmitNotAdmitted
	RequestOK              = impl.RequestOK
	RequestTimedOut        = impl.RequestTimedOut
	RequestNotFound        = impl.RequestNotFound
	RequestTerminated      = impl.RequestTerminated
	RequestProtocolError   = impl.RequestProtocolError
	RequestInternalError   = impl.RequestInternalError
	RequestRejected        = impl.RequestRejected
	RequestConflict        = impl.RequestConflict
	RequestBusy            = impl.RequestBusy
	RequestNotConnected    = impl.RequestNotConnected
	RequestInvalidArgument = impl.RequestInvalidArgument
	RequestInvalidState    = impl.RequestInvalidState
	RequestNotSupported    = impl.RequestNotSupported
	RecvOK                 = impl.RecvOK
	RecvNoData             = impl.RecvNoData
	RecvBusy               = impl.RecvBusy
	RecvTerminated         = impl.RecvTerminated
	RecvInvalidHandle      = impl.RecvInvalidHandle
	RecvNotSupported       = impl.RecvNotSupported
	RecvInternalError      = impl.RecvInternalError
	HandlerOK              = impl.HandlerOK
	HandlerInvalidArgument = impl.HandlerInvalidArgument
	HandlerBusy            = impl.HandlerBusy
	HandlerNotSupported    = impl.HandlerNotSupported
	HandlerDeadlock        = impl.HandlerDeadlock
	HandlerInvalidHandle   = impl.HandlerInvalidHandle
	HandlerInternalError   = impl.HandlerInternalError
	CloseOK                = impl.CloseOK
	CloseBusy              = impl.CloseBusy
	CloseShutdown          = impl.CloseShutdown
	CloseInvalidHandle     = impl.CloseInvalidHandle
	CloseInternalError     = impl.CloseInternalError
	BindOK                 = impl.BindOK
	BindInvalidArgument    = impl.BindInvalidArgument
	BindAddrInUse          = impl.BindAddrInUse
	BindNotSupported       = impl.BindNotSupported
	BindInvalidHandle      = impl.BindInvalidHandle
	BindInternalError      = impl.BindInternalError
	ConnectOK              = impl.ConnectOK
	ConnectInvalidArgument = impl.ConnectInvalidArgument
	ConnectNotSupported    = impl.ConnectNotSupported
	ConnectInvalidHandle   = impl.ConnectInvalidHandle
	ConnectInternalError   = impl.ConnectInternalError
	ConnectNotFound        = impl.ConnectNotFound
	ConnectConflict        = impl.ConnectConflict
	ConnectBusy            = impl.ConnectBusy
	ConfigOK               = impl.ConfigOK
	ConfigInvalidHandle    = impl.ConfigInvalidHandle
	ConfigInvalidArgument  = impl.ConfigInvalidArgument
	ConfigNotSupported     = impl.ConfigNotSupported
	ConfigInternalError    = impl.ConfigInternalError
	ConfigInvalidState     = impl.ConfigInvalidState
	ConfigNotFound         = impl.ConfigNotFound
)
