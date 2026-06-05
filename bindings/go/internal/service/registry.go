// SPDX-License-Identifier: MPL-2.0

package service

import impl "zlink.systems/zlink/internal/native"

type (
	RegistryState                = impl.RegistryState
	TopologySource               = impl.TopologySource
	TopologyState                = impl.TopologyState
	Registry                     = impl.Registry
	RegistryQueryClient          = impl.RegistryQueryClient
	RegistryStatus               = impl.RegistryStatus
	RegistryServiceSummaryEntry  = impl.RegistryServiceSummaryEntry
	RegistryServiceSummaryFilter = impl.RegistryServiceSummaryFilter
	RegistryTopologyEntry        = impl.RegistryTopologyEntry
	RegistryTopologyFilter       = impl.RegistryTopologyFilter
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
)
