// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <errno.h>
#include "zlink.h"
*/
import "C"

func receivedReplyToRouter(reply func(RoutingID, uint64, SendFlags, ...*Message) error, routingID RoutingID, requestSeq uint64) func(SendFlags, []*Message) error {
	return func(flags SendFlags, parts []*Message) error {
		return reply(routingID, requestSeq, flags, parts...)
	}
}

func receivedSendToRouter(send func(RoutingID, SendFlags, ...*Message) (bool, error), routingID RoutingID) func(SendFlags, []*Message) (bool, error) {
	return func(flags SendFlags, parts []*Message) (bool, error) {
		return send(routingID, flags, parts...)
	}
}

func submitSendOpBuilderParts(op SendOp, flags SendFlags, parts []sendBuilderPart) (bool, error) {
	if len(parts) == 0 {
		return false, &SubmitError{Result: SubmitInvalidArgument}
	}
	var submitOp SendSubmitOp
	if parts[0].bytes {
		submitOp = op.Bytes(parts[0].data)
	} else if parts[0].move {
		submitOp = op.MoveMessage(parts[0].message)
	} else {
		submitOp = op.Message(parts[0].message)
	}
	for _, part := range parts[1:] {
		if part.bytes {
			submitOp = submitOp.Bytes(part.data)
		} else if part.move {
			submitOp = submitOp.MoveMessage(part.message)
		} else {
			submitOp = submitOp.Message(part.message)
		}
	}
	return submitOp.Flags(flags).Submit(nil)
}

type routerSenderToSpot interface {
	SendToSpot(RoutingID, RoutingID) SendOp
}

func receivedSendToSpotPeer(socket routerSenderToSpot, nodeRID RoutingID, spotRID RoutingID) func(SendFlags, []*Message) (bool, error) {
	return func(flags SendFlags, parts []*Message) (bool, error) {
		if len(parts) == 0 {
			return false, &SubmitError{Result: SubmitInvalidArgument}
		}
		submitOp := socket.SendToSpot(nodeRID, spotRID).Message(parts[0])
		for _, part := range parts[1:] {
			submitOp = submitOp.Message(part)
		}
		return submitOp.Flags(flags).Submit(nil)
	}
}

func receivedSendFromSpot(socket *Spot, routingID RoutingID, spotRID RoutingID) func(SendFlags, []*Message) (bool, error) {
	return func(flags SendFlags, parts []*Message) (bool, error) {
		if len(parts) == 0 {
			return false, &SubmitError{Result: SubmitInvalidArgument}
		}
		if spotRID.Size() == 0 {
			return false, &SubmitError{Result: SubmitInvalidArgument, internalErrno: int(C.EINVAL)}
		}
		op := socket.SendToSpot(routingID, spotRID).Message(parts[0])
		for _, part := range parts[1:] {
			op = op.Message(part)
		}
		submit := op.Flags(flags)
		sent, err := submit.Submit(nil)
		if err != nil {
			return false, err
		}
		return sent, nil
	}
}

func receivedSendFromSpotBuilder(socket *Spot, routingID RoutingID, spotRID RoutingID) func(SendFlags, []sendBuilderPart) (bool, error) {
	return func(flags SendFlags, parts []sendBuilderPart) (bool, error) {
		if len(parts) == 0 {
			return false, &SubmitError{Result: SubmitInvalidArgument}
		}
		if spotRID.Size() == 0 {
			return false, &SubmitError{Result: SubmitInvalidArgument, internalErrno: int(C.EINVAL)}
		}
		return submitSendOpBuilderParts(socket.SendToSpot(routingID, spotRID), flags, parts)
	}
}

type routerReplier interface {
	ReplyToSpot(RoutingID, RoutingID, uint64) ReplyOp
}

func receivedReplyToSpotPeer(socket routerReplier, nodeRID RoutingID, spotRID RoutingID, requestSeq uint64) func(SendFlags, []*Message) error {
	return func(flags SendFlags, parts []*Message) error {
		if len(parts) == 0 {
			return &SubmitError{Result: SubmitInvalidArgument}
		}
		submitOp := socket.ReplyToSpot(nodeRID, spotRID, requestSeq).Message(parts[0])
		for _, part := range parts[1:] {
			submitOp = submitOp.Message(part)
		}
		return submitOp.Flags(flags).Submit(nil)
	}
}

func receivedReplyToSpot(socket *Spot, routingID RoutingID, spotRID RoutingID, requestSeq uint64) func(SendFlags, []*Message) error {
	if spotRID.Size() == 0 {
		return func(flags SendFlags, parts []*Message) error {
			if len(parts) == 0 {
				return &SubmitError{Result: SubmitInvalidArgument}
			}
			replyOp := socket.ReplyToRouter(routingID, requestSeq)
			submitOp := replyOp.Message(parts[0])
			for _, part := range parts[1:] {
				submitOp = submitOp.Message(part)
			}
			submitOp = submitOp.Flags(flags)
			return submitOp.Submit(nil)
		}
	}
	return func(flags SendFlags, parts []*Message) error {
		if len(parts) == 0 {
			return &SubmitError{Result: SubmitInvalidArgument}
		}
		replyOp := socket.ReplyToSpot(routingID, spotRID, requestSeq)
		submitOp := replyOp.Message(parts[0])
		for _, part := range parts[1:] {
			submitOp = submitOp.Message(part)
		}
		submitOp = submitOp.Flags(flags)
		return submitOp.Submit(nil)
	}
}
