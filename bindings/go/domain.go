// SPDX-License-Identifier: MPL-2.0

package zlink

/*
#include "zlink.h"
*/
import "C"

import "unsafe"

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
	SubmitNotAdmitted     SubmitResult = 13
)

type RequestResult int

const (
	RequestOK            RequestResult = 0
	RequestTimedOut      RequestResult = 101
	RequestNotFound      RequestResult = 102
	RequestTerminated    RequestResult = 103
	RequestProtocolError RequestResult = 104
	RequestInternalError RequestResult = 105
	RequestRejected      RequestResult = 106
	RequestConflict      RequestResult = 107
	RequestBusy          RequestResult = 108
	RequestNotConnected  RequestResult = 109
	RequestInvalidArg    RequestResult = 110
	RequestInvalidState  RequestResult = 111
	RequestNotSupported  RequestResult = 112
)

type RecvResult int

const (
	RecvOK            RecvResult = 0
	RecvNoData        RecvResult = 201
	RecvBusy          RecvResult = 202
	RecvTerminated    RecvResult = 203
	RecvInvalidHandle RecvResult = 204
	RecvNotSupported  RecvResult = 205
	RecvInternalError RecvResult = 206
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
	SpotDispatchEventSubscribeReadable    SpotDispatchEvent = 1
	SpotDispatchEventRoutedReadable       SpotDispatchEvent = 2
	SpotDispatchEventTimerReadable        SpotDispatchEvent = 3
	SpotDispatchEventChannelReplyReadable SpotDispatchEvent = 4
	SpotDispatchEventActorReadable        SpotDispatchEvent = 5
	SpotDispatchEventActorJoinReadable    SpotDispatchEvent = 6
)

type SpotDispatchSubjectKind int

const (
	SpotDispatchSubjectSpot          SpotDispatchSubjectKind = 1
	SpotDispatchSubjectTimer         SpotDispatchSubjectKind = 2
	SpotDispatchSubjectChannelDealer SpotDispatchSubjectKind = 3
	SpotDispatchSubjectActor         SpotDispatchSubjectKind = 4
)

type SpotDispatchInfo struct {
	Event       SpotDispatchEvent
	SubjectKind SpotDispatchSubjectKind
	Subject     unsafe.Pointer
}

func (i SpotDispatchInfo) RecvActorPart(flags RecvFlags) (*ActorPart, error) {
	if i.Event != SpotDispatchEventActorReadable || i.SubjectKind != SpotDispatchSubjectActor {
		return nil, &RecvError{Result: RecvNotSupported, internalErrno: int(C.ENOTSUP)}
	}
	return recvActorPartFromHandle(i.Subject, flags)
}

type Received struct {
	routingID     RoutingID
	spotRID       RoutingID
	parts         []*Message
	requestSeq    uint64
	hasRequestSeq bool
	reply         func(SendFlags, []*Message) error
}

func (r *Received) RoutingID() RoutingID {
	if r == nil {
		return RoutingID{}
	}
	return r.routingID
}

func (r *Received) HasRoutingID() bool {
	return r != nil && r.routingID.Size() > 0
}

func (r *Received) SpotRID() RoutingID {
	if r == nil {
		return RoutingID{}
	}
	return r.spotRID
}

func (r *Received) HasSpotRID() bool {
	return r != nil && r.spotRID.Size() > 0
}

func (r *Received) Parts() []*Message {
	if r == nil {
		return nil
	}
	return r.parts
}

func (r *Received) RequestSeq() uint64 {
	if r == nil {
		return 0
	}
	return r.requestSeq
}

func (r *Received) HasRequestSeq() bool {
	return r != nil && r.hasRequestSeq
}

func (r *Received) IsSinglePart() bool {
	return r != nil && len(r.parts) == 1
}

