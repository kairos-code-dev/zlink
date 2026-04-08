// SPDX-License-Identifier: MPL-2.0

package zlink

/*
#include <stdint.h>
#include "zlink.h"

extern void goZlinkMonitorTrampoline(zlink_monitor_event_t *event_, uintptr_t userdata_);
extern void goZlinkServiceMonitorTrampoline(zlink_service_event_t *event_, uintptr_t userdata_);

static inline int zlink_socket_monitor_handler_go_local(void *m, uintptr_t userdata) {
    return zlink_socket_monitor_handler(m, (zlink_socket_monitor_handler_fn)goZlinkMonitorTrampoline, (void *)userdata);
}

static inline int zlink_service_monitor_handler_go_local(void *m, uintptr_t userdata) {
    return zlink_service_monitor_handler(m, (zlink_service_monitor_handler_fn)goZlinkServiceMonitorTrampoline, (void *)userdata);
}
*/
import "C"

import (
	"runtime/cgo"
	"unsafe"
)

const (
	MonitorEventAll             MonitorEventMask = MonitorEventMask(C.ZLINK_SOCKET_MONITOR_EVENT_ALL)
	MonitorEventConnectionReady MonitorEventMask = MonitorEventMask(C.ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY)

	ServiceMonitorEventAll                       ServiceMonitorEventMask = ServiceMonitorEventError | ServiceMonitorEventClosed | ServiceMonitorEventDiscoveryServiceUp | ServiceMonitorEventDiscoveryServiceDown | ServiceMonitorEventDiscoveryProvidersChanged
	ServiceMonitorEventError                     ServiceMonitorEventMask = ServiceMonitorEventMask(C.ZLINK_SERVICE_MONITOR_EVENT_ERROR)
	ServiceMonitorEventClosed                    ServiceMonitorEventMask = ServiceMonitorEventMask(C.ZLINK_SERVICE_MONITOR_EVENT_CLOSED)
	ServiceMonitorEventDiscoveryServiceUp        ServiceMonitorEventMask = ServiceMonitorEventMask(C.ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_UP)
	ServiceMonitorEventDiscoveryServiceDown      ServiceMonitorEventMask = ServiceMonitorEventMask(C.ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_DOWN)
	ServiceMonitorEventDiscoveryProvidersChanged ServiceMonitorEventMask = ServiceMonitorEventMask(C.ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_PROVIDERS_CHANGED)
)

type MonitorEventMask uint32
type ServiceMonitorEventMask uint32

type MonitorEvent struct {
	Event      uint64
	Value      uint64
	RoutingID  RoutingID
	LocalAddr  string
	RemoteAddr string
}

func (e *MonitorEvent) IsConnected() bool {
	return e != nil && e.Event&uint64(C.ZLINK_SOCKET_MONITOR_EVENT_CONNECTED) != 0
}

func (e *MonitorEvent) IsDisconnected() bool {
	return e != nil && e.Event&uint64(C.ZLINK_SOCKET_MONITOR_EVENT_DISCONNECTED) != 0
}

func (e *MonitorEvent) IsListening() bool {
	return e != nil && e.Event&uint64(C.ZLINK_SOCKET_MONITOR_EVENT_LISTENING) != 0
}

func (e *MonitorEvent) IsAccepted() bool {
	return e != nil && e.Event&uint64(C.ZLINK_SOCKET_MONITOR_EVENT_ACCEPTED) != 0
}

func (e *MonitorEvent) IsConnectionReady() bool {
	return e != nil && e.Event&uint64(C.ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY) != 0
}

type MonitorSnapshot struct {
	StateFlags     uint32
	DetailFlags    uint32
	SendPendingMsg uint64
	RecvPendingMsg uint64
}

func (s *MonitorSnapshot) IsReady() bool {
	return s != nil && s.StateFlags&uint32(C.ZLINK_MONITOR_STATE_READY) != 0
}

type ServiceMonitorEvent struct {
	ServiceKind uint32
	EventType   uint32
	Status      int32
	ErrorCode   int32
	Value       uint32
	DetailFlags uint32
	ServiceName string
	Endpoint    string
	RoutingID   RoutingID
	Subject     string
	SubjectKind uint32
}

type SocketMonitor struct {
	handle   unsafe.Pointer
	callback cgo.Handle
}

type ServiceMonitor struct {
	handle   unsafe.Pointer
	callback cgo.Handle
}

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

func resolveServiceMonitorEvents(events []ServiceMonitorEventMask) ServiceMonitorEventMask {
	if len(events) == 0 {
		return ServiceMonitorEventAll
	}
	var mask ServiceMonitorEventMask
	for _, event := range events {
		mask |= event
	}
	return mask
}

func OpenSocketMonitor(socket SocketTarget, events ...MonitorEventMask) (*SocketMonitor, error) {
	if socket == nil {
		return nil, validationError("monitor target must not be nil")
	}
	options := C.zlink_socket_monitor_open_options_t{
		events: C.zlink_socket_monitor_event_mask_t(resolveMonitorEvents(events)),
	}
	handle := C.zlink_socket_monitor_open(socket.raw(), &options)
	if handle == nil {
		return nil, lastError()
	}
	return &SocketMonitor{handle: handle}, nil
}

func (m *SocketMonitor) Recv() (*MonitorEvent, error) {
	var raw C.zlink_socket_monitor_event_t
	if err := checkRC(C.zlink_socket_monitor_recv(m.handle, &raw, 0)); err != nil {
		return nil, err
	}
	return monitorEventFromC(raw), nil
}

