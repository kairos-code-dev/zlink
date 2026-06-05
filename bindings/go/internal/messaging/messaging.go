// SPDX-License-Identifier: MPL-2.0

package messaging

import impl "zlink.systems/zlink/internal/native"

type (
	RoutingID              = impl.RoutingID
	Message                = impl.Message
	RequestReplyCallback   = impl.RequestReplyCallback
	RequestReplyCompletion = impl.RequestReplyCompletion
	Received               = impl.Received
	RecvPartResult         = impl.RecvPartResult
	SpotForwardResult      = impl.SpotForwardResult
	TopicMessage           = impl.TopicMessage
	SubscriptionEvent      = impl.SubscriptionEvent
)

var (
	NewRoutingID          = impl.NewRoutingID
	NewRoutingIDString    = impl.NewRoutingIDString
	NewRoutingIDUint32    = impl.NewRoutingIDUint32
	NewRoutingIDUUIDBytes = impl.NewRoutingIDUUIDBytes
	NewRoutingIDFromHex   = impl.NewRoutingIDFromHex
	NewMessage            = impl.NewMessage
	NewMessageString      = impl.NewMessageString
	NewMessageWithSize    = impl.NewMessageWithSize
)
