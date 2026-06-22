// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include <stdlib.h>
#include "zlink.h"
*/
import "C"

import (
	"sync"
	"unsafe"
)

type SpotNode struct {
	handle         unsafe.Pointer
	ctx            *Context
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
	SpotNodeOptionRouterHwmProfile    SpotNodeOption = SpotNodeOption(C.ZLINK_SPOT_NODE_OPT_ROUTER_HWM_PROFILE)
	SpotNodeOptionRouterHighWaterMark SpotNodeOption = SpotNodeOption(C.ZLINK_SPOT_NODE_OPT_ROUTER_HWM)
	SpotNodeOptionPubSubHwmProfile    SpotNodeOption = SpotNodeOption(C.ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE)
	SpotNodeOptionPubSubHighWaterMark SpotNodeOption = SpotNodeOption(C.ZLINK_SPOT_NODE_OPT_PUBSUB_HWM)
	SpotNodeOptionDispatchWorkersMin  SpotNodeOption = SpotNodeOption(C.ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MIN)
	SpotNodeOptionDispatchWorkersMax  SpotNodeOption = SpotNodeOption(C.ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MAX)
)

type SpotNodeOptions struct {
	Mode SpotNodeMode
}

type SpotNodeSocketFilter struct {
	Owner      *SpotNodeSocketOwner
	SocketType *SocketType
	SocketName *string
}

type SpotNodeSocketEntry struct {
	Owner          SpotNodeSocketOwner
	OwnerID        uint64
	OwnerName      string
	SocketName     string
	SocketType     SocketType
	AutoHwmVisible bool
	MonitorStatus  MonitorStatus
}

func newSpotNode(ctx *Context) (*SpotNode, error) {
	return newSpotNodeWithOptions(ctx, nil)
}

func newSpotNodeWithOptions(ctx *Context, options *SpotNodeOptions) (*SpotNode, error) {
	if ctx == nil || ctx.closed {
		return nil, &ConfigError{Result: ConfigInvalidHandle, nativeErrno: int(C.EFAULT)}
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
		ctx:    ctx,
		spots:  make(map[*spotCore]struct{}),
	}, nil
}

func (n *SpotNode) raw() unsafe.Pointer {
	if n == nil {
		return nil
	}
	return n.handle
}

func (n *SpotNode) SetPubBind(endpoint string) error {
	return n.withCString(endpoint, func(cstr *C.char) error {
		handle, err := n.handleOrError()
		if err != nil {
			return err
		}
		return configErrorFromResult(C.zlink_spot_node_set_pub_bind(handle, cstr))
	})
}

func (n *SpotNode) SetRouterBind(endpoint string) error {
	return n.withCString(endpoint, func(cstr *C.char) error {
		handle, err := n.handleOrError()
		if err != nil {
			return err
		}
		return configErrorFromResult(C.zlink_spot_node_set_router_bind(handle, cstr))
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
		return &ConfigError{Result: ConfigInvalidHandle, nativeErrno: int(C.EFAULT)}
	}
	handle, err := n.handleOrError()
	if err != nil {
		return err
	}
	return configErrorFromResult(C.zlink_spot_node_attach_discovery(handle, discovery.raw()))
}

func (n *SpotNode) lookupDealer(handle unsafe.Pointer) *DealerSocket {
	if v, ok := n.dealerRegistry.Load(handle); ok {
		return v.(*DealerSocket)
	}
	return nil
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
		return nil, &ConfigError{Result: ConfigInvalidHandle, nativeErrno: int(C.EFAULT)}
	}
	n.mu.Lock()
	defer n.mu.Unlock()
	if n.closed || n.closing || n.handle == nil {
		return nil, &ConfigError{Result: ConfigInvalidHandle, nativeErrno: int(C.EFAULT)}
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
		return nil, &ConfigError{Result: ConfigInvalidHandle, nativeErrno: int(C.EFAULT)}
	}
	n.mu.Lock()
	defer n.mu.Unlock()
	if n.closed || n.closing || n.handle == nil {
		return nil, &ConfigError{Result: ConfigInvalidHandle, nativeErrno: int(C.EFAULT)}
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
		return nil, &ConfigError{Result: ConfigInvalidHandle, nativeErrno: int(C.EFAULT)}
	}
	rid := spotRID.toC()
	n.mu.Lock()
	defer n.mu.Unlock()
	if n.closed || n.closing || n.handle == nil {
		return nil, &ConfigError{Result: ConfigInvalidHandle, nativeErrno: int(C.EFAULT)}
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

func (n *SpotNode) GetOrCreateSpot(spotRID RoutingID) (*Spot, bool, error) {
	if n == nil {
		return nil, false, &ConfigError{Result: ConfigInvalidHandle, nativeErrno: int(C.EFAULT)}
	}
	rid := spotRID.toC()
	n.mu.Lock()
	defer n.mu.Unlock()
	if n.closed || n.closing || n.handle == nil {
		return nil, false, &ConfigError{Result: ConfigInvalidHandle, nativeErrno: int(C.EFAULT)}
	}
	var handle unsafe.Pointer
	var created C.uint32_t
	if err := configErrorFromResult(C.zlink_spot_node_spot_get_or_new(
		n.handle,
		(*C.zlink_routing_id_t)(unsafe.Pointer(&rid)),
		&handle,
		&created,
	)); err != nil {
		return nil, false, err
	}
	core := &spotCore{handle: handle, owner: n}
	n.spots[core] = struct{}{}
	return &Spot{core: core}, created != 0, nil
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

func (n *SpotNode) withChannelCString(value string, fn func(*C.char) error) error {
	if err := validateChannelName(value); err != nil {
		return err
	}
	cstr := C.CString(value)
	defer C.free(unsafe.Pointer(cstr))
	return fn(cstr)
}

func (n *SpotNode) withChannelEndpointCStrings(channelName string, endpoint string, fn func(*C.char, *C.char) error) error {
	if err := validateChannelName(channelName); err != nil {
		return err
	}
	if err := validateEndpointString(endpoint); err != nil {
		return err
	}
	channel := C.CString(channelName)
	defer C.free(unsafe.Pointer(channel))
	ep := C.CString(endpoint)
	defer C.free(unsafe.Pointer(ep))
	return fn(channel, ep)
}

func (n *SpotNode) handleOrError() (unsafe.Pointer, error) {
	if n == nil {
		return nil, &ConfigError{Result: ConfigInvalidHandle, nativeErrno: int(C.EFAULT)}
	}
	n.mu.Lock()
	defer n.mu.Unlock()
	if n.closed || n.closing || n.handle == nil {
		return nil, &ConfigError{Result: ConfigInvalidHandle, nativeErrno: int(C.EFAULT)}
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