func (m *SocketMonitor) TryRecv() (*MonitorEvent, bool, error) {
	var raw C.zlink_socket_monitor_event_t
	rc := C.zlink_socket_monitor_recv(m.handle, &raw, C.ZLINK_DONTWAIT)
	if rc == -1 {
		if emptyErrno() {
			return nil, false, nil
		}
		return nil, false, lastError()
	}
	return monitorEventFromC(raw), true, nil
}

func (m *SocketMonitor) Snapshot() (*MonitorSnapshot, error) {
	var raw C.zlink_monitor_snapshot_t
	if err := checkRC(C.zlink_monitor_snapshot(m.handle, &raw)); err != nil {
		return nil, err
	}
	return &MonitorSnapshot{
		StateFlags:     uint32(raw.state_flags),
		DetailFlags:    uint32(raw.detail_flags),
		SendPendingMsg: uint64(raw.snd_pending_msgs),
		RecvPendingMsg: uint64(raw.rcv_pending_msgs),
	}, nil
}

func (m *SocketMonitor) OnEvent(handler func(*MonitorEvent)) error {
	if handler == nil {
		return validationError("monitor handler must not be nil")
	}
	state := newMonitorCallbackState(handler)
	handle := cgo.NewHandle(state)
	if err := checkRC(C.zlink_socket_monitor_handler_go_local(m.handle, C.uintptr_t(handle))); err != nil {
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
	if err := checkRC(C.zlink_monitor_close(&handle)); err != nil {
		return err
	}
	if m.callback != 0 {
		releaseCallbackHandle(m.callback)
		m.callback = 0
	}
	m.handle = nil
	return nil
}

func (m *ServiceMonitor) Recv() (*ServiceMonitorEvent, error) {
	var raw C.zlink_service_monitor_event_t
	if err := checkRC(C.zlink_service_monitor_recv(m.handle, &raw, 0)); err != nil {
		return nil, err
	}
	return serviceMonitorEventFromC(raw), nil
}

func (m *ServiceMonitor) TryRecv() (*ServiceMonitorEvent, bool, error) {
	var raw C.zlink_service_monitor_event_t
	rc := C.zlink_service_monitor_recv(m.handle, &raw, C.ZLINK_DONTWAIT)
	if rc == -1 {
		if emptyErrno() {
			return nil, false, nil
		}
		return nil, false, lastError()
	}
	return serviceMonitorEventFromC(raw), true, nil
}

func (m *ServiceMonitor) Snapshot() (*MonitorSnapshot, error) {
	var raw C.zlink_monitor_snapshot_t
	if err := checkRC(C.zlink_monitor_snapshot(m.handle, &raw)); err != nil {
		return nil, err
	}
	return &MonitorSnapshot{
		StateFlags:     uint32(raw.state_flags),
		DetailFlags:    uint32(raw.detail_flags),
		SendPendingMsg: uint64(raw.snd_pending_msgs),
		RecvPendingMsg: uint64(raw.rcv_pending_msgs),
	}, nil
}

func (m *ServiceMonitor) OnEvent(handler func(*ServiceMonitorEvent)) error {
	if handler == nil {
		return validationError("service monitor handler must not be nil")
	}
	state := newServiceMonitorCallbackState(handler)
	handle := cgo.NewHandle(state)
	if err := checkRC(C.zlink_service_monitor_handler_go_local(m.handle, C.uintptr_t(handle))); err != nil {
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

func (m *ServiceMonitor) Close() error {
	if m == nil || m.handle == nil {
		return nil
	}
	handle := m.handle
	if err := checkRC(C.zlink_monitor_close(&handle)); err != nil {
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
		Event:      uint64(raw.event),
		Value:      uint64(raw.value),
		RoutingID:  routingIDFromC(raw.routing_id),
		LocalAddr:  C.GoString(&raw.local_addr[0]),
		RemoteAddr: C.GoString(&raw.remote_addr[0]),
	}
}

func serviceMonitorEventFromC(raw C.zlink_service_monitor_event_t) *ServiceMonitorEvent {
	return &ServiceMonitorEvent{
		ServiceKind: uint32(raw.service_kind),
		EventType:   uint32(raw.event_type),
		Status:      int32(raw.status),
		ErrorCode:   int32(raw.error_code),
		Value:       uint32(raw.value),
		DetailFlags: uint32(raw.detail_flags),
		ServiceName: C.GoString(&raw.service_name[0]),
		Endpoint:    C.GoString(&raw.endpoint[0]),
		RoutingID:   routingIDFromC(raw.routing_id),
		Subject:     C.GoString(&raw.subject[0]),
		SubjectKind: uint32(raw.subject_kind),
	}
}

//export goZlinkMonitorTrampoline
func goZlinkMonitorTrampoline(event *C.zlink_monitor_event_t, userdata C.uintptr_t) {
	state := cgo.Handle(userdata).Value().(*monitorCallbackState)
	payload := monitorEventFromC(*event)
	state.dispatcher.enqueue(&callbackTask{
		label: "socket-monitor",
		invoke: func() {
			state.handler(payload)
		},
	})
}

//export goZlinkServiceMonitorTrampoline
func goZlinkServiceMonitorTrampoline(event *C.zlink_service_event_t, userdata C.uintptr_t) {
	state := cgo.Handle(userdata).Value().(*serviceMonitorCallbackState)
	payload := serviceMonitorEventFromC(*event)
	state.dispatcher.enqueue(&callbackTask{
		label: "service-monitor",
		invoke: func() {
			state.handler(payload)
		},
	})
}
