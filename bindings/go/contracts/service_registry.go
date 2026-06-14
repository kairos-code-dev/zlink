// SPDX-License-Identifier: MPL-2.0

package contracts

import impl "zlink.systems/zlink/internal/native"

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
)

const (
	RegistryStateIdle     = impl.RegistryStateIdle
	RegistryStateActive   = impl.RegistryStateActive
	RegistryStateDegraded = impl.RegistryStateDegraded
	RegistryStateError    = impl.RegistryStateError
	// TopologySourceManual identifies a peer added manually by the application.
	TopologySourceManual = impl.TopologySourceManual
	// TopologySourceDiscovery identifies a peer learned from a discovery service.
	TopologySourceDiscovery = impl.TopologySourceDiscovery
	// TopologySourceRegistry identifies a peer learned from a service registry.
	TopologySourceRegistry = impl.TopologySourceRegistry
	// TopologyStateDiscovered means the peer was found but a connection is not yet established.
	TopologyStateDiscovered = impl.TopologyStateDiscovered
	TopologyStateConnecting = impl.TopologyStateConnecting
	// TopologyStateReady means the peer is connected and usable.
	TopologyStateReady = impl.TopologyStateReady
	TopologyStateLost  = impl.TopologyStateLost
	TopologyStateError = impl.TopologyStateError
	// TopologyStateStopped means the connection was explicitly stopped.
	TopologyStateStopped = impl.TopologyStateStopped
)
