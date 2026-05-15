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

static inline int zlink_spot_request_spot_part_go_local(void *spot, const zlink_routing_id_t *dest_node_rid, const zlink_routing_id_t *dest_spot_rid, zlink_msg_t *part, zlink_send_flags_t flags, zlink_part_flag_t part_flag, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_spot_request_spot_part(spot, dest_node_rid, dest_spot_rid, part, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata, flags, part_flag, timeout_ms);
}

static inline int zlink_spot_request_router_part_go_local(void *spot, const zlink_routing_id_t *peer_rid, zlink_msg_t *part, zlink_send_flags_t flags, zlink_part_flag_t part_flag, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_spot_request_router_part(spot, peer_rid, part, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata, flags, part_flag, timeout_ms);
}
*/
import "C"

import (
	"context"
	"errors"
	"runtime/cgo"
	"strings"
	"sync"
	"time"
	"unsafe"
)

type SpotNode struct {
	handle         unsafe.Pointer
	closed         bool
	closing        bool
	mu             sync.Mutex
	spots          map[*spotCore]struct{}
	dealerRegistry sync.Map
}

type SpotNodeMode int

const (
	SpotNodeModePubSub SpotNodeMode = SpotNodeMode(C.ZLINK_SPOT_NODE_MODE_PUBSUB)
	SpotNodeModeRouted SpotNodeMode = SpotNodeMode(C.ZLINK_SPOT_NODE_MODE_ROUTED)
	SpotNodeModeAll    SpotNodeMode = SpotNodeMode(C.ZLINK_SPOT_NODE_MODE_ALL)
)

type SocketType int

const (
	SocketTypeAny    SocketType = SocketType(C.ZLINK_SOCKET_ANY)
	SocketTypePair   SocketType = SocketType(C.ZLINK_SOCKET_PAIR)
	SocketTypePub    SocketType = SocketType(C.ZLINK_SOCKET_PUB)
	SocketTypeSub    SocketType = SocketType(C.ZLINK_SOCKET_SUB)
	SocketTypeDealer SocketType = SocketType(C.ZLINK_SOCKET_DEALER)
	SocketTypeRouter SocketType = SocketType(C.ZLINK_SOCKET_ROUTER)
	SocketTypeXPub   SocketType = SocketType(C.ZLINK_SOCKET_XPUB)
	SocketTypeXSub   SocketType = SocketType(C.ZLINK_SOCKET_XSUB)
	SocketTypeStream SocketType = SocketType(C.ZLINK_SOCKET_STREAM)
)

type SpotNodeSocketOwner int

const (
	SpotNodeSocketOwnerAny  SpotNodeSocketOwner = SpotNodeSocketOwner(C.ZLINK_SPOT_NODE_SOCKET_OWNER_ANY)
	SpotNodeSocketOwnerNode SpotNodeSocketOwner = SpotNodeSocketOwner(C.ZLINK_SPOT_NODE_SOCKET_OWNER_NODE)
	SpotNodeSocketOwnerSpot SpotNodeSocketOwner = SpotNodeSocketOwner(C.ZLINK_SPOT_NODE_SOCKET_OWNER_SPOT)
)

type SpotNodeOption int

const (
	SpotNodeOptionRouterHWMProfile   SpotNodeOption = SpotNodeOption(C.ZLINK_SPOT_NODE_OPT_ROUTER_HWM_PROFILE)
	SpotNodeOptionRouterHWM          SpotNodeOption = SpotNodeOption(C.ZLINK_SPOT_NODE_OPT_ROUTER_HWM)
	SpotNodeOptionPubSubHWMProfile   SpotNodeOption = SpotNodeOption(C.ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE)
	SpotNodeOptionPubSubHWM          SpotNodeOption = SpotNodeOption(C.ZLINK_SPOT_NODE_OPT_PUBSUB_HWM)
	SpotNodeOptionDispatchWorkersMin SpotNodeOption = SpotNodeOption(C.ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MIN)
	SpotNodeOptionDispatchWorkersMax SpotNodeOption = SpotNodeOption(C.ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MAX)
)

