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
	MonitorEventAll               MonitorEventMask = MonitorEventMask(C.ZLINK_SOCKET_MONITOR_EVENT_ALL)
	MonitorEventConnectionReady   MonitorEventMask = MonitorEventMask(C.ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY)
	MonitorEventPeerWeightChanged MonitorEventMask = MonitorEventMask(C.ZLINK_SOCKET_MONITOR_EVENT_PEER_WEIGHT_CHANGED)

	ServiceMonitorEventError                     ServiceMonitorEventMask = ServiceMonitorEventMask(C.ZLINK_SERVICE_MONITOR_EVENT_ERROR)
	ServiceMonitorEventClosed                    ServiceMonitorEventMask = ServiceMonitorEventMask(C.ZLINK_SERVICE_MONITOR_EVENT_CLOSED)
	ServiceMonitorEventDiscoveryServiceUp        ServiceMonitorEventMask = ServiceMonitorEventMask(C.ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_UP)
	ServiceMonitorEventDiscoveryServiceDown      ServiceMonitorEventMask = ServiceMonitorEventMask(C.ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_DOWN)
	ServiceMonitorEventDiscoveryProvidersChanged ServiceMonitorEventMask = ServiceMonitorEventMask(C.ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_PROVIDERS_CHANGED)
	ServiceMonitorEventPeerWeightChanged         ServiceMonitorEventMask = ServiceMonitorEventMask(C.ZLINK_SERVICE_MONITOR_EVENT_PEER_WEIGHT_CHANGED)
	ServiceMonitorEventAll                       ServiceMonitorEventMask = ServiceMonitorEventError | ServiceMonitorEventClosed | ServiceMonitorEventDiscoveryServiceUp | ServiceMonitorEventDiscoveryServiceDown | ServiceMonitorEventDiscoveryProvidersChanged | ServiceMonitorEventPeerWeightChanged
)

type MonitorEventMask uint32
type ServiceMonitorEventMask uint32
type MonitorSourceKind uint32
type MonitorEventType uint64
type ServiceMonitorEventType uint32

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

type MonitorSnapshot struct {
	SourceKind                          MonitorSourceKind
	StateFlags                          uint32
	DetailFlags                         uint32
	SndPendingMsgs                      uint64
	RcvPendingMsgs                      uint64
	AutoHwmEnabled                      bool
	AutoHwmRole                         uint32
	AutoHwmManagedConnections           uint32
	AutoHwmActiveHwmConnections         uint32
	AutoHwmPlanningTransportConnections uint32
	AutoHwmBaseFloorPerConnection       uint32
	AutoHwmAppliedSndHwm                int32
	AutoHwmAppliedRcvHwm                int32
	AutoHwmRequestedSndBuf              int32
	AutoHwmRequestedRcvBuf              int32
	AutoHwmEffectiveSndBuf              int32
	AutoHwmEffectiveRcvBuf              int32
	AutoHwmTotalMemoryBudgetBytes       uint64
	AutoHwmQueueBudgetBytes             uint64
	AutoHwmTransportBudgetBytes         uint64
	AutoHwmRuntimeReserveBytes          uint64
	AutoHwmGroupBudgetBytes             uint64
	AutoHwmGroupMessageSlots            uint64
	AutoHwmEffectiveMessageBytes        uint64
	AutoHwmControlBudgetBytes           uint64
	AutoHwmRoutedBudgetBytes            uint64
	AutoHwmFanoutBudgetBytes            uint64
	AutoHwmRecvIngressBudgetBytes       uint64
	AutoHwmControlActiveConnections     uint32
	AutoHwmRoutedActiveConnections      uint32
	AutoHwmFanoutActiveConnections      uint32
	AutoHwmRecvIngressActiveConnections uint32
	AutoHwmEstimatedMaxMemoryBytes      uint64
	AutoHwmLastRecalcMs                 uint64
	AutoHwmLastRecalcReason             uint32
	AutoHwmSendBlockedRatioPPM          uint32
}

