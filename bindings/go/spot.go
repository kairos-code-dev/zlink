// SPDX-License-Identifier: MPL-2.0

package zlink

/*
#include <stdint.h>
#include <stdlib.h>
#include "zlink.h"

extern void goZlinkSubscribeTrampoline(zlink_routing_id_t *source_rid_, char *topic_, size_t topic_len_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);
extern void goZlinkSendReadyTrampoline(void *subject_, uintptr_t userdata_);

static inline int zlink_spot_subscribe_handler_go_local(void *s, uintptr_t userdata) {
    return zlink_subscribe_handler(s, (zlink_subscribe_handler_fn)goZlinkSubscribeTrampoline, (void *)userdata);
}

static inline int zlink_spot_send_ready_handler_go_local(void *s, uintptr_t userdata) {
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

type SpotNode struct {
	handle unsafe.Pointer
	closed bool
}

func newSpotNode(ctx *Context) (*SpotNode, error) {
	if ctx == nil || ctx.closed {
		return nil, stateError("context is closed")
	}
	handle := C.zlink_spot_node_new(ctx.raw())
	if handle == nil {
		return nil, lastError()
	}
	return &SpotNode{handle: handle}, nil
}

func (n *SpotNode) raw() unsafe.Pointer {
	if n == nil {
		return nil
	}
	return n.handle
}

func (n *SpotNode) Bind(endpoint string) error {
	return n.withCString(endpoint, func(cstr *C.char) error {
		return checkRC(C.zlink_spot_node_bind(n.handle, cstr))
	})
}

func (n *SpotNode) ConnectPeer(endpoint string) error {
	return n.withCString(endpoint, func(cstr *C.char) error {
		return checkRC(C.zlink_spot_node_connect_peer(n.handle, cstr))
	})
}

func (n *SpotNode) AttachDiscovery(discovery *Discovery) error {
	if n == nil || n.closed {
		return stateError("spot node is closed")
	}
	if discovery == nil || discovery.closed {
		return stateError("discovery is closed")
	}
	return checkRC(C.zlink_spot_node_attach_discovery(n.handle, discovery.raw()))
}

func (n *SpotNode) DisconnectPeer(endpoint string) error {
	return n.withCString(endpoint, func(cstr *C.char) error {
		return checkRC(C.zlink_spot_node_disconnect_peer(n.handle, cstr))
	})
}

func (n *SpotNode) Spot() (*Spot, error) {
	if n == nil || n.closed {
		return nil, stateError("spot node is closed")
	}
	handle := C.zlink_spot_new(n.handle)
	if handle == nil {
		return nil, lastError()
	}
	return &Spot{core: &spotCore{handle: handle}}, nil
}

func (n *SpotNode) Close() error {
	if n == nil || n.closed {
		return nil
	}
	handle := n.handle
	if err := checkRC(C.zlink_spot_node_destroy(&handle)); err != nil {
		return err
	}
	n.closed = true
	n.handle = nil
	return nil
}

func (n *SpotNode) withCString(value string, fn func(*C.char) error) error {
	if n == nil || n.closed {
		return stateError("spot node is closed")
	}
	if err := validateEndpointString(value); err != nil {
		return err
	}
	cstr := C.CString(value)
	defer C.free(unsafe.Pointer(cstr))
	return fn(cstr)
}

type spotCore struct {
	handle          unsafe.Pointer
	closed          bool
	subscribeHandle cgo.Handle
	sendReadyHandle cgo.Handle
}

func (s *spotCore) raw() unsafe.Pointer {
	if s == nil {
		return nil
	}
	return s.handle
}

func (s *spotCore) Close() error {
	if s == nil || s.closed {
		return nil
	}
	handle := s.handle
	if err := checkRC(C.zlink_spot_destroy(&handle)); err != nil {
		return err
	}
	if s.subscribeHandle != 0 {
		s.subscribeHandle.Delete()
		s.subscribeHandle = 0
	}
	if s.sendReadyHandle != 0 {
		s.sendReadyHandle.Delete()
		s.sendReadyHandle = 0
	}
	s.closed = true
	s.handle = nil
	return nil
}

func (s *spotCore) setOption(option C.zlink_option_t, ptr unsafe.Pointer, size C.size_t) error {
	if s == nil || s.closed {
		return stateError("spot is closed")
	}
	return checkRC(C.zlink_set_option(s.handle, option, ptr, size))
}

func (s *spotCore) setIntOption(option C.zlink_option_t, value int32) error {
	return s.setOption(option, unsafe.Pointer(&value), C.size_t(C.sizeof_int))
}

func (s *spotCore) setDurationOption(option C.zlink_option_t, value time.Duration) error {
	ms, err := durationToMillis(value)
	if err != nil {
		return err
	}
	return s.setIntOption(option, ms)
}

func (s *spotCore) withCString(value string, fn func(*C.char) error) error {
	if s == nil || s.closed {
		return stateError("spot is closed")
	}
	if strings.IndexByte(value, 0) >= 0 {
		return validationError("string contains null byte")
	}
	cstr := C.CString(value)
	defer C.free(unsafe.Pointer(cstr))
	return fn(cstr)
}

type Spot struct {
	core *spotCore
}

func (s *Spot) raw() unsafe.Pointer {
	if s == nil || s.core == nil {
		return nil
	}
	return s.core.raw()
}

func (s *Spot) Close() error {
	if s == nil || s.core == nil {
		return nil
	}
	return s.core.Close()
}

func (s *Spot) SetSendHWM(value int) error {
	return s.core.setIntOption(C.ZLINK_OPT_SNDHWM, int32(value))
}

func (s *Spot) SetRecvHWM(value int) error {
	return s.core.setIntOption(C.ZLINK_OPT_RCVHWM, int32(value))
}

func (s *Spot) SetLinger(value time.Duration) error {
	return s.core.setDurationOption(C.ZLINK_OPT_LINGER, value)
}

func (s *Spot) SetRecvTimeout(value time.Duration) error {
	return s.core.setDurationOption(C.ZLINK_OPT_RCVTIMEO, value)
}

func (s *Spot) SetSendTimeout(value time.Duration) error {
	return s.core.setDurationOption(C.ZLINK_OPT_SNDTIMEO, value)
}

func (s *Spot) SetNoDrop(value bool) error {
	var raw C.int
	if value {
		raw = 1
	}
	return checkRC(C.zlink_set_pub_option(s.raw(), C.ZLINK_PUB_OPT_NODROP, unsafe.Pointer(&raw), C.size_t(C.sizeof_int)))
}

func (s *Spot) Publish(topic string, parts ...*Message) error {
	native, err := prepareMultipart(parts)
	if err != nil {
		return err
	}
	return s.core.withCString(topic, func(cstr *C.char) error {
		return checkRC(C.zlink_publish(s.raw(), cstr, &native[0], C.size_t(len(native)), 0))
	})
}

func (s *Spot) TryPublish(topic string, parts ...*Message) (SendResult, error) {
	native, err := prepareMultipart(parts)
	if err != nil {
		return 0, err
	}
	var result C.zlink_send_result_t
	err = s.core.withCString(topic, func(cstr *C.char) error {
		return checkRC(C.zlink_try_publish(s.raw(), cstr, &native[0], C.size_t(len(native)), &result))
	})
	if err != nil {
		return 0, err
	}
	return sendResultFromC(result)
}

func (s *Spot) SetTLSServer(certPath string, keyPath string, requireClientCert bool) error {
	if s == nil || s.core == nil || s.core.closed {
		return stateError("spot is closed")
	}
	return setTLSServer(s.raw(), certPath, keyPath, requireClientCert)
}

func (s *Spot) SetTLSClient(caCertPath string, hostname string, trustSystem bool) error {
	if s == nil || s.core == nil || s.core.closed {
		return stateError("spot is closed")
	}
	return setTLSClient(s.raw(), caCertPath, hostname, trustSystem)
}

func (s *Spot) SetSubscription(filter string) error {
	return s.core.withCString(filter, func(cstr *C.char) error {
		return checkRC(C.zlink_set_subscription(s.raw(), cstr))
	})
}

func (s *Spot) UnsetSubscription(filter string) error {
	return s.core.withCString(filter, func(cstr *C.char) error {
		return checkRC(C.zlink_unset_subscription(s.raw(), cstr))
	})
}

func (s *Spot) Subscribe() (*TopicMessage, error) {
	return recvTopicMessage(func(rid *C.zlink_routing_id_t, parts **C.zlink_msg_t, partCount *C.size_t, topic *C.char, topicLen *C.size_t) error {
		return checkRC(C.zlink_subscribe(s.raw(), rid, parts, partCount, topic, topicLen, 0))
	})
}

func (s *Spot) TrySubscribe() (*TopicMessage, bool, error) {
	return tryRecvTopicMessage(func(rid *C.zlink_routing_id_t, parts **C.zlink_msg_t, partCount *C.size_t, topic *C.char, topicLen *C.size_t) C.int {
		return C.zlink_subscribe(s.raw(), rid, parts, partCount, topic, topicLen, C.ZLINK_DONTWAIT)
	})
}

func (s *Spot) OnSubscribe(handler func(*TopicMessage)) error {
	if handler == nil {
		return validationError("subscribe handler must not be nil")
	}
	if s.core.subscribeHandle != 0 {
		s.core.subscribeHandle.Delete()
	}
	handle := cgo.NewHandle(subscribeCallback(handler))
	if err := checkRC(C.zlink_spot_subscribe_handler_go_local(s.raw(), C.uintptr_t(handle))); err != nil {
		handle.Delete()
		return err
	}
	s.core.subscribeHandle = handle
	return nil
}

func (s *Spot) OnSendReady(handler func()) error {
	if handler == nil {
		return validationError("send-ready handler must not be nil")
	}
	if s == nil || s.core == nil || s.core.closed {
		return stateError("spot is closed")
	}
	if s.core.sendReadyHandle != 0 {
		s.core.sendReadyHandle.Delete()
	}
	handle := cgo.NewHandle(sendReadyCallback(handler))
	if err := checkRC(C.zlink_spot_send_ready_handler_go_local(s.raw(), C.uintptr_t(handle))); err != nil {
		handle.Delete()
		return err
	}
	s.core.sendReadyHandle = handle
	return nil
}

func (n *SpotNode) MonitorOpen(events uint32) (*ServiceMonitor, error) {
	return OpenServiceMonitor(n, events)
}

func (s *Spot) MonitorOpen(events uint32) (*ServiceMonitor, error) {
	return OpenServiceMonitor(s, events)
}
