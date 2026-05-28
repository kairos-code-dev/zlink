// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include "zlink.h"

extern void goZlinkMonitorTrampoline(zlink_monitor_event_t *event_, uintptr_t userdata_);

static inline int zlink_socket_monitor_handler_go_local(void *m, uintptr_t userdata) {
    return zlink_socket_monitor_handler(m, (zlink_socket_monitor_handler_fn)goZlinkMonitorTrampoline, (void *)userdata);
}

*/
import "C"

import (
	"runtime/cgo"
	"unsafe"
)

const (
	MonitorEventAll               MonitorEventMask = MonitorEventMask(C.ZLINK_SOCKET_MONITOR_EVENT_ALL)
	MonitorEventConnectionReady   MonitorEventMask = MonitorEventMask(C.ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY)
	MonitorEventPeerWeightChanged MonitorEventMask = MonitorEventMask(C.ZLINK_SOCKET_MONITOR_EVENT_PEER_WEIGHT_CHANGED)
)

type MonitorEventMask uint32
type MonitorSourceKind uint32
type MonitorEventType uint64

const (
	MonitorSourceSocket  MonitorSourceKind = MonitorSourceKind(C.ZLINK_MONITOR_SOURCE_SOCKET)
	MonitorSourceSpotPub MonitorSourceKind = MonitorSourceKind(C.ZLINK_MONITOR_SOURCE_SPOT_PUB)
	MonitorSourceSpotSub MonitorSourceKind = MonitorSourceKind(C.ZLINK_MONITOR_SOURCE_SPOT_SUB)
)

type MonitorEvent struct {
	Event      MonitorEventType
	Value      uint32
	RoutingID  RoutingID
	LocalAddr  string
	RemoteAddr string
}

func (e *MonitorEvent) HasRoutingID() bool {
	return e != nil && monitorHasRoutingID(e.RoutingID)
}

func (e *MonitorEvent) IsConnected() bool {
	return e != nil && e.Event&MonitorEventType(C.ZLINK_SOCKET_MONITOR_EVENT_CONNECTED) != 0
}

func (e *MonitorEvent) IsDisconnected() bool {
	return e != nil && e.Event&MonitorEventType(C.ZLINK_SOCKET_MONITOR_EVENT_DISCONNECTED) != 0
}

func (e *MonitorEvent) IsListening() bool {
	return e != nil && e.Event&MonitorEventType(C.ZLINK_SOCKET_MONITOR_EVENT_LISTENING) != 0
}

func (e *MonitorEvent) IsAccepted() bool {
	return e != nil && e.Event&MonitorEventType(C.ZLINK_SOCKET_MONITOR_EVENT_ACCEPTED) != 0
}

func (e *MonitorEvent) IsConnectionReady() bool {
	return e != nil && e.Event&MonitorEventType(C.ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY) != 0
}

type MonitorStatus struct {
	SourceKind                   MonitorSourceKind
	StateFlags                   uint32
	DetailFlags                  uint32
	SndPendingMsgs               uint64
	RcvPendingMsgs               uint64
	AutoHwmEnabled               bool
	AutoHwmProfile               uint32
	AutoHwmRole                  uint32
	AutoHwmPolicyClass           uint32
	AutoHwmUnitBudgetBytes       uint64
	AutoHwmSizeCap               uint32
	AutoHwmSocketMessageSlots    uint64
	AutoHwmEffectiveMessageBytes uint64
	AutoHwmAppliedSndHwm         int32
	AutoHwmAppliedRcvHwm         int32
	AutoHwmEffectiveSndBuf       int32
	AutoHwmEffectiveRcvBuf       int32
	AutoHwmLastRecalcMs          uint64
	AutoHwmLastRecalcReason      AutoHwmRecalcReason
	AutoHwmSendBlockedRatioPPM   uint32
	AutoHwmDeferredSndHwm        int32
	AutoHwmDeferredRcvHwm        int32
}

func (s *MonitorStatus) IsReady() bool {
	return s != nil && s.StateFlags&uint32(C.ZLINK_MONITOR_STATE_READY) != 0
}