func (s *MonitorSnapshot) IsReady() bool {
	return s != nil && s.StateFlags&uint32(C.ZLINK_MONITOR_STATE_READY) != 0
}

type ServiceMonitorEvent struct {
	ServiceKind ServiceKind
	EventType   ServiceMonitorEventType
	Status      uint32
	ErrorCode   uint32
	Value       uint64
	DetailFlags uint32
	ServiceName string
	Endpoint    string
	RoutingID   RoutingID
	Subject     string
	SubjectKind SubjectKind
}

func (e *ServiceMonitorEvent) HasRoutingID() bool {
	return e != nil && monitorHasRoutingID(e.RoutingID)
}

type SocketMonitor struct {
	handle   unsafe.Pointer
	callback cgo.Handle
}

type ServiceMonitor struct {
	handle   unsafe.Pointer
	callback cgo.Handle
}

var IgnoreMonitorHandler = func(MonitorEvent) {}

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

func (m *SocketMonitor) Recv() (*MonitorEvent, error) {
	var raw C.zlink_socket_monitor_event_t
	if err := recvErrorFromResult(C.zlink_socket_monitor_recv(m.handle, &raw, 0)); err != nil {
		return nil, err
	}
	return monitorEventFromC(raw), nil
}

func (m *SocketMonitor) Snapshot() (*MonitorSnapshot, error) {
	var raw C.zlink_monitor_snapshot_t
	if err := configErrorFromResult(C.zlink_monitor_snapshot(m.handle, &raw)); err != nil {
		return nil, err
	}
	return &MonitorSnapshot{
		SourceKind:                          MonitorSourceKind(raw.source_kind),
		StateFlags:                          uint32(raw.state_flags),
		DetailFlags:                         uint32(raw.detail_flags),
		SndPendingMsgs:                      uint64(raw.snd_pending_msgs),
		RcvPendingMsgs:                      uint64(raw.rcv_pending_msgs),
		AutoHwmEnabled:                      uint32(raw.auto_hwm_enabled) != 0,
		AutoHwmRole:                         uint32(raw.auto_hwm_role),
		AutoHwmManagedConnections:           uint32(raw.auto_hwm_managed_connections),
		AutoHwmActiveHwmConnections:         uint32(raw.auto_hwm_active_hwm_connections),
		AutoHwmPlanningTransportConnections: uint32(raw.auto_hwm_planning_transport_connections),
		AutoHwmBaseFloorPerConnection:       uint32(raw.auto_hwm_base_floor_per_connection),
		AutoHwmAppliedSndHwm:                int32(raw.auto_hwm_applied_sndhwm),
		AutoHwmAppliedRcvHwm:                int32(raw.auto_hwm_applied_rcvhwm),
		AutoHwmRequestedSndBuf:              int32(raw.auto_hwm_requested_sndbuf),
		AutoHwmRequestedRcvBuf:              int32(raw.auto_hwm_requested_rcvbuf),
		AutoHwmEffectiveSndBuf:              int32(raw.auto_hwm_effective_sndbuf),
		AutoHwmEffectiveRcvBuf:              int32(raw.auto_hwm_effective_rcvbuf),
		AutoHwmTotalMemoryBudgetBytes:       uint64(raw.auto_hwm_total_memory_budget_bytes),
		AutoHwmQueueBudgetBytes:             uint64(raw.auto_hwm_queue_budget_bytes),
		AutoHwmTransportBudgetBytes:         uint64(raw.auto_hwm_transport_budget_bytes),
		AutoHwmRuntimeReserveBytes:          uint64(raw.auto_hwm_runtime_reserve_bytes),
		AutoHwmGroupBudgetBytes:             uint64(raw.auto_hwm_group_budget_bytes),
		AutoHwmGroupMessageSlots:            uint64(raw.auto_hwm_group_message_slots),
		AutoHwmEffectiveMessageBytes:        uint64(raw.auto_hwm_effective_message_bytes),
		AutoHwmControlBudgetBytes:           uint64(raw.auto_hwm_control_budget_bytes),
		AutoHwmRoutedBudgetBytes:            uint64(raw.auto_hwm_routed_budget_bytes),
		AutoHwmFanoutBudgetBytes:            uint64(raw.auto_hwm_fanout_budget_bytes),
		AutoHwmRecvIngressBudgetBytes:       uint64(raw.auto_hwm_recv_ingress_budget_bytes),
		AutoHwmControlActiveConnections:     uint32(raw.auto_hwm_control_active_connections),
		AutoHwmRoutedActiveConnections:      uint32(raw.auto_hwm_routed_active_connections),
		AutoHwmFanoutActiveConnections:      uint32(raw.auto_hwm_fanout_active_connections),
		AutoHwmRecvIngressActiveConnections: uint32(raw.auto_hwm_recv_ingress_active_connections),
		AutoHwmEstimatedMaxMemoryBytes:      uint64(raw.auto_hwm_estimated_max_memory_bytes),
		AutoHwmLastRecalcMs:                 uint64(raw.auto_hwm_last_recalc_ms),
		AutoHwmLastRecalcReason:             uint32(raw.auto_hwm_last_recalc_reason),
		AutoHwmSendBlockedRatioPPM:          uint32(raw.auto_hwm_send_blocked_ratio_ppm),
	}, nil
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

func (m *ServiceMonitor) Recv() (*ServiceMonitorEvent, error) {
	var raw C.zlink_service_monitor_event_t
	if err := recvErrorFromResult(C.zlink_service_monitor_recv(m.handle, &raw, 0)); err != nil {
		return nil, err
	}
	return serviceMonitorEventFromC(raw), nil
}

func (m *ServiceMonitor) Snapshot() (*MonitorSnapshot, error) {
	var raw C.zlink_monitor_snapshot_t
	if err := configErrorFromResult(C.zlink_monitor_snapshot(m.handle, &raw)); err != nil {
		return nil, err
	}
	return &MonitorSnapshot{
		SourceKind:                          MonitorSourceKind(raw.source_kind),
		StateFlags:                          uint32(raw.state_flags),
		DetailFlags:                         uint32(raw.detail_flags),
		SndPendingMsgs:                      uint64(raw.snd_pending_msgs),
		RcvPendingMsgs:                      uint64(raw.rcv_pending_msgs),
		AutoHwmEnabled:                      uint32(raw.auto_hwm_enabled) != 0,
		AutoHwmRole:                         uint32(raw.auto_hwm_role),
		AutoHwmManagedConnections:           uint32(raw.auto_hwm_managed_connections),
		AutoHwmActiveHwmConnections:         uint32(raw.auto_hwm_active_hwm_connections),
		AutoHwmPlanningTransportConnections: uint32(raw.auto_hwm_planning_transport_connections),
		AutoHwmBaseFloorPerConnection:       uint32(raw.auto_hwm_base_floor_per_connection),
		AutoHwmAppliedSndHwm:                int32(raw.auto_hwm_applied_sndhwm),
		AutoHwmAppliedRcvHwm:                int32(raw.auto_hwm_applied_rcvhwm),
		AutoHwmRequestedSndBuf:              int32(raw.auto_hwm_requested_sndbuf),
		AutoHwmRequestedRcvBuf:              int32(raw.auto_hwm_requested_rcvbuf),
		AutoHwmEffectiveSndBuf:              int32(raw.auto_hwm_effective_sndbuf),
		AutoHwmEffectiveRcvBuf:              int32(raw.auto_hwm_effective_rcvbuf),
		AutoHwmTotalMemoryBudgetBytes:       uint64(raw.auto_hwm_total_memory_budget_bytes),
		AutoHwmQueueBudgetBytes:             uint64(raw.auto_hwm_queue_budget_bytes),
		AutoHwmTransportBudgetBytes:         uint64(raw.auto_hwm_transport_budget_bytes),
		AutoHwmRuntimeReserveBytes:          uint64(raw.auto_hwm_runtime_reserve_bytes),
		AutoHwmGroupBudgetBytes:             uint64(raw.auto_hwm_group_budget_bytes),
		AutoHwmGroupMessageSlots:            uint64(raw.auto_hwm_group_message_slots),
		AutoHwmEffectiveMessageBytes:        uint64(raw.auto_hwm_effective_message_bytes),
		AutoHwmControlBudgetBytes:           uint64(raw.auto_hwm_control_budget_bytes),
		AutoHwmRoutedBudgetBytes:            uint64(raw.auto_hwm_routed_budget_bytes),
		AutoHwmFanoutBudgetBytes:            uint64(raw.auto_hwm_fanout_budget_bytes),
		AutoHwmRecvIngressBudgetBytes:       uint64(raw.auto_hwm_recv_ingress_budget_bytes),
		AutoHwmControlActiveConnections:     uint32(raw.auto_hwm_control_active_connections),
		AutoHwmRoutedActiveConnections:      uint32(raw.auto_hwm_routed_active_connections),
		AutoHwmFanoutActiveConnections:      uint32(raw.auto_hwm_fanout_active_connections),
		AutoHwmRecvIngressActiveConnections: uint32(raw.auto_hwm_recv_ingress_active_connections),
		AutoHwmEstimatedMaxMemoryBytes:      uint64(raw.auto_hwm_estimated_max_memory_bytes),
		AutoHwmLastRecalcMs:                 uint64(raw.auto_hwm_last_recalc_ms),
		AutoHwmLastRecalcReason:             uint32(raw.auto_hwm_last_recalc_reason),
		AutoHwmSendBlockedRatioPPM:          uint32(raw.auto_hwm_send_blocked_ratio_ppm),
	}, nil
}

func (m *ServiceMonitor) OnEvent(handler func(*ServiceMonitorEvent)) error {
	if handler == nil {
		return &HandlerError{Result: HandlerInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	state := newServiceMonitorCallbackState(handler)
	handle := cgo.NewHandle(state)
	if err := handlerErrorFromResult(C.zlink_service_monitor_handler_go_local(m.handle, C.uintptr_t(handle))); err != nil {
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

func serviceMonitorEventFromC(raw C.zlink_service_monitor_event_t) *ServiceMonitorEvent {
	return &ServiceMonitorEvent{
		ServiceKind: ServiceKind(raw.service_kind),
		EventType:   ServiceMonitorEventType(raw.event_type),
		Status:      uint32(raw.status),
		ErrorCode:   uint32(raw.error_code),
		Value:       uint64(raw.value),
		DetailFlags: uint32(raw.detail_flags),
		ServiceName: C.GoString(&raw.service_name[0]),
		Endpoint:    C.GoString(&raw.endpoint[0]),
		RoutingID:   routingIDFromC(raw.routing_id),
		Subject:     C.GoString(&raw.subject[0]),
		SubjectKind: SubjectKind(raw.subject_kind),
	}
}

//export goZlinkMonitorTrampoline
func goZlinkMonitorTrampoline(event *C.zlink_monitor_event_t, userdata C.uintptr_t) {
	value, ok := safeHandleValue(userdata)
	if !ok {
		return
	}
	state := value.(*monitorCallbackState)
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
	value, ok := safeHandleValue(userdata)
	if !ok {
		return
	}
	state := value.(*serviceMonitorCallbackState)
	payload := serviceMonitorEventFromC(*event)
	state.dispatcher.enqueue(&callbackTask{
		label: "service-monitor",
		invoke: func() {
			state.handler(payload)
		},
	})
}
