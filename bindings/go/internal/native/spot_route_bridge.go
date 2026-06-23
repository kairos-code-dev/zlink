// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include <stdlib.h>
#include "zlink.h"

extern void goZlinkReplyTrampoline(zlink_request_result_t result_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);

static inline int zlink_spot_route_bridge_request_go_local(
    void *bridge,
    const char *channel_name,
    const zlink_routing_id_t *target_spot_rid,
    zlink_msg_t *parts,
    size_t part_count,
    zlink_send_flags_t flags,
    uint32_t timeout_ms,
    uintptr_t userdata) {
    return zlink_spot_route_bridge_request(
        bridge,
        channel_name,
        target_spot_rid,
        parts,
        part_count,
        (zlink_reply_handler_fn)goZlinkReplyTrampoline,
        (void *)userdata,
        flags,
        timeout_ms);
}
*/
import "C"

import (
	"runtime/cgo"
	"sync"
	"time"
	"unsafe"
)

type SpotRouteBridgeEndpointCapabilities uint32

const (
	SpotRouteBridgeCapabilityNone           SpotRouteBridgeEndpointCapabilities = 0
	SpotRouteBridgeCapabilitySpotRoute      SpotRouteBridgeEndpointCapabilities = SpotRouteBridgeEndpointCapabilities(C.ZLINK_SPOT_ROUTE_BRIDGE_CAP_SPOT_ROUTE)
	SpotRouteBridgeCapabilityChannelInbound SpotRouteBridgeEndpointCapabilities = SpotRouteBridgeEndpointCapabilities(C.ZLINK_SPOT_ROUTE_BRIDGE_CAP_CHANNEL_INBOUND)
	SpotRouteBridgeRouteOnly                SpotRouteBridgeEndpointCapabilities = SpotRouteBridgeEndpointCapabilities(C.ZLINK_SPOT_ROUTE_BRIDGE_ROUTE_ONLY)
	SpotRouteBridgeRouteWithChannelInbound  SpotRouteBridgeEndpointCapabilities = SpotRouteBridgeEndpointCapabilities(C.ZLINK_SPOT_ROUTE_BRIDGE_ROUTE_WITH_CHANNEL_INBOUND)
)

type SpotRouteBridgeOptions struct {
	DefaultRequestTimeout time.Duration
	ErrorReplyPolicy      int
	ReceiveMode           int
}

type SpotRouteBridgeEndpointOptions struct {
	Capabilities       SpotRouteBridgeEndpointCapabilities
	InboundRelayPolicy int
}

type SpotRouteBridgeSummary struct {
	AttachedChannelCount   uint32
	PendingRequestCount    uint64
	RejectedInboundCount   uint64
	MalformedInboundCount  uint64
	RoutedSendFailureCount uint64
}

type SpotRouteBridge struct {
	handle    unsafe.Pointer
	closed    bool
	mu        sync.Mutex
	endpoints map[string]unsafe.Pointer
}

func (n *SpotNode) CreateRouteBridge(options *SpotRouteBridgeOptions) (*SpotRouteBridge, error) {
	if n == nil || n.ctx == nil {
		return nil, &ConfigError{Result: ConfigInvalidHandle, nativeErrno: int(C.EFAULT)}
	}
	nodeHandle, err := n.handleOrError()
	if err != nil {
		return nil, err
	}
	var nativeOptions C.zlink_spot_route_bridge_options_t
	var optionsPtr *C.zlink_spot_route_bridge_options_t
	if options != nil {
		nativeOptions.struct_size = C.uint32_t(C.sizeof_zlink_spot_route_bridge_options_t)
		nativeOptions.default_request_timeout_ms = C.int(requestTimeoutMillis(options.DefaultRequestTimeout))
		nativeOptions.error_reply_policy = C.int(options.ErrorReplyPolicy)
		nativeOptions.receive_mode = C.int(options.ReceiveMode)
		optionsPtr = &nativeOptions
	}
	handle := C.zlink_spot_route_bridge_new(n.ctx.raw(), nodeHandle, optionsPtr)
	if handle == nil {
		return nil, configErrorFromErrno(currentErrno())
	}
	return &SpotRouteBridge{handle: handle, endpoints: make(map[string]unsafe.Pointer)}, nil
}

func (b *SpotRouteBridge) AttachDealerChannel(channelName string, dealer *DealerSocket, options *SpotRouteBridgeEndpointOptions) error {
	dealerHandle, err := socketHandle(dealer)
	if err != nil {
		return err
	}
	return b.attach(channelName, dealerHandle, options, func(name *C.char, opts *C.zlink_spot_route_bridge_endpoint_options_t) C.int {
		return C.zlink_spot_route_bridge_attach_dealer_channel(b.raw(), name, dealerHandle, opts)
	})
}

