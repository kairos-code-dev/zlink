// SPDX-License-Identifier: MPL-2.0

// Package contracts is the public Go binding projection.
package contracts

import impl "zlink.systems/zlink/internal/core"

type (
	// Version is the native zlink library version (major, minor, patch).
	Version = impl.Version
	// Context is a messaging context: the factory and owner of sockets and services.
	Context = impl.Context
	// ContextOptions are context-wide options governing the I/O threads and defaults shared by every socket.
	ContextOptions = impl.ContextOptions
	// AutoHwmProfile selects an automatic high-water-mark sizing profile.
	AutoHwmProfile = impl.AutoHwmProfile
	// AutoHwmRecalcReason reports what triggered the last automatic high-water-mark recalculation.
	AutoHwmRecalcReason = impl.AutoHwmRecalcReason
	// SocketTarget identifies a socket that can participate in poll and proxy operations.
	SocketTarget = impl.SocketTarget
	// Stopwatch is a high-resolution elapsed-time stopwatch.
	Stopwatch = impl.Stopwatch
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
	// RuntimeVersion returns the native zlink library version.
	RuntimeVersion = impl.RuntimeVersion
	// NewContext creates a messaging context; the caller owns it and must close it.
	NewContext = impl.NewContext
	// Has reports whether the native library was built with the named capability.
	Has = impl.Has
	// Proxy forwards messages between two sockets, blocking until the context is terminated.
	Proxy = impl.Proxy
	// ProxySteerable forwards messages between two sockets under runtime control, blocking until terminated.
	ProxySteerable = impl.ProxySteerable
	// Sleep blocks the calling goroutine for the given duration.
	Sleep = impl.Sleep
	// MultipartClose closes every message in a multipart payload.
	MultipartClose = impl.MultipartClose
	// NewStopwatch creates a high-resolution stopwatch; the caller owns it.
	NewStopwatch = impl.NewStopwatch
)
