// SPDX-License-Identifier: MPL-2.0

package native

func receivedReplyToRouter(
	reply func(RoutingID, uint64, SendFlags, ...*Message) error,
	routingID RoutingID,
	requestSeq uint64,
) func(SendFlags, []*Message) error {
	return func(flags SendFlags, parts []*Message) error {
		return reply(routingID, requestSeq, flags, parts...)
	}
}

func receivedSendToRouter(
	send func(RoutingID, SendFlags, ...*Message) (bool, error),
	routingID RoutingID,
) func(SendFlags, []*Message) (bool, error) {
	return func(flags SendFlags, parts []*Message) (bool, error) {
		return send(routingID, flags, parts...)
	}
}
