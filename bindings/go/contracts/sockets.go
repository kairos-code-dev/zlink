// SPDX-License-Identifier: MPL-2.0

package contracts

import impl "zlink.systems/zlink/internal/sockets"

type (
	// SocketType identifies a socket's messaging pattern.
	SocketType = impl.SocketType
	// RidDuplicatePolicy determines how a socket reacts to a peer that reuses an existing routing id.
	RidDuplicatePolicy = impl.RidDuplicatePolicy
	// SubmitRetryMode determines whether a failed submit is retried.
	SubmitRetryMode = impl.SubmitRetryMode
	// CommonSocketOptions is the typed facade over the socket options shared by every socket type.
	CommonSocketOptions = impl.CommonSocketOptions
	// PubSocketOptions is the typed facade over PUB/XPUB-specific socket options.
	PubSocketOptions = impl.PubSocketOptions
	// PairSocket is an exclusive one-to-one peering with no routing.
	PairSocket = impl.PairSocket
	// PubSocket is a topic-filtered publisher that drops messages with no matching subscriber.
	PubSocket = impl.PubSocket
	// SubSocket is a receive-only subscriber filtered by its subscriptions.
	SubSocket = impl.SubSocket
	// DealerSocket load-balances sends across its connected peers and can issue routed requests.
	DealerSocket = impl.DealerSocket
	// RouterSocket routes messages to peers addressed by routing id; the request/reply server side.
	RouterSocket = impl.RouterSocket
	// XPubSocket is like PubSocket but also surfaces subscriber subscription events.
	XPubSocket = impl.XPubSocket
	// XSubSocket is a subscriber whose subscriptions are carried as messages.
	XSubSocket = impl.XSubSocket
	// StreamSocket exchanges framed packets with raw TCP peers and can host actor gateways.
	StreamSocket = impl.StreamSocket
	// SendOp builds a multipart send; submitting consumes the added parts.
	SendOp = impl.SendOp
	// SendSubmitOp accepts further parts, flags, and the terminal submit of a send.
	SendSubmitOp = impl.SendSubmitOp
	// RequestOp builds a request; submitting consumes the parts and awaits a reply.
	RequestOp = impl.RequestOp
	// RequestSubmitOp accepts further parts, timeout, flags, and the terminal submit of a request.
	RequestSubmitOp = impl.RequestSubmitOp
	// RequestCallbackSubmitOp is the callback-submission stage of a request.
	RequestCallbackSubmitOp = impl.RequestCallbackSubmitOp
	// ReplyOp builds a reply; submitting consumes the parts.
	ReplyOp = impl.ReplyOp
	// ReplySubmitOp accepts further parts, flags, and the terminal submit of a reply.
	ReplySubmitOp = impl.ReplySubmitOp
	// SendFlags modify send behavior; DontWait reports back-pressure instead of blocking.
	SendFlags = impl.SendFlags
	// RecvFlags modify receive behavior; DontWait returns instead of blocking when no message is available.
	RecvFlags = impl.RecvFlags
	// SpotDispatchEvent is the kind of readable event surfaced by a spot dispatch.
	SpotDispatchEvent = impl.SpotDispatchEvent
	// SpotDispatchSubjectKind is what kind of subject a spot dispatch event concerns.
	SpotDispatchSubjectKind = impl.SpotDispatchSubjectKind
	// SpotDispatchInfo is the event and context passed to a spot dispatch callback.
	SpotDispatchInfo = impl.SpotDispatchInfo
)

const (
	SocketTypeAny                           = impl.SocketTypeAny
	SocketTypePair                          = impl.SocketTypePair
	SocketTypePub                           = impl.SocketTypePub
	SocketTypeSub                           = impl.SocketTypeSub
	SocketTypeDealer                        = impl.SocketTypeDealer
	SocketTypeRouter                        = impl.SocketTypeRouter
	SocketTypeXPub                          = impl.SocketTypeXPub
	SocketTypeXSub                          = impl.SocketTypeXSub
	SocketTypeStream                        = impl.SocketTypeStream
	RidDuplicateReject                      = impl.RidDuplicateReject
	RidDuplicateHandover                    = impl.RidDuplicateHandover
	SubmitRetryOff                          = impl.SubmitRetryOff
	SubmitRetryLocalFailure                 = impl.SubmitRetryLocalFailure
	SendFlagsNone                           = impl.SendFlagsNone
	SendFlagsDontWait                       = impl.SendFlagsDontWait
	RecvFlagsNone                           = impl.RecvFlagsNone
	RecvFlagsDontWait                       = impl.RecvFlagsDontWait
	SpotDispatchEventSubscribeReadable      = impl.SpotDispatchEventSubscribeReadable
	SpotDispatchEventRoutedReadable         = impl.SpotDispatchEventRoutedReadable
	SpotDispatchEventTimerReadable          = impl.SpotDispatchEventTimerReadable
	SpotDispatchEventChannelReplyReadable   = impl.SpotDispatchEventChannelReplyReadable
	SpotDispatchEventActorReadable          = impl.SpotDispatchEventActorReadable
	SpotDispatchEventActorJoinReadable      = impl.SpotDispatchEventActorJoinReadable
	SpotDispatchEventActorLifecycleReadable = impl.SpotDispatchEventActorLifecycleReadable
	SpotDispatchSubjectSpot                 = impl.SpotDispatchSubjectSpot
	SpotDispatchSubjectTimer                = impl.SpotDispatchSubjectTimer
	SpotDispatchSubjectChannelDealer        = impl.SpotDispatchSubjectChannelDealer
	SpotDispatchSubjectActor                = impl.SpotDispatchSubjectActor
)