func (r *Received) FirstPart() (*Message, error) {
	if r == nil {
		return nil, &RecvError{Result: RecvInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	if len(r.parts) == 0 {
		return nil, &RecvError{Result: RecvNotSupported, internalErrno: int(C.EINVAL)}
	}
	return r.parts[0], nil
}

func (r *Received) SinglePartOrError() (*Message, error) {
	if r == nil {
		return nil, &RecvError{Result: RecvInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	if len(r.parts) != 1 {
		return nil, &RecvError{Result: RecvNotSupported, internalErrno: int(C.EINVAL)}
	}
	return r.parts[0], nil
}

func (r *Received) Reply(parts []*Message) error {
	return r.ReplyWithFlags(parts, SendFlagsNone)
}

func (r *Received) ReplyWithFlags(parts []*Message, flags SendFlags) error {
	if r == nil {
		return &SubmitError{Result: SubmitInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	if !r.hasRequestSeq || r.reply == nil {
		return &SubmitError{Result: SubmitInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	if err := validateReplyFlags(flags); err != nil {
		return err
	}
	return r.reply(flags, parts)
}

func validateReplyFlags(flags SendFlags) error {
	if flags == SendFlagsNone {
		return nil
	}
	return &SubmitError{Result: SubmitNotSupported, internalErrno: int(C.ENOTSUP)}
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
	routingID   RoutingID
	serviceName string
	topic       string
	parts       []*Message
}

func (t *TopicMessage) RoutingID() RoutingID {
	if t == nil {
		return RoutingID{}
	}
	return t.routingID
}

func (t *TopicMessage) HasRoutingID() bool {
	return t != nil && t.routingID.Size() > 0
}

func (t *TopicMessage) ServiceName() string {
	if t == nil {
		return ""
	}
	return t.serviceName
}

func (t *TopicMessage) HasServiceName() bool {
	return t != nil && t.serviceName != ""
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

func (t *TopicMessage) IsSinglePart() bool {
	return t != nil && len(t.parts) == 1
}

func (t *TopicMessage) FirstPart() (*Message, error) {
	if t == nil {
		return nil, &RecvError{Result: RecvInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	if len(t.parts) == 0 {
		return nil, &RecvError{Result: RecvNotSupported, internalErrno: int(C.EINVAL)}
	}
	return t.parts[0], nil
}

func (t *TopicMessage) SinglePartOrError() (*Message, error) {
	if t == nil {
		return nil, &RecvError{Result: RecvInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	if len(t.parts) != 1 {
		return nil, &RecvError{Result: RecvNotSupported, internalErrno: int(C.EINVAL)}
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
	routingID   RoutingID
	serviceName string
	subscribed  bool
	topic       string
}

func (s SubscriptionEvent) RoutingID() RoutingID {
	return s.routingID
}

func (s SubscriptionEvent) HasRoutingID() bool {
	return s.routingID.Size() > 0
}

func (s SubscriptionEvent) ServiceName() string {
	return s.serviceName
}

func (s SubscriptionEvent) HasServiceName() bool {
	return s.serviceName != ""
}

func (s SubscriptionEvent) Subscribed() bool {
	return s.subscribed
}

func (s SubscriptionEvent) Topic() string {
	return s.topic
}

func receivedReplyToRouter(reply func(RoutingID, uint64, SendFlags, ...*Message) error, routingID RoutingID, requestSeq uint64) func(SendFlags, []*Message) error {
	return func(flags SendFlags, parts []*Message) error {
		return reply(routingID, requestSeq, flags, parts...)
	}
}

func receivedReplyToSpotPeer(socket interface {
	ReplyToSpot(RoutingID, RoutingID, uint64, SendFlags, ...*Message) error
}, nodeRID RoutingID, spotRID RoutingID, requestSeq uint64) func(SendFlags, []*Message) error {
	return func(flags SendFlags, parts []*Message) error {
		return socket.ReplyToSpot(nodeRID, spotRID, requestSeq, flags, parts...)
	}
}

func receivedReplyToSpotRouter(socket interface {
	ReplyToRouter(RoutingID, uint64, SendFlags, ...*Message) error
}, peerRID RoutingID, requestSeq uint64) func(SendFlags, []*Message) error {
	return func(flags SendFlags, parts []*Message) error {
		return socket.ReplyToRouter(peerRID, requestSeq, flags, parts...)
	}
}

func receivedReplyToSpot(socket *Spot, routingID RoutingID, spotRID RoutingID, requestSeq uint64) func(SendFlags, []*Message) error {
	if spotRID.Size() == 0 {
		return receivedReplyToSpotRouter(socket, routingID, requestSeq)
	}
	return receivedReplyToSpotPeer(socket, routingID, spotRID, requestSeq)
}

func monitorHasRoutingID(routingID RoutingID) bool {
	return routingID.Size() > 0
}

func serviceEntryHasRoutingID(routingID RoutingID) bool {
	return routingID.Size() > 0
}

func spotNodeHasRoutingID(routingID RoutingID) bool {
	return routingID.Size() > 0
}

func emptyRoutingID() RoutingID {
	return RoutingID{}
}
