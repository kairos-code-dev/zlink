// SPDX-License-Identifier: MPL-2.0

package contracts

import impl "zlink.systems/zlink/internal/messaging"

type (
	// RoutingID is an opaque identifier for a messaging peer or route, 1 to 255 bytes.
	RoutingID = impl.RoutingID
	// Message owns a message payload; sending consumes it and Close releases it.
	Message = impl.Message
	// RequestReplyCallback is invoked with a request result and its reply parts, which it owns.
	RequestReplyCallback = impl.RequestReplyCallback
	// RequestReplyCompletion carries the result and reply parts of a completed request.
	RequestReplyCompletion = impl.RequestReplyCompletion
	// Received is a received message envelope: routing metadata, parts, and an optional reply/send context.
	Received = impl.Received
	// RecvPartResult is the outcome of receiving a single message part.
	RecvPartResult = impl.RecvPartResult
	// SpotForwardResult is the outcome of forwarding a message through a spot.
	SpotForwardResult = impl.SpotForwardResult
	// TopicMessage is a received publish: its topic and message parts.
	TopicMessage = impl.TopicMessage
	// SubscriptionEvent is a subscriber's subscribe or unsubscribe as observed by an XPUB socket.
	SubscriptionEvent = impl.SubscriptionEvent
)

var (
	// NewRoutingID creates a routing id from a copy of the given bytes (1 to 255).
	NewRoutingID = impl.NewRoutingID
	// NewRoutingIDFromString creates a routing id from the UTF-8 bytes of the string.
	NewRoutingIDFromString = impl.NewRoutingIDFromString
	// NewRoutingIDFromUInt32 creates a 4-byte big-endian routing id from the value.
	NewRoutingIDFromUInt32 = impl.NewRoutingIDFromUInt32
	// NewRoutingIDFromUUIDBytes creates a 16-byte routing id from the UUID bytes.
	NewRoutingIDFromUUIDBytes = impl.NewRoutingIDFromUUIDBytes
	// NewRoutingIDFromHex creates a routing id by decoding the hex string.
	NewRoutingIDFromHex = impl.NewRoutingIDFromHex
	// ParseRoutingIDHex parses a hex string into a routing id, returning an error on invalid input.
	ParseRoutingIDHex = impl.ParseRoutingIDHex
	// NewMessage creates an empty message.
	NewMessage = impl.NewMessage
	// NewMessageFrom creates a message holding an independent copy of the given bytes.
	NewMessageFrom = impl.NewMessageFrom
	// NewMessageFromString creates a message holding the UTF-8 bytes of the string.
	NewMessageFromString = impl.NewMessageFromString
	// NewMessageWithSize allocates a message with the given number of writable payload bytes.
	NewMessageWithSize = impl.NewMessageWithSize
)