type SpotNodeOptions struct {
	Mode SpotNodeMode
}

type SpotNodeSocketSnapshotFilter struct {
	Owner      *SpotNodeSocketOwner
	SocketType *SocketType
	SocketName *string
}

type SpotNodeSocketSnapshotEntry struct {
	Owner          SpotNodeSocketOwner
	OwnerID        uint64
	OwnerName      string
	SocketName     string
	SocketType     SocketType
	AutoHwmVisible bool
	Snapshot       MonitorSnapshot
}

func newSpotNode(ctx *Context) (*SpotNode, error) {
	return newSpotNodeWithOptions(ctx, nil)
}

func newSpotNodeWithOptions(ctx *Context, options *SpotNodeOptions) (*SpotNode, error) {
	if ctx == nil || ctx.closed {
		return nil, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	var nativeOptions C.zlink_spot_node_options_t
	var optionsPtr *C.zlink_spot_node_options_t
	if options != nil {
		nativeOptions.mode = C.zlink_spot_node_mode_t(options.Mode)
		optionsPtr = &nativeOptions
	}
	handle := C.zlink_spot_node_new(ctx.raw(), optionsPtr)
	if handle == nil {
		return nil, configErrorFromErrno(currentErrno())
	}
	return &SpotNode{
		handle: handle,
		spots:  make(map[*spotCore]struct{}),
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
	if err := configErrorFromResult(C.zlink_spot_node_attach_channel_dealer(handle, discovery.raw(), dealerHandle)); err != nil {
		return err
	}
	n.dealerRegistry.Store(dealerHandle, dealer)
	return nil
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
	if err := n.withCString(channelName, func(cstr *C.char) error {
		return configErrorFromResult(C.zlink_spot_node_attach_channel_dealer_manual(handle, cstr, dealerHandle))
	}); err != nil {
		return err
	}
	n.dealerRegistry.Store(dealerHandle, dealer)
	return nil
}

func (n *SpotNode) lookupDealer(handle unsafe.Pointer) *DealerSocket {
	if v, ok := n.dealerRegistry.Load(handle); ok {
		return v.(*DealerSocket)
	}
	return nil
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

func (n *SpotNode) SetOption(option SpotNodeOption, value int) error {
	handle, err := n.handleOrError()
	if err != nil {
		return err
	}
	raw := C.int(value)
	return configErrorFromResult(C.zlink_set_spot_node_option(
		handle,
		C.zlink_spot_node_option_t(option),
		unsafe.Pointer(&raw),
		C.size_t(C.sizeof_int),
	))
}

func (n *SpotNode) Option(option SpotNodeOption) (int, error) {
	handle, err := n.handleOrError()
	if err != nil {
		return 0, err
	}
	var raw C.int
	size := C.size_t(C.sizeof_int)
	if err := configErrorFromResult(C.zlink_get_spot_node_option(
		handle,
		C.zlink_spot_node_option_t(option),
		unsafe.Pointer(&raw),
		&size,
	)); err != nil {
		return 0, err
	}
	return int(raw), nil
}

func (n *SpotNode) SetRouterHWM(value int) error {
	return n.SetOption(SpotNodeOptionRouterHWM, value)
}

func (n *SpotNode) RouterHWM() (int, error) {
	return n.Option(SpotNodeOptionRouterHWM)
}

func (n *SpotNode) SetPubSubHWM(value int) error {
	return n.SetOption(SpotNodeOptionPubSubHWM, value)
}

func (n *SpotNode) PubSubHWM() (int, error) {
	return n.Option(SpotNodeOptionPubSubHWM)
}

func (n *SpotNode) SetRouterHWMProfile(value AutoHwmProfile) error {
	return n.SetOption(SpotNodeOptionRouterHWMProfile, int(value))
}

func (n *SpotNode) RouterHWMProfile() (AutoHwmProfile, error) {
	value, err := n.Option(SpotNodeOptionRouterHWMProfile)
	return AutoHwmProfile(value), err
}

func (n *SpotNode) SetPubSubHWMProfile(value AutoHwmProfile) error {
	return n.SetOption(SpotNodeOptionPubSubHWMProfile, int(value))
}

func (n *SpotNode) PubSubHWMProfile() (AutoHwmProfile, error) {
	value, err := n.Option(SpotNodeOptionPubSubHWMProfile)
	return AutoHwmProfile(value), err
}

func (n *SpotNode) SetDispatchWorkersMin(value int) error {
	return n.SetOption(SpotNodeOptionDispatchWorkersMin, value)
}

func (n *SpotNode) DispatchWorkersMin() (int, error) {
	return n.Option(SpotNodeOptionDispatchWorkersMin)
}

func (n *SpotNode) SetDispatchWorkersMax(value int) error {
	return n.SetOption(SpotNodeOptionDispatchWorkersMax, value)
}

func (n *SpotNode) DispatchWorkersMax() (int, error) {
	return n.Option(SpotNodeOptionDispatchWorkersMax)
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

func (n *SpotNode) DisconnectPeerRID(targetNodeRID RoutingID) error {
	handle, err := n.handleOrError()
	if err != nil {
		return err
	}
	rid := targetNodeRID.toC()
	return connectErrorFromResult(
		C.zlink_spot_node_disconnect_peer_rid(
			handle,
			(*C.zlink_routing_id_t)(unsafe.Pointer(&rid)),
		))
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

func (n *SpotNode) EntrySpot() (*Spot, error) {
	if n == nil {
		return nil, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	n.mu.Lock()
	defer n.mu.Unlock()
	if n.closed || n.closing || n.handle == nil {
		return nil, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	var handle unsafe.Pointer
	if err := configErrorFromResult(C.zlink_spot_node_entry_spot(n.handle, &handle)); err != nil {
		return nil, err
	}
	core := &spotCore{handle: handle, owner: n}
	n.spots[core] = struct{}{}
	return &Spot{core: core}, nil
}

func (n *SpotNode) SpotLookup(spotRID RoutingID) (*Spot, error) {
	if n == nil {
		return nil, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	rid := spotRID.toC()
	n.mu.Lock()
	defer n.mu.Unlock()
	if n.closed || n.closing || n.handle == nil {
		return nil, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	var handle unsafe.Pointer
	if err := configErrorFromResult(C.zlink_spot_node_spot_lookup(
		n.handle,
		(*C.zlink_routing_id_t)(unsafe.Pointer(&rid)),
		&handle,
	)); err != nil {
		return nil, err
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
	lifecycleHandle cgo.Handle
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

// Builder interfaces for spot send/request/reply operations.

type SendOp interface {
	Message(message *Message) SendSubmitOp
}

type SendSubmitOp interface {
	Message(message *Message) SendSubmitOp
	Flags(flags SendFlags) SendSubmitOp
	Submit(ctx context.Context) (bool, error)
}

type RequestOp interface {
	Message(message *Message) RequestSubmitOp
}

type RequestSubmitOp interface {
	Message(message *Message) RequestSubmitOp
	Timeout(timeout time.Duration) RequestSubmitOp
	Flags(flags SendFlags) RequestCallbackSubmitOp
	SubmitAsync(ctx context.Context) (<-chan RequestReplyCompletion, error)
	Submit(ctx context.Context, callback RequestReplyCallback) (bool, error)
}

type RequestCallbackSubmitOp interface {
	Message(message *Message) RequestCallbackSubmitOp
	Timeout(timeout time.Duration) RequestCallbackSubmitOp
	Flags(flags SendFlags) RequestCallbackSubmitOp
	Submit(ctx context.Context, callback RequestReplyCallback) (bool, error)
}

type ReplyOp interface {
	Message(message *Message) ReplySubmitOp
}

type ReplySubmitOp interface {
	Message(message *Message) ReplySubmitOp
	Flags(flags SendFlags) ReplySubmitOp
	Submit(ctx context.Context) error
}

// --- sendBuilder ---

type sendBuilder struct {
	spot      *Spot
	parts     []*Message
	flags     SendFlags
	submitted bool
	submit    func(parts []*Message, flags SendFlags) error
}

func newSendBuilder(spot *Spot, submit func(parts []*Message, flags SendFlags) error) SendOp {
	return &sendBuilder{spot: spot, submit: submit}
}

func (b *sendBuilder) Message(message *Message) SendSubmitOp {
	b.parts = append(b.parts, message)
	return b
}

func (b *sendBuilder) Flags(flags SendFlags) SendSubmitOp {
	b.flags = flags
	return b
}

func (b *sendBuilder) Submit(_ context.Context) (bool, error) {
	if b.submitted {
		return false, &ConfigError{Result: ConfigInvalidState, internalErrno: int(C.EINVAL)}
	}
	if len(b.parts) == 0 {
		return false, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	b.submitted = true
	if err := b.submit(b.parts, b.flags); err != nil {
		var submitErr *SubmitError
		if errors.As(err, &submitErr) && submitErr.Result == SubmitBackpressured {
			return false, nil
		}
		return false, err
	}
	return true, nil
}

// --- requestBuilder ---

type requestBuilderState struct {
	parts     []*Message
	flags     SendFlags
	timeout   time.Duration
	submitted bool
	submit    func(parts []*Message, flags SendFlags, timeout time.Duration, callback RequestReplyCallback) error
}

type requestBuilder struct {
	state *requestBuilderState
}

type requestCallbackBuilder struct {
	state *requestBuilderState
}

func newRequestBuilder(spot *Spot, submit func(parts []*Message, flags SendFlags, timeout time.Duration, callback RequestReplyCallback) error) RequestOp {
	return &requestBuilder{state: &requestBuilderState{submit: submit}}
}

func (b *requestBuilder) Message(message *Message) RequestSubmitOp {
	b.state.parts = append(b.state.parts, message)
	return b
}

func (b *requestBuilder) Timeout(timeout time.Duration) RequestSubmitOp {
	b.state.timeout = timeout
	return b
}

func (b *requestBuilder) Flags(flags SendFlags) RequestCallbackSubmitOp {
	b.state.flags = flags
	return &requestCallbackBuilder{state: b.state}
}

func (b *requestBuilder) SubmitAsync(_ context.Context) (<-chan RequestReplyCompletion, error) {
	return b.state.doSubmitAsync()
}

func (b *requestBuilder) Submit(_ context.Context, callback RequestReplyCallback) (bool, error) {
	return b.state.doSubmitCallback(callback)
}

func (b *requestCallbackBuilder) Message(message *Message) RequestCallbackSubmitOp {
	b.state.parts = append(b.state.parts, message)
	return b
}

func (b *requestCallbackBuilder) Timeout(timeout time.Duration) RequestCallbackSubmitOp {
	b.state.timeout = timeout
	return b
}

func (b *requestCallbackBuilder) Flags(flags SendFlags) RequestCallbackSubmitOp {
	b.state.flags = flags
	return b
}

func (b *requestCallbackBuilder) Submit(_ context.Context, callback RequestReplyCallback) (bool, error) {
	return b.state.doSubmitCallback(callback)
}

func (s *requestBuilderState) doSubmitAsync() (<-chan RequestReplyCompletion, error) {
	result := make(chan RequestReplyCompletion, 1)
	ok, err := s.doSubmitCallback(func(r RequestResult, parts []*Message) {
		completion := RequestReplyCompletion{Result: r, Parts: parts}
		if r != RequestOK {
			completion.Err = &RequestError{Result: r}
		}
		result <- completion
		close(result)
	})
	if err != nil {
		return nil, err
	}
	if !ok {
		return nil, &SubmitError{Result: SubmitBackpressured}
	}
	return result, nil
}

func (s *requestBuilderState) doSubmitCallback(callback RequestReplyCallback) (bool, error) {
	if s.submitted {
		return false, &ConfigError{Result: ConfigInvalidState, internalErrno: int(C.EINVAL)}
	}
	if len(s.parts) == 0 || callback == nil {
		return false, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	s.submitted = true
	if err := s.submit(s.parts, s.flags, s.timeout, callback); err != nil {
		var submitErr *SubmitError
		if errors.As(err, &submitErr) && submitErr.Result == SubmitBackpressured {
			return false, nil
		}
		return false, err
	}
	return true, nil
}

// --- replyBuilder ---

type replyBuilder struct {
	spot      *Spot
	parts     []*Message
	flags     SendFlags
	submitted bool
	submit    func(parts []*Message, flags SendFlags) error
}

func newReplyBuilder(spot *Spot, submit func(parts []*Message, flags SendFlags) error) ReplyOp {
	return &replyBuilder{spot: spot, submit: submit}
}

func (b *replyBuilder) Message(message *Message) ReplySubmitOp {
	b.parts = append(b.parts, message)
	return b
}

func (b *replyBuilder) Flags(flags SendFlags) ReplySubmitOp {
	b.flags = flags
	return b
}

func (b *replyBuilder) Submit(_ context.Context) error {
	if b.submitted {
		return &ConfigError{Result: ConfigInvalidState, internalErrno: int(C.EINVAL)}
	}
	if len(b.parts) == 0 {
		return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	b.submitted = true
	return b.submit(b.parts, b.flags)
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

// isInvalid centralizes the s==nil / core==nil / closed guard. Callers that
// need a typed error use checkValid; recv/handler paths just inspect the
// bool so they can construct their own context-specific error type.
func (s *Spot) isInvalid() bool {
	return s == nil || s.core == nil || s.core.closed
}

// checkValid returns a ConfigError if the spot is unusable, otherwise nil.
func (s *Spot) checkValid() error {
	if s.isInvalid() {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	return nil
}

func (s *Spot) Close() error {
	if s == nil || s.core == nil {
		return nil
	}
	return s.core.Close()
}

func (s *Spot) SetRoutingID(rid RoutingID) error {
	if err := s.checkValid(); err != nil {
		return err
	}
	raw := rid.toC()
	return configErrorFromResult(C.zlink_set_routing_id(s.raw(), routingIDPointer(&raw), C.size_t(raw.size)))
}

func (s *Spot) RoutingID() (RoutingID, error) {
	if err := s.checkValid(); err != nil {
		return RoutingID{}, err
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

func (s *Spot) SetRequestTimeout(value time.Duration) error {
	if err := s.checkValid(); err != nil {
		return err
	}
	ms, err := durationToMillis(value)
	if err != nil {
		return err
	}
	raw := C.int(ms)
	return configErrorFromResult(C.zlink_set_spot_option(s.raw(), C.zlink_spot_option_t(C.ZLINK_SPOT_OPT_REQUEST_TIMEOUT_MS), unsafe.Pointer(&raw), C.size_t(C.sizeof_int)))
}

func (s *Spot) RequestTimeout() (time.Duration, error) {
	if err := s.checkValid(); err != nil {
		return 0, err
	}
	var raw C.int
	size := C.size_t(C.sizeof_int)
	if err := configErrorFromResult(C.zlink_get_spot_option(s.raw(), C.zlink_spot_option_t(C.ZLINK_SPOT_OPT_REQUEST_TIMEOUT_MS), unsafe.Pointer(&raw), &size)); err != nil {
		return 0, err
	}
	return time.Duration(raw) * time.Millisecond, nil
}

func (s *Spot) SetNoDrop(value bool) error {
	if s == nil || s.core == nil {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	return setNativePubBoolOption(s.raw(), s.core.closed, "spot is closed", C.ZLINK_PUB_OPT_NODROP, value)
}

func (s *Spot) Publish(topic string) SendOp {
	return newSendBuilder(s, func(parts []*Message, flags SendFlags) error {
		return s.core.withCString(topic, func(topicC *C.char) error {
			return submitMultipartFromClones(parts, true, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
				return submitErrorFromResult(C.zlink_spot_publish_part(s.raw(), topicC, part, C.zlink_send_flags_t(flags), partFlag))
			})
		})
	})
}

func (s *Spot) SendChannel(channelName string) SendOp {
	return newSendBuilder(s, func(parts []*Message, flags SendFlags) error {
		return s.core.withCString(channelName, func(cstr *C.char) error {
			return submitMultipartFromClones(parts, true, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
				return submitErrorFromResult(C.zlink_spot_send_channel_part(s.raw(), cstr, part, C.zlink_send_flags_t(flags), partFlag))
			})
		})
	})
}

func (s *Spot) SendToSpot(destNodeRid, destSpotRid RoutingID) SendOp {
	return newSendBuilder(s, func(parts []*Message, flags SendFlags) error {
		node := destNodeRid.toC()
		spot := destSpotRid.toC()
		return submitMultipartFromClones(parts, true, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
			return submitErrorFromResult(C.zlink_spot_send_spot_part(s.raw(), &node, &spot, part, C.zlink_send_flags_t(flags), partFlag))
		})
	})
}

func (s *Spot) RequestChannel(channelName string) RequestOp {
	return newRequestBuilder(s, func(parts []*Message, flags SendFlags, timeout time.Duration, callback RequestReplyCallback) error {
		if callback == nil {
			return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
		}
		state, err := s.startChannelRequest(channelName, flags, timeout, parts...)
		if err != nil {
			return err
		}
		go func() {
			result := state.wait()
			callback(result.result, result.parts)
		}()
		return nil
	})
}

func (s *Spot) RequestToSpot(destNodeRid, destSpotRid RoutingID) RequestOp {
	return newRequestBuilder(s, func(parts []*Message, flags SendFlags, timeout time.Duration, callback RequestReplyCallback) error {
		if callback == nil {
			return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
		}
		if timeout <= 0 {
			timeout = defaultRequestTimeout
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
		state := newReplyCallbackState()
		handle := cgo.NewHandle(state)
		node := destNodeRid.toC()
		spot := destSpotRid.toC()
		if err := submitPreparedMultipart(prepared, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
			return submitErrorFromResult(C.zlink_spot_request_spot_part_go_local(
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
			return err
		}
		prepared.commit()
		startSpotRequestProgress(s.raw(), state)
		go func() {
			result := state.wait()
			callback(result.result, result.parts)
		}()
		return nil
	})
}

func (s *Spot) RequestToRouter(peerRid RoutingID) RequestOp {
	return newRequestBuilder(s, func(parts []*Message, flags SendFlags, timeout time.Duration, callback RequestReplyCallback) error {
		if callback == nil {
			return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
		}
		if timeout <= 0 {
			timeout = defaultRequestTimeout
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
		state := newReplyCallbackState()
		handle := cgo.NewHandle(state)
		peer := peerRid.toC()
		if err := submitPreparedMultipart(prepared, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
			return submitErrorFromResult(C.zlink_spot_request_router_part_go_local(
				s.raw(),
				&peer,
				part,
				C.zlink_send_flags_t(flags),
				partFlag,
				C.uint32_t(requestTimeoutMillis(timeout)),
				C.uintptr_t(handle),
			))
		}); err != nil {
			handle.Delete()
			prepared.commit()
			return err
		}
		prepared.commit()
		startSpotRequestProgress(s.raw(), state)
		go func() {
			result := state.wait()
			callback(result.result, result.parts)
		}()
		return nil
	})
}

func (s *Spot) ReplyToSpot(destNodeRid, destSpotRid RoutingID, requestSeq uint64) ReplyOp {
	return newReplyBuilder(s, func(parts []*Message, flags SendFlags) error {
		if err := validateReplyFlags(flags); err != nil {
			return err
		}
		node := destNodeRid.toC()
		spot := destSpotRid.toC()
		return submitMultipartFromClones(parts, false, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
			return submitErrorFromResult(C.zlink_spot_reply_spot_part(s.raw(), &node, &spot, C.uint64_t(requestSeq), part, partFlag))
		})
	})
}

func (s *Spot) ReplyToRouter(peerRid RoutingID, requestSeq uint64) ReplyOp {
	return newReplyBuilder(s, func(parts []*Message, flags SendFlags) error {
		if err := validateReplyFlags(flags); err != nil {
			return err
		}
		peer := peerRid.toC()
		return submitMultipartFromClones(parts, false, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
			return submitErrorFromResult(C.zlink_spot_reply_router_part(s.raw(), &peer, C.uint64_t(requestSeq), part, partFlag))
		})
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

func (s *Spot) Subscribe(out *TopicMessage, flags RecvFlags) (bool, error) {
	if out == nil {
		return false, &RecvError{Result: RecvInvalidHandle, internalErrno: int(C.EINVAL)}
	}
	fresh, err := recvSpotTopicMessage(func(rid **C.zlink_routing_id_t, topic *C.char, topicLen *C.size_t, part *C.zlink_msg_t, hasMore *C.zlink_part_flag_t, recvFlags C.zlink_recv_flags_t) error {
		return recvErrorFromResult(C.zlink_spot_subscribe_part(s.raw(), rid, topic, C.size_t(recvTopicBufferCap), topicLen, part, hasMore, recvFlags))
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

func (s *Spot) ReceiveSubscriptionEvent(out *SubscriptionEvent, flags RecvFlags) (bool, error) {
	if out == nil {
		return false, &RecvError{Result: RecvInvalidHandle, internalErrno: int(C.EINVAL)}
	}
	if s.isInvalid() {
		return false, &RecvError{Result: RecvInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	_ = flags
	return false, &RecvError{Result: RecvNotSupported, internalErrno: int(C.ENOTSUP)}
}

func (s *Spot) OnSendReady(handler func()) error {
	if handler == nil {
		return &HandlerError{Result: HandlerInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	if s.isInvalid() {
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

func (s *Spot) RecvRouted(out *Received, flags RecvFlags) (bool, error) {
	if out == nil {
		return false, &RecvError{Result: RecvInvalidHandle, internalErrno: int(C.EINVAL)}
	}
	var sourceRID *C.zlink_routing_id_t
	var spotRID *C.zlink_routing_id_t
	var requestSeq C.uint64_t
	clonedParts, err := recvMultipart(flags, func(part *C.zlink_msg_t, hasMore *C.zlink_part_flag_t, recvFlags C.zlink_recv_flags_t) error {
		return recvErrorFromResult(C.zlink_spot_recv_part(s.raw(), &sourceRID, &spotRID, &requestSeq, part, hasMore, recvFlags))
	})
	if err != nil {
		var recvErr *RecvError
		if errors.As(err, &recvErr) && recvErr.Result == RecvNoData {
			return false, nil
		}
		return false, err
	}
	out.AdoptFrom(&Received{
		routingID:     routingIDFromCPtr(sourceRID),
		spotRID:       routingIDFromCPtr(spotRID),
		parts:         clonedParts,
		requestSeq:    uint64(requestSeq),
		hasRequestSeq: requestSeq != 0,
		reply:         receivedReplyToSpot(s, routingIDFromCPtr(sourceRID), routingIDFromCPtr(spotRID), uint64(requestSeq)),
		send:          receivedSendFromSpot(s, routingIDFromCPtr(sourceRID), routingIDFromCPtr(spotRID)),
	})
	return true, nil
}

func (s *Spot) OnRoutedReceive(handler func(*Received)) error {
	if handler == nil {
		return &HandlerError{Result: HandlerInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	state := newSpotRoutedCallbackState(s, handler)
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

func (s *Spot) startChannelRequest(channelName string, flags SendFlags, timeout time.Duration, parts ...*Message) (*replyCallbackState, error) {
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
	state := newReplyCallbackState()
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
	return state, nil
}