func (b *SpotRouteBridge) AttachRouterChannel(channelName string, router *RouterSocket, options *SpotRouteBridgeEndpointOptions) error {
	routerHandle, err := socketHandle(router)
	if err != nil {
		return err
	}
	return b.attach(channelName, routerHandle, options, func(name *C.char, opts *C.zlink_spot_route_bridge_endpoint_options_t) C.int {
		return C.zlink_spot_route_bridge_attach_router_channel(b.raw(), name, routerHandle, opts)
	})
}

func (b *SpotRouteBridge) SetTargetNode(channelName string, targetNodeRID RoutingID) error {
	rid := targetNodeRID.toC()
	return b.withChannelCString(channelName, func(name *C.char) error {
		return configErrorFromResult(C.zlink_spot_route_bridge_set_target_node(b.raw(), name, &rid))
	})
}

func (b *SpotRouteBridge) Send(channelName string, targetSpotRID RoutingID, flags SendFlags, parts ...*Message) (bool, error) {
	rid := targetSpotRID.toC()
	cloned, err := cloneParts(parts)
	if err != nil {
		return false, err
	}
	prepared, err := prepareMultipart(cloned)
	if err != nil {
		closeMessageSlice(cloned)
		return false, err
	}
	err = b.withChannelCString(channelName, func(name *C.char) error {
		return submitErrorFromResult(C.zlink_spot_route_bridge_send(
			b.raw(),
			name,
			&rid,
			prepared.ptr(),
			prepared.count(),
			C.zlink_send_flags_t(flags),
		))
	})
	prepared.commit()
	return submitBackpressureResult(err)
}

func (b *SpotRouteBridge) Request(channelName string, targetSpotRID RoutingID, flags SendFlags, timeout time.Duration, callback RequestReplyCallback, parts ...*Message) (bool, error) {
	if callback == nil {
		return false, &ConfigError{Result: ConfigInvalidArgument, nativeErrno: int(C.EINVAL)}
	}
	state, err := b.startRequest(channelName, targetSpotRID, flags, timeout, parts...)
	ok, err := submitBackpressureResult(err)
	if err != nil || !ok {
		return ok, err
	}
	dispatchRequestCallback(state, callback)
	return true, nil
}

func (b *SpotRouteBridge) RequestSync(channelName string, targetSpotRID RoutingID, timeout time.Duration, parts ...*Message) ([]*Message, error) {
	state, err := b.startRequest(channelName, targetSpotRID, SendFlagsNone, timeout, parts...)
	if err != nil {
		return nil, err
	}
	result := state.wait()
	if result.result != RequestOK {
		return nil, requestErrorFromResult(result.result)
	}
	return result.parts, nil
}

func (b *SpotRouteBridge) Drain() error {
	return configErrorFromResult(C.zlink_spot_route_bridge_drain(b.raw()))
}

func (b *SpotRouteBridge) Summary() (SpotRouteBridgeSummary, error) {
	var raw C.zlink_spot_route_bridge_summary_t
	raw.struct_size = C.uint32_t(C.sizeof_zlink_spot_route_bridge_summary_t)
	if err := configErrorFromResult(C.zlink_spot_route_bridge_summary(b.raw(), &raw)); err != nil {
		return SpotRouteBridgeSummary{}, err
	}
	return SpotRouteBridgeSummary{
		AttachedChannelCount:   uint32(raw.attached_channel_count),
		PendingRequestCount:    uint64(raw.pending_request_count),
		RejectedInboundCount:   uint64(raw.rejected_inbound_count),
		MalformedInboundCount:  uint64(raw.malformed_inbound_count),
		RoutedSendFailureCount: uint64(raw.routed_send_failure_count),
	}, nil
}

func (b *SpotRouteBridge) Close() error {
	if b == nil || b.closed {
		return nil
	}
	if err := configErrorFromResult(C.zlink_spot_route_bridge_close(b.handle)); err != nil {
		return err
	}
	b.closed = true
	b.handle = nil
	return nil
}

