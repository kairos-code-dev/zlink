// SPDX-License-Identifier: MPL-2.0

package zlink

/*
#include <stdint.h>
#include "zlink.h"
*/
import "C"

import (
	"runtime/cgo"
	"unsafe"
)

//export goZlinkRecvTrampoline
func goZlinkRecvTrampoline(sourceRID *C.zlink_routing_id_t, parts *C.zlink_msg_t, partCount C.size_t, userdata C.uintptr_t) {
	handler := cgo.Handle(userdata).Value().(recvCallback)
	handler(&Received{
		routingID: routingIDFromC(*sourceRID),
		parts:     mustTakeParts(parts, partCount),
	})
}

//export goZlinkSubscribeTrampoline
func goZlinkSubscribeTrampoline(sourceRID *C.zlink_routing_id_t, topic *C.char, topicLen C.size_t, parts *C.zlink_msg_t, partCount C.size_t, userdata C.uintptr_t) {
	handler := cgo.Handle(userdata).Value().(subscribeCallback)
	handler(&TopicMessage{
		routingID: routingIDFromC(*sourceRID),
		topic:     C.GoStringN(topic, C.int(topicLen)),
		parts:     mustTakeParts(parts, partCount),
	})
}

//export goZlinkSendReadyTrampoline
func goZlinkSendReadyTrampoline(_ unsafe.Pointer, userdata C.uintptr_t) {
	handler := cgo.Handle(userdata).Value().(sendReadyCallback)
	handler()
}
