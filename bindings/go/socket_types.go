// SPDX-License-Identifier: MPL-2.0

package zlink

/*
#include <stdint.h>
#include <stdlib.h>
#include "zlink.h"

extern void goZlinkRecvTrampoline(zlink_routing_id_t *source_rid_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);
extern void goZlinkSubscribeTrampoline(zlink_routing_id_t *source_rid_, char *topic_, size_t topic_len_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);
extern void goZlinkSendReadyTrampoline(void *subject_, uintptr_t userdata_);

static inline int zlink_recv_handler_go_local(void *s, uintptr_t userdata) {
    return zlink_recv_handler(s, (zlink_socket_msg_handler_fn)goZlinkRecvTrampoline, (void *)userdata);
}

static inline int zlink_subscribe_handler_go_local(void *s, uintptr_t userdata) {
    return zlink_subscribe_handler(s, (zlink_subscribe_handler_fn)goZlinkSubscribeTrampoline, (void *)userdata);
}

static inline int zlink_send_ready_handler_go_local(void *s, uintptr_t userdata) {
    return zlink_send_ready_handler(s, (zlink_send_ready_handler_fn)goZlinkSendReadyTrampoline, (void *)userdata);
}
*/
import "C"

import (
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
		return nil, stateError("context is closed")
	}
	handle := C.zlink_socket(ctx.raw(), socketType)
	if handle == nil {
		return nil, lastError()
	}
	return &socketCore{handle: handle}, nil
}

func (s *socketCore) raw() unsafe.Pointer {
	return s.handle
}

func (s *socketCore) Bind(endpoint string) error {
	return s.withCString(endpoint, func(cstr *C.char) error {
		return checkRC(C.zlink_bind(s.handle, cstr))
	})
}

func (s *socketCore) Connect(endpoint string) error {
	return s.withCString(endpoint, func(cstr *C.char) error {
		return checkRC(C.zlink_connect(s.handle, cstr))
	})
}

func (s *socketCore) Unbind(endpoint string) error {
	return s.withCString(endpoint, func(cstr *C.char) error {
		return checkRC(C.zlink_unbind(s.handle, cstr))
	})
}

func (s *socketCore) Disconnect(endpoint string) error {
	return s.withCString(endpoint, func(cstr *C.char) error {
		return checkRC(C.zlink_disconnect(s.handle, cstr))
	})
}

func (s *socketCore) Close() error {
	if s == nil || s.closed {
		return nil
	}
	if err := checkRC(C.zlink_close(s.handle)); err != nil {
		return err
	}
	s.closed = true
	s.handle = nil
	s.releaseCallbacks()
	return nil
}

func (s *socketCore) releaseCallbacks() {
	if s.recvHandle != 0 {
		s.recvHandle.Delete()
		s.recvHandle = 0
	}
	if s.subscribeHandle != 0 {
		s.subscribeHandle.Delete()
		s.subscribeHandle = 0
	}
	if s.sendReadyHandle != 0 {
		s.sendReadyHandle.Delete()
		s.sendReadyHandle = 0
	}
}

func (s *socketCore) setIntOption(option C.zlink_option_t, value int32) error {
	return s.setOption(option, unsafe.Pointer(&value), C.size_t(C.sizeof_int))
}

func (s *socketCore) getIntOption(option C.zlink_option_t) (int32, error) {
	var value C.int
	size := C.size_t(C.sizeof_int)
	err := checkRC(C.zlink_get_option(s.handle, option, unsafe.Pointer(&value), &size))
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
	err := checkRC(C.zlink_get_option(s.handle, option, unsafe.Pointer(&buf[0]), &size))
	if err != nil {
		return "", err
	}
	return string(buf[:int(size)]), nil
}

func (s *socketCore) setOption(option C.zlink_option_t, ptr unsafe.Pointer, size C.size_t) error {
	if s == nil || s.closed {
		return stateError("socket is closed")
	}
	return checkRC(C.zlink_set_option(s.handle, option, ptr, size))
}

