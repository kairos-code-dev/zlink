// SPDX-License-Identifier: MPL-2.0

package contracts

import impl "zlink.systems/zlink/internal/messaging"

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
	NewRoutingID              = impl.NewRoutingID
	NewRoutingIDFromString    = impl.NewRoutingIDFromString
	NewRoutingIDFromUInt32    = impl.NewRoutingIDFromUInt32
	NewRoutingIDFromUUIDBytes = impl.NewRoutingIDFromUUIDBytes
	NewRoutingIDFromHex       = impl.NewRoutingIDFromHex
	ParseRoutingIDHex         = impl.ParseRoutingIDHex
	NewMessage                = impl.NewMessage
	NewMessageFrom            = impl.NewMessageFrom
	NewMessageFromString      = impl.NewMessageFromString
	NewMessageWithSize        = impl.NewMessageWithSize
)