func monitorStatusFromC(raw C.zlink_monitor_status_t) MonitorStatus {
	return MonitorStatus{
		SourceKind:                   MonitorSourceKind(raw.source_kind),
		StateFlags:                   uint32(raw.state_flags),
		DetailFlags:                  uint32(raw.detail_flags),
		SndPendingMsgs:               uint64(raw.snd_pending_msgs),
		RcvPendingMsgs:               uint64(raw.rcv_pending_msgs),
		AutoHwmEnabled:               uint32(raw.auto_hwm_enabled) != 0,
		AutoHwmProfile:               uint32(raw.auto_hwm_profile),
		AutoHwmRole:                  uint32(raw.auto_hwm_role),
		AutoHwmPolicyClass:           uint32(raw.auto_hwm_policy_class),
		AutoHwmUnitBudgetBytes:       uint64(raw.auto_hwm_unit_budget_bytes),
		AutoHwmSizeCap:               uint32(raw.auto_hwm_size_cap),
		AutoHwmSocketMessageSlots:    uint64(raw.auto_hwm_socket_message_slots),
		AutoHwmEffectiveMessageBytes: uint64(raw.auto_hwm_effective_message_bytes),
		AutoHwmAppliedSndHwm:         int32(raw.auto_hwm_applied_sndhwm),
		AutoHwmAppliedRcvHwm:         int32(raw.auto_hwm_applied_rcvhwm),
		AutoHwmEffectiveSndBuf:       int32(raw.auto_hwm_effective_sndbuf),
		AutoHwmEffectiveRcvBuf:       int32(raw.auto_hwm_effective_rcvbuf),
		AutoHwmLastRecalcMs:          uint64(raw.auto_hwm_last_recalc_ms),
		AutoHwmLastRecalcReason:      AutoHwmRecalcReason(raw.auto_hwm_last_recalc_reason),
		AutoHwmSendBlockedRatioPPM:   uint32(raw.auto_hwm_send_blocked_ratio_ppm),
		AutoHwmDeferredSndHwm:        int32(raw.auto_hwm_deferred_sndhwm),
		AutoHwmDeferredRcvHwm:        int32(raw.auto_hwm_deferred_rcvhwm),
	}
}

type SocketMonitor struct {
	handle   unsafe.Pointer
	callback cgo.Handle
}

var IgnoreMonitorHandler func(*MonitorEvent) = func(*MonitorEvent) {}

func resolveMonitorEvents(events []MonitorEventMask) MonitorEventMask {
	if len(events) == 0 {
		return MonitorEventAll
	}
	var mask MonitorEventMask
	for _, event := range events {
		mask |= event
	}
	return mask
}

func OpenSocketMonitor(socket SocketTarget, events ...MonitorEventMask) (*SocketMonitor, error) {
	if socket == nil {
		return nil, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	options := C.zlink_socket_monitor_open_options_t{
		events: C.zlink_socket_monitor_event_mask_t(resolveMonitorEvents(events)),
	}
	handle := C.zlink_socket_monitor_open(socket.raw(), &options)
	if handle == nil {
		return nil, configErrorFromErrno(currentErrno())
	}
	return &SocketMonitor{handle: handle}, nil
}

// Recv returns the next monitor event. Returns (nil, *RecvError{Result:RecvNoData})
// when DONTWAIT finds nothing. Value-return form is allowed for monitor/timer
// control-plane APIs by doc/spec/bindings/go/README.md §Receive And Subscribe Shape.
func (m *SocketMonitor) Recv(flags RecvFlags) (*MonitorEvent, error) {
	var raw C.zlink_socket_monitor_event_t
	if err := recvErrorFromResult(C.zlink_socket_monitor_recv(m.handle, &raw, C.zlink_recv_flags_t(flags))); err != nil {
		return nil, err
	}
	return monitorEventFromC(raw), nil
}

func (m *SocketMonitor) Status() (*MonitorStatus, error) {
	var raw C.zlink_monitor_status_t
	if err := configErrorFromResult(C.zlink_monitor_status(m.handle, &raw)); err != nil {
		return nil, err
	}
	snapshot := monitorStatusFromC(raw)
	return &snapshot, nil
}

func (m *SocketMonitor) OnEvent(handler func(*MonitorEvent)) error {
	if handler == nil {
		return &HandlerError{Result: HandlerInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	state := newMonitorCallbackState(handler)
	handle := cgo.NewHandle(state)
	if err := handlerErrorFromResult(C.zlink_socket_monitor_handler_go_local(m.handle, C.uintptr_t(handle))); err != nil {
		state.close()
		handle.Delete()
		return err
	}
	if m.callback != 0 {
		releaseCallbackHandle(m.callback)
	}
	m.callback = handle
	return nil
}

func (m *SocketMonitor) Close() error {
	if m == nil || m.handle == nil {
		return nil
	}
	handle := m.handle
	if err := closeErrorFromResult(C.zlink_monitor_close(&handle)); err != nil {
		return err
	}
	if m.callback != 0 {
		releaseCallbackHandle(m.callback)
		m.callback = 0
	}
	m.handle = nil
	return nil
}

func monitorEventFromC(raw C.zlink_socket_monitor_event_t) *MonitorEvent {
	return &MonitorEvent{
		Event:      MonitorEventType(raw.event),
		Value:      uint32(raw.value),
		RoutingID:  routingIDFromC(raw.routing_id),
		LocalAddr:  C.GoString(&raw.local_addr[0]),
		RemoteAddr: C.GoString(&raw.remote_addr[0]),
	}
}

//export goZlinkMonitorTrampoline
func goZlinkMonitorTrampoline(event *C.zlink_monitor_event_t, userdata C.uintptr_t) {
	state, ok := safeHandleAs[*monitorCallbackState](userdata)
	if !ok {
		return
	}
	payload := monitorEventFromC(*event)
	state.dispatcher.enqueue(&callbackTask{
		label: "socket-monitor",
		invoke: func() {
			state.handler(payload)
		},
	})
}
