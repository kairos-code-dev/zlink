// SPDX-License-Identifier: MPL-2.0

package contracts

import impl "zlink.systems/zlink/internal/eventing"

type (
	MonitorEventMask  = impl.MonitorEventMask
	MonitorSourceKind = impl.MonitorSourceKind
	MonitorEventType  = impl.MonitorEventType
	MonitorEvent      = impl.MonitorEvent
	MonitorStatus     = impl.MonitorStatus
	SocketMonitor     = impl.SocketMonitor
	PollEventFlag     = impl.PollEventFlag
	PollSourceKind    = impl.PollSourceKind
	PollItem          = impl.PollItem
	PollEvent         = impl.PollEvent
	Poller            = impl.Poller
	Timer             = impl.Timer
)

const (
	MonitorEventAll               = impl.MonitorEventAll
	MonitorEventConnectionReady   = impl.MonitorEventConnectionReady
	MonitorEventPeerWeightChanged = impl.MonitorEventPeerWeightChanged
	MonitorSourceSocket           = impl.MonitorSourceSocket
	MonitorSourceSpotPub          = impl.MonitorSourceSpotPub
	MonitorSourceSpotSub          = impl.MonitorSourceSpotSub
	PollIn                        = impl.PollIn
	PollOut                       = impl.PollOut
	PollCompletion                = impl.PollCompletion
	PollSourceSocket              = impl.PollSourceSocket
	PollSourceFD                  = impl.PollSourceFD
	PollSourceTimer               = impl.PollSourceTimer
)

var (
	IgnoreMonitorHandler = impl.IgnoreMonitorHandler
	OpenSocketMonitor    = impl.OpenSocketMonitor
	NewTimer             = impl.NewTimer
	NewTimerFromSpot     = impl.NewTimerFromSpot
	NewPoller            = impl.NewPoller
	Poll                 = impl.Poll
)
