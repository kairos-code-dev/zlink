// SPDX-License-Identifier: MPL-2.0

package zlink

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

const (
	RidDuplicateReject   = 0
	RidDuplicateHandover = 1
)

type RIDDuplicatePolicy int

const (
	RIDDuplicateReject   RIDDuplicatePolicy = 0
	RIDDuplicateHandover RIDDuplicatePolicy = 1
)

type CommonSocketOptions struct {
	socket *connectionSocket
}

type PubSocketOptions struct {
	socket *connectionSocket
}

func (o *PubSocketOptions) SetNoDrop(value bool) error {
	return o.socket.setPubBoolOption(C.ZLINK_PUB_OPT_NODROP, value)
}

func (o *PubSocketOptions) NoDrop() (bool, error) {
	return o.socket.getPubBoolOption(C.ZLINK_PUB_OPT_NODROP)
}

func (o *PubSocketOptions) SetVerbose(value bool) error {
	return o.socket.setPubBoolOption(C.ZLINK_PUB_OPT_VERBOSE, value)
}

func (o *PubSocketOptions) Verbose() (bool, error) {
	return o.socket.getPubBoolOption(C.ZLINK_PUB_OPT_VERBOSE)
}

func (o *PubSocketOptions) SetVerboser(value bool) error {
	return o.socket.setPubBoolOption(C.ZLINK_PUB_OPT_VERBOSER, value)
}

func (o *PubSocketOptions) Verboser() (bool, error) {
	return o.socket.getPubBoolOption(C.ZLINK_PUB_OPT_VERBOSER)
}

func (o *PubSocketOptions) SetManual(value bool) error {
	return o.socket.setPubBoolOption(C.ZLINK_PUB_OPT_MANUAL, value)
}

func (o *PubSocketOptions) Manual() (bool, error) {
	return o.socket.getPubBoolOption(C.ZLINK_PUB_OPT_MANUAL)
}

func (o *PubSocketOptions) TopicsCount() (int, error) {
	return o.socket.getPubIntOption(C.ZLINK_PUB_OPT_TOPICS_COUNT)
}

func (o *PubSocketOptions) SetManualLastValue(value bool) error {
	return o.socket.setPubBoolOption(C.ZLINK_PUB_OPT_MANUAL_LAST_VALUE, value)
}

func (o *PubSocketOptions) ManualLastValue() (bool, error) {
	return o.socket.getPubBoolOption(C.ZLINK_PUB_OPT_MANUAL_LAST_VALUE)
}

func (o *PubSocketOptions) SetWelcomeMessage(message *Message) error {
	if message == nil {
		return o.socket.setPubBytesOption(C.ZLINK_PUB_OPT_WELCOME_MSG, nil)
	}
	return o.socket.setPubBytesOption(C.ZLINK_PUB_OPT_WELCOME_MSG, message.Data())
}

func (o *PubSocketOptions) WelcomeMessage() (*Message, error) {
	data, err := o.socket.getPubBytesOption(C.ZLINK_PUB_OPT_WELCOME_MSG, 256)
	if err != nil {
		return nil, err
	}
	return NewMessage(data)
}

func (o *PubSocketOptions) ApproveSubscribe(routingID RoutingID) error {
	return o.socket.setPubRoutingIDOption(C.ZLINK_PUB_OPT_APPROVE_SUBSCRIBE, routingID)
}

func (o *PubSocketOptions) RejectSubscribe(routingID RoutingID) error {
	return o.socket.setPubRoutingIDOption(C.ZLINK_PUB_OPT_REJECT_SUBSCRIBE, routingID)
}

func (o *CommonSocketOptions) SetLinger(value time.Duration) error {
	return o.socket.setDurationOption(C.ZLINK_OPT_LINGER, value)
}

func (o *CommonSocketOptions) Linger() (time.Duration, error) {
	return o.socket.getDurationOption(C.ZLINK_OPT_LINGER)
}

func (o *CommonSocketOptions) SetSendHWM(value int) error {
	return o.socket.setIntOption(C.ZLINK_OPT_SNDHWM, int32(value))
}

func (o *CommonSocketOptions) SendHWM() (int, error) {
	value, err := o.socket.getIntOption(C.ZLINK_OPT_SNDHWM)
	return int(value), err
}

func (o *CommonSocketOptions) SetRecvHWM(value int) error {
	return o.socket.setIntOption(C.ZLINK_OPT_RCVHWM, int32(value))
}

func (o *CommonSocketOptions) RecvHWM() (int, error) {
	value, err := o.socket.getIntOption(C.ZLINK_OPT_RCVHWM)
	return int(value), err
}

func (o *CommonSocketOptions) SetSendTimeout(value time.Duration) error {
	return o.socket.setDurationOption(C.ZLINK_OPT_SNDTIMEO, value)
}

func (o *CommonSocketOptions) SendTimeout() (time.Duration, error) {
	return o.socket.getDurationOption(C.ZLINK_OPT_SNDTIMEO)
}

