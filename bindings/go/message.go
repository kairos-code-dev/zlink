// SPDX-License-Identifier: MPL-2.0

package zlink

/*
#include <stdlib.h>
#include <string.h>
#include "zlink.h"
*/
import "C"

import (
	"bytes"
	"encoding/hex"
	"strings"
	"unsafe"
)

const maxRoutingIDSize = 255
const maxFixedCStringFieldSize = 255

// Message type constants for request-reply envelope.
const (
	MsgTypeData    = 0
	MsgTypeRequest = 1
	MsgTypeReply   = 2
)

// Per-message metadata key range.
const (
	MetadataKeyUserMin = 0x0100
	MetadataValueMax   = 65535
)

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

func (m *Message) GetProperty(name string) (string, bool, error) {
	if m == nil || m.closed {
		return "", false, stateError("message is closed")
	}
	if name == "" {
		return "", false, validationError("property name must not be empty")
	}
	if strings.IndexByte(name, 0) >= 0 {
		return "", false, validationError("property name contains null byte")
	}
	cname := C.CString(name)
	defer C.free(unsafe.Pointer(cname))
	value := C.zlink_msg_gets(&m.msg, cname)
	if value == nil {
		return "", false, nil
	}
	return C.GoString(value), true, nil
}

// SetRequest marks this message as a REQUEST with the given correlation ID.
func (m *Message) SetRequest(correlationID uint64) error {
	if m == nil || m.closed {
		return stateError("message is closed")
	}
	return checkRC(C.zlink_msg_set_request(&m.msg, C.uint64_t(correlationID)))
}

// SetReply marks this message as a REPLY with the given correlation ID.
func (m *Message) SetReply(correlationID uint64) error {
	if m == nil || m.closed {
		return stateError("message is closed")
	}
	return checkRC(C.zlink_msg_set_reply(&m.msg, C.uint64_t(correlationID)))
}

// RequestInfo returns the message type (0=DATA, 1=REQUEST, 2=REPLY) and correlation ID.
func (m *Message) RequestInfo() (msgType uint8, correlationID uint64, err error) {
	if m == nil || m.closed {
		return 0, 0, stateError("message is closed")
	}
	var cType C.uint8_t
	var cID C.uint64_t
	if err := checkRC(C.zlink_msg_get_request_info(&m.msg, &cType, &cID)); err != nil {
		return 0, 0, err
	}
	return uint8(cType), uint64(cID), nil
}

// SetMetadata sets a metadata key-value pair on this message.
// Keys below 0x0100 are reserved and will return an error.
func (m *Message) SetMetadata(key uint16, value []byte) error {
	if m == nil || m.closed {
		return stateError("message is closed")
	}
	var ptr unsafe.Pointer
	if len(value) > 0 {
		ptr = unsafe.Pointer(&value[0])
	}
	return checkRC(C.zlink_msg_set_metadata(&m.msg, C.uint16_t(key), ptr, C.size_t(len(value))))
}

// GetMetadata retrieves a metadata value by key. Returns nil if absent.
func (m *Message) GetMetadata(key uint16) ([]byte, error) {
	if m == nil || m.closed {
		return nil, stateError("message is closed")
	}
	var size C.size_t
	ptr := C.zlink_msg_get_metadata(&m.msg, C.uint16_t(key), &size)
	if ptr == nil {
		return nil, nil
	}
	return C.GoBytes(ptr, C.int(size)), nil
}

func (m *Message) moved() {
	m.closed = true
}
