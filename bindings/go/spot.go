// SPDX-License-Identifier: MPL-2.0

package zlink

/*
#include <stdint.h>
#include <stdlib.h>
#include "zlink.h"

extern void goZlinkSubscribeTrampoline(zlink_routing_id_t *source_rid_, char *topic_, size_t topic_len_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);
extern void goZlinkSendReadyTrampoline(void *subject_, uintptr_t userdata_);
extern void goZlinkSpotRoutedTrampoline(zlink_routing_id_t *source_node_rid_, zlink_routing_id_t *source_spot_rid_, uint64_t request_seq_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);
extern void goZlinkSpotDispatchEventTrampoline(void *spot_, const zlink_spot_dispatch_info_t *info_, uintptr_t userdata_);
extern void goZlinkReplyTrampoline(zlink_request_result_t result_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);

static inline int zlink_spot_send_ready_handler_go_local(void *s, uintptr_t userdata) {
    return zlink_send_ready_handler(s, (zlink_send_ready_handler_fn)goZlinkSendReadyTrampoline, (void *)userdata);
}

static inline int zlink_spot_handler_go_local(void *s, uintptr_t userdata) {
    return zlink_spot_handler(s, (zlink_spot_handler_fn)goZlinkSpotRoutedTrampoline, (void *)userdata);
}

static inline int zlink_spot_dispatch_event_handler_go_local(void *s, uintptr_t userdata) {
    return zlink_spot_dispatch_event_handler(s, (zlink_spot_dispatch_event_handler_fn)goZlinkSpotDispatchEventTrampoline, (void *)userdata);
}

static inline int zlink_spot_request_channel_part_go_local(void *spot, const char *channel_name, zlink_msg_t *part, zlink_send_flags_t flags, zlink_part_flag_t part_flag, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_spot_request_channel_part(spot, channel_name, part, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata, flags, part_flag, timeout_ms);
}
*/
import "C"

import (
	"runtime/cgo"
	"strings"
	"sync"
	"time"
	"unsafe"
)

type SpotNode struct {
	handle  unsafe.Pointer
	closed  bool
	closing bool
	mu      sync.Mutex
	spots   map[*spotCore]struct{}
}

