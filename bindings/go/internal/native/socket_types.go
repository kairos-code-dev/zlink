// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include <stdlib.h>
#include "zlink.h"

extern void goZlinkRecvTrampoline(zlink_routing_id_t *source_rid_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);
extern void goZlinkSendReadyTrampoline(void *subject_, uintptr_t userdata_);
extern void goZlinkReplyTrampoline(zlink_request_result_t result_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);
extern void goZlinkStreamPacketTrampoline(void *stream_, const zlink_routing_id_t *source_rid_, zlink_msg_t *header_, zlink_msg_t *body_, uintptr_t userdata_);

static inline int zlink_recv_handler_go_local(void *s, uintptr_t userdata) {
    return zlink_recv_handler(s, (zlink_socket_msg_handler_fn)goZlinkRecvTrampoline, (void *)userdata);
}

static inline int zlink_send_ready_handler_go_local(void *s, uintptr_t userdata) {
    return zlink_send_ready_handler(s, (zlink_send_ready_handler_fn)goZlinkSendReadyTrampoline, (void *)userdata);
}

static inline int zlink_stream_packet_handler_go_local(void *s, uintptr_t userdata) {
    return zlink_stream_packet_handler(s, (zlink_stream_packet_handler_fn)goZlinkStreamPacketTrampoline, (void *)userdata);
}

static inline int zlink_router_request_spot_part_go_local(void *router, const zlink_routing_id_t *dest_node_rid, const zlink_routing_id_t *dest_spot_rid, zlink_msg_t *part, zlink_send_flags_t flags, zlink_part_flag_t part_flag, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_router_request_spot_part(router, dest_node_rid, dest_spot_rid, part, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata, flags, part_flag, timeout_ms);
}

static inline int zlink_router_send_spot_part_go_local(void *router, const zlink_routing_id_t *dest_node_rid, const zlink_routing_id_t *dest_spot_rid, zlink_msg_t *part, zlink_send_flags_t flags, zlink_part_flag_t part_flag) {
    return zlink_router_send_spot_part(router, dest_node_rid, dest_spot_rid, part, flags, part_flag);
}
*/
import "C"

import (
	"errors"
	"runtime/cgo"
	"strings"
	"sync"
	"time"
	"unsafe"
)

type RidDuplicatePolicy int

const (
	RidDuplicateReject   RidDuplicatePolicy = 0
	RidDuplicateHandover RidDuplicatePolicy = 1
)

type SubmitRetryMode int

const (
	SubmitRetryOff          SubmitRetryMode = 0
	SubmitRetryLocalFailure SubmitRetryMode = 1
)

type recvCallback func(*Received)
type subscribeCallback func(*TopicMessage)
type sendReadyCallback func()

const recvTopicBufferCap = 64 * 1024

type socketCore struct {
	handle             unsafe.Pointer
	closed             bool
	recvHandle         cgo.Handle
	subscribeHandle    cgo.Handle
	sendReadyHandle    cgo.Handle
	streamPacketHandle cgo.Handle
}

