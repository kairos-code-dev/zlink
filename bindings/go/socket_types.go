// SPDX-License-Identifier: MPL-2.0

package zlink

/*
#include <stdint.h>
#include <stdlib.h>
#include "zlink.h"

extern void goZlinkRecvTrampoline(zlink_routing_id_t *source_rid_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);
extern void goZlinkRouterRecvTrampoline(zlink_routing_id_t *source_node_rid_, zlink_routing_id_t *source_spot_rid_, uint64_t request_seq_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);
extern void goZlinkSubscribeTrampoline(zlink_routing_id_t *source_rid_, char *topic_, size_t topic_len_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);
extern void goZlinkSendReadyTrampoline(void *subject_, uintptr_t userdata_);
extern void goZlinkReplyTrampoline(zlink_request_result_t result_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);

static inline int zlink_recv_handler_go_local(void *s, uintptr_t userdata) {
    return zlink_recv_handler(s, (zlink_socket_msg_handler_fn)goZlinkRecvTrampoline, (void *)userdata);
}

static inline int zlink_router_recv_handler_go_local(void *s, uintptr_t userdata) {
    return zlink_router_handler(s, (zlink_router_handler_fn)goZlinkRouterRecvTrampoline, (void *)userdata);
}

static inline int zlink_subscribe_handler_go_local(void *s, uintptr_t userdata) {
    return zlink_subscribe_handler(s, (zlink_subscribe_handler_fn)goZlinkSubscribeTrampoline, (void *)userdata);
}

static inline int zlink_send_ready_handler_go_local(void *s, uintptr_t userdata) {
    return zlink_send_ready_handler(s, (zlink_send_ready_handler_fn)goZlinkSendReadyTrampoline, (void *)userdata);
}

static inline int zlink_router_request_spot_go_local(void *router, const zlink_routing_id_t *dest_node_rid, const zlink_routing_id_t *dest_spot_rid, zlink_msg_t *parts, size_t part_count, zlink_send_flags_t flags, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_router_request_spot(router, dest_node_rid, dest_spot_rid, parts, part_count, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata, flags, timeout_ms);
}

static inline int zlink_router_send_spot_go_local(void *router, const zlink_routing_id_t *dest_node_rid, const zlink_routing_id_t *dest_spot_rid, zlink_msg_t *parts, size_t part_count, zlink_send_flags_t flags) {
    return zlink_router_send_spot(router, dest_node_rid, dest_spot_rid, parts, part_count, flags);
}
*/
import "C"

import (
	"errors"
	"runtime/cgo"
	"strings"
	"time"
	"unsafe"
)

type recvCallback func(*Received)
type subscribeCallback func(*TopicMessage)
type sendReadyCallback func()

const recvTopicBufferCap = 64 * 1024

