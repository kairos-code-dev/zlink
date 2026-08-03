// SPDX-License-Identifier: MPL-2.0

package contracts

import impl "zlink.systems/zlink/v11/internal/native"

type (
	// MonitorEventMask is a bitmask selecting which socket monitor events to subscribe to.
	MonitorEventMask = impl.MonitorEventMask
	// MonitorSourceKind identifies what a monitored source is.
	MonitorSourceKind = impl.MonitorSourceKind
	// MonitorEventType is the kind of a delivered socket monitor event.
	MonitorEventType = impl.MonitorEventType
	// MonitorEvent is a single socket connection-lifecycle event reported by a monitor.
	MonitorEvent = impl.MonitorEvent
	// MonitorStatus is a snapshot of a monitored socket's state and auto-high-water-mark telemetry.
	MonitorStatus = impl.MonitorStatus
	// SocketMonitor observes a socket's connection lifecycle events and current status.
	SocketMonitor = impl.SocketMonitor
	// PollEventFlag is a readiness condition a poll source can be watched for or report.
	PollEventFlag = impl.PollEventFlag
	// PollSourceKind is whether a poll event came from a socket, file descriptor, or timer.
	PollSourceKind = impl.PollSourceKind
	// PollItem is a raw poll descriptor: a file descriptor and its watched/fired events.
	PollItem = impl.PollItem
	// PollEvent is one ready source reported by a poller wait.
	PollEvent = impl.PollEvent
	// Poller multiplexes sockets, file descriptors, and timers, reporting which become ready.
	Poller = impl.Poller
	// Timer fires on an interval and can be polled or awaited.
	Timer = impl.Timer
)

const (
	// MonitorEventAll subscribes the monitor to every connection lifecycle event.
	MonitorEventAll = impl.MonitorEventAll
	// MonitorEventConnectionReady fires when a connection completes its handshake and is ready for traffic.
	MonitorEventConnectionReady = impl.MonitorEventConnectionReady
	// MonitorEventPeerWeightChanged fires when a peer's load-balancing weight changes.
	MonitorEventPeerWeightChanged = impl.MonitorEventPeerWeightChanged
	// MonitorSourceSocket identifies a plain socket as the monitored source.
	MonitorSourceSocket = impl.MonitorSourceSocket
	// PollIn reports that a receive will not block.
	PollIn = impl.PollIn
	// PollOut reports that a send will not block.
	PollOut = impl.PollOut
	// PollCompletion reports that an asynchronous operation has completed.
	PollCompletion = impl.PollCompletion
	// PollSourceSocket identifies a socket as the source of a poll event.
	PollSourceSocket = impl.PollSourceSocket
	// PollSourceFD identifies a raw file descriptor as the source of a poll event.
	PollSourceFD = impl.PollSourceFD
	// PollSourceTimer identifies a timer as the source of a poll event.
	PollSourceTimer = impl.PollSourceTimer
)

var (
	// OpenSocketMonitor opens a monitor on a socket for the selected events; the caller owns it.
	OpenSocketMonitor = impl.OpenSocketMonitor
	// NewTimer creates a standalone timer; the caller owns it.
	NewTimer = impl.NewTimer
	// NewPoller creates an empty poller; the caller owns it.
	NewPoller = impl.NewPoller
	// Poll waits for events across several sockets or monitors at once.
	Poll = impl.Poll
)