func newSocketCore(ctx *Context, socketType C.zlink_socket_type_t) (*socketCore, error) {
	if ctx == nil || ctx.closed {
		return nil, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	handle := C.zlink_socket(ctx.raw(), socketType)
	if handle == nil {
		return nil, configErrorFromErrno(currentErrno())
	}
	return &socketCore{handle: handle}, nil
}

func (s *socketCore) raw() unsafe.Pointer {
	return s.handle
}

func (s *socketCore) Bind(endpoint string) error {
	return s.withCString(endpoint, func(cstr *C.char) error {
		return bindErrorFromResult(C.zlink_bind(s.handle, cstr))
	})
}

func (s *socketCore) Connect(endpoint string) error {
	return s.withCString(endpoint, func(cstr *C.char) error {
		return connectErrorFromResult(C.zlink_connect(s.handle, cstr))
	})
}

func (s *socketCore) Unbind(endpoint string) error {
	return s.withCString(endpoint, func(cstr *C.char) error {
		return connectErrorFromResult(C.zlink_unbind(s.handle, cstr))
	})
}

func (s *socketCore) Disconnect(endpoint string) error {
	return s.withCString(endpoint, func(cstr *C.char) error {
		return connectErrorFromResult(C.zlink_disconnect(s.handle, cstr))
	})
}

func (s *socketCore) DisconnectRID(peerRID RoutingID) error {
	rid := peerRID.toC()
	return connectErrorFromResult(
		C.zlink_disconnect_rid(
			s.handle,
			(*C.zlink_routing_id_t)(unsafe.Pointer(&rid)),
		))
}

func (s *socketCore) Close() error {
	if s == nil || s.closed {
		return nil
	}
	if err := closeErrorFromResult(C.zlink_close(s.handle)); err != nil {
		return err
	}
	s.closed = true
	s.handle = nil
	s.releaseCallbacks()
	return nil
}

func (s *socketCore) releaseCallbacks() {
	if s.recvHandle != 0 {
		releaseCallbackHandle(s.recvHandle)
		s.recvHandle = 0
	}
	if s.subscribeHandle != 0 {
		releaseCallbackHandle(s.subscribeHandle)
		s.subscribeHandle = 0
	}
	if s.sendReadyHandle != 0 {
		releaseCallbackHandle(s.sendReadyHandle)
		s.sendReadyHandle = 0
	}
	if s.streamPacketHandle != 0 {
		releaseCallbackHandle(s.streamPacketHandle)
		s.streamPacketHandle = 0
	}
}

func (s *socketCore) setIntOption(option C.zlink_option_t, value int32) error {
	return s.setOption(option, unsafe.Pointer(&value), C.size_t(C.sizeof_int))
}

func (s *socketCore) getIntOption(option C.zlink_option_t) (int32, error) {
	var value C.int
	size := C.size_t(C.sizeof_int)
	err := configErrorFromResult(C.zlink_get_option(s.handle, option, unsafe.Pointer(&value), &size))
	return int32(value), err
}

func (s *socketCore) setInt64Option(option C.zlink_option_t, value int64) error {
	raw := C.int64_t(value)
	return s.setOption(option, unsafe.Pointer(&raw), C.size_t(unsafe.Sizeof(raw)))
}

func (s *socketCore) getInt64Option(option C.zlink_option_t) (int64, error) {
	var value C.int64_t
	size := C.size_t(unsafe.Sizeof(value))
	err := configErrorFromResult(C.zlink_get_option(s.handle, option, unsafe.Pointer(&value), &size))
	return int64(value), err
}

func (s *socketCore) setBoolOption(option C.zlink_option_t, value bool) error {
	var raw C.int
	if value {
		raw = 1
	}
	return s.setOption(option, unsafe.Pointer(&raw), C.size_t(C.sizeof_int))
}

func (s *socketCore) getBoolOption(option C.zlink_option_t) (bool, error) {
	value, err := s.getIntOption(option)
	return value != 0, err
}

func (s *socketCore) setStringOption(option C.zlink_option_t, value string) error {
	if strings.IndexByte(value, 0) >= 0 {
		return validationError("string contains null byte")
	}
	cstr := C.CString(value)
	defer C.free(unsafe.Pointer(cstr))
	return s.setOption(option, unsafe.Pointer(cstr), C.size_t(len(value)))
}

func (s *socketCore) getStringOption(option C.zlink_option_t, capHint int) (string, error) {
	if capHint <= 0 {
		capHint = 256
	}
	buf := make([]byte, capHint)
	size := C.size_t(len(buf))
	err := configErrorFromResult(C.zlink_get_option(s.handle, option, unsafe.Pointer(&buf[0]), &size))
	if err != nil {
		return "", err
	}
	return string(buf[:int(size)]), nil
}

func (s *socketCore) setOption(option C.zlink_option_t, ptr unsafe.Pointer, size C.size_t) error {
	if s == nil {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	return setNativeOption(s.handle, s.closed, "socket is closed", option, ptr, size)
}

func (s *socketCore) setDurationOption(option C.zlink_option_t, value time.Duration) error {
	return setNativeDurationOption(s.handle, s.closed, "socket is closed", option, value)
}

func (s *socketCore) getDurationOption(option C.zlink_option_t) (time.Duration, error) {
	value, err := s.getIntOption(option)
	return time.Duration(value) * time.Millisecond, err
}

func (s *socketCore) withCString(value string, fn func(*C.char) error) error {
	if s == nil || s.closed {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	if err := validateEndpointString(value); err != nil {
		return err
	}
	cstr := C.CString(value)
	defer C.free(unsafe.Pointer(cstr))
	return fn(cstr)
}

func closeNativeMultipart(parts []C.zlink_msg_t, count int) {
	if count <= 0 || len(parts) == 0 {
		return
	}
	C.zlink_multipart_close(&parts[0], C.size_t(count))
}

func closeMessageSlice(parts []*Message) {
	for _, part := range parts {
		_ = part.Close()
	}
}

type preparedMultipart struct {
	native []C.zlink_msg_t
	parts  []*Message
}

type multipartSubmitFunc func(*C.zlink_msg_t, C.zlink_part_flag_t) error
type multipartRecvFunc func(*C.zlink_msg_t, *C.zlink_part_flag_t, C.zlink_recv_flags_t) error

func prepareMultipart(parts []*Message) (*preparedMultipart, error) {
	if len(parts) == 0 {
		return nil, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	native := make([]C.zlink_msg_t, len(parts))
	for i, part := range parts {
		if part == nil {
			return nil, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
		}
		if part.closed {
			return nil, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
		}
		if err := configErrorFromResult(C.zlink_msg_init(&native[i])); err != nil {
			closeNativeMultipart(native, i)
			return nil, err
		}
		if err := configErrorFromResult(C.zlink_msg_move(&native[i], &part.msg)); err != nil {
			prepared := &preparedMultipart{native: native[:i+1], parts: parts[:i+1]}
			restoreErr := prepared.restore()
			if restoreErr != nil {
				return nil, restoreErr
			}
			return nil, err
		}
	}
	return &preparedMultipart{native: native, parts: parts}, nil
}

func (p *preparedMultipart) ptr() *C.zlink_msg_t {
	if p == nil || len(p.native) == 0 {
		return nil
	}
	return &p.native[0]
}

func (p *preparedMultipart) count() C.size_t {
	if p == nil {
		return 0
	}
	return C.size_t(len(p.native))
}

func (p *preparedMultipart) commit() {
	if p == nil {
		return
	}
	for _, part := range p.parts {
		part.moved()
	}
}

func (p *preparedMultipart) restore() error {
	if p == nil {
		return nil
	}
	for i, part := range p.parts {
		if err := configErrorFromResult(C.zlink_msg_move(&part.msg, &p.native[i])); err != nil {
			closeNativeMultipart(p.native, len(p.native))
			return err
		}
	}
	closeNativeMultipart(p.native, len(p.native))
	return nil
}

func markPartsMoved(parts []*Message) {
	for _, part := range parts {
		if part != nil {
			part.moved()
		}
	}
}

func submitPreparedMultipart(prepared *preparedMultipart, submit multipartSubmitFunc) error {
	if prepared == nil || len(prepared.native) == 0 {
		return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	for i := range prepared.native {
		partFlag := C.zlink_part_flag_t(C.ZLINK_PART_FINAL)
		if i+1 < len(prepared.native) {
			partFlag = C.ZLINK_PART_MORE
		}
		if err := submit(&prepared.native[i], partFlag); err != nil {
			if i+1 < len(prepared.native) {
				closeNativeMultipart(prepared.native[i+1:], len(prepared.native)-(i+1))
			}
			return err
		}
	}
	return nil
}

func initNativeMessageFromBytes(native *C.zlink_msg_t, data []byte) error {
	if native == nil {
		return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	if err := configErrorFromResult(C.zlink_msg_init_size(native, C.size_t(len(data)))); err != nil {
		return err
	}
	if len(data) > 0 {
		copy(unsafe.Slice((*byte)(C.zlink_msg_data(native)), len(data)), data)
	}
	return nil
}

func submitMultipartFromClones(parts []*Message, consumeOriginal bool, submit multipartSubmitFunc) error {
	if consumeOriginal && len(parts) == 1 {
		return submitSinglePartFromCopy(parts[0], submit)
	}
	cloned, err := cloneParts(parts)
	if err != nil {
		return err
	}
	prepared, err := prepareMultipart(cloned)
	if err != nil {
		closeMessageSlice(cloned)
		return err
	}
	err = submitPreparedMultipart(prepared, submit)
	prepared.commit()
	if err != nil {
		return err
	}
	if consumeOriginal {
		markPartsMoved(parts)
	}
	return nil
}

func submitSinglePartFromCopy(part *Message, submit multipartSubmitFunc) error {
	if part == nil {
		return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	if part.closed {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	var native C.zlink_msg_t
	if err := configErrorFromResult(C.zlink_msg_init(&native)); err != nil {
		return err
	}
	if err := configErrorFromResult(C.zlink_msg_copy(&native, &part.msg)); err != nil {
		_ = configErrorFromResult(C.zlink_msg_close(&native))
		return err
	}
	err := submit(&native, C.zlink_part_flag_t(C.ZLINK_PART_FINAL))
	if err != nil {
		_ = configErrorFromResult(C.zlink_msg_close(&native))
		return err
	}
	_ = configErrorFromResult(C.zlink_msg_close(&part.msg))
	part.moved()
	return nil
}

func submitSinglePartMoved(part *Message, submit multipartSubmitFunc) error {
	if part == nil {
		return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	if part.closed {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	var native C.zlink_msg_t
	if err := configErrorFromResult(C.zlink_msg_init(&native)); err != nil {
		return err
	}
	if err := configErrorFromResult(C.zlink_msg_move(&native, &part.msg)); err != nil {
		_ = configErrorFromResult(C.zlink_msg_close(&native))
		return err
	}
	err := submit(&native, C.zlink_part_flag_t(C.ZLINK_PART_FINAL))
	if err != nil {
		_ = configErrorFromResult(C.zlink_msg_close(&native))
	}
	part.moved()
	return err
}

func submitSinglePartFromBytes(data []byte, submit multipartSubmitFunc) error {
	var native C.zlink_msg_t
	if err := initNativeMessageFromBytes(&native, data); err != nil {
		return err
	}
	err := submit(&native, C.zlink_part_flag_t(C.ZLINK_PART_FINAL))
	if err != nil {
		_ = configErrorFromResult(C.zlink_msg_close(&native))
	}
	return err
}

func builderMessages(parts []sendBuilderPart) []*Message {
	messages := make([]*Message, len(parts))
	for i, part := range parts {
		messages[i] = part.message
	}
	return messages
}

func closeMovedSendBuilderParts(parts []sendBuilderPart) {
	for _, part := range parts {
		if part.move && part.message != nil {
			_ = part.message.Close()
		}
	}
}

func sendBuilderPartsNeedBuilder(parts []sendBuilderPart) bool {
	for _, part := range parts {
		if part.move || part.bytes {
			return true
		}
	}
	return false
}

func sendBuilderPartsUseOnlyRetainedMessages(parts []sendBuilderPart) bool {
	for _, part := range parts {
		if part.move || part.bytes {
			return false
		}
	}
	return true
}

func submitMultipartFromBuilderParts(parts []sendBuilderPart, submit multipartSubmitFunc) error {
	if len(parts) == 0 {
		return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	if len(parts) == 1 {
		if parts[0].bytes {
			return submitSinglePartFromBytes(parts[0].data, submit)
		}
		if parts[0].move {
			return submitSinglePartMoved(parts[0].message, submit)
		}
		return submitSinglePartFromCopy(parts[0].message, submit)
	}
	if sendBuilderPartsUseOnlyRetainedMessages(parts) {
		return submitMultipartFromClones(builderMessages(parts), true, submit)
	}

	native := make([]C.zlink_msg_t, len(parts))
	for i, part := range parts {
		if part.bytes {
			if err := initNativeMessageFromBytes(&native[i], part.data); err != nil {
				closeNativeMultipart(native, i)
				return err
			}
			continue
		}
		if part.message == nil {
			closeNativeMultipart(native, i)
			return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
		}
		if part.message.closed {
			closeNativeMultipart(native, i)
			return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
		}
		if err := configErrorFromResult(C.zlink_msg_init(&native[i])); err != nil {
			closeNativeMultipart(native, i)
			return err
		}
		if part.move {
			if err := configErrorFromResult(C.zlink_msg_move(&native[i], &part.message.msg)); err != nil {
				closeNativeMultipart(native, i+1)
				return err
			}
		} else {
			if err := configErrorFromResult(C.zlink_msg_copy(&native[i], &part.message.msg)); err != nil {
				closeNativeMultipart(native, i+1)
				return err
			}
		}
	}

	prepared := &preparedMultipart{native: native, parts: builderMessages(parts)}
	err := submitPreparedMultipart(prepared, submit)
	for _, part := range parts {
		if part.move {
			part.message.moved()
			continue
		}
		if err == nil {
			_ = configErrorFromResult(C.zlink_msg_close(&part.message.msg))
			part.message.moved()
		}
	}
	return err
}

func requestBuilderMessages(parts []requestBuilderPart) []*Message {
	messages := make([]*Message, len(parts))
	for i, part := range parts {
		messages[i] = part.message
	}
	return messages
}

func requestBuilderMessagesForClone(parts []requestBuilderPart) ([]*Message, func(), error) {
	messages := make([]*Message, len(parts))
	temporary := make([]*Message, 0)
	cleanup := func() {
		for _, message := range temporary {
			_ = message.Close()
		}
	}
	for i, part := range parts {
		if part.bytes {
			message, err := NewMessage(part.data)
			if err != nil {
				cleanup()
				return nil, nil, err
			}
			temporary = append(temporary, message)
			messages[i] = message
			continue
		}
		messages[i] = part.message
	}
	return messages, cleanup, nil
}

func requestBuilderPartsUseOnlyMessages(parts []requestBuilderPart) bool {
	for _, part := range parts {
		if part.bytes {
			return false
		}
	}
	return true
}

func submitMultipartFromRequestParts(parts []requestBuilderPart, submit multipartSubmitFunc) error {
	if len(parts) == 0 {
		return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	if len(parts) == 1 {
		if parts[0].bytes {
			return submitSinglePartFromBytes(parts[0].data, submit)
		}
		return submitMultipartFromClones([]*Message{parts[0].message}, false, submit)
	}
	if requestBuilderPartsUseOnlyMessages(parts) {
		return submitMultipartFromClones(requestBuilderMessages(parts), false, submit)
	}

	native := make([]C.zlink_msg_t, len(parts))
	for i, part := range parts {
		if part.bytes {
			if err := initNativeMessageFromBytes(&native[i], part.data); err != nil {
				closeNativeMultipart(native, i)
				return err
			}
			continue
		}
		if part.message == nil {
			closeNativeMultipart(native, i)
			return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
		}
		if part.message.closed {
			closeNativeMultipart(native, i)
			return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
		}
		if err := configErrorFromResult(C.zlink_msg_init(&native[i])); err != nil {
			closeNativeMultipart(native, i)
			return err
		}
		if err := configErrorFromResult(C.zlink_msg_copy(&native[i], &part.message.msg)); err != nil {
			closeNativeMultipart(native, i+1)
			return err
		}
	}

	prepared := &preparedMultipart{native: native}
	return submitPreparedMultipart(prepared, submit)
}

func recvMultipart(flags RecvFlags, recv multipartRecvFunc) ([]*Message, error) {
	parts := make([]*Message, 0, 1)
	recvFlags := C.zlink_recv_flags_t(flags)
	for {
		var part C.zlink_msg_t
		if err := configErrorFromResult(C.zlink_msg_init(&part)); err != nil {
			closeMessageSlice(parts)
			return nil, err
		}

		var hasMore C.zlink_part_flag_t
		if err := recv(&part, &hasMore, recvFlags); err != nil {
			_ = configErrorFromResult(C.zlink_msg_close(&part))
			closeMessageSlice(parts)
			return nil, err
		}

		msg := &Message{}
		if err := configErrorFromResult(C.zlink_msg_adopt(&msg.msg, &part)); err != nil {
			_ = configErrorFromResult(C.zlink_msg_close(&part))
			closeMessageSlice(parts)
			return nil, err
		}
		parts = append(parts, msg)

		if hasMore == 0 {
			break
		}
		recvFlags = C.zlink_recv_flags_t(C.ZLINK_DONTWAIT)
	}

	return parts, nil
}

func takeParts(ptr *C.zlink_msg_t, partCount C.size_t) ([]*Message, error) {
	count := int(partCount)
	if count == 0 || ptr == nil {
		return nil, nil
	}
	raw := unsafe.Slice(ptr, count)
	parts := make([]*Message, 0, count)
	for i := 0; i < count; i++ {
		msg := &Message{}
		if err := configErrorFromResult(C.zlink_msg_init(&msg.msg)); err != nil {
			closeMessageSlice(parts)
			C.zlink_multipart_close(ptr, partCount)
			return nil, err
		}
		if err := configErrorFromResult(C.zlink_msg_move(&msg.msg, &raw[i])); err != nil {
			_ = msg.Close()
			closeMessageSlice(parts)
			C.zlink_multipart_close(ptr, partCount)
			return nil, err
		}
		parts = append(parts, msg)
	}
	C.zlink_multipart_close(ptr, partCount)
	return parts, nil
}

func bytePartsToMessages(parts [][]byte) ([]*Message, error) {
	if len(parts) == 0 {
		return nil, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	messages := make([]*Message, 0, len(parts))
	for _, part := range parts {
		msg, err := NewMessage(part)
		if err != nil {
			closeMessageSlice(messages)
			return nil, err
		}
		messages = append(messages, msg)
	}
	return messages, nil
}

func mustTakeParts(ptr *C.zlink_msg_t, partCount C.size_t) []*Message {
	parts, err := takeParts(ptr, partCount)
	if err != nil {
		panic(err)
	}
	return parts
}

func routingIDPointer(raw *C.zlink_routing_id_t) unsafe.Pointer {
	if raw == nil || raw.size == 0 {
		return nil
	}
	return unsafe.Pointer(&raw.data[0])
}

func getHandleRoutingID(handle unsafe.Pointer) (RoutingID, error) {
	var raw C.zlink_routing_id_t
	if err := configErrorFromResult(C.zlink_get_routing_id(handle, &raw)); err != nil {
		return RoutingID{}, err
	}
	return routingIDFromC(raw), nil
}

func withCStringPair(left string, right string, fn func(*C.char, *C.char) error) error {
	for _, value := range []string{left, right} {
		if err := validateEndpointString(value); err != nil {
			return err
		}
	}
	leftC := C.CString(left)
	defer C.free(unsafe.Pointer(leftC))
	rightC := C.CString(right)
	defer C.free(unsafe.Pointer(rightC))
	return fn(leftC, rightC)
}

func subscriptionAt(handle unsafe.Pointer, index int) (string, bool, error) {
	if index < 0 {
		return "", false, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	var size C.size_t
	var isPattern C.int
	err := configErrorFromResult(C.zlink_subscription_at(handle, C.size_t(index), nil, &size, &isPattern))
	if err == nil {
		return "", isPattern != 0, nil
	}
	zerr, ok := err.(*ConfigError)
	if !ok || zerr.Result != ConfigInvalidArgument || size == 0 {
		return "", false, err
	}
	buf := make([]byte, int(size))
	if err := configErrorFromResult(C.zlink_subscription_at(handle, C.size_t(index), (*C.char)(unsafe.Pointer(&buf[0])), &size, &isPattern)); err != nil {
		return "", false, err
	}
	return string(buf[:int(size)]), isPattern != 0, nil
}

func setTLSServer(handle unsafe.Pointer, certPath string, keyPath string, requireClientCert bool) error {
	return withCStringPair(certPath, keyPath, func(certC *C.char, keyC *C.char) error {
		var required C.int
		if requireClientCert {
			required = 1
		}
		return configErrorFromResult(C.zlink_set_tls_server(handle, certC, keyC, required))
	})
}

func setTLSClient(handle unsafe.Pointer, caCertPath string, hostname string, trustSystem bool) error {
	return withCStringPair(caCertPath, hostname, func(caCertC *C.char, hostnameC *C.char) error {
		var trust C.int
		if trustSystem {
			trust = 1
		}
		return configErrorFromResult(C.zlink_set_tls_client(handle, caCertC, hostnameC, trust))
	})
}

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
		return SubscribePartResult{}, &RecvError{Result: RecvInvalidHandle, internalErrno: int(C.EINVAL)}
	}
	if len(topicBuffer) == 0 {
		return SubscribePartResult{}, &RecvError{Result: RecvInvalidHandle, internalErrno: int(C.EINVAL)}
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
		return RecvPartResult{}, &RecvError{Result: RecvInvalidHandle, internalErrno: int(C.EINVAL)}
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
	call func(**C.zlink_routing_id_t, **C.zlink_routing_id_t, *C.uint64_t, *C.zlink_msg_t, *C.zlink_part_flag_t, C.zlink_recv_flags_t) error,
) (RecvPartResult, error) {
	if out == nil {
		return RecvPartResult{}, &RecvError{Result: RecvInvalidHandle, internalErrno: int(C.EINVAL)}
	}
	var sourceNodeRID *C.zlink_routing_id_t
	var sourceSpotRID *C.zlink_routing_id_t
	var requestSeq C.uint64_t
	var part C.zlink_msg_t
	if err := configErrorFromResult(C.zlink_msg_init(&part)); err != nil {
		return RecvPartResult{}, err
	}
	var hasMore C.zlink_part_flag_t
	if err := call(&sourceNodeRID, &sourceSpotRID, &requestSeq, &part, &hasMore, C.zlink_recv_flags_t(flags)); err != nil {
		_ = configErrorFromResult(C.zlink_msg_close(&part))
		return RecvPartResult{}, err
	}
	if err := adoptRecvPart(out, &part); err != nil {
		return RecvPartResult{}, err
	}
	seq := uint64(requestSeq)
	return RecvPartResult{
		RoutingID:     routingIDFromCPtr(sourceNodeRID),
		SpotRID:       routingIDFromCPtr(sourceSpotRID),
		RequestSeq:    seq,
		HasRequestSeq: seq != 0,
		More:          hasMore != C.ZLINK_PART_FINAL,
	}, nil
}

func recvSpotTopicMessageInto(
	out *TopicMessage,
	call func(**C.zlink_routing_id_t, *C.char, *C.size_t, *C.zlink_msg_t, *C.zlink_part_flag_t, C.zlink_recv_flags_t) error,
	flags RecvFlags,
) error {
	var sourceRID *C.zlink_routing_id_t
	var topicBuf [recvTopicBufferCap]byte
	topicLen := C.size_t(len(topicBuf))
	clonedParts, err := recvMultipart(flags, func(part *C.zlink_msg_t, hasMore *C.zlink_part_flag_t, recvFlags C.zlink_recv_flags_t) error {
		return call(
			&sourceRID,
			(*C.char)(unsafe.Pointer(&topicBuf[0])),
			&topicLen,
			part,
			hasMore,
			recvFlags,
		)
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

func recvSpotSubscriptionEvent(
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

type directSocket struct {
	*connectionSocket
}

// OnSendReady is promoted to Pair/Dealer via *directSocket embedding,
// eliminating the per-type duplicates while keeping it hidden from
// receive-only sockets (Sub/XSub) that don't embed *directSocket.
func (s *directSocket) OnSendReady(handler func()) error {
	return s.setSendReady(handler)
}

func (s *directSocket) submit(flags SendFlags, parts ...*Message) (bool, error) {
	err := submitMultipartFromClones(parts, true, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
		return submitErrorFromResult(C.zlink_send_part(s.raw(), part, C.zlink_send_flags_t(flags), partFlag))
	})
	return submitBackpressureResult(err)
}

func (s *directSocket) submitBuilder(flags SendFlags, parts []sendBuilderPart) (bool, error) {
	err := submitMultipartFromBuilderParts(parts, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
		return submitErrorFromResult(C.zlink_send_part(s.raw(), part, C.zlink_send_flags_t(flags), partFlag))
	})
	return submitBackpressureResult(err)
}

// Recv is the canonical caller-provided storage recv. Pass a long-lived
// *Received and the binding refills its internal state in place each
// successful call.
//
// Returns (true, nil) on success, (false, nil) when RecvFlagsDontWait
// finds no data, (false, *RecvError) on hard error.
//
// See doc/spec/bindings/README.md "Canonical Recv: Caller-Provided Storage".
func (s *directSocket) Recv(out *Received, flags RecvFlags) (bool, error) {
	if out == nil {
		return false, &RecvError{Result: RecvInvalidHandle, internalErrno: int(C.EINVAL)}
	}
	var sourceRID *C.zlink_routing_id_t
	clonedParts, err := recvMultipart(flags, func(part *C.zlink_msg_t, hasMore *C.zlink_part_flag_t, recvFlags C.zlink_recv_flags_t) error {
		return recvErrorFromResult(C.zlink_recv_part(s.raw(), &sourceRID, part, hasMore, recvFlags))
	})
	if err != nil {
		var recvErr *RecvError
		if errors.As(err, &recvErr) && recvErr.Result == RecvNoData {
			return false, nil
		}
		return false, err
	}
	out.replace(routingIDFromCPtr(sourceRID), RoutingID{}, clonedParts, 0, false, nil, nil, nil)
	return true, nil
}

func (s *directSocket) RecvPart(out *Message, flags RecvFlags) (RecvPartResult, bool, error) {
	result, err := recvDirectPartInto(out, flags, func(rid **C.zlink_routing_id_t, part *C.zlink_msg_t, hasMore *C.zlink_part_flag_t, recvFlags C.zlink_recv_flags_t) error {
		return recvErrorFromResult(C.zlink_recv_part(s.raw(), rid, part, hasMore, recvFlags))
	})
	if err != nil {
		var recvErr *RecvError
		if errors.As(err, &recvErr) && recvErr.Result == RecvNoData {
			return RecvPartResult{}, false, nil
		}
		return RecvPartResult{}, false, err
	}
	return result, true, nil
}

func (s *directSocket) onReceive(handler func(*Received)) error {
	if handler == nil {
		return &HandlerError{Result: HandlerInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	state := newRecvCallbackState(recvCallback(handler), nil)
	handle := cgo.NewHandle(state)
	if err := handlerErrorFromResult(C.zlink_recv_handler_go_local(s.raw(), C.uintptr_t(handle))); err != nil {
		state.close()
		handle.Delete()
		return err
	}
	if s.recvHandle != 0 {
		releaseCallbackHandle(s.recvHandle)
	}
	s.recvHandle = handle
	return nil
}

type publishSocket struct {
	*connectionSocket
}

// OnSendReady is promoted to Pub/XPub via the *publishSocket chain.
func (s *publishSocket) OnSendReady(handler func()) error {
	return s.setSendReady(handler)
}

// Pub-style options shared by PubSocket and XPubSocket. Defining them on
// *publishSocket means both concrete sockets inherit them via embedding,
// avoiding the previous two-place duplication.
func (s *publishSocket) SetNoDrop(value bool) error {
	return s.setPubBoolOption(C.ZLINK_PUB_OPT_NODROP, value)
}

func (s *publishSocket) NoDrop() (bool, error) {
	return s.getPubBoolOption(C.ZLINK_PUB_OPT_NODROP)
}

func (s *publishSocket) SetVerbose(value bool) error {
	return s.setPubBoolOption(C.ZLINK_PUB_OPT_VERBOSE, value)
}

func (s *publishSocket) Verbose() (bool, error) {
	return s.getPubBoolOption(C.ZLINK_PUB_OPT_VERBOSE)
}

func (s *publishSocket) SetVerboser(value bool) error {
	return s.setPubBoolOption(C.ZLINK_PUB_OPT_VERBOSER, value)
}

func (s *publishSocket) Verboser() (bool, error) {
	return s.getPubBoolOption(C.ZLINK_PUB_OPT_VERBOSER)
}

func (s *publishSocket) SetManual(value bool) error {
	return s.setPubBoolOption(C.ZLINK_PUB_OPT_MANUAL, value)
}

func (s *publishSocket) Manual() (bool, error) {
	return s.getPubBoolOption(C.ZLINK_PUB_OPT_MANUAL)
}

func (s *publishSocket) SetManualLastValue(value bool) error {
	return s.setPubBoolOption(C.ZLINK_PUB_OPT_MANUAL_LAST_VALUE, value)
}

func (s *publishSocket) ManualLastValue() (bool, error) {
	return s.getPubBoolOption(C.ZLINK_PUB_OPT_MANUAL_LAST_VALUE)
}

func (s *publishSocket) SetWelcomeMessage(message *Message) error {
	if message == nil {
		return s.setPubBytesOption(C.ZLINK_PUB_OPT_WELCOME_MSG, nil)
	}
	return s.setPubBytesOption(C.ZLINK_PUB_OPT_WELCOME_MSG, message.Data())
}

func (s *publishSocket) WelcomeMessage() (*Message, error) {
	data, err := s.getPubBytesOption(C.ZLINK_PUB_OPT_WELCOME_MSG, 256)
	if err != nil {
		return nil, err
	}
	return NewMessage(data)
}

func (s *publishSocket) ApproveSubscribe(routingID RoutingID) error {
	return s.setPubRoutingIDOption(C.ZLINK_PUB_OPT_APPROVE_SUBSCRIBE, routingID)
}

func (s *publishSocket) RejectSubscribe(routingID RoutingID) error {
	return s.setPubRoutingIDOption(C.ZLINK_PUB_OPT_REJECT_SUBSCRIBE, routingID)
}

func (s *publishSocket) PubOptions() *PubSocketOptions {
	return &PubSocketOptions{socket: s.connectionSocket}
}

func (s *publishSocket) submitPublish(topic string, flags SendFlags, parts ...*Message) (bool, error) {
	err := s.withCString(topic, func(cstr *C.char) error {
		return submitMultipartFromClones(parts, true, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
			return submitErrorFromResult(C.zlink_publish_part(s.raw(), cstr, part, C.zlink_send_flags_t(flags), partFlag))
		})
	})
	return submitBackpressureResult(err)
}

type routedSocket struct {
	*connectionSocket
}

// OnSendReady is promoted to Router via *routedSocket embedding.
// StreamSocket has its own copy because it doesn't embed *routedSocket.
func (s *routedSocket) OnSendReady(handler func()) error {
	return s.setSendReady(handler)
}

func (s *routedSocket) submitTo(target RoutingID, flags SendFlags, parts ...*Message) (bool, error) {
	rid := target.toC()
	err := submitMultipartFromClones(parts, true, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
		return submitErrorFromResult(C.zlink_send_part_rid(s.raw(), &rid, part, C.zlink_send_flags_t(flags), partFlag))
	})
	return submitBackpressureResult(err)
}

func (s *routedSocket) submitToBuilder(target RoutingID, flags SendFlags, parts []sendBuilderPart) (bool, error) {
	rid := target.toC()
	err := submitMultipartFromBuilderParts(parts, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
		return submitErrorFromResult(C.zlink_send_part_rid(s.raw(), &rid, part, C.zlink_send_flags_t(flags), partFlag))
	})
	return submitBackpressureResult(err)
}

func (s *routedSocket) submitToSpot(destNodeRid, destSpotRid RoutingID, flags SendFlags, parts ...*Message) (bool, error) {
	node := destNodeRid.toC()
	spot := destSpotRid.toC()
	err := submitMultipartFromClones(parts, true, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
		return submitErrorFromResult(C.zlink_router_send_spot_part_go_local(s.raw(), &node, &spot, part, C.zlink_send_flags_t(flags), partFlag))
	})
	return submitBackpressureResult(err)
}

func (s *routedSocket) submitToSpotBuilder(destNodeRid, destSpotRid RoutingID, flags SendFlags, parts []sendBuilderPart) (bool, error) {
	node := destNodeRid.toC()
	spot := destSpotRid.toC()
	err := submitMultipartFromBuilderParts(parts, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
		return submitErrorFromResult(C.zlink_router_send_spot_part_go_local(s.raw(), &node, &spot, part, C.zlink_send_flags_t(flags), partFlag))
	})
	return submitBackpressureResult(err)
}

func (s *routedSocket) requestToSpot(destNodeRid, destSpotRid RoutingID, callback RequestReplyCallback, flags SendFlags, timeout time.Duration, parts ...*Message) (bool, error) {
	if callback == nil {
		return false, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	state, err := s.startSpotRequest(destNodeRid, destSpotRid, flags, timeout, parts...)
	ok, err := submitBackpressureResult(err)
	if err != nil {
		return false, err
	}
	if !ok {
		return false, nil
	}
	dispatchRequestCallback(state, callback)
	return true, nil
}

func (s *routedSocket) requestToSpotCallback(destNodeRid, destSpotRid RoutingID, callback RequestReplyCallback, flags SendFlags, timeout time.Duration, parts ...*Message) (bool, error) {
	return s.requestToSpot(destNodeRid, destSpotRid, callback, flags, timeout, parts...)
}

func (s *routedSocket) reply(rid RoutingID, requestSeq uint64, flags SendFlags, parts ...*Message) error {
	if err := validateReplyFlags(flags); err != nil {
		return err
	}
	target := rid.toC()
	return submitMultipartFromClones(parts, false, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
		return submitErrorFromResult(C.zlink_router_reply_part(s.raw(), &target, C.uint64_t(requestSeq), part, partFlag))
	})
}

func (s *routedSocket) replyToSpot(destNodeRid, destSpotRid RoutingID, requestSeq uint64, flags SendFlags, parts ...*Message) (bool, error) {
	if err := validateReplyFlags(flags); err != nil {
		return false, err
	}
	node := destNodeRid.toC()
	spot := destSpotRid.toC()
	err := submitMultipartFromClones(parts, false, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
		return submitErrorFromResult(C.zlink_router_reply_spot_part(s.raw(), &node, &spot, C.uint64_t(requestSeq), part, partFlag))
	})
	return submitBackpressureResult(err)
}

func (s *routedSocket) recvInto(out *Received, flags RecvFlags) error {
	recvOnce := func(recvFlags RecvFlags) (*C.zlink_routing_id_t, *C.zlink_routing_id_t, C.uint64_t, []*Message, error) {
		var nodeRID *C.zlink_routing_id_t
		var spotRID *C.zlink_routing_id_t
		var requestSeq C.uint64_t
		parts, err := recvMultipart(recvFlags, func(part *C.zlink_msg_t, hasMore *C.zlink_part_flag_t, currentFlags C.zlink_recv_flags_t) error {
			return recvErrorFromResult(C.zlink_router_recv_part(s.raw(), &nodeRID, &spotRID, &requestSeq, part, hasMore, currentFlags))
		})
		if err != nil {
			return nil, nil, 0, nil, err
		}
		return nodeRID, spotRID, requestSeq, parts, nil
	}

	var nodeRID *C.zlink_routing_id_t
	var spotRID *C.zlink_routing_id_t
	var requestSeq C.uint64_t
	var parts []*Message
	if flags == RecvFlagsNone {
		primedNodeRID, primedSpotRID, primedRequestSeq, primedParts, primedErr := recvOnce(RecvFlagsDontWait)
		if primedErr == nil {
			nodeRID = primedNodeRID
			spotRID = primedSpotRID
			requestSeq = primedRequestSeq
			parts = primedParts
		} else {
			var recvErr *RecvError
			if !errors.As(primedErr, &recvErr) || recvErr.Result != RecvNoData {
				return primedErr
			}
			var err error
			nodeRID, spotRID, requestSeq, parts, err = recvOnce(flags)
			if err != nil {
				return err
			}
		}
	} else {
		var err error
		nodeRID, spotRID, requestSeq, parts, err = recvOnce(flags)
		if err != nil {
			return err
		}
	}
	routingID := routingIDFromCPtr(nodeRID)
	spotRoutingID := routingIDFromCPtr(spotRID)
	seq := uint64(requestSeq)
	hasSeq := requestSeq != 0
	var reply func(SendFlags, []*Message) error
	var send func(SendFlags, []*Message) (bool, error)
	var sendBuilder func(SendFlags, []sendBuilderPart) (bool, error)
	if hasSeq {
		if spotRoutingID.Size() == 0 {
			reply = receivedReplyToRouter(s.reply, routingID, seq)
		} else {
			reply = func(flags SendFlags, parts []*Message) error {
				_, err := s.replyToSpot(routingID, spotRoutingID, seq, flags, parts...)
				return err
			}
		}
	}
	if routingID.Size() > 0 {
		if spotRoutingID.Size() == 0 {
			send = receivedSendToRouter(s.submitTo, routingID)
			sendBuilder = func(flags SendFlags, parts []sendBuilderPart) (bool, error) {
				return s.submitToBuilder(routingID, flags, parts)
			}
		} else {
			send = func(flags SendFlags, parts []*Message) (bool, error) {
				return s.submitToSpot(routingID, spotRoutingID, flags, parts...)
			}
			sendBuilder = func(flags SendFlags, parts []sendBuilderPart) (bool, error) {
				return s.submitToSpotBuilder(routingID, spotRoutingID, flags, parts)
			}
		}
	}
	out.replace(routingID, spotRoutingID, parts, seq, hasSeq, reply, send, sendBuilder)
	return nil
}

// Recv is the canonical caller-provided storage recv. Returns
// (true, nil) on success, (false, nil) when RecvFlagsDontWait finds no
// data, (false, *RecvError) on hard error. See
// doc/spec/bindings/README.md "Canonical Recv: Caller-Provided Storage".
func (s *routedSocket) Recv(out *Received, flags RecvFlags) (bool, error) {
	if out == nil {
		return false, &RecvError{Result: RecvInvalidHandle, internalErrno: int(C.EINVAL)}
	}
	if s.recvHandle != 0 {
		return false, &RecvError{Result: RecvBusy, internalErrno: int(C.EBUSY)}
	}
	if err := s.recvInto(out, flags); err != nil {
		var recvErr *RecvError
		if errors.As(err, &recvErr) && recvErr.Result == RecvNoData {
			return false, nil
		}
		return false, err
	}
	return true, nil
}

func (s *routedSocket) RecvPart(out *Message, flags RecvFlags) (RecvPartResult, bool, error) {
	result, err := recvRoutedPartInto(out, flags, func(nodeRID **C.zlink_routing_id_t, spotRID **C.zlink_routing_id_t, requestSeq *C.uint64_t, part *C.zlink_msg_t, hasMore *C.zlink_part_flag_t, recvFlags C.zlink_recv_flags_t) error {
		return recvErrorFromResult(C.zlink_router_recv_part(s.raw(), nodeRID, spotRID, requestSeq, part, hasMore, recvFlags))
	})
	if err != nil {
		var recvErr *RecvError
		if errors.As(err, &recvErr) && recvErr.Result == RecvNoData {
			return RecvPartResult{}, false, nil
		}
		return RecvPartResult{}, false, err
	}
	return result, true, nil
}

func (s *routedSocket) startSpotRequest(destNodeRid, destSpotRid RoutingID, flags SendFlags, timeout time.Duration, parts ...*Message) (*replyCallbackState, error) {
	builderParts := make([]requestBuilderPart, len(parts))
	for i, part := range parts {
		builderParts[i] = requestBuilderPart{message: part}
	}
	return s.startSpotRequestBuilder(destNodeRid, destSpotRid, flags, timeout, builderParts)
}

func (s *routedSocket) startSpotRequestBuilder(destNodeRid, destSpotRid RoutingID, flags SendFlags, timeout time.Duration, parts []requestBuilderPart) (*replyCallbackState, error) {
	if timeout <= 0 {
		timeout = defaultRequestTimeout
	}
	state := newReplyCallbackState()
	handle := cgo.NewHandle(state)
	node := destNodeRid.toC()
	spot := destSpotRid.toC()
	if err := submitMultipartFromRequestParts(parts, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
		return submitErrorFromResult(C.zlink_router_request_spot_part_go_local(
			s.raw(),
			&node,
			&spot,
			part,
			C.zlink_send_flags_t(flags),
			partFlag,
			C.uint32_t(requestTimeoutMillis(timeout)),
			C.uintptr_t(handle),
		))
	}); err != nil {
		handle.Delete()
		return nil, err
	}
	startSocketRequestProgress(s.raw(), state)
	return state, nil
}

type subscribeSocket struct {
	*connectionSocket
}

func (s *subscribeSocket) SetSubscription(filter string) error {
	return s.withCString(filter, func(cstr *C.char) error {
		return configErrorFromResult(C.zlink_set_subscription(s.raw(), cstr))
	})
}

func (s *subscribeSocket) UnsetSubscription(filter string) error {
	return s.withCString(filter, func(cstr *C.char) error {
		return configErrorFromResult(C.zlink_unset_subscription(s.raw(), cstr))
	})
}

func (s *subscribeSocket) Subscribe(out *TopicMessage, flags RecvFlags) (bool, error) {
	if out == nil {
		return false, &RecvError{Result: RecvInvalidHandle, internalErrno: int(C.EINVAL)}
	}
	err := recvTopicMessageInto(out, func(rid **C.zlink_routing_id_t, topic *C.char, topicLen *C.size_t, part *C.zlink_msg_t, hasMore *C.zlink_part_flag_t, recvFlags C.zlink_recv_flags_t) error {
		return recvErrorFromResult(C.zlink_subscribe_part(s.raw(), rid, topic, recvTopicBufferCap, topicLen, part, hasMore, recvFlags))
	}, flags)
	if err != nil {
		var recvErr *RecvError
		if errors.As(err, &recvErr) && recvErr.Result == RecvNoData {
			return false, nil
		}
		return false, err
	}
	return true, nil
}

func (s *subscribeSocket) SubscribePart(out *Message, topicBuffer []byte, flags RecvFlags) (SubscribePartResult, bool, error) {
	result, err := recvSubscribePartInto(out, topicBuffer, flags, func(rid **C.zlink_routing_id_t, topic *C.char, topicCap C.size_t, topicLen *C.size_t, part *C.zlink_msg_t, hasMore *C.zlink_part_flag_t, recvFlags C.zlink_recv_flags_t) error {
		return recvErrorFromResult(C.zlink_subscribe_part(s.raw(), rid, topic, topicCap, topicLen, part, hasMore, recvFlags))
	})
	if err != nil {
		var recvErr *RecvError
		if errors.As(err, &recvErr) && recvErr.Result == RecvNoData {
			return SubscribePartResult{}, false, nil
		}
		return SubscribePartResult{}, false, err
	}
	return result, true, nil
}

type xpubSubscribeSocket struct {
	*publishSocket
}

func (s *xpubSubscribeSocket) ReceiveSubscriptionEvent(out *SubscriptionEvent, flags RecvFlags) (bool, error) {
	if out == nil {
		return false, &RecvError{Result: RecvInvalidHandle, internalErrno: int(C.EINVAL)}
	}
	fresh, err := recvSubscriptionEvent(func(rid *C.zlink_routing_id_t, subscribed *C.int, topic *C.char, topicLen *C.size_t, recvFlags C.zlink_recv_flags_t) error {
		var sourceRID *C.zlink_routing_id_t
		if err := recvErrorFromResult(C.zlink_xpub_recv_part(s.raw(), &sourceRID, subscribed, topic, recvTopicBufferCap, topicLen, recvFlags)); err != nil {
			return err
		}
		if sourceRID != nil {
			*rid = *sourceRID
		} else {
			*rid = C.zlink_routing_id_t{}
		}
		return nil
	}, flags)
	if err != nil {
		var recvErr *RecvError
		if errors.As(err, &recvErr) && recvErr.Result == RecvNoData {
			return false, nil
		}
		return false, err
	}
	out.adoptFrom(fresh)
	return true, nil
}

func (s *connectionSocket) setSendReady(handler func()) error {
	if handler == nil {
		return &HandlerError{Result: HandlerInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	state := newSendReadyCallbackState(sendReadyCallback(handler))
	handle := cgo.NewHandle(state)
	if err := handlerErrorFromResult(C.zlink_send_ready_handler_go_local(s.raw(), C.uintptr_t(handle))); err != nil {
		state.close()
		handle.Delete()
		return err
	}
	if s.sendReadyHandle != 0 {
		releaseCallbackHandle(s.sendReadyHandle)
	}
	s.sendReadyHandle = handle
	return nil
}

type PairSocket struct {
	*directSocket
}

func newPairSocket(ctx *Context) (*PairSocket, error) {
	core, err := newSocketCore(ctx, C.ZLINK_SOCKET_PAIR)
	if err != nil {
		return nil, err
	}
	return &PairSocket{
		directSocket: &directSocket{connectionSocket: &connectionSocket{socketCore: core}},
	}, nil
}

func (s *PairSocket) Send() SendOp {
	return newSendBuilder(nil, func(parts []sendBuilderPart, flags SendFlags) error {
		return submitMultipartFromBuilderParts(parts, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
			return submitErrorFromResult(C.zlink_send_part(s.raw(), part, C.zlink_send_flags_t(flags), partFlag))
		})
	})
}

type PubSocket struct {
	*publishSocket
}

func newPubSocket(ctx *Context, socketType C.zlink_socket_type_t) (*PubSocket, error) {
	core, err := newSocketCore(ctx, socketType)
	if err != nil {
		return nil, err
	}
	return &PubSocket{
		publishSocket: &publishSocket{connectionSocket: &connectionSocket{socketCore: core}},
	}, nil
}

func (s *PubSocket) AttachDiscovery(discovery *Discovery) error {
	if discovery == nil || discovery.closed {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	return configErrorFromResult(C.zlink_socket_attach_discovery(s.raw(), discovery.raw()))
}

func (s *PubSocket) Publish(topic string) SendOp {
	return newSendBuilder(nil, func(parts []sendBuilderPart, flags SendFlags) error {
		return s.withCString(topic, func(cstr *C.char) error {
			return submitMultipartFromBuilderParts(parts, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
				return submitErrorFromResult(C.zlink_publish_part(s.raw(), cstr, part, C.zlink_send_flags_t(flags), partFlag))
			})
		})
	})
}

type SubSocket struct {
	*subscribeSocket
}

func newSubSocket(ctx *Context, socketType C.zlink_socket_type_t) (*SubSocket, error) {
	core, err := newSocketCore(ctx, socketType)
	if err != nil {
		return nil, err
	}
	return &SubSocket{
		subscribeSocket: &subscribeSocket{connectionSocket: &connectionSocket{socketCore: core}},
	}, nil
}

func (s *SubSocket) AttachDiscovery(discovery *Discovery) error {
	if discovery == nil || discovery.closed {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	return configErrorFromResult(C.zlink_socket_attach_discovery(s.raw(), discovery.raw()))
}

func (s *SubSocket) SubscriptionAt(index int) (string, bool, error) {
	return subscriptionAt(s.raw(), index)
}

func (s *SubSocket) TopicsCount() (int, error) {
	return s.connectionSocket.getSubIntOption(C.ZLINK_SUB_OPT_TOPICS_COUNT)
}

type DealerSocket struct {
	*directSocket
}

func newDealerSocket(ctx *Context) (*DealerSocket, error) {
	core, err := newSocketCore(ctx, C.ZLINK_SOCKET_DEALER)
	if err != nil {
		return nil, err
	}
	return &DealerSocket{
		directSocket: &directSocket{connectionSocket: &connectionSocket{socketCore: core}},
	}, nil
}

func borrowedTimer(handle unsafe.Pointer) *Timer {
	return &Timer{handle: handle}
}

func borrowedDealerSocket(handle unsafe.Pointer) *DealerSocket {
	return &DealerSocket{directSocket: &directSocket{connectionSocket: &connectionSocket{socketCore: &socketCore{handle: handle}}}}
}

func (s *DealerSocket) SetRoutingID(id RoutingID) error {
	raw := id.toC()
	return configErrorFromResult(C.zlink_set_routing_id(s.raw(), routingIDPointer(&raw), C.size_t(raw.size)))
}

func (s *DealerSocket) SetProbe(value bool) error {
	var raw C.int
	if value {
		raw = 1
	}
	return configErrorFromResult(C.zlink_set_dealer_option(s.raw(), C.ZLINK_DEALER_OPT_PROBE, unsafe.Pointer(&raw), C.size_t(C.sizeof_int)))
}

func (s *DealerSocket) RoutingID() (RoutingID, error) {
	return getHandleRoutingID(s.raw())
}

func (s *DealerSocket) SetWeight(value int) error {
	raw := C.int(value)
	return configErrorFromResult(C.zlink_set_dealer_option(s.raw(), C.ZLINK_DEALER_OPT_WEIGHT, unsafe.Pointer(&raw), C.size_t(C.sizeof_int)))
}

func (s *DealerSocket) Weight() (int, error) {
	var raw C.int
	size := C.size_t(C.sizeof_int)
	if err := configErrorFromResult(C.zlink_get_option(s.raw(), C.zlink_option_t(C.ZLINK_DEALER_OPT_WEIGHT), unsafe.Pointer(&raw), &size)); err != nil {
		return 0, err
	}
	return int(raw), nil
}

func (s *DealerSocket) SetRequestTimeout(value time.Duration) error {
	ms, err := durationToMillis(value)
	if err != nil {
		return err
	}
	raw := C.int(ms)
	return configErrorFromResult(C.zlink_set_dealer_option(s.raw(), C.ZLINK_DEALER_OPT_REQUEST_TIMEOUT_MS, unsafe.Pointer(&raw), C.size_t(C.sizeof_int)))
}

func (s *DealerSocket) AttachDiscovery(discovery *Discovery) error {
	if discovery == nil || discovery.closed {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	return configErrorFromResult(C.zlink_socket_attach_discovery(s.raw(), discovery.raw()))
}

func (s *DealerSocket) Send() SendOp {
	return newSendBuilder(nil, func(parts []sendBuilderPart, flags SendFlags) error {
		return submitMultipartFromBuilderParts(parts, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
			return submitErrorFromResult(C.zlink_send_part(s.raw(), part, C.zlink_send_flags_t(flags), partFlag))
		})
	})
}

func (s *DealerSocket) Request() RequestOp {
	return newRequestBuilder(nil, func(parts []requestBuilderPart, flags SendFlags, timeout time.Duration, callback RequestReplyCallback) error {
		if callback == nil {
			return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
		}
		messages, cleanup, err := requestBuilderMessagesForClone(parts)
		if err != nil {
			return err
		}
		defer cleanup()
		resultCh, err := (&dealerRequestSupport{socket: s}).startRequest(flags, timeout, messages...)
		if err != nil {
			return err
		}
		dispatchRequestCallback(resultCh, callback)
		return nil
	})
}

type RouterSocket struct {
	*routedSocket
}

func newRouterSocket(ctx *Context) (*RouterSocket, error) {
	core, err := newSocketCore(ctx, C.ZLINK_SOCKET_ROUTER)
	if err != nil {
		return nil, err
	}
	return &RouterSocket{
		routedSocket: &routedSocket{connectionSocket: &connectionSocket{socketCore: core}},
	}, nil
}

func (s *RouterSocket) SetMandatory(value bool) error {
	var raw C.int
	if value {
		raw = 1
	}
	return configErrorFromResult(C.zlink_set_router_option(s.raw(), C.ZLINK_ROUTER_OPT_MANDATORY, unsafe.Pointer(&raw), C.size_t(C.sizeof_int)))
}

func (s *RouterSocket) SetProbe(value bool) error {
	var raw C.int
	if value {
		raw = 1
	}
	return configErrorFromResult(C.zlink_set_router_option(s.raw(), C.ZLINK_ROUTER_OPT_PROBE, unsafe.Pointer(&raw), C.size_t(C.sizeof_int)))
}

func (s *RouterSocket) SetRoutingID(id RoutingID) error {
	raw := id.toC()
	return configErrorFromResult(C.zlink_set_routing_id(s.raw(), routingIDPointer(&raw), C.size_t(raw.size)))
}

func (s *RouterSocket) SetWeight(value int) error {
	raw := C.int(value)
	return configErrorFromResult(C.zlink_set_router_option(s.raw(), C.ZLINK_ROUTER_OPT_WEIGHT, unsafe.Pointer(&raw), C.size_t(C.sizeof_int)))
}

func (s *RouterSocket) Weight() (int, error) {
	var raw C.int
	size := C.size_t(C.sizeof_int)
	if err := configErrorFromResult(C.zlink_get_router_option(s.raw(), C.ZLINK_ROUTER_OPT_WEIGHT, unsafe.Pointer(&raw), &size)); err != nil {
		return 0, err
	}
	return int(raw), nil
}

func (s *RouterSocket) SetRequestTimeout(value time.Duration) error {
	ms, err := durationToMillis(value)
	if err != nil {
		return err
	}
	raw := C.int(ms)
	return configErrorFromResult(C.zlink_set_router_option(s.raw(), C.ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS, unsafe.Pointer(&raw), C.size_t(C.sizeof_int)))
}

func (s *RouterSocket) RequestTimeout() (time.Duration, error) {
	var raw C.int
	size := C.size_t(C.sizeof_int)
	if err := configErrorFromResult(C.zlink_get_router_option(s.raw(), C.ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS, unsafe.Pointer(&raw), &size)); err != nil {
		return 0, err
	}
	return time.Duration(raw) * time.Millisecond, nil
}

func (s *RouterSocket) SetHandover(value bool) error {
	var raw C.int
	if value {
		raw = C.int(C.ZLINK_RID_DUPLICATE_HANDOVER)
	}
	return configErrorFromResult(C.zlink_set_option(s.raw(), C.ZLINK_OPT_RID_DUPLICATE_POLICY, unsafe.Pointer(&raw), C.size_t(C.sizeof_int)))
}

func (s *RouterSocket) SetConnectRoutingID(id RoutingID) error {
	raw := id.toC()
	return configErrorFromResult(C.zlink_set_router_option(s.raw(), C.ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID, routingIDPointer(&raw), C.size_t(raw.size)))
}

func (s *RouterSocket) RoutingID() (RoutingID, error) {
	return getHandleRoutingID(s.raw())
}

func (s *RouterSocket) AttachDiscovery(discovery *Discovery) error {
	if discovery == nil || discovery.closed {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	return configErrorFromResult(C.zlink_socket_attach_discovery(s.raw(), discovery.raw()))
}

func (s *RouterSocket) SendTo(target RoutingID) SendOp {
	return newSendBuilder(nil, func(parts []sendBuilderPart, flags SendFlags) error {
		rid := target.toC()
		return submitMultipartFromBuilderParts(parts, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
			return submitErrorFromResult(C.zlink_send_part_rid(s.raw(), &rid, part, C.zlink_send_flags_t(flags), partFlag))
		})
	})
}

func (s *RouterSocket) Request(peerRid RoutingID) RequestOp {
	return newRequestBuilder(nil, func(parts []requestBuilderPart, flags SendFlags, timeout time.Duration, callback RequestReplyCallback) error {
		if callback == nil {
			return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
		}
		messages, cleanup, err := requestBuilderMessagesForClone(parts)
		if err != nil {
			return err
		}
		defer cleanup()
		resultCh, err := (&routerRequestSupport{socket: s}).startRequest(peerRid, flags, timeout, messages...)
		if err != nil {
			return err
		}
		dispatchRequestCallback(resultCh, callback)
		return nil
	})
}

func (s *RouterSocket) Reply(rid RoutingID, requestSeq uint64) ReplyOp {
	return newReplyBuilder(nil, func(parts []*Message, flags SendFlags) error {
		_, err := (&routerRequestSupport{socket: s}).Reply(rid, requestSeq, flags, parts...)
		return err
	})
}

func (s *RouterSocket) SendToSpot(destNodeRid, destSpotRid RoutingID) SendOp {
	return newSendBuilder(nil, func(parts []sendBuilderPart, flags SendFlags) error {
		node := destNodeRid.toC()
		spot := destSpotRid.toC()
		return submitMultipartFromBuilderParts(parts, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
			return submitErrorFromResult(C.zlink_router_send_spot_part_go_local(s.raw(), &node, &spot, part, C.zlink_send_flags_t(flags), partFlag))
		})
	})
}

func (s *RouterSocket) RequestToSpot(destNodeRid, destSpotRid RoutingID) RequestOp {
	return newRequestBuilder(nil, func(parts []requestBuilderPart, flags SendFlags, timeout time.Duration, callback RequestReplyCallback) error {
		if callback == nil {
			return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
		}
		state, err := s.routedSocket.startSpotRequestBuilder(destNodeRid, destSpotRid, flags, timeout, parts)
		if err != nil {
			return err
		}
		dispatchRequestCallback(state, callback)
		return nil
	})
}

func (s *RouterSocket) ReplyToSpot(destNodeRid, destSpotRid RoutingID, requestSeq uint64) ReplyOp {
	return newReplyBuilder(nil, func(parts []*Message, flags SendFlags) error {
		_, err := s.routedSocket.replyToSpot(destNodeRid, destSpotRid, requestSeq, flags, parts...)
		return err
	})
}

type XPubSocket struct {
	*xpubSubscribeSocket
}

func newXPubSocket(ctx *Context) (*XPubSocket, error) {
	pub, err := newPubSocket(ctx, C.ZLINK_SOCKET_XPUB)
	if err != nil {
		return nil, err
	}
	return &XPubSocket{xpubSubscribeSocket: &xpubSubscribeSocket{publishSocket: pub.publishSocket}}, nil
}

func (s *XPubSocket) Publish(topic string) SendOp {
	return newSendBuilder(nil, func(parts []sendBuilderPart, flags SendFlags) error {
		return s.withCString(topic, func(cstr *C.char) error {
			return submitMultipartFromBuilderParts(parts, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
				return submitErrorFromResult(C.zlink_publish_part(s.raw(), cstr, part, C.zlink_send_flags_t(flags), partFlag))
			})
		})
	})
}

type XSubSocket struct {
	*subscribeSocket
}

func newXSubSocket(ctx *Context) (*XSubSocket, error) {
	core, err := newSocketCore(ctx, C.ZLINK_SOCKET_XSUB)
	if err != nil {
		return nil, err
	}
	return &XSubSocket{
		subscribeSocket: &subscribeSocket{connectionSocket: &connectionSocket{socketCore: core}},
	}, nil
}

func (s *XSubSocket) SubscriptionAt(index int) (string, bool, error) {
	return subscriptionAt(s.raw(), index)
}

func (s *XSubSocket) TopicsCount() (int, error) {
	return s.connectionSocket.getSubIntOption(C.ZLINK_SUB_OPT_TOPICS_COUNT)
}

type StreamSocket struct {
	core          *routedSocket
	boundSessions sync.Map // RoutingID.Hex() -> actorID string
}

func newStreamSocket(ctx *Context) (*StreamSocket, error) {
	core, err := newSocketCore(ctx, C.ZLINK_SOCKET_STREAM)
	if err != nil {
		return nil, err
	}
	return &StreamSocket{
		core: &routedSocket{connectionSocket: &connectionSocket{socketCore: core}},
	}, nil
}

func (s *StreamSocket) raw() unsafe.Pointer {
	if s == nil || s.core == nil {
		return nil
	}
	return s.core.raw()
}

func (s *StreamSocket) Bind(endpoint string) error {
	return s.core.Bind(endpoint)
}

func (s *StreamSocket) Unbind(endpoint string) error {
	return s.core.Unbind(endpoint)
}

func (s *StreamSocket) Close() error {
	if s == nil || s.core == nil {
		return nil
	}
	return s.core.Close()
}

func (s *StreamSocket) SetSendHighWaterMark(value int) error {
	return s.core.SetSendHighWaterMark(value)
}

func (s *StreamSocket) SendHighWaterMark() (int, error) {
	return s.core.SendHighWaterMark()
}

func (s *StreamSocket) SetReceiveHighWaterMark(value int) error {
	return s.core.SetReceiveHighWaterMark(value)
}

func (s *StreamSocket) ReceiveHighWaterMark() (int, error) {
	return s.core.ReceiveHighWaterMark()
}

func (s *StreamSocket) SetLinger(value time.Duration) error {
	return s.core.SetLinger(value)
}

func (s *StreamSocket) SetReceiveTimeout(value time.Duration) error {
	return s.core.SetReceiveTimeout(value)
}

func (s *StreamSocket) SetSendTimeout(value time.Duration) error {
	return s.core.SetSendTimeout(value)
}

func (s *StreamSocket) SetTCPKeepalive(value bool) error {
	return s.core.SetTCPKeepalive(value)
}

func (s *StreamSocket) SetTCPNoDelay(value bool) error {
	return s.core.SetTCPNoDelay(value)
}

func (s *StreamSocket) SetIPv6(value bool) error {
	return s.core.SetIPv6(value)
}

func (s *StreamSocket) LastEndpoint() (string, error) {
	return s.core.LastEndpoint()
}

func (s *StreamSocket) SetTLSServer(certPath string, keyPath string, requireClientCert bool) error {
	return s.core.SetTLSServer(certPath, keyPath, requireClientCert)
}

func (s *StreamSocket) SetTLSClient(caCertPath string, hostname string, trustSystem bool) error {
	return s.core.SetTLSClient(caCertPath, hostname, trustSystem)
}

func (s *StreamSocket) SetRoutingID(id RoutingID) error {
	raw := id.toC()
	return configErrorFromResult(C.zlink_set_routing_id(s.raw(), routingIDPointer(&raw), C.size_t(raw.size)))
}

func (s *StreamSocket) RoutingID() (RoutingID, error) {
	return getHandleRoutingID(s.raw())
}

func (s *StreamSocket) SendTo(target RoutingID) SendOp {
	return newSendBuilder(nil, func(parts []sendBuilderPart, flags SendFlags) error {
		rid := target.toC()
		return submitMultipartFromBuilderParts(parts, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
			return submitErrorFromResult(C.zlink_send_part_rid(s.raw(), &rid, part, C.zlink_send_flags_t(flags), partFlag))
		})
	})
}

// Recv is the canonical caller-provided storage recv. See
// doc/spec/bindings/README.md "Canonical Recv: Caller-Provided Storage".
func (s *StreamSocket) Recv(out *Received, flags RecvFlags) (bool, error) {
	ok, err := (&directSocket{connectionSocket: s.core.connectionSocket}).Recv(out, flags)
	if err != nil || !ok {
		return ok, err
	}
	if out.routingID.Size() > 0 {
		out.send = receivedSendToRouter(s.core.submitTo, out.routingID)
	}
	return true, nil
}

func (s *StreamSocket) OnPacket(handler func(RoutingID, *Message, *Message)) error {
	if handler == nil {
		return &HandlerError{Result: HandlerInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	if s == nil || s.core == nil || s.core.closed {
		return &HandlerError{Result: HandlerInvalidArgument, internalErrno: int(C.EFAULT)}
	}
	state := newStreamPacketCallbackState(handler)
	handle := cgo.NewHandle(state)
	if err := handlerErrorFromResult(C.zlink_stream_packet_handler_go_local(s.raw(), C.uintptr_t(handle))); err != nil {
		state.close()
		handle.Delete()
		return err
	}
	if s.core.streamPacketHandle != 0 {
		releaseCallbackHandle(s.core.streamPacketHandle)
	}
	s.core.streamPacketHandle = handle
	return nil
}

func (s *StreamSocket) SetNotify(value bool) error {
	var raw C.int
	if value {
		raw = 1
	}
	return configErrorFromResult(C.zlink_set_stream_option(s.raw(), C.ZLINK_STREAM_OPT_NOTIFY, unsafe.Pointer(&raw), C.size_t(C.sizeof_int)))
}

func (s *StreamSocket) Notify() (bool, error) {
	var raw C.int
	size := C.size_t(C.sizeof_int)
	if err := configErrorFromResult(C.zlink_get_stream_option(s.raw(), C.ZLINK_STREAM_OPT_NOTIFY, unsafe.Pointer(&raw), &size)); err != nil {
		return false, err
	}
	return raw != 0, nil
}

func (s *StreamSocket) OnSendReady(handler func()) error {
	return s.core.connectionSocket.setSendReady(handler)
}