type socketCore struct {
	handle          unsafe.Pointer
	closed          bool
	recvHandle      cgo.Handle
	subscribeHandle cgo.Handle
	sendReadyHandle cgo.Handle
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
	call func(*C.zlink_routing_id_t, **C.zlink_msg_t, *C.size_t, *C.char, *C.size_t, C.zlink_recv_flags_t) error,
	flags RecvFlags,
) (*TopicMessage, error) {
	var rid C.zlink_routing_id_t
	var parts *C.zlink_msg_t
	var partCount C.size_t
	topicBuf := make([]byte, recvTopicBufferCap)
	topicLen := C.size_t(len(topicBuf))
	if err := call(&rid, &parts, &partCount, (*C.char)(unsafe.Pointer(&topicBuf[0])), &topicLen, C.zlink_recv_flags_t(flags)); err != nil {
		return nil, err
	}
	clonedParts, err := takeParts(parts, partCount)
	if err != nil {
		return nil, err
	}
	return &TopicMessage{
		routingID: routingIDFromC(rid),
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

func (s *connectionSocket) LastEndpoint() (string, error) {
	return s.getStringOption(C.ZLINK_OPT_LAST_ENDPOINT, 256)
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

func (s *directSocket) Send(flags SendFlags, parts ...*Message) error {
	prepared, err := prepareMultipart(parts)
	if err != nil {
		return err
	}
	if err := submitErrorFromResult(C.zlink_send(s.raw(), prepared.ptr(), prepared.count(), C.zlink_send_flags_t(flags))); err != nil {
		if restoreErr := prepared.restore(); restoreErr != nil {
			return restoreErr
		}
		return err
	}
	prepared.commit()
	return nil
}

func (s *directSocket) Recv(flags RecvFlags) (*Received, error) {
	var rid C.zlink_routing_id_t
	var parts *C.zlink_msg_t
	var partCount C.size_t
	if err := recvErrorFromResult(C.zlink_recv(s.raw(), &rid, &parts, &partCount, C.zlink_recv_flags_t(flags))); err != nil {
		return nil, err
	}
	clonedParts, err := takeParts(parts, partCount)
	if err != nil {
		return nil, err
	}
	return &Received{
		routingID: routingIDFromC(rid),
		parts:     clonedParts,
	}, nil
}

func (s *directSocket) OnReceive(handler func(*Received)) error {
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

func (s *publishSocket) Publish(topic string, flags SendFlags, parts ...*Message) error {
	prepared, err := prepareMultipart(parts)
	if err != nil {
		return err
	}
	err = s.withCString(topic, func(cstr *C.char) error {
		return submitErrorFromResult(C.zlink_publish(s.raw(), cstr, prepared.ptr(), prepared.count(), C.zlink_send_flags_t(flags)))
	})
	if err != nil {
		if restoreErr := prepared.restore(); restoreErr != nil {
			return restoreErr
		}
		return err
	}
	prepared.commit()
	return nil
}

type routedSocket struct {
	*connectionSocket
}

func (s *routedSocket) SendTo(target RoutingID, flags SendFlags, parts ...*Message) error {
	prepared, err := prepareMultipart(parts)
	if err != nil {
		return err
	}
	rid := target.toC()
	if err := submitErrorFromResult(C.zlink_send_rid(s.raw(), &rid, prepared.ptr(), prepared.count(), C.zlink_send_flags_t(flags))); err != nil {
		if restoreErr := prepared.restore(); restoreErr != nil {
			return restoreErr
		}
		return err
	}
	prepared.commit()
	return nil
}

func (s *routedSocket) SendToSpot(destNodeRid, destSpotRid RoutingID, flags SendFlags, parts ...*Message) error {
	prepared, err := prepareMultipart(parts)
	if err != nil {
		return err
	}
	node := destNodeRid.toC()
	spot := destSpotRid.toC()
	if err := submitErrorFromResult(C.zlink_router_send_spot_go_local(s.raw(), &node, &spot, prepared.ptr(), prepared.count(), C.zlink_send_flags_t(flags))); err != nil {
		if restoreErr := prepared.restore(); restoreErr != nil {
			return restoreErr
		}
		return err
	}
	prepared.commit()
	return nil
}

func (s *routedSocket) RequestToSpot(destNodeRid, destSpotRid RoutingID, callback RequestReplyCallback, flags SendFlags, timeout time.Duration, parts ...*Message) error {
	if callback == nil {
		return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	resultCh, err := s.startSpotRequest(destNodeRid, destSpotRid, flags, timeout, parts...)
	if err != nil {
		return err
	}
	go func() {
		result := <-resultCh
		callback(result.result, result.parts)
	}()
	return nil
}

func (s *routedSocket) reply(rid RoutingID, requestSeq uint64, flags SendFlags, parts ...*Message) error {
	if err := validateReplyFlags(flags); err != nil {
		return err
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
	target := rid.toC()
	if err := submitErrorFromResult(C.zlink_router_reply(s.raw(), &target, C.uint64_t(requestSeq), prepared.ptr(), prepared.count())); err != nil {
		if restoreErr := prepared.restore(); restoreErr != nil {
			return restoreErr
		}
		return err
	}
	prepared.commit()
	return nil
}

func (s *routedSocket) ReplyToSpot(destNodeRid, destSpotRid RoutingID, requestSeq uint64, flags SendFlags, parts ...*Message) error {
	if err := validateReplyFlags(flags); err != nil {
		return err
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
	node := destNodeRid.toC()
	spot := destSpotRid.toC()
	if err := submitErrorFromResult(C.zlink_router_reply_spot(s.raw(), &node, &spot, C.uint64_t(requestSeq), prepared.ptr(), prepared.count())); err != nil {
		if restoreErr := prepared.restore(); restoreErr != nil {
			return restoreErr
		}
		return err
	}
	prepared.commit()
	return nil
}

func (s *routedSocket) directRecv(flags RecvFlags) (*Received, error) {
	recvOnce := func(recvFlags RecvFlags) (*C.zlink_routing_id_t, *C.zlink_routing_id_t, C.uint64_t, *C.zlink_msg_t, C.size_t, error) {
		var nodeRID *C.zlink_routing_id_t
		var spotRID *C.zlink_routing_id_t
		var requestSeq C.uint64_t
		var parts *C.zlink_msg_t
		var partCount C.size_t
		if err := recvErrorFromResult(C.zlink_router_recv(s.raw(), &nodeRID, &spotRID, &requestSeq, &parts, &partCount, C.zlink_recv_flags_t(recvFlags))); err != nil {
			return nil, nil, 0, nil, 0, err
		}
		return nodeRID, spotRID, requestSeq, parts, partCount, nil
	}

	var nodeRID *C.zlink_routing_id_t
	var spotRID *C.zlink_routing_id_t
	var requestSeq C.uint64_t
	var parts *C.zlink_msg_t
	var partCount C.size_t
	if flags == RecvFlagsNone {
		primedNodeRID, primedSpotRID, primedRequestSeq, primedParts, primedPartCount, primedErr := recvOnce(RecvFlagsDontWait)
		if primedErr == nil {
			nodeRID = primedNodeRID
			spotRID = primedSpotRID
			requestSeq = primedRequestSeq
			parts = primedParts
			partCount = primedPartCount
		} else {
			var recvErr *RecvError
			if !errors.As(primedErr, &recvErr) || recvErr.Result != RecvNoData {
				return nil, primedErr
			}
			var err error
			nodeRID, spotRID, requestSeq, parts, partCount, err = recvOnce(flags)
			if err != nil {
				return nil, err
			}
		}
	} else {
		var err error
		nodeRID, spotRID, requestSeq, parts, partCount, err = recvOnce(flags)
		if err != nil {
			return nil, err
		}
	}
	clonedParts, err := takeParts(parts, partCount)
	if err != nil {
		return nil, err
	}
	received := &Received{
		routingID:     routingIDFromCPtr(nodeRID),
		spotRID:       routingIDFromCPtr(spotRID),
		parts:         clonedParts,
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

func (s *routedSocket) Recv(flags RecvFlags) (*Received, error) {
	if s.recvHandle != 0 {
		return nil, &RecvError{Result: RecvBusy, internalErrno: int(C.EBUSY)}
	}
	return s.directRecv(flags)
}

func (s *routedSocket) OnReceive(handler func(*Received)) error {
	if handler == nil {
		return &HandlerError{Result: HandlerInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	state := newRecvCallbackState(recvCallback(handler), func(routingID RoutingID, spotRID RoutingID, requestSeq uint64) func(SendFlags, []*Message) error {
		if spotRID.Size() == 0 {
			return receivedReplyToRouter(s.reply, routingID, requestSeq)
		}
		return receivedReplyToSpotPeer(s, routingID, spotRID, requestSeq)
	})
	handle := cgo.NewHandle(state)
	if err := handlerErrorFromResult(C.zlink_router_recv_handler_go_local(s.raw(), C.uintptr_t(handle))); err != nil {
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
	resultCh := make(chan requestResult, 1)
	handle := cgo.NewHandle(&replyCallbackState{result: resultCh})
	node := destNodeRid.toC()
	spot := destSpotRid.toC()
	if err := submitErrorFromResult(C.zlink_router_request_spot_go_local(
		s.raw(),
		&node,
		&spot,
		prepared.ptr(),
		prepared.count(),
		C.zlink_send_flags_t(flags),
		C.uint32_t(requestTimeoutMillis(timeout)),
		C.uintptr_t(handle),
	)); err != nil {
		handle.Delete()
		if restoreErr := prepared.restore(); restoreErr != nil {
			return nil, restoreErr
		}
		return nil, err
	}
	prepared.commit()
	return resultCh, nil
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
	return recvTopicMessage(func(rid *C.zlink_routing_id_t, parts **C.zlink_msg_t, partCount *C.size_t, topic *C.char, topicLen *C.size_t, recvFlags C.zlink_recv_flags_t) error {
		return recvErrorFromResult(C.zlink_subscribe(s.raw(), rid, parts, partCount, topic, topicLen, recvFlags))
	}, flags)
}

func (s *subscribeSocket) OnSubscribe(handler func(*TopicMessage)) error {
	if handler == nil {
		return &HandlerError{Result: HandlerInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	state := newSubscribeCallbackState(subscribeCallback(handler))
	handle := cgo.NewHandle(state)
	if err := handlerErrorFromResult(C.zlink_subscribe_handler_go_local(s.raw(), C.uintptr_t(handle))); err != nil {
		state.close()
		handle.Delete()
		return err
	}
	if s.subscribeHandle != 0 {
		releaseCallbackHandle(s.subscribeHandle)
	}
	s.subscribeHandle = handle
	return nil
}

type xpubSubscribeSocket struct {
	*publishSocket
}

func (s *xpubSubscribeSocket) ReceiveSubscriptionEvent(flags RecvFlags) (*SubscriptionEvent, error) {
	return recvSubscriptionEvent(func(rid *C.zlink_routing_id_t, subscribed *C.int, topic *C.char, topicLen *C.size_t, recvFlags C.zlink_recv_flags_t) error {
		return recvErrorFromResult(C.zlink_subscription_event(s.raw(), rid, subscribed, topic, topicLen, recvFlags))
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

func (s *DealerSocket) RequestCallback(parts [][]byte, callback RequestReplyCallback, flags SendFlags, timeout time.Duration) error {
	msgs, err := bytePartsToMessages(parts)
	if err != nil {
		return err
	}
	return (&dealerRequestSupport{socket: s}).RequestCallback(callback, flags, timeout, msgs...)
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

func (s *RouterSocket) SetHandover(value bool) error {
	var raw C.int
	if value {
		raw = 1
	}
	return configErrorFromResult(C.zlink_set_router_option(s.raw(), C.ZLINK_ROUTER_OPT_HANDOVER, unsafe.Pointer(&raw), C.size_t(C.sizeof_int)))
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

func (s *RouterSocket) RequestCallback(peerRid RoutingID, parts [][]byte, callback RequestReplyCallback, flags SendFlags, timeout time.Duration) error {
	msgs, err := bytePartsToMessages(parts)
	if err != nil {
		return err
	}
	return (&routerRequestSupport{socket: s}).RequestCallback(peerRid, callback, flags, timeout, msgs...)
}

func (s *RouterSocket) Reply(rid RoutingID, requestSeq uint64, flags SendFlags, parts ...*Message) error {
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
	core *routedSocket
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

func (s *StreamSocket) SendTo(target RoutingID, flags SendFlags, parts ...*Message) error {
	return s.core.SendTo(target, flags, parts...)
}

func (s *StreamSocket) Recv(flags RecvFlags) (*Received, error) {
	return s.core.Recv(flags)
}

func (s *StreamSocket) OnReceive(handler func(*Received)) error {
	return s.core.OnReceive(handler)
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
