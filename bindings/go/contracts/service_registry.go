// SPDX-License-Identifier: MPL-2.0

package contracts

import impl "zlink.systems/zlink/internal/service"

type (
	// RegistryState is the operational state of a registry.
	RegistryState = impl.RegistryState
	// TopologySource is where a topology entry was learned from.
	TopologySource = impl.TopologySource
	// TopologyState is the lifecycle state of a topology connection.
	TopologyState = impl.TopologyState
	// Registry is a service registry that members publish to and peers query for topology.
	Registry = impl.Registry
	// RegistryQueryClient is a read-only client that queries a registry for its topology.
	RegistryQueryClient = impl.RegistryQueryClient
	// RegistryStatus is a snapshot of a registry's status.
	RegistryStatus = impl.RegistryStatus
	// RegistryServiceSummaryEntry is a per-service rollup of registered endpoints grouped by state.
	RegistryServiceSummaryEntry = impl.RegistryServiceSummaryEntry
	// RegistryServiceSummaryFilter filters a service-summary query; zero fields match anything.
	RegistryServiceSummaryFilter = impl.RegistryServiceSummaryFilter
	// RegistryTopologyEntry is one entry in a registry's topology: a service endpoint and its state.
	RegistryTopologyEntry = impl.RegistryTopologyEntry
	// RegistryTopologyFilter filters a registry topology query; zero fields match anything.
	RegistryTopologyFilter = impl.RegistryTopologyFilter
	// RegistryOption is a configurable option of a service registry.
	RegistryOption = impl.RegistryOption
)

const (
	RegistryStateIdle                 = impl.RegistryStateIdle
	RegistryStateActive               = impl.RegistryStateActive
	RegistryStateDegraded             = impl.RegistryStateDegraded
	RegistryStateError                = impl.RegistryStateError
	TopologySourceManual              = impl.TopologySourceManual
	TopologySourceDiscovery           = impl.TopologySourceDiscovery
	TopologySourceRegistry            = impl.TopologySourceRegistry
	TopologyStateDiscovered           = impl.TopologyStateDiscovered
	TopologyStateConnecting           = impl.TopologyStateConnecting
	TopologyStateReady                = impl.TopologyStateReady
	TopologyStateLost                 = impl.TopologyStateLost
	TopologyStateError                = impl.TopologyStateError
	TopologyStateStopped              = impl.TopologyStateStopped
	RegistryOptionID                  = impl.RegistryOptionID
	RegistryOptionHeartbeatIntervalMS = impl.RegistryOptionHeartbeatIntervalMS
	RegistryOptionHeartbeatTimeoutMS  = impl.RegistryOptionHeartbeatTimeoutMS
	RegistryOptionBroadcastIntervalMS = impl.RegistryOptionBroadcastIntervalMS
)
