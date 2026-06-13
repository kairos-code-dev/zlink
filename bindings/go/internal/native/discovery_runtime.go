// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include <stdlib.h>
#include "zlink.h"

static inline int zlink_discovery_resolve_spot_go(void *discovery, const zlink_routing_id_t *spot_rid, zlink_spot_route_t *route_out) {
    return zlink_discovery_resolve_spot(discovery, spot_rid, route_out);
}

static inline int zlink_discovery_bind_route_go(void *discovery, zlink_route_kind_t kind, const void *key, size_t key_size, const void *value, size_t value_size) {
    return zlink_discovery_bind_route(discovery, kind, key, key_size, value, value_size);
}

static inline int zlink_discovery_unbind_route_go(void *discovery, zlink_route_kind_t kind, const void *key, size_t key_size) {
    return zlink_discovery_unbind_route(discovery, kind, key, key_size);
}

static inline int zlink_discovery_resolve_route_go(void *discovery, zlink_route_kind_t kind, const void *key, size_t key_size, zlink_routing_id_t *owner_rid_out, zlink_msg_t *value_out) {
    return zlink_discovery_resolve_route(discovery, kind, key, key_size, owner_rid_out, value_out);
}
*/
import "C"

import "unsafe"

func withRouteBytes(data []byte, fn func(unsafe.Pointer, C.size_t) error) error {
	if len(data) == 0 {
		return fn(nil, 0)
	}
	ptr := C.CBytes(data)
	defer C.free(ptr)
	return fn(ptr, C.size_t(len(data)))
}

func newDiscovery(ctx *Context, autoConnectType AutoConnectType, channelName string) (*Discovery, error) {
	if ctx == nil || ctx.closed {
		return nil, stateError("context is closed")
	}
	if err := validateChannelName(channelName); err != nil {
		return nil, err
	}
	channelNameC := C.CString(channelName)
	defer C.free(unsafe.Pointer(channelNameC))
	handle := C.zlink_discovery_new(ctx.raw(), C.zlink_auto_connect_type_t(autoConnectType), channelNameC)
	if handle == nil {
		return nil, lastError()
	}
	return &Discovery{handle: handle}, nil
}

func (d *Discovery) ConnectRegistry(endpoint string) error {
	return withDiscoveryCString(d, endpoint, func(cstr *C.char) error {
		return checkRC(C.zlink_discovery_connect_registry(d.raw(), cstr))
	})
}

func (d *Discovery) SetValue(value int64) error {
	if d == nil || d.closed {
		return stateError("discovery is closed")
	}
	return checkRC(C.zlink_discovery_set_value(d.raw(), C.int64_t(value)))
}

func (d *Discovery) SetSpotOwnerSyncEnabled(enabled bool) error {
	if d == nil || d.closed {
		return stateError("discovery is closed")
	}
	return setNativeBoolOption(d.raw(), d.closed, "discovery is closed", C.ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC, enabled)
}

func (d *Discovery) SpotOwnerSyncEnabled() (bool, error) {
	if d == nil || d.closed {
		return false, stateError("discovery is closed")
	}
	var value C.int
	size := C.size_t(C.sizeof_int)
	if err := configErrorFromResult(ConfigResult(C.zlink_get_option(
		d.raw(),
		C.ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC,
		unsafe.Pointer(&value),
		&size,
	))); err != nil {
		return false, err
	}
	return value != 0, nil
}

func (d *Discovery) SetActorRouteSyncEnabled(enabled bool) error {
	if d == nil || d.closed {
		return stateError("discovery is closed")
	}
	return setNativeBoolOption(d.raw(), d.closed, "discovery is closed", C.ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC, enabled)
}

func (d *Discovery) ActorRouteSyncEnabled() (bool, error) {
	if d == nil || d.closed {
		return false, stateError("discovery is closed")
	}
	var value C.int
	size := C.size_t(C.sizeof_int)
	if err := configErrorFromResult(ConfigResult(C.zlink_get_option(
		d.raw(),
		C.ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC,
		unsafe.Pointer(&value),
		&size,
	))); err != nil {
		return false, err
	}
	return value != 0, nil
}

func (d *Discovery) ResolveSpot(spotRid RoutingID) (SpotRoute, error) {
	if d == nil || d.closed {
		return SpotRoute{}, stateError("discovery is closed")
	}
	nativeSpotRID := spotRid.toC()
	var raw C.zlink_spot_route_t
	if err := checkRC(C.zlink_discovery_resolve_spot_go(d.raw(), &nativeSpotRID, &raw)); err != nil {
		return SpotRoute{}, err
	}
	return spotRouteFromC(raw), nil
}

func (d *Discovery) BindRoute(kind RouteKind, key []byte, value []byte) error {
	if d == nil || d.closed {
		return stateError("discovery is closed")
	}
	return withRouteBytes(key, func(keyPtr unsafe.Pointer, keySize C.size_t) error {
		return withRouteBytes(value, func(valuePtr unsafe.Pointer, valueSize C.size_t) error {
			return configErrorFromResult(ConfigResult(C.zlink_discovery_bind_route_go(
				d.raw(),
				C.zlink_route_kind_t(kind),
				keyPtr,
				keySize,
				valuePtr,
				valueSize,
			)))
		})
	})
}

func (d *Discovery) UnbindRoute(kind RouteKind, key []byte) error {
	if d == nil || d.closed {
		return stateError("discovery is closed")
	}
	return withRouteBytes(key, func(keyPtr unsafe.Pointer, keySize C.size_t) error {
		return configErrorFromResult(ConfigResult(C.zlink_discovery_unbind_route_go(
			d.raw(),
			C.zlink_route_kind_t(kind),
			keyPtr,
			keySize,
		)))
	})
}

func (d *Discovery) ResolveRoute(kind RouteKind, key []byte) (DiscoveryRoute, error) {
	if d == nil || d.closed {
		return DiscoveryRoute{}, stateError("discovery is closed")
	}
	var owner C.zlink_routing_id_t
	value := &Message{}
	err := withRouteBytes(key, func(keyPtr unsafe.Pointer, keySize C.size_t) error {
		return configErrorFromResult(ConfigResult(C.zlink_discovery_resolve_route_go(
			d.raw(),
			C.zlink_route_kind_t(kind),
			keyPtr,
			keySize,
			&owner,
			&value.msg,
		)))
	})
	if err != nil {
		return DiscoveryRoute{}, err
	}
	return DiscoveryRoute{OwnerRoutingID: routingIDFromC(owner), Value: value}, nil
}

func (d *Discovery) GetValue() (int64, error) {
	if d == nil || d.closed {
		return 0, stateError("discovery is closed")
	}
	var value C.int64_t
	if err := checkRC(C.zlink_discovery_get_value(d.raw(), &value)); err != nil {
		return 0, err
	}
	return int64(value), nil
}

func (d *Discovery) MemberPeers() ([]MemberPeerEntry, error) {
	if d == nil || d.closed {
		return nil, stateError("discovery is closed")
	}
	return queryMemberPeers(func(entries *C.zlink_member_peer_entry_t, count *C.size_t) error {
		return checkRC(C.zlink_discovery_member_peers(d.raw(), entries, count))
	})
}

func (d *Discovery) SetTLSClient(caCertPath string, hostname string, trustSystem bool) error {
	if d == nil || d.closed {
		return stateError("discovery is closed")
	}
	return setTLSClient(d.raw(), caCertPath, hostname, trustSystem)
}
