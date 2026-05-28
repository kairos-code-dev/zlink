// SPDX-License-Identifier: MPL-2.0

package contracts

import impl "zlink.systems/zlink/internal/service"

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
	RegistryOption               = impl.RegistryOption
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
