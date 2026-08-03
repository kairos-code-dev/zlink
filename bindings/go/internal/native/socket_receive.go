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
	reuse := out.parts
	_ = out.Close()
	clonedParts, err := recvMultipart(reuse, flags, func(part *C.zlink_msg_t, hasMore *C.zlink_part_flag_t, recvFlags C.zlink_recv_flags_t) error {
		return call(&sourceRID, (*C.char)(unsafe.Pointer(&topicBuf[0])), &topicLen, part, hasMore, recvFlags)
	})
	if err != nil {
		return err
	}
	out.routingID = routingIDFromCPtr(sourceRID)
	out.topic = string(topicBuf[:int(topicLen)])
	out.parts = clonedParts
	return nil
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
