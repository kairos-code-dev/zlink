// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <errno.h>
#include "zlink.h"
*/
import "C"

type Received struct {
	routingID     RoutingID
	parts         []*Message
	requestSeq    uint64
	hasRequestSeq bool
	reply         func(SendFlags, []*Message) error
	send          func(SendFlags, []*Message) (bool, error)
	sendBuilder   func(SendFlags, []sendBuilderPart) (bool, error)
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

func (r *Received) Parts() []*Message {
	if r == nil {
		return nil
	}
	return r.parts
}

// AdoptFrom replaces r's internal state with the contents of source.
// Any messages currently owned by r are closed first; source is left in
// an empty (already-detached) state after the call. Used by Socket.RecvInto
// and equivalents to refill caller-provided Received storage without
// allocating a new value per call.
func (r *Received) AdoptFrom(source *Received) {
	if r == nil || source == nil || r == source {
		return
	}
	r.replace(source.routingID, source.parts, source.requestSeq, source.hasRequestSeq, source.reply, source.send, source.sendBuilder)
	// Detach source so its lifecycle no-ops.
	source.routingID = RoutingID{}
	source.parts = nil
	source.requestSeq = 0
	source.hasRequestSeq = false
	source.reply = nil
	source.send = nil
	source.sendBuilder = nil
}

func (r *Received) replace(
	routingID RoutingID,
	parts []*Message,
	requestSeq uint64,
	hasRequestSeq bool,
	reply func(SendFlags, []*Message) error,
	send func(SendFlags, []*Message) (bool, error),
	sendBuilder func(SendFlags, []sendBuilderPart) (bool, error),
) {
	for _, part := range r.parts {
		if part != nil {
			part.Close()
		}
	}
	r.routingID = routingID
	r.parts = parts
	r.requestSeq = requestSeq
	r.hasRequestSeq = hasRequestSeq
	r.reply = reply
	r.send = send
	r.sendBuilder = sendBuilder
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
		return nil, &RecvError{Result: RecvInvalidHandle, nativeErrno: int(C.EFAULT)}
	}
	if len(r.parts) == 0 {
		return nil, &RecvError{Result: RecvNotSupported, nativeErrno: int(C.EINVAL)}
	}
	return r.parts[0], nil
}

func (r *Received) SinglePartOrError() (*Message, error) {
	if r == nil {
		return nil, &RecvError{Result: RecvInvalidHandle, nativeErrno: int(C.EFAULT)}
	}
	if len(r.parts) != 1 {
		return nil, &RecvError{Result: RecvNotSupported, nativeErrno: int(C.EINVAL)}
	}
	return r.parts[0], nil
}

// Reply returns an operation builder for replying to this received request.
// Only valid when HasRequestSeq() is true; otherwise Submit returns
// *SubmitError. RoutingID and RequestSeq are encapsulated.
func (r *Received) Reply() ReplyOp {
	return newReplyBuilder(func(parts []*Message, flags SendFlags) error {
		if r == nil {
			return &SubmitError{Result: SubmitInvalidHandle, nativeErrno: int(C.EFAULT)}
		}
		if !r.hasRequestSeq || r.reply == nil {
			return &SubmitError{Result: SubmitInvalidArgument, nativeErrno: int(C.EINVAL)}
		}
		if err := validateReplyFlags(flags); err != nil {
			return err
		}
		return r.reply(flags, parts)
	})
}

// Send returns an operation builder for a regular routed message back to
// the sender of this Received. The source routing ID is encapsulated.
func (r *Received) Send() SendOp {
	return newSendBuilder(func(parts []sendBuilderPart, flags SendFlags) error {
		if r == nil {
			return &SubmitError{Result: SubmitInvalidHandle, nativeErrno: int(C.EFAULT)}
		}
		if r.send == nil {
			return &SubmitError{Result: SubmitInvalidArgument, nativeErrno: int(C.EINVAL)}
		}
		if r.sendBuilder != nil && sendBuilderPartsNeedBuilder(parts) {
			sent, err := r.sendBuilder(flags, parts)
			if err != nil {
				return err
			}
			if !sent {
				return &SubmitError{Result: SubmitBackpressured}
			}
			return nil
		}
		sent, err := r.send(flags, builderMessages(parts))
		if err != nil {
			closeMovedSendBuilderParts(parts)
			return err
		}
		if !sent {
			closeMovedSendBuilderParts(parts)
			return &SubmitError{Result: SubmitBackpressured}
		}
		return nil
	})
}

func validateReplyFlags(flags SendFlags) error {
	if flags == SendFlagsNone {
		return nil
	}
	return &SubmitError{Result: SubmitNotSupported, nativeErrno: int(C.ENOTSUP)}
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