func (o *CommonSocketOptions) SetRecvTimeout(value time.Duration) error {
	return o.socket.setDurationOption(C.ZLINK_OPT_RCVTIMEO, value)
}

func (o *CommonSocketOptions) RecvTimeout() (time.Duration, error) {
	return o.socket.getDurationOption(C.ZLINK_OPT_RCVTIMEO)
}

func (o *CommonSocketOptions) SetImmediate(value bool) error {
	return o.socket.setBoolOption(C.ZLINK_OPT_IMMEDIATE, value)
}

func (o *CommonSocketOptions) Immediate() (bool, error) {
	return o.socket.getBoolOption(C.ZLINK_OPT_IMMEDIATE)
}

func (o *CommonSocketOptions) SetRIDDuplicatePolicy(value RIDDuplicatePolicy) error {
	return o.socket.setIntOption(C.ZLINK_OPT_RID_DUPLICATE_POLICY, int32(value))
}

func (o *CommonSocketOptions) RIDDuplicatePolicy() (RIDDuplicatePolicy, error) {
	value, err := o.socket.getIntOption(C.ZLINK_OPT_RID_DUPLICATE_POLICY)
	return RIDDuplicatePolicy(value), err
}

func (o *CommonSocketOptions) SetAutoHwmMsgUnitBytes(value int) error {
	return o.socket.setIntOption(C.ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES, int32(value))
}

func (o *CommonSocketOptions) AutoHwmMsgUnitBytes() (int, error) {
	value, err := o.socket.getIntOption(C.ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES)
	return int(value), err
}

func (o *CommonSocketOptions) SetConnectTimeout(value time.Duration) error {
	return o.socket.setDurationOption(C.ZLINK_OPT_CONNECT_TIMEOUT, value)
}

func (o *CommonSocketOptions) ConnectTimeout() (time.Duration, error) {
	return o.socket.getDurationOption(C.ZLINK_OPT_CONNECT_TIMEOUT)
}

func (o *CommonSocketOptions) SetIPv6(value bool) error {
	return o.socket.setBoolOption(C.ZLINK_OPT_IPV6, value)
}

func (o *CommonSocketOptions) IPv6() (bool, error) {
	return o.socket.getBoolOption(C.ZLINK_OPT_IPV6)
}

func (o *CommonSocketOptions) SetTCPNoDelay(value bool) error {
	return o.socket.setBoolOption(C.ZLINK_OPT_TCP_NODELAY, value)
}

func (o *CommonSocketOptions) TCPNoDelay() (bool, error) {
	return o.socket.getBoolOption(C.ZLINK_OPT_TCP_NODELAY)
}

func (o *CommonSocketOptions) SetTCPKeepalive(value bool) error {
	return o.socket.setBoolOption(C.ZLINK_OPT_TCP_KEEPALIVE, value)
}

func (o *CommonSocketOptions) TCPKeepalive() (bool, error) {
	return o.socket.getBoolOption(C.ZLINK_OPT_TCP_KEEPALIVE)
}

func (o *CommonSocketOptions) SetHeartbeatInterval(value time.Duration) error {
	return o.socket.setDurationOption(C.ZLINK_OPT_HEARTBEAT_IVL, value)
}

func (o *CommonSocketOptions) HeartbeatInterval() (time.Duration, error) {
	return o.socket.getDurationOption(C.ZLINK_OPT_HEARTBEAT_IVL)
}

func (o *CommonSocketOptions) SetHeartbeatTTL(value time.Duration) error {
	return o.socket.setDurationOption(C.ZLINK_OPT_HEARTBEAT_TTL, value)
}

func (o *CommonSocketOptions) HeartbeatTTL() (time.Duration, error) {
	return o.socket.getDurationOption(C.ZLINK_OPT_HEARTBEAT_TTL)
}

func (o *CommonSocketOptions) SetHeartbeatTimeout(value time.Duration) error {
	return o.socket.setDurationOption(C.ZLINK_OPT_HEARTBEAT_TIMEOUT, value)
}

func (o *CommonSocketOptions) HeartbeatTimeout() (time.Duration, error) {
	return o.socket.getDurationOption(C.ZLINK_OPT_HEARTBEAT_TIMEOUT)
}

func (o *CommonSocketOptions) SetMaxMsgSize(value int64) error {
	return o.socket.setInt64Option(C.ZLINK_OPT_MAXMSGSIZE, value)
}

func (o *CommonSocketOptions) MaxMsgSize() (int64, error) {
	return o.socket.getInt64Option(C.ZLINK_OPT_MAXMSGSIZE)
}

func (o *CommonSocketOptions) SetBacklog(value int) error {
	return o.socket.setIntOption(C.ZLINK_OPT_BACKLOG, int32(value))
}

func (o *CommonSocketOptions) Backlog() (int, error) {
	value, err := o.socket.getIntOption(C.ZLINK_OPT_BACKLOG)
	return int(value), err
}