func newSpotNode(ctx *Context) (*SpotNode, error) {
	if ctx == nil || ctx.closed {
		return nil, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	handle := C.zlink_spot_node_new(ctx.raw())
	if handle == nil {
		return nil, configErrorFromErrno(currentErrno())
	}
	return &SpotNode{
		handle: handle,
		spots: make(map[*spotCore]struct{}),
	}, nil
}

func (n *SpotNode) raw() unsafe.Pointer {
	if n == nil {
		return nil
	}
	return n.handle
}

func (n *SpotNode) Bind(endpoint string) error {
	return n.withCString(endpoint, func(cstr *C.char) error {
		handle, err := n.handleOrError()
		if err != nil {
			return err
		}
		return bindErrorFromResult(C.zlink_spot_node_bind(handle, cstr))
	})
}

func (n *SpotNode) ConnectPeer(endpoint string) error {
	return n.withCString(endpoint, func(cstr *C.char) error {
		handle, err := n.handleOrError()
		if err != nil {
			return err
		}
		return connectErrorFromResult(C.zlink_spot_node_connect_peer(handle, cstr))
	})
}

func (n *SpotNode) AttachDiscovery(discovery *Discovery) error {
	if discovery == nil || discovery.closed {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	handle, err := n.handleOrError()
	if err != nil {
		return err
	}
	return configErrorFromResult(C.zlink_spot_node_attach_discovery(handle, discovery.raw()))
}

func (n *SpotNode) AttachChannelDealer(discovery *Discovery, dealer *DealerSocket) error {
	if discovery == nil || discovery.closed {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	handle, err := n.handleOrError()
	if err != nil {
		return err
	}
	dealerHandle, err := socketHandle(dealer)
	if err != nil {
		return err
	}
	return configErrorFromResult(C.zlink_spot_node_attach_channel_dealer(handle, discovery.raw(), dealerHandle))
}

func (n *SpotNode) AttachChannelDealerManual(channelName string, dealer *DealerSocket) error {
	handle, err := n.handleOrError()
	if err != nil {
		return err
	}
	dealerHandle, err := socketHandle(dealer)
	if err != nil {
		return err
	}
	return n.withCString(channelName, func(cstr *C.char) error {
		return configErrorFromResult(C.zlink_spot_node_attach_channel_dealer_manual(handle, cstr, dealerHandle))
	})
}

func (n *SpotNode) AttachPubIngress(pub *PubSocket) error {
	handle, err := n.handleOrError()
	if err != nil {
		return err
	}
	pubHandle, err := socketHandle(pub)
	if err != nil {
		return err
	}
	return configErrorFromResult(C.zlink_spot_node_attach_pub_ingress(handle, pubHandle))
}

func (n *SpotNode) SetRoutingID(id RoutingID) error {
	handle, err := n.handleOrError()
	if err != nil {
		return err
	}
	raw := id.toC()
	return configErrorFromResult(C.zlink_set_routing_id(handle, routingIDPointer(&raw), C.size_t(raw.size)))
}

func (n *SpotNode) RoutingID() (RoutingID, error) {
	handle, err := n.handleOrError()
	if err != nil {
		return RoutingID{}, err
	}
	return getHandleRoutingID(handle)
}

func (n *SpotNode) DisconnectPeer(endpoint string) error {
	return n.withCString(endpoint, func(cstr *C.char) error {
		handle, err := n.handleOrError()
		if err != nil {
			return err
		}
		return connectErrorFromResult(C.zlink_spot_node_disconnect_peer(handle, cstr))
	})
}

func (n *SpotNode) SetTLSServer(certPath string, keyPath string, requireClientCert bool) error {
	handle, err := n.handleOrError()
	if err != nil {
		return err
	}
	return setTLSServer(handle, certPath, keyPath, requireClientCert)
}

func (n *SpotNode) SetTLSClient(caCertPath string, hostname string, trustSystem bool) error {
	handle, err := n.handleOrError()
	if err != nil {
		return err
	}
	return setTLSClient(handle, caCertPath, hostname, trustSystem)
}

func (n *SpotNode) Spot() (*Spot, error) {
	if n == nil {
		return nil, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	n.mu.Lock()
	defer n.mu.Unlock()
	if n.closed || n.closing || n.handle == nil {
		return nil, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	handle := C.zlink_spot_new(n.handle)
	if handle == nil {
		return nil, configErrorFromErrno(currentErrno())
	}
	core := &spotCore{handle: handle, owner: n}
	n.spots[core] = struct{}{}
	return &Spot{core: core}, nil
}

func (n *SpotNode) Close() error {
	if n == nil {
		return nil
	}
	n.mu.Lock()
	if n.closed || n.closing {
		n.mu.Unlock()
		return nil
	}
	n.closing = true
	spots := make([]*spotCore, 0, len(n.spots))
	for spot := range n.spots {
		spots = append(spots, spot)
	}
	n.mu.Unlock()

	for _, spot := range spots {
		if err := spot.Close(); err != nil {
			n.mu.Lock()
			n.closing = false
			n.mu.Unlock()
			return err
		}
	}

	n.mu.Lock()
	defer n.mu.Unlock()
	if n.closed {
		n.closing = false
		return nil
	}
	handle := n.handle
	if err := closeErrorFromResult(C.zlink_spot_node_destroy(&handle)); err != nil {
		n.closing = false
		return err
	}
	n.closed = true
	n.closing = false
	n.handle = nil
	n.spots = nil
	return nil
}

func (n *SpotNode) withCString(value string, fn func(*C.char) error) error {
	if err := validateEndpointString(value); err != nil {
		return err
	}
	cstr := C.CString(value)
	defer C.free(unsafe.Pointer(cstr))
	return fn(cstr)
}

func (n *SpotNode) handleOrError() (unsafe.Pointer, error) {
	if n == nil {
		return nil, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	n.mu.Lock()
	defer n.mu.Unlock()
	if n.closed || n.closing || n.handle == nil {
		return nil, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	return n.handle, nil
}

func (n *SpotNode) unregisterSpot(spot *spotCore) {
	if n == nil {
		return
	}
	n.mu.Lock()
	defer n.mu.Unlock()
	if n.spots == nil {
		return
	}
	delete(n.spots, spot)
}

type spotCore struct {
	handle          unsafe.Pointer
	closed          bool
	subscribeHandle cgo.Handle
	routedHandle    cgo.Handle
	sendReadyHandle cgo.Handle
	dispatchHandle  cgo.Handle
	owner           *SpotNode
	mu              sync.Mutex
}

func (s *spotCore) raw() unsafe.Pointer {
	if s == nil {
		return nil
	}
	return s.handle
}

func (s *spotCore) Close() error {
	if s == nil {
		return nil
	}
	s.mu.Lock()
	if s.closed {
		s.mu.Unlock()
		return nil
	}
	handle := s.handle
	if err := closeErrorFromResult(C.zlink_spot_destroy(&handle)); err != nil {
		s.mu.Unlock()
		return err
	}
	subscribeHandle := s.subscribeHandle
	routedHandle := s.routedHandle
	sendReadyHandle := s.sendReadyHandle
	dispatchHandle := s.dispatchHandle
	owner := s.owner
	s.subscribeHandle = 0
	s.routedHandle = 0
	s.sendReadyHandle = 0
	s.dispatchHandle = 0
	s.closed = true
	s.handle = nil
	s.owner = nil
	s.mu.Unlock()
	if subscribeHandle != 0 {
		subscribeHandle.Delete()
	}
	if routedHandle != 0 {
		routedHandle.Delete()
	}
	if sendReadyHandle != 0 {
		sendReadyHandle.Delete()
	}
	if dispatchHandle != 0 {
		dispatchHandle.Delete()
	}
	if owner != nil {
		owner.unregisterSpot(s)
	}
	return nil
}

func (s *spotCore) setOption(option C.zlink_option_t, ptr unsafe.Pointer, size C.size_t) error {
	if s == nil {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	return setNativeOption(s.handle, s.closed, "spot is closed", option, ptr, size)
}

func (s *spotCore) AdmissionState() (AdmissionState, error) {
	if s == nil {
		return AdmissionStateServing, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	var raw C.zlink_admission_state_t
	if err := configErrorFromResult(C.zlink_get_admission_state(s.handle, &raw)); err != nil {
		return AdmissionStateServing, err
	}
	return AdmissionState(raw), nil
}

func (s *spotCore) SetAdmissionState(state AdmissionState) error {
	if s == nil {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	return configErrorFromResult(C.zlink_set_admission_state(s.handle, C.zlink_admission_state_t(state)))
}

func (s *spotCore) setIntOption(option C.zlink_option_t, value int32) error {
	if s == nil {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	return setNativeIntOption(s.handle, s.closed, "spot is closed", option, value)
}

func (s *spotCore) setDurationOption(option C.zlink_option_t, value time.Duration) error {
	if s == nil {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	return setNativeDurationOption(s.handle, s.closed, "spot is closed", option, value)
}

func (s *spotCore) withCString(value string, fn func(*C.char) error) error {
	if s == nil || s.closed {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	if strings.IndexByte(value, 0) >= 0 {
		return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
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

func (s *Spot) SetRoutingID(rid RoutingID) error {
	if s == nil || s.core == nil || s.core.closed {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	raw := rid.toC()
	return configErrorFromResult(C.zlink_set_routing_id(s.raw(), routingIDPointer(&raw), C.size_t(raw.size)))
}

func (s *Spot) RoutingID() (RoutingID, error) {
	if s == nil || s.core == nil || s.core.closed {
		return RoutingID{}, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	return getHandleRoutingID(s.raw())
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
	if s == nil || s.core == nil {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	return setNativePubBoolOption(s.raw(), s.core.closed, "spot is closed", C.ZLINK_PUB_OPT_NODROP, value)
}

func (s *Spot) AdmissionState() (AdmissionState, error) {
	if s == nil || s.core == nil || s.core.closed {
		return AdmissionStateServing, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	return s.core.AdmissionState()
}

func (s *Spot) SetAdmissionState(state AdmissionState) error {
	if s == nil || s.core == nil || s.core.closed {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	return s.core.SetAdmissionState(state)
}

func (s *Spot) Publish(serviceName, topic string, flags SendFlags, parts ...*Message) error {
	return s.core.withCString(serviceName, func(serviceC *C.char) error {
		return s.core.withCString(topic, func(topicC *C.char) error {
			return submitMultipartFromClones(parts, true, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
				return submitErrorFromResult(C.zlink_spot_publish_part(s.raw(), serviceC, topicC, part, C.zlink_send_flags_t(flags), partFlag))
			})
		})
	})
}

func (s *Spot) SendChannel(channelName string, flags SendFlags, parts ...*Message) error {
	return s.core.withCString(channelName, func(cstr *C.char) error {
		return submitMultipartFromClones(parts, true, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
			return submitErrorFromResult(C.zlink_spot_send_channel_part(s.raw(), cstr, part, C.zlink_send_flags_t(flags), partFlag))
		})
	})
}

func (s *Spot) SendToSpot(destNodeRid, destSpotRid RoutingID, flags SendFlags, parts ...*Message) error {
	node := destNodeRid.toC()
	spot := destSpotRid.toC()
	return submitMultipartFromClones(parts, true, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
		return submitErrorFromResult(C.zlink_spot_send_spot_part(s.raw(), &node, &spot, part, C.zlink_send_flags_t(flags), partFlag))
	})
}

func (s *Spot) RequestChannel(channelName string, callback RequestReplyCallback, flags SendFlags, timeout time.Duration, parts ...*Message) error {
	if callback == nil {
		return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	resultCh, err := s.startChannelRequest(channelName, flags, timeout, parts...)
	if err != nil {
		return err
	}
	go func() {
		result := <-resultCh
		callback(result.result, result.parts)
	}()
	return nil
}

func (s *Spot) ReplyToSpot(destNodeRid, destSpotRid RoutingID, requestSeq uint64, flags SendFlags, parts ...*Message) error {
	if err := validateReplyFlags(flags); err != nil {
		return err
	}
	node := destNodeRid.toC()
	spot := destSpotRid.toC()
	return submitMultipartFromClones(parts, false, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
		return submitErrorFromResult(C.zlink_spot_reply_spot_part(s.raw(), &node, &spot, C.uint64_t(requestSeq), part, partFlag))
	})
}

func (s *Spot) ReplyToRouter(peerRid RoutingID, requestSeq uint64, flags SendFlags, parts ...*Message) error {
	if err := validateReplyFlags(flags); err != nil {
		return err
	}
	peer := peerRid.toC()
	return submitMultipartFromClones(parts, false, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
		return submitErrorFromResult(C.zlink_spot_reply_router_part(s.raw(), &peer, C.uint64_t(requestSeq), part, partFlag))
	})
}

func (s *Spot) SetSubscription(filter string) error {
	return s.core.withCString(filter, func(cstr *C.char) error {
		return configErrorFromResult(C.zlink_set_subscription(s.raw(), cstr))
	})
}

func (s *Spot) UnsetSubscription(filter string) error {
	return s.core.withCString(filter, func(cstr *C.char) error {
		return configErrorFromResult(C.zlink_unset_subscription(s.raw(), cstr))
	})
}

func (s *Spot) Subscribe(flags RecvFlags) (*TopicMessage, error) {
	return recvSpotTopicMessage(func(rid **C.zlink_routing_id_t, serviceName *C.char, serviceNameLen *C.size_t, topic *C.char, topicLen *C.size_t, part *C.zlink_msg_t, hasMore *C.zlink_part_flag_t, recvFlags C.zlink_recv_flags_t) error {
		return recvErrorFromResult(C.zlink_spot_subscribe_part(s.raw(), rid, serviceName, C.size_t(recvTopicBufferCap), serviceNameLen, topic, C.size_t(recvTopicBufferCap), topicLen, part, hasMore, recvFlags))
	}, flags)
}

func (s *Spot) ReceiveSubscriptionEvent(flags RecvFlags) (*SubscriptionEvent, error) {
	if s == nil || s.core == nil || s.core.closed {
		return nil, &RecvError{Result: RecvInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	_ = flags
	return nil, &RecvError{Result: RecvNotSupported, internalErrno: int(C.ENOTSUP)}
}

func (s *Spot) OnSendReady(handler func()) error {
	if handler == nil {
		return &HandlerError{Result: HandlerInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	if s == nil || s.core == nil || s.core.closed {
		return &HandlerError{Result: HandlerInvalidArgument, internalErrno: int(C.EFAULT)}
	}
	state := newSendReadyCallbackState(sendReadyCallback(handler))
	handle := cgo.NewHandle(state)
	if err := handlerErrorFromResult(C.zlink_spot_send_ready_handler_go_local(s.raw(), C.uintptr_t(handle))); err != nil {
		state.close()
		handle.Delete()
		return err
	}
	if s.core.sendReadyHandle != 0 {
		releaseCallbackHandle(s.core.sendReadyHandle)
	}
	s.core.sendReadyHandle = handle
	return nil
}

func (s *Spot) RecvRouted(flags RecvFlags) (*Received, error) {
	var sourceRID *C.zlink_routing_id_t
	var spotRID *C.zlink_routing_id_t
	var requestSeq C.uint64_t
	clonedParts, err := recvMultipart(func(part *C.zlink_msg_t, hasMore *C.zlink_part_flag_t) error {
		return recvErrorFromResult(C.zlink_spot_recv_part(s.raw(), &sourceRID, &spotRID, &requestSeq, part, hasMore, C.zlink_recv_flags_t(flags)))
	})
	if err != nil {
		return nil, err
	}
	return &Received{
		routingID:     routingIDFromCPtr(sourceRID),
		spotRID:       routingIDFromCPtr(spotRID),
		parts:         clonedParts,
		requestSeq:    uint64(requestSeq),
		hasRequestSeq: requestSeq != 0,
		reply:         receivedReplyToSpot(s, routingIDFromCPtr(sourceRID), routingIDFromCPtr(spotRID), uint64(requestSeq)),
	}, nil
}

func (s *Spot) OnRoutedReceive(handler func(sourceRid, spotRid RoutingID, requestSeq uint64, parts []*Message)) error {
	if handler == nil {
		return &HandlerError{Result: HandlerInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	state := newSpotRoutedCallbackState(handler)
	handle := cgo.NewHandle(state)
	if err := handlerErrorFromResult(C.zlink_spot_handler_go_local(s.raw(), C.uintptr_t(handle))); err != nil {
		state.close()
		handle.Delete()
		return err
	}
	if s.core.routedHandle != 0 {
		releaseCallbackHandle(s.core.routedHandle)
	}
	s.core.routedHandle = handle
	return nil
}

func (s *Spot) OnDispatchEvent(handler func(*Spot, SpotDispatchInfo)) error {
	if handler == nil {
		return &HandlerError{Result: HandlerInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	state := newSpotDispatchCallbackState(s, handler)
	handle := cgo.NewHandle(state)
	if err := handlerErrorFromResult(C.zlink_spot_dispatch_event_handler_go_local(s.raw(), C.uintptr_t(handle))); err != nil {
		state.close()
		handle.Delete()
		return err
	}
	if s.core.dispatchHandle != 0 {
		releaseCallbackHandle(s.core.dispatchHandle)
	}
	s.core.dispatchHandle = handle
	return nil
}

func (s *Spot) DrainChannelReplyFrom(subject unsafe.Pointer) error {
	if s == nil || s.core == nil || s.core.closed {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	if subject == nil {
		return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	if rc := C.zlink_spot_channel_reply_progress_from(s.raw(), subject); rc != 0 {
		return configErrorFromErrno(currentErrno())
	}
	return nil
}

func (s *Spot) startChannelRequest(channelName string, flags SendFlags, timeout time.Duration, parts ...*Message) (<-chan requestResult, error) {
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
	if err := s.core.withCString(channelName, func(cstr *C.char) error {
		return submitPreparedMultipart(prepared, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
			return submitErrorFromResult(C.zlink_spot_request_channel_part_go_local(
				s.raw(),
				cstr,
				part,
				C.zlink_send_flags_t(flags),
				partFlag,
				C.uint32_t(requestTimeoutMillis(timeout)),
				C.uintptr_t(handle),
			))
		})
	}); err != nil {
		handle.Delete()
		prepared.commit()
		return nil, err
	}
	prepared.commit()
	startSpotRequestProgress(s.raw(), state)
	return state.result, nil
}
