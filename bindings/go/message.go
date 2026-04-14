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
	"hash/fnv"
	"strings"
	"unsafe"
)

const maxRoutingIDSize = 255
const maxFixedCStringFieldSize = 255

type RoutingID struct {
	data []byte
}

func NewRoutingID(data []byte) RoutingID {
	if len(data) == 0 || len(data) > maxRoutingIDSize {
		return RoutingID{}
	}
	cloned := append([]byte(nil), data...)
	return RoutingID{data: cloned}
}

func (r RoutingID) Bytes() []byte {
	return append([]byte(nil), r.data...)
}

func (r RoutingID) Size() int {
	return len(r.data)
}

func (r RoutingID) Hash() uint64 {
	hasher := fnv.New64a()
	_, _ = hasher.Write(r.data)
	return hasher.Sum64()
}

func (r RoutingID) String() string {
	return r.Hex()
}

func (r RoutingID) Hex() string {
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