func (o *CommonSocketOptions) SetReconnectInterval(value time.Duration) error {
	return o.socket.setDurationOption(C.ZLINK_OPT_RECONNECT_IVL, value)
}

func (o *CommonSocketOptions) ReconnectInterval() (time.Duration, error) {
	return o.socket.getDurationOption(C.ZLINK_OPT_RECONNECT_IVL)
}

func (o *CommonSocketOptions) SetReconnectIntervalMax(value time.Duration) error {
	return o.socket.setDurationOption(C.ZLINK_OPT_RECONNECT_IVL_MAX, value)
}

func (o *CommonSocketOptions) ReconnectIntervalMax() (time.Duration, error) {
	return o.socket.getDurationOption(C.ZLINK_OPT_RECONNECT_IVL_MAX)
}

func (o *CommonSocketOptions) LastEndpoint() (string, error) {
	return o.socket.LastEndpoint()
}

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
type multipartRecvFunc func(*C.zlink_msg_t, *C.zlink_part_flag_t) error

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

func submitMultipartFromClones(parts []*Message, consumeOriginal bool, submit multipartSubmitFunc) error {
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

func recvMultipart(recv multipartRecvFunc) ([]*Message, error) {
	native := make([]C.zlink_msg_t, 0, 1)
	for {
		var part C.zlink_msg_t
		if err := configErrorFromResult(C.zlink_msg_init(&part)); err != nil {
			closeNativeMultipart(native, len(native))
			return nil, err
		}

		var hasMore C.zlink_part_flag_t
		if err := recv(&part, &hasMore); err != nil {
			_ = configErrorFromResult(C.zlink_msg_close(&part))
			closeNativeMultipart(native, len(native))
			return nil, err
		}

		native = append(native, C.zlink_msg_t{})
		last := len(native) - 1
		if err := configErrorFromResult(C.zlink_msg_init(&native[last])); err != nil {
			_ = configErrorFromResult(C.zlink_msg_close(&part))
			closeNativeMultipart(native[:last], last)
			return nil, err
		}
		if err := configErrorFromResult(C.zlink_msg_move(&native[last], &part)); err != nil {
			_ = configErrorFromResult(C.zlink_msg_close(&part))
			closeNativeMultipart(native, len(native))
			return nil, err
		}

		if hasMore == 0 {
			break
		}
	}

	return takeParts(&native[0], C.size_t(len(native)))
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

func recvTopicMessage(
	call func(**C.zlink_routing_id_t, *C.char, *C.size_t, *C.zlink_msg_t, *C.zlink_part_flag_t, C.zlink_recv_flags_t) error,
	flags RecvFlags,
) (*TopicMessage, error) {
	var sourceRID *C.zlink_routing_id_t
	topicBuf := make([]byte, recvTopicBufferCap)
	topicLen := C.size_t(len(topicBuf))
	clonedParts, err := recvMultipart(func(part *C.zlink_msg_t, hasMore *C.zlink_part_flag_t) error {
		return call(&sourceRID, (*C.char)(unsafe.Pointer(&topicBuf[0])), &topicLen, part, hasMore, C.zlink_recv_flags_t(flags))
	})
	if err != nil {
		return nil, err
	}
	return &TopicMessage{
		routingID: routingIDFromCPtr(sourceRID),
		topic:     string(topicBuf[:int(topicLen)]),
		parts:     clonedParts,
	}, nil
}

func recvSpotTopicMessage(
	call func(**C.zlink_routing_id_t, *C.char, *C.size_t, *C.zlink_msg_t, *C.zlink_part_flag_t, C.zlink_recv_flags_t) error,
	flags RecvFlags,
) (*TopicMessage, error) {
	var sourceRID *C.zlink_routing_id_t
	topicBuf := make([]byte, recvTopicBufferCap)
	topicLen := C.size_t(len(topicBuf))
	clonedParts, err := recvMultipart(func(part *C.zlink_msg_t, hasMore *C.zlink_part_flag_t) error {
		return call(
			&sourceRID,
			(*C.char)(unsafe.Pointer(&topicBuf[0])),
			&topicLen,
			part,
			hasMore,
			C.zlink_recv_flags_t(flags),
		)
	})
	if err != nil {
		return nil, err
	}
	return &TopicMessage{
		routingID: routingIDFromCPtr(sourceRID),
		topic:     string(topicBuf[:int(topicLen)]),
		parts:     clonedParts,
	}, nil
}

func recvSubscriptionEvent(
	call func(*C.zlink_routing_id_t, *C.int, *C.char, *C.size_t, C.zlink_recv_flags_t) error,
	flags RecvFlags,
) (*SubscriptionEvent, error) {
	var rid C.zlink_routing_id_t
	var subscribed C.int
	topicBuf := make([]byte, recvTopicBufferCap)
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
	topicBuf := make([]byte, recvTopicBufferCap)
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

type connectionSocket struct {
	*socketCore
}

func (s *connectionSocket) SetSendHWM(value int) error {
	return s.setIntOption(C.ZLINK_OPT_SNDHWM, int32(value))
}

func (s *connectionSocket) SendHWM() (int, error) {
	value, err := s.getIntOption(C.ZLINK_OPT_SNDHWM)
	return int(value), err
}

func (s *connectionSocket) SetRecvHWM(value int) error {
	return s.setIntOption(C.ZLINK_OPT_RCVHWM, int32(value))
}

func (s *connectionSocket) RecvHWM() (int, error) {
	value, err := s.getIntOption(C.ZLINK_OPT_RCVHWM)
	return int(value), err
}

func (s *connectionSocket) SetLinger(value time.Duration) error {
	return s.setDurationOption(C.ZLINK_OPT_LINGER, value)
}

func (s *connectionSocket) SetRecvTimeout(value time.Duration) error {
	return s.setDurationOption(C.ZLINK_OPT_RCVTIMEO, value)
}

func (s *connectionSocket) SetSendTimeout(value time.Duration) error {
	return s.setDurationOption(C.ZLINK_OPT_SNDTIMEO, value)
}

func (s *connectionSocket) SetTCPKeepalive(value bool) error {
	return s.setBoolOption(C.ZLINK_OPT_TCP_KEEPALIVE, value)
}

func (s *connectionSocket) SetTCPNoDelay(value bool) error {
	return s.setBoolOption(C.ZLINK_OPT_TCP_NODELAY, value)
}

func (s *connectionSocket) SetIPv6(value bool) error {
	return s.setBoolOption(C.ZLINK_OPT_IPV6, value)
}

func (s *connectionSocket) SetRidDuplicatePolicy(value int) error {
	return s.setIntOption(C.ZLINK_OPT_RID_DUPLICATE_POLICY, int32(value))
}

func (s *connectionSocket) RidDuplicatePolicy() (int, error) {
	value, err := s.getIntOption(C.ZLINK_OPT_RID_DUPLICATE_POLICY)
	return int(value), err
}

func (s *connectionSocket) CommonOptions() *CommonSocketOptions {
	return &CommonSocketOptions{socket: s}
}

func (s *connectionSocket) SetAutoHwmMsgUnitBytes(value int) error {
	return s.setIntOption(C.ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES, int32(value))
}

func (s *connectionSocket) AutoHwmMsgUnitBytes() (int, error) {
	value, err := s.getIntOption(C.ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES)
	return int(value), err
}

func (s *connectionSocket) LastEndpoint() (string, error) {
	return s.getStringOption(C.ZLINK_OPT_LAST_ENDPOINT, 256)
}

func (s *connectionSocket) SetChannelName(value string) error {
	if s == nil || s.closed {
		return stateError("socket is closed")
	}
	if strings.IndexByte(value, 0) >= 0 || value == "" {
		return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	cstr := C.CString(value)
	defer C.free(unsafe.Pointer(cstr))
	return configErrorFromResult(C.zlink_socket_set_channel_name(s.raw(), cstr))
}

func (s *connectionSocket) ChannelName() (string, error) {
	if s == nil || s.closed {
		return "", stateError("socket is closed")
	}
	buf := make([]byte, 256)
	size := C.size_t(0)
	if err := configErrorFromResult(C.zlink_socket_get_channel_name(
		s.raw(),
		(*C.char)(unsafe.Pointer(&buf[0])),
		C.size_t(len(buf)),
		&size,
	)); err != nil {
		return "", err
	}
	return string(buf[:int(size)]), nil
}

func (s *connectionSocket) setPubBoolOption(option C.zlink_pub_option_t, value bool) error {
	return setNativePubBoolOption(s.raw(), s.socketCore.closed, "socket is closed", option, value)
}

func (s *connectionSocket) getPubBoolOption(option C.zlink_pub_option_t) (bool, error) {
	return getNativePubBoolOption(s.raw(), s.socketCore.closed, "socket is closed", option)
}

func (s *connectionSocket) getPubIntOption(option C.zlink_pub_option_t) (int, error) {
	var raw C.int
	size := C.size_t(C.sizeof_int)
	if err := configErrorFromResult(C.zlink_get_pub_option(s.raw(), option, unsafe.Pointer(&raw), &size)); err != nil {
		return 0, err
	}
	return int(raw), nil
}

func (s *connectionSocket) setPubRoutingIDOption(option C.zlink_pub_option_t, id RoutingID) error {
	raw := id.toC()
	return configErrorFromResult(C.zlink_set_pub_option(s.raw(), option, routingIDPointer(&raw), C.size_t(raw.size)))
}

func (s *connectionSocket) setPubBytesOption(option C.zlink_pub_option_t, value []byte) error {
	var ptr unsafe.Pointer
	if len(value) > 0 {
		ptr = unsafe.Pointer(&value[0])
	}
	return configErrorFromResult(C.zlink_set_pub_option(s.raw(), option, ptr, C.size_t(len(value))))
}

func (s *connectionSocket) getPubBytesOption(option C.zlink_pub_option_t, capHint int) ([]byte, error) {
	if capHint <= 0 {
		capHint = 256
	}
	buf := make([]byte, capHint)
	size := C.size_t(len(buf))
	if err := configErrorFromResult(C.zlink_get_pub_option(s.raw(), option, unsafe.Pointer(&buf[0]), &size)); err != nil {
		return nil, err
	}
	return append([]byte(nil), buf[:int(size)]...), nil
}

func (s *connectionSocket) getSubIntOption(option C.zlink_sub_option_t) (int, error) {
	var raw C.int
	size := C.size_t(C.sizeof_int)
	if err := configErrorFromResult(C.zlink_get_sub_option(s.raw(), option, unsafe.Pointer(&raw), &size)); err != nil {
		return 0, err
	}
	return int(raw), nil
}

func (s *connectionSocket) SetTLSServer(certPath string, keyPath string, requireClientCert bool) error {
	if s == nil || s.closed {
		return stateError("socket is closed")
	}
	return setTLSServer(s.raw(), certPath, keyPath, requireClientCert)
}

func (s *connectionSocket) SetTLSClient(caCertPath string, hostname string, trustSystem bool) error {
	if s == nil || s.closed {
		return stateError("socket is closed")
	}
	return setTLSClient(s.raw(), caCertPath, hostname, trustSystem)
}

type directSocket struct {
	*connectionSocket
}

func (s *directSocket) Send(flags SendFlags, parts ...*Message) (bool, error) {
	err := submitMultipartFromClones(parts, true, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
		return submitErrorFromResult(C.zlink_send_part(s.raw(), part, C.zlink_send_flags_t(flags), partFlag))
	})
	return submitBackpressureResult(err)
}

func (s *directSocket) TrySend(parts ...*Message) (bool, error) {
	return s.Send(SendFlagsDontWait, parts...)
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
	clonedParts, err := recvMultipart(func(part *C.zlink_msg_t, hasMore *C.zlink_part_flag_t) error {
		return recvErrorFromResult(C.zlink_recv_part(s.raw(), &sourceRID, part, hasMore, C.zlink_recv_flags_t(flags)))
	})
	if err != nil {
		var recvErr *RecvError
		if errors.As(err, &recvErr) && recvErr.Result == RecvNoData {
			return false, nil
		}
		return false, err
	}
	fresh := &Received{
		routingID: routingIDFromCPtr(sourceRID),
		parts:     clonedParts,
	}
	out.AdoptFrom(fresh)
	return true, nil
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

func (s *publishSocket) Publish(topic string, flags SendFlags, parts ...*Message) (bool, error) {
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

func (s *routedSocket) SendTo(target RoutingID, flags SendFlags, parts ...*Message) (bool, error) {
	rid := target.toC()
	err := submitMultipartFromClones(parts, true, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
		return submitErrorFromResult(C.zlink_send_part_rid(s.raw(), &rid, part, C.zlink_send_flags_t(flags), partFlag))
	})
	return submitBackpressureResult(err)
}

func (s *routedSocket) TrySendTo(target RoutingID, parts ...*Message) (bool, error) {
	return s.SendTo(target, SendFlagsDontWait, parts...)
}

func (s *routedSocket) SendToSpot(destNodeRid, destSpotRid RoutingID, flags SendFlags, parts ...*Message) (bool, error) {
	node := destNodeRid.toC()
	spot := destSpotRid.toC()
	err := submitMultipartFromClones(parts, true, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
		return submitErrorFromResult(C.zlink_router_send_spot_part_go_local(s.raw(), &node, &spot, part, C.zlink_send_flags_t(flags), partFlag))
	})
	return submitBackpressureResult(err)
}

func (s *routedSocket) requestToSpot(destNodeRid, destSpotRid RoutingID, callback RequestReplyCallback, flags SendFlags, timeout time.Duration, parts ...*Message) (bool, error) {
	if callback == nil {
		return false, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	resultCh, err := s.startSpotRequest(destNodeRid, destSpotRid, flags, timeout, parts...)
	ok, err := submitBackpressureResult(err)
	if err != nil {
		return false, err
	}
	if !ok {
		return false, nil
	}
	dispatchRequestCallback(resultCh, callback)
	return true, nil
}

func (s *routedSocket) RequestToSpot(destNodeRid, destSpotRid RoutingID, callback RequestReplyCallback, flags SendFlags, timeout time.Duration, parts ...*Message) (bool, error) {
	return s.requestToSpot(destNodeRid, destSpotRid, callback, flags, timeout, parts...)
}

func (s *routedSocket) TryRequestToSpot(destNodeRid, destSpotRid RoutingID, callback RequestReplyCallback, timeout time.Duration, parts ...*Message) (bool, error) {
	return s.RequestToSpot(destNodeRid, destSpotRid, callback, SendFlagsDontWait, timeout, parts...)
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

func (s *routedSocket) ReplyToSpot(destNodeRid, destSpotRid RoutingID, requestSeq uint64, flags SendFlags, parts ...*Message) (bool, error) {
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

func (s *routedSocket) directRecv(flags RecvFlags) (*Received, error) {
	recvOnce := func(recvFlags RecvFlags) (*C.zlink_routing_id_t, *C.zlink_routing_id_t, C.uint64_t, []*Message, error) {
		var nodeRID *C.zlink_routing_id_t
		var spotRID *C.zlink_routing_id_t
		var requestSeq C.uint64_t
		parts, err := recvMultipart(func(part *C.zlink_msg_t, hasMore *C.zlink_part_flag_t) error {
			return recvErrorFromResult(C.zlink_router_recv_part(s.raw(), &nodeRID, &spotRID, &requestSeq, part, hasMore, C.zlink_recv_flags_t(recvFlags)))
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
				return nil, primedErr
			}
			var err error
			nodeRID, spotRID, requestSeq, parts, err = recvOnce(flags)
			if err != nil {
				return nil, err
			}
		}
	} else {
		var err error
		nodeRID, spotRID, requestSeq, parts, err = recvOnce(flags)
		if err != nil {
			return nil, err
		}
	}
	received := &Received{
		routingID:     routingIDFromCPtr(nodeRID),
		spotRID:       routingIDFromCPtr(spotRID),
		parts:         parts,
		requestSeq:    uint64(requestSeq),
		hasRequestSeq: requestSeq != 0,
	}
	if received.hasRequestSeq {
		if received.spotRID.Size() == 0 {
			received.reply = receivedReplyToRouter(s.reply, received.routingID, received.requestSeq)
		} else {
			received.reply = receivedReplyToSpotPeer(s, received.routingID, received.spotRID, received.requestSeq)
		}
	}
	return received, nil
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
	fresh, err := s.directRecv(flags)
	if err != nil {
		var recvErr *RecvError
		if errors.As(err, &recvErr) && recvErr.Result == RecvNoData {
			return false, nil
		}
		return false, err
	}
	out.AdoptFrom(fresh)
	return true, nil
}

func (s *routedSocket) startSpotRequest(destNodeRid, destSpotRid RoutingID, flags SendFlags, timeout time.Duration, parts ...*Message) (<-chan requestResult, error) {
	if timeout <= 0 {
		timeout = defaultRequestTimeout
	}
	cloned, err := cloneParts(parts)
	if err != nil {
		return nil, err
	}
	prepared, err := prepareMultipart(cloned)
	if err != nil {
		closeMessageSlice(cloned)
		return nil, err
	}
	state := &replyCallbackState{
		result: make(chan requestResult, 1),
		done:   make(chan struct{}),
	}
	handle := cgo.NewHandle(state)
	node := destNodeRid.toC()
	spot := destSpotRid.toC()
	if err := submitPreparedMultipart(prepared, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
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
		prepared.commit()
		return nil, err
	}
	prepared.commit()
	startSocketRequestProgress(s.raw(), state)
	return state.result, nil
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

func (s *subscribeSocket) Subscribe(flags RecvFlags) (*TopicMessage, error) {
	return recvTopicMessage(func(rid **C.zlink_routing_id_t, topic *C.char, topicLen *C.size_t, part *C.zlink_msg_t, hasMore *C.zlink_part_flag_t, recvFlags C.zlink_recv_flags_t) error {
		return recvErrorFromResult(C.zlink_subscribe_part(s.raw(), rid, topic, recvTopicBufferCap, topicLen, part, hasMore, recvFlags))
	}, flags)
}

type xpubSubscribeSocket struct {
	*publishSocket
}

func (s *xpubSubscribeSocket) ReceiveSubscriptionEvent(flags RecvFlags) (*SubscriptionEvent, error) {
	return recvSubscriptionEvent(func(rid *C.zlink_routing_id_t, subscribed *C.int, topic *C.char, topicLen *C.size_t, recvFlags C.zlink_recv_flags_t) error {
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

func (s *PairSocket) OnSendReady(handler func()) error {
	return s.connectionSocket.setSendReady(handler)
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

func (s *PubSocket) SetNoDrop(value bool) error {
	return s.connectionSocket.setPubBoolOption(C.ZLINK_PUB_OPT_NODROP, value)
}

func (s *PubSocket) NoDrop() (bool, error) {
	return s.connectionSocket.getPubBoolOption(C.ZLINK_PUB_OPT_NODROP)
}

func (s *PubSocket) SetVerbose(value bool) error {
	return s.connectionSocket.setPubBoolOption(C.ZLINK_PUB_OPT_VERBOSE, value)
}

func (s *PubSocket) Verbose() (bool, error) {
	return s.connectionSocket.getPubBoolOption(C.ZLINK_PUB_OPT_VERBOSE)
}

func (s *PubSocket) SetVerboser(value bool) error {
	return s.connectionSocket.setPubBoolOption(C.ZLINK_PUB_OPT_VERBOSER, value)
}

func (s *PubSocket) Verboser() (bool, error) {
	return s.connectionSocket.getPubBoolOption(C.ZLINK_PUB_OPT_VERBOSER)
}

func (s *PubSocket) SetManual(value bool) error {
	return s.connectionSocket.setPubBoolOption(C.ZLINK_PUB_OPT_MANUAL, value)
}

func (s *PubSocket) Manual() (bool, error) {
	return s.connectionSocket.getPubBoolOption(C.ZLINK_PUB_OPT_MANUAL)
}

func (s *PubSocket) SetManualLastValue(value bool) error {
	return s.connectionSocket.setPubBoolOption(C.ZLINK_PUB_OPT_MANUAL_LAST_VALUE, value)
}

func (s *PubSocket) ManualLastValue() (bool, error) {
	return s.connectionSocket.getPubBoolOption(C.ZLINK_PUB_OPT_MANUAL_LAST_VALUE)
}

func (s *PubSocket) SetWelcomeMessage(message *Message) error {
	if message == nil {
		return s.connectionSocket.setPubBytesOption(C.ZLINK_PUB_OPT_WELCOME_MSG, nil)
	}
	return s.connectionSocket.setPubBytesOption(C.ZLINK_PUB_OPT_WELCOME_MSG, message.Data())
}

func (s *PubSocket) WelcomeMessage() (*Message, error) {
	data, err := s.connectionSocket.getPubBytesOption(C.ZLINK_PUB_OPT_WELCOME_MSG, 256)
	if err != nil {
		return nil, err
	}
	return NewMessage(data)
}

func (s *PubSocket) ApproveSubscribe(routingID RoutingID) error {
	return s.connectionSocket.setPubRoutingIDOption(C.ZLINK_PUB_OPT_APPROVE_SUBSCRIBE, routingID)
}

func (s *PubSocket) RejectSubscribe(routingID RoutingID) error {
	return s.connectionSocket.setPubRoutingIDOption(C.ZLINK_PUB_OPT_REJECT_SUBSCRIBE, routingID)
}

func (s *PubSocket) PubOptions() *PubSocketOptions {
	return &PubSocketOptions{socket: s.connectionSocket}
}

func (s *PubSocket) OnSendReady(handler func()) error {
	return s.connectionSocket.setSendReady(handler)
}

func (s *PubSocket) AttachDiscovery(discovery *Discovery) error {
	if discovery == nil || discovery.closed {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	return configErrorFromResult(C.zlink_socket_attach_discovery(s.raw(), discovery.raw()))
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

func (s *DealerSocket) OnSendReady(handler func()) error {
	return s.connectionSocket.setSendReady(handler)
}

func (s *DealerSocket) Request(parts [][]byte, timeout time.Duration) ([]*Message, error) {
	msgs, err := bytePartsToMessages(parts)
	if err != nil {
		return nil, err
	}
	return (&dealerRequestSupport{socket: s}).Request(timeout, msgs...)
}

func (s *DealerSocket) RequestCallback(parts [][]byte, cb func(RequestResult, []*Message), flags SendFlags, timeout time.Duration) (bool, error) {
	msgs, err := bytePartsToMessages(parts)
	if err != nil {
		return false, err
	}
	return (&dealerRequestSupport{socket: s}).TryRequestCallback(RequestReplyCallback(cb), timeout, msgs...)
}

func (s *DealerSocket) TryRequestCallback(parts [][]byte, callback RequestReplyCallback, timeout time.Duration) (bool, error) {
	msgs, err := bytePartsToMessages(parts)
	if err != nil {
		return false, err
	}
	return (&dealerRequestSupport{socket: s}).TryRequestCallback(callback, timeout, msgs...)
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

func (s *RouterSocket) OnSendReady(handler func()) error {
	return s.connectionSocket.setSendReady(handler)
}

func (s *RouterSocket) Request(peerRid RoutingID, parts [][]byte, timeout time.Duration) ([]*Message, error) {
	msgs, err := bytePartsToMessages(parts)
	if err != nil {
		return nil, err
	}
	return (&routerRequestSupport{socket: s}).Request(peerRid, timeout, msgs...)
}

func (s *RouterSocket) RequestCallback(peerRid RoutingID, parts [][]byte, cb func(RequestResult, []*Message), flags SendFlags, timeout time.Duration) (bool, error) {
	msgs, err := bytePartsToMessages(parts)
	if err != nil {
		return false, err
	}
	return (&routerRequestSupport{socket: s}).TryRequestCallback(peerRid, RequestReplyCallback(cb), timeout, msgs...)
}

func (s *RouterSocket) TryRequestCallback(peerRid RoutingID, parts [][]byte, callback RequestReplyCallback, timeout time.Duration) (bool, error) {
	msgs, err := bytePartsToMessages(parts)
	if err != nil {
		return false, err
	}
	return (&routerRequestSupport{socket: s}).TryRequestCallback(peerRid, callback, timeout, msgs...)
}

func (s *RouterSocket) Reply(rid RoutingID, requestSeq uint64, flags SendFlags, parts ...*Message) (bool, error) {
	return (&routerRequestSupport{socket: s}).Reply(rid, requestSeq, flags, parts...)
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

func (s *XPubSocket) SetNoDrop(value bool) error {
	return s.connectionSocket.setPubBoolOption(C.ZLINK_PUB_OPT_NODROP, value)
}

func (s *XPubSocket) NoDrop() (bool, error) {
	return s.connectionSocket.getPubBoolOption(C.ZLINK_PUB_OPT_NODROP)
}

func (s *XPubSocket) SetVerbose(value bool) error {
	return s.connectionSocket.setPubBoolOption(C.ZLINK_PUB_OPT_VERBOSE, value)
}

func (s *XPubSocket) Verbose() (bool, error) {
	return s.connectionSocket.getPubBoolOption(C.ZLINK_PUB_OPT_VERBOSE)
}

func (s *XPubSocket) SetVerboser(value bool) error {
	return s.connectionSocket.setPubBoolOption(C.ZLINK_PUB_OPT_VERBOSER, value)
}

func (s *XPubSocket) Verboser() (bool, error) {
	return s.connectionSocket.getPubBoolOption(C.ZLINK_PUB_OPT_VERBOSER)
}

func (s *XPubSocket) SetManual(value bool) error {
	return s.connectionSocket.setPubBoolOption(C.ZLINK_PUB_OPT_MANUAL, value)
}

func (s *XPubSocket) Manual() (bool, error) {
	return s.connectionSocket.getPubBoolOption(C.ZLINK_PUB_OPT_MANUAL)
}

func (s *XPubSocket) SetManualLastValue(value bool) error {
	return s.connectionSocket.setPubBoolOption(C.ZLINK_PUB_OPT_MANUAL_LAST_VALUE, value)
}

func (s *XPubSocket) ManualLastValue() (bool, error) {
	return s.connectionSocket.getPubBoolOption(C.ZLINK_PUB_OPT_MANUAL_LAST_VALUE)
}

func (s *XPubSocket) SetWelcomeMessage(message *Message) error {
	if message == nil {
		return s.connectionSocket.setPubBytesOption(C.ZLINK_PUB_OPT_WELCOME_MSG, nil)
	}
	return s.connectionSocket.setPubBytesOption(C.ZLINK_PUB_OPT_WELCOME_MSG, message.Data())
}

func (s *XPubSocket) WelcomeMessage() (*Message, error) {
	data, err := s.connectionSocket.getPubBytesOption(C.ZLINK_PUB_OPT_WELCOME_MSG, 256)
	if err != nil {
		return nil, err
	}
	return NewMessage(data)
}

func (s *XPubSocket) ApproveSubscribe(routingID RoutingID) error {
	return s.connectionSocket.setPubRoutingIDOption(C.ZLINK_PUB_OPT_APPROVE_SUBSCRIBE, routingID)
}

func (s *XPubSocket) RejectSubscribe(routingID RoutingID) error {
	return s.connectionSocket.setPubRoutingIDOption(C.ZLINK_PUB_OPT_REJECT_SUBSCRIBE, routingID)
}

func (s *XPubSocket) PubOptions() *PubSocketOptions {
	return &PubSocketOptions{socket: s.connectionSocket}
}

func (s *XPubSocket) OnSendReady(handler func()) error {
	return s.connectionSocket.setSendReady(handler)
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

func (s *StreamSocket) SetSendHWM(value int) error {
	return s.core.SetSendHWM(value)
}

func (s *StreamSocket) SendHWM() (int, error) {
	return s.core.SendHWM()
}

func (s *StreamSocket) SetRecvHWM(value int) error {
	return s.core.SetRecvHWM(value)
}

func (s *StreamSocket) RecvHWM() (int, error) {
	return s.core.RecvHWM()
}

func (s *StreamSocket) SetLinger(value time.Duration) error {
	return s.core.SetLinger(value)
}

func (s *StreamSocket) SetRecvTimeout(value time.Duration) error {
	return s.core.SetRecvTimeout(value)
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

func (s *StreamSocket) SendTo(target RoutingID, flags SendFlags, parts ...*Message) (bool, error) {
	return s.core.SendTo(target, flags, parts...)
}

func (s *StreamSocket) TrySendTo(target RoutingID, parts ...*Message) (bool, error) {
	return s.SendTo(target, SendFlagsDontWait, parts...)
}

// Recv is the canonical caller-provided storage recv. See
// doc/spec/bindings/README.md "Canonical Recv: Caller-Provided Storage".
func (s *StreamSocket) Recv(out *Received, flags RecvFlags) (bool, error) {
	return (&directSocket{connectionSocket: s.core.connectionSocket}).Recv(out, flags)
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
