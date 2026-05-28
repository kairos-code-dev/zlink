// SPDX-License-Identifier: MPL-2.0

package core

import impl "zlink.systems/zlink/internal/native"

type (
	Version             = impl.Version
	Context             = impl.Context
	ContextOptions      = impl.ContextOptions
	AutoHwmProfile      = impl.AutoHwmProfile
	AutoHwmRecalcReason = impl.AutoHwmRecalcReason
	SocketTarget        = impl.SocketTarget
	Stopwatch           = impl.Stopwatch
)

const (
	AutoHwmProfileCompact             = impl.AutoHwmProfileCompact
	AutoHwmProfileLowLatency          = impl.AutoHwmProfileLowLatency
	AutoHwmProfileBalanced            = impl.AutoHwmProfileBalanced
	AutoHwmProfileThroughput          = impl.AutoHwmProfileThroughput
	AutoHwmRecalcReasonNone           = impl.AutoHwmRecalcReasonNone
	AutoHwmRecalcReasonInitial        = impl.AutoHwmRecalcReasonInitial
	AutoHwmRecalcReasonRoleChange     = impl.AutoHwmRecalcReasonRoleChange
	AutoHwmRecalcReasonPolicyToggle   = impl.AutoHwmRecalcReasonPolicyToggle
	AutoHwmRecalcReasonRefresh        = impl.AutoHwmRecalcReasonRefresh
	AutoHwmRecalcReasonDeferredShrink = impl.AutoHwmRecalcReasonDeferredShrink
)

var (
	RuntimeVersion = impl.RuntimeVersion
	NewContext     = impl.NewContext
	Has            = impl.Has
	Proxy          = impl.Proxy
	ProxySteerable = impl.ProxySteerable
	Sleep          = impl.Sleep
	MultipartClose = impl.MultipartClose
	NewStopwatch   = impl.NewStopwatch
)