func (b *SpotRouteBridge) startRequest(channelName string, targetSpotRID RoutingID, flags SendFlags, timeout time.Duration, parts ...*Message) (*replyCallbackState, error) {
	cloned, err := cloneParts(parts)
	if err != nil {
		return nil, err
	}
	prepared, err := prepareMultipart(cloned)
	if err != nil {
		closeMessageSlice(cloned)
		return nil, err
	}
	state := newReplyCallbackState()
	handle := cgo.NewHandle(state)
	rid := targetSpotRID.toC()
	err = b.withChannelCString(channelName, func(name *C.char) error {
		return submitErrorFromResult(C.zlink_spot_route_bridge_request_go_local(
			b.raw(),
			name,
			&rid,
			prepared.ptr(),
			prepared.count(),
			C.zlink_send_flags_t(flags),
			C.uint32_t(requestTimeoutMillis(timeout)),
			C.uintptr_t(handle),
		))
	})
	if err != nil {
		handle.Delete()
		prepared.commit()
		return nil, err
	}
	prepared.commit()
	startSocketRequestProgress(b.endpointHandle(channelName), state)
	return state, nil
}

func (b *SpotRouteBridge) attach(channelName string, endpointHandle unsafe.Pointer, options *SpotRouteBridgeEndpointOptions, attach func(*C.char, *C.zlink_spot_route_bridge_endpoint_options_t) C.int) error {
	var nativeOptions C.zlink_spot_route_bridge_endpoint_options_t
	var optionsPtr *C.zlink_spot_route_bridge_endpoint_options_t
	if options != nil {
		nativeOptions.struct_size = C.uint32_t(C.sizeof_zlink_spot_route_bridge_endpoint_options_t)
		nativeOptions.capabilities = C.uint32_t(options.Capabilities)
		nativeOptions.inbound_relay_policy = C.int(options.InboundRelayPolicy)
		optionsPtr = &nativeOptions
	}
	err := b.withChannelCString(channelName, func(name *C.char) error {
		return configErrorFromResult(attach(name, optionsPtr))
	})
	if err != nil {
		return err
	}
	b.mu.Lock()
	b.endpoints[channelName] = endpointHandle
	b.mu.Unlock()
	return nil
}

func (b *SpotRouteBridge) endpointHandle(channelName string) unsafe.Pointer {
	b.mu.Lock()
	defer b.mu.Unlock()
	return b.endpoints[channelName]
}

func (b *SpotRouteBridge) raw() unsafe.Pointer {
	if b == nil {
		return nil
	}
	return b.handle
}

func (b *SpotRouteBridge) withChannelCString(value string, fn func(*C.char) error) error {
	if b == nil || b.closed {
		return &ConfigError{Result: ConfigInvalidHandle, nativeErrno: int(C.EFAULT)}
	}
	if err := validateRouteChannelName(value); err != nil {
		return err
	}
	cstr := C.CString(value)
	defer C.free(unsafe.Pointer(cstr))
	return fn(cstr)
}

type SpotNodePublisher struct {
	handle unsafe.Pointer
	closed bool
}

func (n *SpotNode) CreatePublisher() (*SpotNodePublisher, error) {
	nodeHandle, err := n.handleOrError()
	if err != nil {
		return nil, err
	}
	handle := C.zlink_spot_node_publisher_new(nodeHandle)
	if handle == nil {
		return nil, configErrorFromErrno(currentErrno())
	}
	return &SpotNodePublisher{handle: handle}, nil
}

func (p *SpotNodePublisher) Publish(topic string, flags SendFlags, parts ...*Message) (bool, error) {
	cloned, err := cloneParts(parts)
	if err != nil {
		return false, err
	}
	prepared, err := prepareMultipart(cloned)
	if err != nil {
		closeMessageSlice(cloned)
		return false, err
	}
	err = p.withCString(topic, func(topicName *C.char) error {
		return submitErrorFromResult(C.zlink_spot_node_publisher_publish(
			p.raw(),
			topicName,
			prepared.ptr(),
			prepared.count(),
			C.zlink_send_flags_t(flags),
		))
	})
	prepared.commit()
	return submitBackpressureResult(err)
}

func (p *SpotNodePublisher) Close() error {
	if p == nil || p.closed {
		return nil
	}
	if err := configErrorFromResult(C.zlink_spot_node_publisher_close(p.handle)); err != nil {
		return err
	}
	p.closed = true
	p.handle = nil
	return nil
}

func (p *SpotNodePublisher) raw() unsafe.Pointer {
	if p == nil {
		return nil
	}
	return p.handle
}

func (p *SpotNodePublisher) withCString(value string, fn func(*C.char) error) error {
	if p == nil || p.closed {
		return &ConfigError{Result: ConfigInvalidHandle, nativeErrno: int(C.EFAULT)}
	}
	if err := validateTopicName(value); err != nil {
		return err
	}
	cstr := C.CString(value)
	defer C.free(unsafe.Pointer(cstr))
	return fn(cstr)
}
