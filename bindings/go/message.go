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

// RoutingID is a fixed-size value type. The data lives inline in a
// [255]byte array (matching zlink_routing_id_t) rather than a heap
// slice, so construction from a recv path or hex string no longer
// allocates and copies are register/stack moves.
type RoutingID struct {
	data [maxRoutingIDSize]byte
	size uint8
}

func NewRoutingID(data []byte) RoutingID {
	if len(data) == 0 || len(data) > maxRoutingIDSize {
		return RoutingID{}
	}
	var rid RoutingID
	rid.size = uint8(copy(rid.data[:], data))
	return rid
}

// NewRoutingIDFromString parses the hex form returned by RoutingID.String or
// RoutingID.Hex. Invalid input returns the empty RoutingID value.
func NewRoutingIDFromString(value string) RoutingID {
	rid, err := ParseRoutingIDString(value)
	if err != nil {
		return RoutingID{}
	}
	return rid
}

// ParseRoutingIDString parses the hex form returned by RoutingID.String or
// RoutingID.Hex. Invalid input returns *ConfigError.
func ParseRoutingIDString(value string) (RoutingID, error) {
	if len(value) == 0 || len(value)%2 != 0 || len(value) > maxRoutingIDSize*2 {
		return RoutingID{}, validationError("routing id string must be non-empty even-length hex and decode to at most %d bytes", maxRoutingIDSize)
	}
	data, err := hex.DecodeString(value)
	if err != nil {
		return RoutingID{}, validationError("routing id string must contain only hex digits")
	}
	rid := NewRoutingID(data)
	if rid.Size() == 0 {
		return RoutingID{}, validationError("routing id length must be between 1 and %d bytes", maxRoutingIDSize)
	}
	return rid, nil
}

func (r RoutingID) Bytes() []byte {
	return append([]byte(nil), r.data[:r.size]...)
}

func (r RoutingID) Size() int {
	return int(r.size)
}

func (r RoutingID) Hash() uint64 {
	const (
		offset64 = 14695981039346656037
		prime64  = 1099511628211
	)
	hash := uint64(offset64)
	for _, b := range r.data[:r.size] {
		hash ^= uint64(b)
		hash *= prime64
	}
	return hash
}

func (r RoutingID) String() string {
	return r.Hex()
}

func (r RoutingID) Hex() string {
	return hex.EncodeToString(r.data[:r.size])
}

func (r RoutingID) Equal(other RoutingID) bool {
	return bytes.Equal(r.data[:r.size], other.data[:other.size])
}

func (r RoutingID) toC() C.zlink_routing_id_t {
	var out C.zlink_routing_id_t
	out.size = C.uint8_t(r.size)
	if r.size > 0 {
		C.memcpy(unsafe.Pointer(&out.data[0]), unsafe.Pointer(&r.data[0]), C.size_t(r.size))
	}
	return out
}

func routingIDFromC(raw C.zlink_routing_id_t) RoutingID {
	size := int(raw.size)
	if size == 0 {
		return RoutingID{}
	}
	var rid RoutingID
	rid.size = uint8(copy(rid.data[:], unsafe.Slice((*byte)(unsafe.Pointer(&raw.data[0])), size)))
	return rid
}

func routingIDFromCPtr(raw *C.zlink_routing_id_t) RoutingID {
	if raw == nil {
		return RoutingID{}
	}
	return routingIDFromC(*raw)
}

type Message struct {
	msg    C.zlink_msg_t
	closed bool
}

func NewMessage(data []byte) (*Message, error) {
	m := &Message{}
	if err := configErrorFromResult(ConfigResult(C.zlink_msg_init_size(&m.msg, C.size_t(len(data))))); err != nil {
		return nil, err
	}
	if len(data) > 0 {
		ptr := C.zlink_msg_data(&m.msg)
		copy(unsafe.Slice((*byte)(ptr), len(data)), data)
	}
	return m, nil
}

func NewMessageWithSize(size int) (*Message, error) {
	if size < 0 {
		return nil, validationError("message size must be >= 0")
	}
	m := &Message{}
	if err := configErrorFromResult(ConfigResult(C.zlink_msg_init_size(&m.msg, C.size_t(size)))); err != nil {
		return nil, err
	}
	return m, nil
}

// NewMessageFrom creates a message from a byte slice. The current Go
// binding uses the owned-message path; the helper exists so codec extensions
// can target a stable constructor.
func NewMessageFrom(data []byte) (*Message, error) {
	return NewMessage(data)
}

func (m *Message) clone() (*Message, error) {
	dup := &Message{}
	if err := configErrorFromResult(ConfigResult(C.zlink_msg_init(&dup.msg))); err != nil {
		return nil, err
	}
	if err := configErrorFromResult(ConfigResult(C.zlink_msg_copy(&dup.msg, &m.msg))); err != nil {
		_ = dup.Close()
		return nil, err
	}
	return dup, nil
}

func (m *Message) Close() error {
	if m == nil || m.closed {
		return nil
	}
	if err := configErrorFromResult(ConfigResult(C.zlink_msg_close(&m.msg))); err != nil {
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

// Bytes returns the message payload as a byte slice.
func (m *Message) Bytes() []byte {
	return m.Data()
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
	var result C.zlink_config_result_t
	count := C.zlink_msg_refcnt(&m.msg, &result)
	if result != 0 {
		return 0
	}
	return int(count)
}

func (m *Message) GetProperty(name string) (string, bool, error) {
	if m == nil || m.closed {
		return "", false, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	if name == "" {
		return "", false, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	if strings.IndexByte(name, 0) >= 0 {
		return "", false, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	cname := C.CString(name)
	defer C.free(unsafe.Pointer(cname))
	value := C.zlink_msg_gets(&m.msg, cname)
	if value == nil {
		return "", false, nil
	}
	return C.GoString(value), true, nil
}

func (m *Message) moved() {
	m.closed = true
}
