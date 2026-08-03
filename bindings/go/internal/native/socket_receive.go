// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include <stdlib.h>
#include "zlink.h"
*/
import "C"

import "unsafe"

func recvTopicMessageInto(
	out *TopicMessage,
	call func(**C.zlink_routing_id_t, *C.char, *C.size_t, *C.zlink_msg_t, *C.zlink_part_flag_t, C.zlink_recv_flags_t) error,
	flags RecvFlags,
) error {
	var sourceRID *C.zlink_routing_id_t
	var topicBuf [recvTopicBufferCap]byte
	topicLen := C.size_t(len(topicBuf))
	clonedParts, err := recvMultipart(flags, func(part *C.zlink_msg_t, hasMore *C.zlink_part_flag_t, recvFlags C.zlink_recv_flags_t) error {
		return call(&sourceRID, (*C.char)(unsafe.Pointer(&topicBuf[0])), &topicLen, part, hasMore, recvFlags)
	})
	if err != nil {
		return err
	}
	_ = out.Close()
	out.routingID = routingIDFromCPtr(sourceRID)
	out.topic = string(topicBuf[:int(topicLen)])
	out.parts = clonedParts
	return nil
}

func recvSubscribePartInto(
	out *Message,
	topicBuffer []byte,
	flags RecvFlags,
	call func(**C.zlink_routing_id_t, *C.char, C.size_t, *C.size_t, *C.zlink_msg_t, *C.zlink_part_flag_t, C.zlink_recv_flags_t) error,
) (SubscribePartResult, error) {
	if out == nil {
		return SubscribePartResult{}, &RecvError{Result: RecvInvalidHandle, nativeErrno: int(C.EINVAL)}
	}
	if len(topicBuffer) == 0 {
		return SubscribePartResult{}, &RecvError{Result: RecvInvalidHandle, nativeErrno: int(C.EINVAL)}
	}
	var sourceRID *C.zlink_routing_id_t
	topicLen := C.size_t(len(topicBuffer))
	var part C.zlink_msg_t
	if err := configErrorFromResult(C.zlink_msg_init(&part)); err != nil {
		return SubscribePartResult{}, err
	}
	var hasMore C.zlink_part_flag_t
	if err := call(
		&sourceRID,
		(*C.char)(unsafe.Pointer(&topicBuffer[0])),
		C.size_t(len(topicBuffer)),
		&topicLen,
		&part,
		&hasMore,
		C.zlink_recv_flags_t(flags),
	); err != nil {
		_ = configErrorFromResult(C.zlink_msg_close(&part))
		return SubscribePartResult{}, err
	}
	// HOT PATH: public SubscribePart receives one payload frame into caller-owned
	// storage. Adopt the native frame directly so repeated receives do not build a
	// TopicMessage or allocate a parts slice only to read a single message.
	_ = out.Close()
	if err := configErrorFromResult(C.zlink_msg_adopt(&out.msg, &part)); err != nil {
		_ = configErrorFromResult(C.zlink_msg_close(&part))
		return SubscribePartResult{}, err
	}
	out.closed = false
	return SubscribePartResult{
		RoutingID: routingIDFromCPtr(sourceRID),
		TopicLen:  int(topicLen),
		More:      hasMore != C.ZLINK_PART_FINAL,
	}, nil
}

func adoptRecvPart(out *Message, part *C.zlink_msg_t) error {
	// HOT PATH: public RecvPart reuses the caller's Message object. Replacing the
	// native frame here avoids allocating an intermediate Received envelope for
	// single-part receive loops.
	_ = out.Close()
	if err := configErrorFromResult(C.zlink_msg_adopt(&out.msg, part)); err != nil {
		_ = configErrorFromResult(C.zlink_msg_close(part))
		return err
	}
	out.closed = false
	return nil
}

func recvDirectPartInto(
	out *Message,
	flags RecvFlags,
	call func(**C.zlink_routing_id_t, *C.zlink_msg_t, *C.zlink_part_flag_t, C.zlink_recv_flags_t) error,
) (RecvPartResult, error) {
	if out == nil {
		return RecvPartResult{}, &RecvError{Result: RecvInvalidHandle, nativeErrno: int(C.EINVAL)}
	}
	var sourceRID *C.zlink_routing_id_t
	var part C.zlink_msg_t
	if err := configErrorFromResult(C.zlink_msg_init(&part)); err != nil {
		return RecvPartResult{}, err
	}
	var hasMore C.zlink_part_flag_t
	if err := call(&sourceRID, &part, &hasMore, C.zlink_recv_flags_t(flags)); err != nil {
		_ = configErrorFromResult(C.zlink_msg_close(&part))
		return RecvPartResult{}, err
	}
	if err := adoptRecvPart(out, &part); err != nil {
		return RecvPartResult{}, err
	}
	return RecvPartResult{
		RoutingID: routingIDFromCPtr(sourceRID),
		More:      hasMore != C.ZLINK_PART_FINAL,
	}, nil
}

func recvRoutedPartInto(
	out *Message,
	flags RecvFlags,
	call func(**C.zlink_routing_id_t, *C.uint64_t, *C.zlink_msg_t, *C.zlink_part_flag_t, C.zlink_recv_flags_t) error,
) (RecvPartResult, error) {
	if out == nil {
		return RecvPartResult{}, &RecvError{Result: RecvInvalidHandle, nativeErrno: int(C.EINVAL)}
	}
	var sourceNodeRID *C.zlink_routing_id_t
	var requestSeq C.uint64_t
	var part C.zlink_msg_t
	if err := configErrorFromResult(C.zlink_msg_init(&part)); err != nil {
		return RecvPartResult{}, err
	}
	var hasMore C.zlink_part_flag_t
	if err := call(&sourceNodeRID, &requestSeq, &part, &hasMore, C.zlink_recv_flags_t(flags)); err != nil {
		_ = configErrorFromResult(C.zlink_msg_close(&part))
		return RecvPartResult{}, err
	}
	if err := adoptRecvPart(out, &part); err != nil {
		return RecvPartResult{}, err
	}
	seq := uint64(requestSeq)
	return RecvPartResult{
		RoutingID:     routingIDFromCPtr(sourceNodeRID),
		RequestSeq:    seq,
		HasRequestSeq: seq != 0,
		More:          hasMore != C.ZLINK_PART_FINAL,
	}, nil
}

func recvSubscriptionEvent(
	call func(*C.zlink_routing_id_t, *C.int, *C.char, *C.size_t, C.zlink_recv_flags_t) error,
	flags RecvFlags,
) (*SubscriptionEvent, error) {
	var rid C.zlink_routing_id_t
	var subscribed C.int
	var topicBuf [recvTopicBufferCap]byte
	topicLen := C.size_t(len(topicBuf))
	if err := call(&rid, &subscribed, (*C.char)(unsafe.Pointer(&topicBuf[0])), &topicLen, C.zlink_recv_flags_t(flags)); err != nil {
		return nil, err
	}
	return &SubscriptionEvent{
		routingID:  routingIDFromC(rid),
		subscribed: subscribed != 0,
		topic:      string(topicBuf[:int(topicLen)]),
	}, nil
}