func (s *socketCore) setDurationOption(option C.zlink_option_t, value time.Duration) error {
	ms, err := durationToMillis(value)
	if err != nil {
		return err
	}
	return s.setIntOption(option, ms)
}

func (s *socketCore) withCString(value string, fn func(*C.char) error) error {
	if s == nil || s.closed {
		return stateError("socket is closed")
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

func prepareMultipart(parts []*Message) ([]C.zlink_msg_t, error) {
	if len(parts) == 0 {
		return nil, validationError("multipart payload must contain at least one part")
	}
	native := make([]C.zlink_msg_t, len(parts))
	for i, part := range parts {
		if part == nil {
			return nil, validationError("part %d is nil", i)
		}
		if part.closed {
			return nil, validationError("part %d is already closed", i)
		}
		if err := checkRC(C.zlink_msg_init(&native[i])); err != nil {
			closeNativeMultipart(native, i)
			return nil, err
		}
		if err := checkRC(C.zlink_msg_move(&native[i], &part.msg)); err != nil {
			closeNativeMultipart(native, i+1)
			return nil, err
		}
		part.moved()
	}
	return native, nil
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
		if err := checkRC(C.zlink_msg_init(&msg.msg)); err != nil {
			closeMessageSlice(parts)
			C.zlink_multipart_close(ptr, partCount)
			return nil, err
		}
		if err := checkRC(C.zlink_msg_move(&msg.msg, &raw[i])); err != nil {
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
	if err := checkRC(C.zlink_get_routing_id(handle, &raw)); err != nil {
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
		return "", false, validationError("subscription index must be non-negative")
	}
	var size C.size_t
	var isPattern C.int
	err := checkRC(C.zlink_subscription_at(handle, C.size_t(index), nil, &size, &isPattern))
	if err == nil {
		return "", isPattern != 0, nil
	}
	zerr, ok := err.(*ZlinkError)
	if !ok || zerr.Kind != ErrorKindNative || zerr.Code != int(C.EINVAL) || size == 0 {
		return "", false, err
	}
	buf := make([]byte, int(size))
	if err := checkRC(C.zlink_subscription_at(handle, C.size_t(index), (*C.char)(unsafe.Pointer(&buf[0])), &size, &isPattern)); err != nil {
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
		return checkRC(C.zlink_set_tls_server(handle, certC, keyC, required))
	})
}

func setTLSClient(handle unsafe.Pointer, caCertPath string, hostname string, trustSystem bool) error {
	return withCStringPair(caCertPath, hostname, func(caCertC *C.char, hostnameC *C.char) error {
		var trust C.int
		if trustSystem {
			trust = 1
		}
		return checkRC(C.zlink_set_tls_client(handle, caCertC, hostnameC, trust))
	})
}

func recvTopicMessage(
	call func(*C.zlink_routing_id_t, **C.zlink_msg_t, *C.size_t, *C.char, *C.size_t) error,
) (*TopicMessage, error) {
	var rid C.zlink_routing_id_t
	var parts *C.zlink_msg_t
	var partCount C.size_t
	topicBuf := make([]byte, recvTopicBufferCap)
	topicLen := C.size_t(len(topicBuf))
	if err := call(&rid, &parts, &partCount, (*C.char)(unsafe.Pointer(&topicBuf[0])), &topicLen); err != nil {
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

func tryRecvTopicMessage(
	call func(*C.zlink_routing_id_t, **C.zlink_msg_t, *C.size_t, *C.char, *C.size_t) C.int,
) (*TopicMessage, bool, error) {
	var rid C.zlink_routing_id_t
	var parts *C.zlink_msg_t
	var partCount C.size_t
	topicBuf := make([]byte, recvTopicBufferCap)
	topicLen := C.size_t(len(topicBuf))
	rc := call(&rid, &parts, &partCount, (*C.char)(unsafe.Pointer(&topicBuf[0])), &topicLen)
	if rc == -1 {
		if emptyErrno() {
			return nil, false, nil
		}
		return nil, false, lastError()
	}
	clonedParts, err := takeParts(parts, partCount)
	if err != nil {
		return nil, false, err
	}
	return &TopicMessage{
		routingID: routingIDFromC(rid),
		topic:     string(topicBuf[:int(topicLen)]),
		parts:     clonedParts,
	}, true, nil
}

func recvSubscriptionEvent(
	call func(*C.zlink_routing_id_t, *C.int, *C.char, *C.size_t) error,
) (*SubscriptionEvent, error) {
	var rid C.zlink_routing_id_t
	var subscribed C.int
	topicBuf := make([]byte, recvTopicBufferCap)
	topicLen := C.size_t(len(topicBuf))
	if err := call(&rid, &subscribed, (*C.char)(unsafe.Pointer(&topicBuf[0])), &topicLen); err != nil {
		return nil, err
	}
	return &SubscriptionEvent{
		routingID:  routingIDFromC(rid),
		subscribed: subscribed != 0,
		topic:      string(topicBuf[:int(topicLen)]),
	}, nil
}

func tryRecvSubscriptionEvent(
	call func(*C.zlink_routing_id_t, *C.int, *C.char, *C.size_t) C.int,
) (*SubscriptionEvent, bool, error) {
	var rid C.zlink_routing_id_t
	var subscribed C.int
	topicBuf := make([]byte, recvTopicBufferCap)
	topicLen := C.size_t(len(topicBuf))
	rc := call(&rid, &subscribed, (*C.char)(unsafe.Pointer(&topicBuf[0])), &topicLen)
	if rc == -1 {
		if emptyErrno() {
			return nil, false, nil
		}
		return nil, false, lastError()
	}
	return &SubscriptionEvent{
		routingID:  routingIDFromC(rid),
		subscribed: subscribed != 0,
		topic:      string(topicBuf[:int(topicLen)]),
	}, true, nil
}

func emptyErrno() bool {
	return int(C.zlink_errno()) == int(C.EAGAIN)
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
	var raw C.int
	if value {
		raw = 1
	}
	return checkRC(C.zlink_set_pub_option(s.raw(), option, unsafe.Pointer(&raw), C.size_t(C.sizeof_int)))
}

func (s *connectionSocket) getPubBoolOption(option C.zlink_pub_option_t) (bool, error) {
	var raw C.int
	size := C.size_t(C.sizeof_int)
	if err := checkRC(C.zlink_get_pub_option(s.raw(), option, unsafe.Pointer(&raw), &size)); err != nil {
		return false, err
	}
	return raw != 0, nil
}

func (s *connectionSocket) getPubIntOption(option C.zlink_pub_option_t) (int, error) {
	var raw C.int
	size := C.size_t(C.sizeof_int)
	if err := checkRC(C.zlink_get_pub_option(s.raw(), option, unsafe.Pointer(&raw), &size)); err != nil {
		return 0, err
	}
	return int(raw), nil
}

func (s *connectionSocket) getSubIntOption(option C.zlink_sub_option_t) (int, error) {
	var raw C.int
	size := C.size_t(C.sizeof_int)
	if err := checkRC(C.zlink_get_sub_option(s.raw(), option, unsafe.Pointer(&raw), &size)); err != nil {
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

func (s *directSocket) Send(parts ...*Message) error {
	native, err := prepareMultipart(parts)
	if err != nil {
		return err
	}
	return checkRC(C.zlink_send(s.raw(), &native[0], C.size_t(len(native)), 0))
}

func (s *directSocket) TrySend(parts ...*Message) (SendResult, error) {
	native, err := prepareMultipart(parts)
	if err != nil {
		return 0, err
	}
	var result C.zlink_send_result_t
	if err := checkRC(C.zlink_try_send(s.raw(), &native[0], C.size_t(len(native)), &result)); err != nil {
		return 0, err
	}
	return sendResultFromC(result)
}

func (s *directSocket) Recv() (*Received, error) {
	var rid C.zlink_routing_id_t
	var parts *C.zlink_msg_t
	var partCount C.size_t
	if err := checkRC(C.zlink_recv(s.raw(), &rid, &parts, &partCount, 0)); err != nil {
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

func (s *directSocket) TryRecv() (*Received, bool, error) {
	var rid C.zlink_routing_id_t
	var parts *C.zlink_msg_t
	var partCount C.size_t
	rc := C.zlink_recv(s.raw(), &rid, &parts, &partCount, C.ZLINK_DONTWAIT)
	if rc == -1 {
		if emptyErrno() {
			return nil, false, nil
		}
		return nil, false, lastError()
	}
	clonedParts, err := takeParts(parts, partCount)
	if err != nil {
		return nil, false, err
	}
	return &Received{
		routingID: routingIDFromC(rid),
		parts:     clonedParts,
	}, true, nil
}

func (s *directSocket) OnReceive(handler func(*Received)) error {
	if handler == nil {
		return validationError("receive handler must not be nil")
	}
	if s.recvHandle != 0 {
		s.recvHandle.Delete()
	}
	handle := cgo.NewHandle(recvCallback(handler))
	if err := checkRC(C.zlink_recv_handler_go_local(s.raw(), C.uintptr_t(handle))); err != nil {
		handle.Delete()
		return err
	}
	s.recvHandle = handle
	return nil
}

type publishSocket struct {
	*connectionSocket
}

func (s *publishSocket) Publish(topic string, parts ...*Message) error {
	native, err := prepareMultipart(parts)
	if err != nil {
		return err
	}
	return s.withCString(topic, func(cstr *C.char) error {
		return checkRC(C.zlink_publish(s.raw(), cstr, &native[0], C.size_t(len(native)), 0))
	})
}

func (s *publishSocket) TryPublish(topic string, parts ...*Message) (SendResult, error) {
	native, err := prepareMultipart(parts)
	if err != nil {
		return 0, err
	}
	var result C.zlink_send_result_t
	err = s.withCString(topic, func(cstr *C.char) error {
		return checkRC(C.zlink_try_publish(s.raw(), cstr, &native[0], C.size_t(len(native)), &result))
	})
	if err != nil {
		return 0, err
	}
	return sendResultFromC(result)
}

type routedSocket struct {
	*connectionSocket
}

func (s *routedSocket) SendTo(target RoutingID, parts ...*Message) error {
	native, err := prepareMultipart(parts)
	if err != nil {
		return err
	}
	rid := target.toC()
	return checkRC(C.zlink_send_rid(s.raw(), &rid, &native[0], C.size_t(len(native)), 0))
}

func (s *routedSocket) TrySendTo(target RoutingID, parts ...*Message) (SendResult, error) {
	native, err := prepareMultipart(parts)
	if err != nil {
		return 0, err
	}
	rid := target.toC()
	var result C.zlink_send_result_t
	if err := checkRC(C.zlink_try_send_rid(s.raw(), &rid, &native[0], C.size_t(len(native)), &result)); err != nil {
		return 0, err
	}
	return sendResultFromC(result)
}

func (s *routedSocket) Recv() (*Received, error) {
	ds := &directSocket{connectionSocket: s.connectionSocket}
	return ds.Recv()
}

func (s *routedSocket) TryRecv() (*Received, bool, error) {
	ds := &directSocket{connectionSocket: s.connectionSocket}
	return ds.TryRecv()
}

func (s *routedSocket) OnReceive(handler func(*Received)) error {
	ds := &directSocket{connectionSocket: s.connectionSocket}
	return ds.OnReceive(handler)
}

type subscribeSocket struct {
	*connectionSocket
}

func (s *subscribeSocket) SetSubscription(filter string) error {
	return s.withCString(filter, func(cstr *C.char) error {
		return checkRC(C.zlink_set_subscription(s.raw(), cstr))
	})
}

func (s *subscribeSocket) UnsetSubscription(filter string) error {
	return s.withCString(filter, func(cstr *C.char) error {
		return checkRC(C.zlink_unset_subscription(s.raw(), cstr))
	})
}

func (s *subscribeSocket) Subscribe() (*TopicMessage, error) {
	return recvTopicMessage(func(rid *C.zlink_routing_id_t, parts **C.zlink_msg_t, partCount *C.size_t, topic *C.char, topicLen *C.size_t) error {
		return checkRC(C.zlink_subscribe(s.raw(), rid, parts, partCount, topic, topicLen, 0))
	})
}

func (s *subscribeSocket) TrySubscribe() (*TopicMessage, bool, error) {
	return tryRecvTopicMessage(func(rid *C.zlink_routing_id_t, parts **C.zlink_msg_t, partCount *C.size_t, topic *C.char, topicLen *C.size_t) C.int {
		return C.zlink_subscribe(s.raw(), rid, parts, partCount, topic, topicLen, C.ZLINK_DONTWAIT)
	})
}

func (s *subscribeSocket) OnSubscribe(handler func(*TopicMessage)) error {
	if handler == nil {
		return validationError("subscribe handler must not be nil")
	}
	if s.subscribeHandle != 0 {
		s.subscribeHandle.Delete()
	}
	handle := cgo.NewHandle(subscribeCallback(handler))
	if err := checkRC(C.zlink_subscribe_handler_go_local(s.raw(), C.uintptr_t(handle))); err != nil {
		handle.Delete()
		return err
	}
	s.subscribeHandle = handle
	return nil
}

type xpubSubscribeSocket struct {
	*publishSocket
}

func (s *xpubSubscribeSocket) ReceiveSubscriptionEvent() (*SubscriptionEvent, error) {
	return recvSubscriptionEvent(func(rid *C.zlink_routing_id_t, subscribed *C.int, topic *C.char, topicLen *C.size_t) error {
		return checkRC(C.zlink_subscription_event(s.raw(), rid, subscribed, topic, topicLen, 0))
	})
}

func (s *xpubSubscribeSocket) TryReceiveSubscriptionEvent() (*SubscriptionEvent, bool, error) {
	return tryRecvSubscriptionEvent(func(rid *C.zlink_routing_id_t, subscribed *C.int, topic *C.char, topicLen *C.size_t) C.int {
		return C.zlink_subscription_event(s.raw(), rid, subscribed, topic, topicLen, C.ZLINK_DONTWAIT)
	})
}

func (s *connectionSocket) setSendReady(handler func()) error {
	if handler == nil {
		return validationError("send-ready handler must not be nil")
	}
	if s.sendReadyHandle != 0 {
		s.sendReadyHandle.Delete()
	}
	handle := cgo.NewHandle(sendReadyCallback(handler))
	if err := checkRC(C.zlink_send_ready_handler_go_local(s.raw(), C.uintptr_t(handle))); err != nil {
		handle.Delete()
		return err
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
		return stateError("discovery is closed")
	}
	return checkRC(C.zlink_socket_attach_discovery(s.raw(), discovery.raw()))
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
		return stateError("discovery is closed")
	}
	return checkRC(C.zlink_socket_attach_discovery(s.raw(), discovery.raw()))
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
	return checkRC(C.zlink_set_routing_id(s.raw(), routingIDPointer(&raw), C.size_t(raw.size)))
}

func (s *DealerSocket) SetProbe(value bool) error {
	var raw C.int
	if value {
		raw = 1
	}
	return checkRC(C.zlink_set_dealer_option(s.raw(), C.ZLINK_DEALER_OPT_PROBE, unsafe.Pointer(&raw), C.size_t(C.sizeof_int)))
}

func (s *DealerSocket) RoutingID() (RoutingID, error) {
	return getHandleRoutingID(s.raw())
}

func (s *DealerSocket) AttachDiscovery(discovery *Discovery) error {
	if discovery == nil || discovery.closed {
		return stateError("discovery is closed")
	}
	return checkRC(C.zlink_socket_attach_discovery(s.raw(), discovery.raw()))
}

func (s *DealerSocket) OnSendReady(handler func()) error {
	return s.connectionSocket.setSendReady(handler)
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
	return checkRC(C.zlink_set_router_option(s.raw(), C.ZLINK_ROUTER_OPT_MANDATORY, unsafe.Pointer(&raw), C.size_t(C.sizeof_int)))
}

func (s *RouterSocket) SetHandover(value bool) error {
	var raw C.int
	if value {
		raw = 1
	}
	return checkRC(C.zlink_set_router_option(s.raw(), C.ZLINK_ROUTER_OPT_HANDOVER, unsafe.Pointer(&raw), C.size_t(C.sizeof_int)))
}

func (s *RouterSocket) SetProbe(value bool) error {
	var raw C.int
	if value {
		raw = 1
	}
	return checkRC(C.zlink_set_router_option(s.raw(), C.ZLINK_ROUTER_OPT_PROBE, unsafe.Pointer(&raw), C.size_t(C.sizeof_int)))
}

func (s *RouterSocket) SetRoutingID(id RoutingID) error {
	raw := id.toC()
	return checkRC(C.zlink_set_routing_id(s.raw(), routingIDPointer(&raw), C.size_t(raw.size)))
}

func (s *RouterSocket) SetConnectRoutingID(id RoutingID) error {
	raw := id.toC()
	return checkRC(C.zlink_set_router_option(s.raw(), C.ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID, routingIDPointer(&raw), C.size_t(raw.size)))
}

func (s *RouterSocket) RoutingID() (RoutingID, error) {
	return getHandleRoutingID(s.raw())
}

func (s *RouterSocket) AttachDiscovery(discovery *Discovery) error {
	if discovery == nil || discovery.closed {
		return stateError("discovery is closed")
	}
	return checkRC(C.zlink_socket_attach_discovery(s.raw(), discovery.raw()))
}

func (s *RouterSocket) OnSendReady(handler func()) error {
	return s.connectionSocket.setSendReady(handler)
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
	return checkRC(C.zlink_set_routing_id(s.raw(), routingIDPointer(&raw), C.size_t(raw.size)))
}

func (s *StreamSocket) RoutingID() (RoutingID, error) {
	return getHandleRoutingID(s.raw())
}

func (s *StreamSocket) SendTo(target RoutingID, parts ...*Message) error {
	return s.core.SendTo(target, parts...)
}

func (s *StreamSocket) TrySendTo(target RoutingID, parts ...*Message) (SendResult, error) {
	return s.core.TrySendTo(target, parts...)
}

func (s *StreamSocket) Recv() (*Received, error) {
	return s.core.Recv()
}

func (s *StreamSocket) TryRecv() (*Received, bool, error) {
	return s.core.TryRecv()
}

func (s *StreamSocket) OnReceive(handler func(*Received)) error {
	return s.core.OnReceive(handler)
}

func (s *StreamSocket) SetNotify(value bool) error {
	var raw C.int
	if value {
		raw = 1
	}
	return checkRC(C.zlink_set_stream_option(s.raw(), C.ZLINK_STREAM_OPT_NOTIFY, unsafe.Pointer(&raw), C.size_t(C.sizeof_int)))
}

func (s *StreamSocket) Notify() (bool, error) {
	var raw C.int
	size := C.size_t(C.sizeof_int)
	if err := checkRC(C.zlink_get_stream_option(s.raw(), C.ZLINK_STREAM_OPT_NOTIFY, unsafe.Pointer(&raw), &size)); err != nil {
		return false, err
	}
	return raw != 0, nil
}

func (s *StreamSocket) OnSendReady(handler func()) error {
	return s.core.connectionSocket.setSendReady(handler)
}
