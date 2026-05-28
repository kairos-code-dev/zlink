// SPDX-License-Identifier: MPL-2.0

package service

import impl "zlink.systems/zlink/internal/native"

type (
	AutoConnectType = impl.AutoConnectType
	ServiceRole     = impl.ServiceRole
	ServiceKind     = impl.ServiceKind
	SpotKind        = impl.SpotKind
	SubjectKind     = impl.SubjectKind
	SpotRole        = impl.SpotRole
	Discovery       = impl.Discovery
	SpotRoute       = impl.SpotRoute
	MemberPeerEntry = impl.MemberPeerEntry
)

const (
	AutoConnectInvalid      = impl.AutoConnectInvalid
	AutoConnectRouteMesh    = impl.AutoConnectRouteMesh
	AutoConnectClientServer = impl.AutoConnectClientServer
	AutoConnectDealerMesh   = impl.AutoConnectDealerMesh
	AutoConnectFanout       = impl.AutoConnectFanout
	AutoConnectSpotMesh     = impl.AutoConnectSpotMesh
	ServiceRoleInvalid      = impl.ServiceRoleInvalid
	ServiceRoleSpot         = impl.ServiceRoleSpot
	ServiceRoleRouter       = impl.ServiceRoleRouter
	ServiceRoleDealer       = impl.ServiceRoleDealer
	ServiceRolePub          = impl.ServiceRolePub
	ServiceRoleSub          = impl.ServiceRoleSub
	ServiceKindDiscovery    = impl.ServiceKindDiscovery
	ServiceKindSpotSub      = impl.ServiceKindSpotSub
	ServiceKindSpotPub      = impl.ServiceKindSpotPub
	ServiceKindSocket       = impl.ServiceKindSocket
	SpotKindInvalid         = impl.SpotKindInvalid
	SpotKindEntry           = impl.SpotKindEntry
	SpotKindUser            = impl.SpotKindUser
	SubjectKindNone         = impl.SubjectKindNone
	SubjectKindTopic        = impl.SubjectKindTopic
	SubjectKindPattern      = impl.SubjectKindPattern
	SpotRolePub             = impl.SpotRolePub
	SpotRoleSub             = impl.SpotRoleSub
)
