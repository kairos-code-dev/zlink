// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include <stdlib.h>
#include "zlink.h"
*/
import "C"

import "unsafe"

func (r *Registry) Bind(pubEndpoint string, routerEndpoint string) error {
	if r == nil || r.closed {
		return stateError("registry is closed")
	}
	return withCStringPair(pubEndpoint, routerEndpoint, func(pubC *C.char, routerC *C.char) error {
		return checkRC(C.zlink_registry_bind(r.raw(), pubC, routerC))
	})
}

func (r *Registry) SetTLSServer(certPath string, keyPath string, requireClientCert bool) error {
	if r == nil || r.closed {
		return stateError("registry is closed")
	}
	return setTLSServer(r.raw(), certPath, keyPath, requireClientCert)
}

func (r *Registry) SetTLSClient(caCertPath string, hostname string, trustSystem bool) error {
	if r == nil || r.closed {
		return stateError("registry is closed")
	}
	return setTLSClient(r.raw(), caCertPath, hostname, trustSystem)
}

func (r *Registry) SetId(registryID uint32) error {
	return r.SetOption(RegistryOptionID, registryID)
}

type RegistryOption uint32

const (
	RegistryOptionID                  RegistryOption = RegistryOption(C.ZLINK_REGISTRY_OPT_ID)
	RegistryOptionHeartbeatIntervalMS RegistryOption = RegistryOption(C.ZLINK_REGISTRY_OPT_HEARTBEAT_INTERVAL_MS)
	RegistryOptionHeartbeatTimeoutMS  RegistryOption = RegistryOption(C.ZLINK_REGISTRY_OPT_HEARTBEAT_TIMEOUT_MS)
	RegistryOptionBroadcastIntervalMS RegistryOption = RegistryOption(C.ZLINK_REGISTRY_OPT_BROADCAST_INTERVAL_MS)
)

func (r *Registry) SetOption(option RegistryOption, value uint32) error {
	if r == nil || r.closed {
		return stateError("registry is closed")
	}
	return checkRC(C.zlink_registry_set(r.raw(), C.zlink_registry_option_t(option), C.uint32_t(value)))
}

func (r *Registry) GetOption(option RegistryOption) (uint32, error) {
	if r == nil || r.closed {
		return 0, stateError("registry is closed")
	}
	var result C.zlink_config_result_t
	value := C.zlink_registry_get(r.raw(), C.zlink_registry_option_t(option), &result)
	return uint32(value), checkRC(C.int(result))
}

func (r *Registry) AddPeer(peerPubEndpoint string) error {
	return withRegistryCString(r, peerPubEndpoint, func(cstr *C.char) error {
		return checkRC(C.zlink_registry_add_peer(r.raw(), cstr))
	})
}

func (r *Registry) SetHeartbeat(intervalMS uint32, timeoutMS uint32) error {
	if err := r.SetOption(RegistryOptionHeartbeatIntervalMS, intervalMS); err != nil {
		return err
	}
	return r.SetOption(RegistryOptionHeartbeatTimeoutMS, timeoutMS)
}

func (r *Registry) SetBroadcastInterval(intervalMS uint32) error {
	return r.SetOption(RegistryOptionBroadcastIntervalMS, intervalMS)
}

func (r *Registry) Status() (*RegistryStatus, error) {
	if r == nil || r.closed {
		return nil, stateError("registry is closed")
	}
	var raw C.zlink_registry_status_t
	if err := checkRC(C.zlink_registry_status(r.raw(), &raw)); err != nil {
		return nil, err
	}
	return registryStatusFromC(raw), nil
}

func (r *Registry) ServiceSummary(filter *RegistryServiceSummaryFilter) ([]RegistryServiceSummaryEntry, error) {
	if r == nil || r.closed {
		return nil, stateError("registry is closed")
	}
	var rawFilter *C.zlink_registry_service_summary_filter_t
	var cfilter C.zlink_registry_service_summary_filter_t
	if filter != nil {
		if err := validateRegistryServiceSummaryFilter(*filter); err != nil {
			return nil, err
		}
		cfilter = filter.toC()
		rawFilter = &cfilter
	}
	return queryRegistryServiceSummary(func(entries *C.zlink_registry_service_summary_entry_t, count *C.size_t) error {
		return checkRC(C.zlink_registry_service_summary(r.raw(), rawFilter, entries, count))
	})
}

func (r *Registry) MemberPeers(channelName string) ([]MemberPeerEntry, error) {
	if r == nil || r.closed {
		return nil, stateError("registry is closed")
	}
	if err := validateChannelName(channelName); err != nil {
		return nil, err
	}
	var out []MemberPeerEntry
	channelNameC := C.CString(channelName)
	defer C.free(unsafe.Pointer(channelNameC))
	err := func() error {
		entries, err := queryMemberPeers(func(native *C.zlink_member_peer_entry_t, count *C.size_t) error {
			return checkRC(C.zlink_registry_member_peers(r.raw(), channelNameC, native, count))
		})
		if err != nil {
			return err
		}
		out = entries
		return nil
	}()
	return out, err
}

func (r *Registry) Topology() ([]RegistryTopologyEntry, error) {
	if r == nil || r.closed {
		return nil, stateError("registry is closed")
	}
	return queryRegistryTopology(func(entries *C.zlink_registry_topology_entry_t, count *C.size_t) error {
		return checkRC(C.zlink_registry_topology(r.raw(), nil, entries, count))
	})
}

func (r *Registry) TopologyWithFilter(filter *RegistryTopologyFilter) ([]RegistryTopologyEntry, error) {
	if r == nil || r.closed {
		return nil, stateError("registry is closed")
	}
	var rawFilter *C.zlink_registry_topology_filter_t
	var cfilter C.zlink_registry_topology_filter_t
	if filter != nil {
		if err := validateRegistryTopologyFilter(*filter); err != nil {
			return nil, err
		}
		cfilter = filter.toC()
		rawFilter = &cfilter
	}
	return queryRegistryTopology(func(entries *C.zlink_registry_topology_entry_t, count *C.size_t) error {
		return checkRC(C.zlink_registry_topology(r.raw(), rawFilter, entries, count))
	})
}
