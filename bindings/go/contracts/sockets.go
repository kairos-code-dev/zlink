// SPDX-License-Identifier: MPL-2.0

package contracts

import impl "zlink.systems/zlink/internal/sockets"

type (
	SocketType              = impl.SocketType
	RIDDuplicatePolicy      = impl.RIDDuplicatePolicy
	SubmitRetryMode         = impl.SubmitRetryMode
	CommonSocketOptions     = impl.CommonSocketOptions
	PubSocketOptions        = impl.PubSocketOptions
	PairSocket              = impl.PairSocket
	PubSocket               = impl.PubSocket
	SubSocket               = impl.SubSocket
	DealerSocket            = impl.DealerSocket
	RouterSocket            = impl.RouterSocket
	XPubSocket              = impl.XPubSocket
	XSubSocket              = impl.XSubSocket
	StreamSocket            = impl.StreamSocket
	SendOp                  = impl.SendOp
	SendSubmitOp            = impl.SendSubmitOp
	RequestOp               = impl.RequestOp
	RequestSubmitOp         = impl.RequestSubmitOp
	RequestCallbackSubmitOp = impl.RequestCallbackSubmitOp
	ReplyOp                 = impl.ReplyOp
	ReplySubmitOp           = impl.ReplySubmitOp
	SendFlags               = impl.SendFlags
	RecvFlags               = impl.RecvFlags
	SpotDispatchEvent       = impl.SpotDispatchEvent
	SpotDispatchSubjectKind = impl.SpotDispatchSubjectKind
	SpotDispatchInfo        = impl.SpotDispatchInfo
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
	RIDDuplicateReject                      = impl.RIDDuplicateReject
	RIDDuplicateHandover                    = impl.RIDDuplicateHandover
	SubmitRetryOff                         = impl.SubmitRetryOff
	SubmitRetryLocalFailure                = impl.SubmitRetryLocalFailure
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
