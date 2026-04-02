// SPDX-License-Identifier: MPL-2.0

package zlink

/*
#include <string.h>
#include "zlink.h"
*/
import "C"

import (
	"bytes"
	"encoding/hex"
	"unsafe"
)

const maxRoutingIDSize = 255
const maxFixedCStringFieldSize = 255

type RoutingID struct {
	data []byte
}

func NewRoutingID(data []byte) (RoutingID, error) {
	if data == nil {
		return RoutingID{}, validationError("routing id must not be nil")
	}
	if len(data) > maxRoutingIDSize {
		return RoutingID{}, validationError("routing id length %d exceeds %d", len(data), maxRoutingIDSize)
	}
	cloned := append([]byte(nil), data...)
	return RoutingID{data: cloned}, nil
}

func (r RoutingID) Bytes() []byte {
	return append([]byte(nil), r.data...)
}

func (r RoutingID) String() string {
	return hex.EncodeToString(r.data)
}

func (r RoutingID) Equal(other RoutingID) bool {
	return bytes.Equal(r.data, other.data)
}

func (r RoutingID) toC() C.zlink_routing_id_t {
	var out C.zlink_routing_id_t
	out.size = C.uint8_t(len(r.data))
	if len(r.data) > 0 {
		C.memcpy(unsafe.Pointer(&out.data[0]), unsafe.Pointer(&r.data[0]), C.size_t(len(r.data)))
	}
	return out
}

func routingIDFromC(raw C.zlink_routing_id_t) RoutingID {
	size := int(raw.size)
	if size == 0 {
		return RoutingID{}
	}
	data := C.GoBytes(unsafe.Pointer(&raw.data[0]), C.int(size))
	return RoutingID{data: data}
}

type Message struct {
	msg    C.zlink_msg_t
	closed bool
}

func NewMessage(data []byte) (*Message, error) {
	m := &Message{}
	if err := checkRC(C.zlink_msg_init_size(&m.msg, C.size_t(len(data)))); err != nil {
		return nil, err
	}
	if len(data) > 0 {
		ptr := C.zlink_msg_data(&m.msg)
		copy(unsafe.Slice((*byte)(ptr), len(data)), data)
	}
	return m, nil
}

func (m *Message) clone() (*Message, error) {
	dup := &Message{}
	if err := checkRC(C.zlink_msg_init(&dup.msg)); err != nil {
		return nil, err
	}
	if err := checkRC(C.zlink_msg_copy(&dup.msg, &m.msg)); err != nil {
		_ = dup.Close()
		return nil, err
	}
	return dup, nil
}

func (m *Message) Close() error {
	if m == nil || m.closed {
		return nil
	}
	if err := checkRC(C.zlink_msg_close(&m.msg)); err != nil {
		return err
	}
	m.closed = true
	return nil
}

func (m *Message) Data() []byte {
	if m == nil || m.closed {
		return nil
	}
	size := int(C.zlink_msg_size(&m.msg))
	if size == 0 {
		return nil
	}
	ptr := C.zlink_msg_data(&m.msg)
	return unsafe.Slice((*byte)(ptr), size)
}

func (m *Message) Size() int {
	if m == nil || m.closed {
		return 0
	}
	return int(C.zlink_msg_size(&m.msg))
}

func (m *Message) RefCount() int {
	if m == nil || m.closed {
		return 0
	}
	return int(C.zlink_msg_refcnt(&m.msg))
}

func (m *Message) moved() {
	m.closed = true
}
