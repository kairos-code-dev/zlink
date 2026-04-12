// SPDX-License-Identifier: MPL-2.0

package zlink

/*
#include "zlink.h"
*/
import "C"

type SendFlags int

const (
	SendFlagsNone     SendFlags = 0
	SendFlagsDontWait SendFlags = 1
)

type RecvFlags int

const (
	RecvFlagsNone     RecvFlags = 0
	RecvFlagsDontWait RecvFlags = 1
)

type SubmitResult int

const (
	SubmitOK              SubmitResult = 0
	SubmitBackpressured   SubmitResult = 1
	SubmitNotConnected    SubmitResult = 2
	SubmitNotFound        SubmitResult = 3
	SubmitTerminated      SubmitResult = 4
	SubmitInvalidHandle   SubmitResult = 5
	SubmitInvalidArgument SubmitResult = 6
	SubmitNotSupported    SubmitResult = 7
	SubmitInvalidState    SubmitResult = 8
	SubmitThreadViolation SubmitResult = 9
	SubmitOutOfMemory     SubmitResult = 10
	SubmitSeqExhausted    SubmitResult = 11
	SubmitInternalError   SubmitResult = 12
)

type RequestResult int

const (
	RequestOK            RequestResult = 0
	RequestTimedOut      RequestResult = 101
	RequestNotFound      RequestResult = 102
	RequestTerminated    RequestResult = 103
	RequestProtocolError RequestResult = 104
)

type RecvResult int

const (
	RecvOK            RecvResult = 0
	RecvNoData        RecvResult = 201
	RecvBusy          RecvResult = 202
	RecvTerminated    RecvResult = 203
	RecvInvalidHandle RecvResult = 204
	RecvNotSupported  RecvResult = 205
)

type HandlerResult int

const (
	HandlerOK              HandlerResult = 0
	HandlerInvalidArgument HandlerResult = 301
	HandlerBusy            HandlerResult = 302
	HandlerNotSupported    HandlerResult = 303
	HandlerDeadlock        HandlerResult = 304
	HandlerInvalidHandle   HandlerResult = 305
)

type CloseResult int

const (
	CloseOK            CloseResult = 0
	CloseBusy          CloseResult = 401
	CloseShutdown      CloseResult = 402
	CloseInvalidHandle CloseResult = 403
)

type BindResult int

const (
	BindOK              BindResult = 0
	BindInvalidArgument BindResult = 501
	BindAddrInUse       BindResult = 502
	BindNotSupported    BindResult = 503
	BindInvalidHandle   BindResult = 504
)

type ConnectResult int

const (
	ConnectOK              ConnectResult = 0
	ConnectInvalidArgument ConnectResult = 601
	ConnectNotSupported    ConnectResult = 602
	ConnectInvalidHandle   ConnectResult = 603
)

type ConfigResult int

const (
	ConfigOK              ConfigResult = 0
	ConfigInvalidHandle   ConfigResult = 701
	ConfigInvalidArgument ConfigResult = 702
	ConfigNotSupported    ConfigResult = 703
)

type SpotDispatchEvent int

const (
	SpotDispatchEventSubscribeReadable SpotDispatchEvent = 1
	SpotDispatchEventRoutedReadable    SpotDispatchEvent = 2
	SpotDispatchEventTimerReadable     SpotDispatchEvent = 3
)

type Received struct {
	routingID     RoutingID
	parts         []*Message
	requestSeq    uint64
	hasRequestSeq bool
}

func (r *Received) RoutingID() RoutingID {
	if r == nil {
		return RoutingID{}
	}
	return r.routingID
}

func (r *Received) Parts() []*Message {
	if r == nil {
		return nil
	}
	return r.parts
}

func (r *Received) RequestSeq() (uint64, bool) {
	if r == nil {
		return 0, false
	}
	return r.requestSeq, r.hasRequestSeq
}

func (r *Received) SinglePartOrError() (*Message, error) {
	if r == nil {
		return nil, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	if len(r.parts) != 1 {
		return nil, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	return r.parts[0], nil
}

func (r *Received) Close() error {
	if r == nil {
		return nil
	}
	var first error
	for _, part := range r.parts {
		if err := part.Close(); err != nil && first == nil {
			first = err
		}
	}
	return first
}

type TopicMessage struct {
	routingID RoutingID
	topic     string
	parts     []*Message
}

func (t *TopicMessage) RoutingID() RoutingID {
	if t == nil {
		return RoutingID{}
	}
	return t.routingID
}

func (t *TopicMessage) Topic() string {
	if t == nil {
		return ""
	}
	return t.topic
}

func (t *TopicMessage) Parts() []*Message {
	if t == nil {
		return nil
	}
	return t.parts
}

func (t *TopicMessage) SinglePartOrError() (*Message, error) {
	if t == nil {
		return nil, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	if len(t.parts) != 1 {
		return nil, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	return t.parts[0], nil
}

func (t *TopicMessage) Close() error {
	if t == nil {
		return nil
	}
	var first error
	for _, part := range t.parts {
		if err := part.Close(); err != nil && first == nil {
			first = err
		}
	}
	return first
}

type SubscriptionEvent struct {
	routingID  RoutingID
	subscribed bool
	topic      string
}

func (s *SubscriptionEvent) RoutingID() RoutingID {
	if s == nil {
		return RoutingID{}
	}
	return s.routingID
}

func (s *SubscriptionEvent) Subscribed() bool {
	return s != nil && s.subscribed
}

func (s *SubscriptionEvent) Topic() string {
	if s == nil {
		return ""
	}
	return s.topic
}
