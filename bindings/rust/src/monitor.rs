use std::ffi::{c_void, CStr};
use std::marker::PhantomData;
use std::mem::MaybeUninit;
use std::ops::{BitOr, BitOrAssign};

/// Typed bitmask for socket monitor subscriptions.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SocketMonitorEventMask(u32);

impl SocketMonitorEventMask {
    pub const ALL: Self = Self(ffi::ZLINK_SOCKET_MONITOR_EVENT_ALL);
    pub const CONNECTION_READY: Self = Self(ffi::ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY);

    pub const fn bits(self) -> u32 {
        self.0
    }
}

impl Default for SocketMonitorEventMask {
    fn default() -> Self {
        Self::ALL
    }
}

impl BitOr for SocketMonitorEventMask {
    type Output = Self;

    fn bitor(self, rhs: Self) -> Self::Output {
        Self(self.0 | rhs.0)
    }
}

impl BitOrAssign for SocketMonitorEventMask {
    fn bitor_assign(&mut self, rhs: Self) {
        self.0 |= rhs.0;
    }
}

/// Typed bitmask for discovery service monitor subscriptions.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ServiceMonitorEventMask(u32);

impl ServiceMonitorEventMask {
    pub const ALL: Self = Self(
        ffi::ZLINK_DISCOVERY_MONITOR_EVENT_ERROR
            | ffi::ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_UP
            | ffi::ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_DOWN
            | ffi::ZLINK_DISCOVERY_MONITOR_EVENT_PROVIDERS_CHANGED
            | ffi::ZLINK_DISCOVERY_MONITOR_EVENT_CLOSED,
    );
    pub const ERROR: Self = Self(ffi::ZLINK_DISCOVERY_MONITOR_EVENT_ERROR);
    pub const SERVICE_UP: Self = Self(ffi::ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_UP);
    pub const SERVICE_DOWN: Self = Self(ffi::ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_DOWN);
    pub const PROVIDERS_CHANGED: Self = Self(ffi::ZLINK_DISCOVERY_MONITOR_EVENT_PROVIDERS_CHANGED);
    pub const CLOSED: Self = Self(ffi::ZLINK_DISCOVERY_MONITOR_EVENT_CLOSED);

    pub const fn bits(self) -> u32 {
        self.0
    }
}

impl Default for ServiceMonitorEventMask {
    fn default() -> Self {
        Self::ALL
    }
}

impl BitOr for ServiceMonitorEventMask {
    type Output = Self;

    fn bitor(self, rhs: Self) -> Self::Output {
        Self(self.0 | rhs.0)
    }
}

impl BitOrAssign for ServiceMonitorEventMask {
    fn bitor_assign(&mut self, rhs: Self) {
        self.0 |= rhs.0;
    }
}

pub const MONITOR_EVENT_ALL: SocketMonitorEventMask = SocketMonitorEventMask::ALL;
pub const MONITOR_EVENT_CONNECTION_READY: SocketMonitorEventMask =
    SocketMonitorEventMask::CONNECTION_READY;
pub const SERVICE_MONITOR_EVENT_DISCOVERY_ERROR: ServiceMonitorEventMask =
    ServiceMonitorEventMask::ERROR;
pub const SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_UP: ServiceMonitorEventMask =
    ServiceMonitorEventMask::SERVICE_UP;
pub const SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_DOWN: ServiceMonitorEventMask =
    ServiceMonitorEventMask::SERVICE_DOWN;
pub const SERVICE_MONITOR_EVENT_DISCOVERY_PROVIDERS_CHANGED: ServiceMonitorEventMask =
    ServiceMonitorEventMask::PROVIDERS_CHANGED;
pub const SERVICE_MONITOR_EVENT_DISCOVERY_CLOSED: ServiceMonitorEventMask =
    ServiceMonitorEventMask::CLOSED;

use crate::error::{
    check_close_rc, check_config_rc, check_handler_rc, check_recv_rc, CloseError, ConfigError,
    HandlerError, RecvError,
};
use crate::ffi;
use crate::message::RoutingId;
use crate::service::Discovery;
use crate::service::ServiceKind;
use crate::socket::*;

fn ignore_monitor_event(_: &MonitorEvent) {}

/// Typed monitor target for socket monitor observation.
pub struct MonitorTarget<'a> {
    handle: *mut c_void,
    _marker: PhantomData<&'a ()>,
}

impl MonitorTarget<'_> {
    fn raw(&self) -> *mut c_void {
        self.handle
    }
}

/// Typed service monitor target for discovery observation.
pub struct ServiceMonitorTarget<'a> {
    handle: *mut c_void,
    _marker: PhantomData<&'a ()>,
}

impl ServiceMonitorTarget<'_> {
    fn raw(&self) -> *mut c_void {
        self.handle
    }
}

macro_rules! impl_monitor_target_from_ref {
    ($ty:ident) => {
        impl<'a> From<&'a $ty> for MonitorTarget<'a> {
            fn from(socket: &'a $ty) -> Self {
                Self {
                    handle: socket.inner.handle,
                    _marker: PhantomData,
                }
            }
        }
    };
}

impl_monitor_target_from_ref!(PairSocket);
impl_monitor_target_from_ref!(PubSocket);
impl_monitor_target_from_ref!(SubSocket);
impl_monitor_target_from_ref!(DealerSocket);
impl_monitor_target_from_ref!(RouterSocket);
impl_monitor_target_from_ref!(XPubSocket);
impl_monitor_target_from_ref!(XSubSocket);
impl_monitor_target_from_ref!(StreamSocket);

impl<'a> From<&'a Discovery> for ServiceMonitorTarget<'a> {
    fn from(target: &'a Discovery) -> Self {
        Self {
            handle: target.raw(),
            _marker: PhantomData,
        }
    }
}

// ---------------------------------------------------------------------------
// MonitorEvent – typed socket monitor event
// ---------------------------------------------------------------------------

/// A typed socket monitor event.
#[derive(Debug, Clone)]
pub struct MonitorEvent {
    pub event: MonitorEventType,
    pub value: u32,
    pub routing_id: Option<RoutingId>,
    pub local_addr: String,
    pub remote_addr: String,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MonitorEventType(pub u64);

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MonitorSourceKind {
    Socket,
    SpotPub,
    SpotSub,
}

impl MonitorEvent {
    fn from_raw(raw: &ffi::zlink_monitor_event_t) -> Self {
        Self {
            event: MonitorEventType(raw.event),
            value: raw.value as u32,
            routing_id: RoutingId::from_raw_optional(raw.routing_id),
            local_addr: cstr_array_to_string(&raw.local_addr),
            remote_addr: cstr_array_to_string(&raw.remote_addr),
        }
    }

    pub fn is_connected(&self) -> bool {
        self.event.0 & ffi::ZLINK_SOCKET_MONITOR_EVENT_CONNECTED as u64 != 0
    }

    pub fn is_disconnected(&self) -> bool {
        self.event.0 & ffi::ZLINK_SOCKET_MONITOR_EVENT_DISCONNECTED as u64 != 0
    }

    pub fn is_listening(&self) -> bool {
        self.event.0 & ffi::ZLINK_SOCKET_MONITOR_EVENT_LISTENING as u64 != 0
    }

    pub fn is_accepted(&self) -> bool {
        self.event.0 & ffi::ZLINK_SOCKET_MONITOR_EVENT_ACCEPTED as u64 != 0
    }

    pub fn is_closed(&self) -> bool {
        self.event.0 & ffi::ZLINK_SOCKET_MONITOR_EVENT_CLOSED as u64 != 0
    }

    pub fn is_connection_ready(&self) -> bool {
        self.event.0 & ffi::ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY as u64 != 0
    }
}

// ---------------------------------------------------------------------------
// MonitorSnapshot
// ---------------------------------------------------------------------------

/// A point-in-time snapshot of a monitored entity's state.
#[derive(Debug, Clone)]
pub struct MonitorSnapshot {
    pub source_kind: MonitorSourceKind,
    pub state_flags: u32,
    pub detail_flags: u32,
    pub snd_pending_msgs: u64,
    pub rcv_pending_msgs: u64,
    pub auto_hwm_enabled: bool,
    pub auto_hwm_role: u32,
    pub auto_hwm_managed_connections: u32,
    pub auto_hwm_active_hwm_connections: u32,
    pub auto_hwm_planning_transport_connections: u32,
    pub auto_hwm_base_floor_per_connection: u32,
    pub auto_hwm_applied_sndhwm: i32,
    pub auto_hwm_applied_rcvhwm: i32,
    pub auto_hwm_requested_sndbuf: i32,
    pub auto_hwm_requested_rcvbuf: i32,
    pub auto_hwm_effective_sndbuf: i32,
    pub auto_hwm_effective_rcvbuf: i32,
    pub auto_hwm_total_memory_budget_bytes: u64,
    pub auto_hwm_queue_budget_bytes: u64,
    pub auto_hwm_transport_budget_bytes: u64,
    pub auto_hwm_runtime_reserve_bytes: u64,
    pub auto_hwm_group_budget_bytes: u64,
    pub auto_hwm_group_message_slots: u64,
    pub auto_hwm_effective_message_bytes: u64,
    pub auto_hwm_control_budget_bytes: u64,
    pub auto_hwm_routed_budget_bytes: u64,
    pub auto_hwm_fanout_budget_bytes: u64,
    pub auto_hwm_recv_ingress_budget_bytes: u64,
    pub auto_hwm_control_active_connections: u32,
    pub auto_hwm_routed_active_connections: u32,
    pub auto_hwm_fanout_active_connections: u32,
    pub auto_hwm_recv_ingress_active_connections: u32,
    pub auto_hwm_estimated_max_memory_bytes: u64,
    pub auto_hwm_last_recalc_ms: u64,
    pub auto_hwm_last_recalc_reason: u32,
    pub auto_hwm_send_blocked_ratio_ppm: u32,
    pub auto_hwm_scope: u32,
    pub auto_hwm_scope_count: u32,
    pub auto_hwm_role_group_budget_bytes: u64,
    pub auto_hwm_scope_group_budget_bytes: u64,
    pub auto_hwm_auto_buffer_bytes: u64,
    pub auto_hwm_manual_buffer_bytes: u64,
    pub auto_hwm_buffer_connections: u32,
    pub auto_hwm_deferred_sndhwm: i32,
    pub auto_hwm_deferred_rcvhwm: i32,
}

impl MonitorSnapshot {
    fn from_raw(raw: &ffi::zlink_monitor_snapshot_t) -> Self {
        Self {
            source_kind: match raw.source_kind {
                ffi::zlink_monitor_source_kind_t::ZLINK_MONITOR_SOURCE_SOCKET => {
                    MonitorSourceKind::Socket
                }
                ffi::zlink_monitor_source_kind_t::ZLINK_MONITOR_SOURCE_SPOT_PUB => {
                    MonitorSourceKind::SpotPub
                }
                ffi::zlink_monitor_source_kind_t::ZLINK_MONITOR_SOURCE_SPOT_SUB => {
                    MonitorSourceKind::SpotSub
                }
            },
            state_flags: raw.state_flags,
            detail_flags: raw.detail_flags,
            snd_pending_msgs: raw.snd_pending_msgs,
            rcv_pending_msgs: raw.rcv_pending_msgs,
            auto_hwm_enabled: raw.auto_hwm_enabled != 0,
            auto_hwm_role: raw.auto_hwm_role,
            auto_hwm_managed_connections: raw.auto_hwm_managed_connections,
            auto_hwm_active_hwm_connections: raw.auto_hwm_active_hwm_connections,
            auto_hwm_planning_transport_connections: raw.auto_hwm_planning_transport_connections,
            auto_hwm_base_floor_per_connection: raw.auto_hwm_base_floor_per_connection,
            auto_hwm_applied_sndhwm: raw.auto_hwm_applied_sndhwm,
            auto_hwm_applied_rcvhwm: raw.auto_hwm_applied_rcvhwm,
            auto_hwm_requested_sndbuf: raw.auto_hwm_requested_sndbuf,
            auto_hwm_requested_rcvbuf: raw.auto_hwm_requested_rcvbuf,
            auto_hwm_effective_sndbuf: raw.auto_hwm_effective_sndbuf,
            auto_hwm_effective_rcvbuf: raw.auto_hwm_effective_rcvbuf,
            auto_hwm_total_memory_budget_bytes: raw.auto_hwm_total_memory_budget_bytes,
            auto_hwm_queue_budget_bytes: raw.auto_hwm_queue_budget_bytes,
            auto_hwm_transport_budget_bytes: raw.auto_hwm_transport_budget_bytes,
            auto_hwm_runtime_reserve_bytes: raw.auto_hwm_runtime_reserve_bytes,
            auto_hwm_group_budget_bytes: raw.auto_hwm_group_budget_bytes,
            auto_hwm_group_message_slots: raw.auto_hwm_group_message_slots,
            auto_hwm_effective_message_bytes: raw.auto_hwm_effective_message_bytes,
            auto_hwm_control_budget_bytes: raw.auto_hwm_control_budget_bytes,
            auto_hwm_routed_budget_bytes: raw.auto_hwm_routed_budget_bytes,
            auto_hwm_fanout_budget_bytes: raw.auto_hwm_fanout_budget_bytes,
            auto_hwm_recv_ingress_budget_bytes: raw.auto_hwm_recv_ingress_budget_bytes,
            auto_hwm_control_active_connections: raw.auto_hwm_control_active_connections,
            auto_hwm_routed_active_connections: raw.auto_hwm_routed_active_connections,
            auto_hwm_fanout_active_connections: raw.auto_hwm_fanout_active_connections,
            auto_hwm_recv_ingress_active_connections: raw.auto_hwm_recv_ingress_active_connections,
            auto_hwm_estimated_max_memory_bytes: raw.auto_hwm_estimated_max_memory_bytes,
            auto_hwm_last_recalc_ms: raw.auto_hwm_last_recalc_ms,
            auto_hwm_last_recalc_reason: raw.auto_hwm_last_recalc_reason,
            auto_hwm_send_blocked_ratio_ppm: raw.auto_hwm_send_blocked_ratio_ppm,
            auto_hwm_scope: raw.auto_hwm_scope,
            auto_hwm_scope_count: raw.auto_hwm_scope_count,
            auto_hwm_role_group_budget_bytes: raw.auto_hwm_role_group_budget_bytes,
            auto_hwm_scope_group_budget_bytes: raw.auto_hwm_scope_group_budget_bytes,
            auto_hwm_auto_buffer_bytes: raw.auto_hwm_auto_buffer_bytes,
            auto_hwm_manual_buffer_bytes: raw.auto_hwm_manual_buffer_bytes,
            auto_hwm_buffer_connections: raw.auto_hwm_buffer_connections,
            auto_hwm_deferred_sndhwm: raw.auto_hwm_deferred_sndhwm,
            auto_hwm_deferred_rcvhwm: raw.auto_hwm_deferred_rcvhwm,
        }
    }

    pub fn is_ready(&self) -> bool {
        self.state_flags & ffi::ZLINK_MONITOR_STATE_READY != 0
    }

    pub fn is_closed(&self) -> bool {
        self.state_flags & ffi::ZLINK_MONITOR_STATE_CLOSED != 0
    }
}

// ---------------------------------------------------------------------------
// SocketMonitor
// ---------------------------------------------------------------------------

/// Monitor handle for observing socket lifecycle and connection events.
///
/// The monitor is an independent observation plane that does not interfere
/// with the data plane. It uses `recv_with_flags(RecvFlags::DONT_WAIT)` for
/// non-blocking polling.
pub struct SocketMonitor {
    handle: *mut c_void,
    _cb: Option<super::socket::CallbackBox>,
}

unsafe impl Send for SocketMonitor {}

impl SocketMonitor {
    /// Open a socket monitor for all events.
    pub fn open<'a>(socket: impl Into<MonitorTarget<'a>>) -> Result<Self, ConfigError> {
        Self::open_with_events(socket, SocketMonitorEventMask::ALL)
    }

    /// Open a socket monitor with an explicit typed event mask.
    pub(crate) fn open_with_events<'a>(
        socket: impl Into<MonitorTarget<'a>>,
        events: SocketMonitorEventMask,
    ) -> Result<Self, ConfigError> {
        let target = socket.into();
        let opts = ffi::zlink_socket_monitor_open_options_t {
            events: events.bits(),
        };
        let handle = unsafe { ffi::zlink_socket_monitor_open(target.raw(), &opts) };
        if handle.is_null() {
            return Err(crate::error::ConfigError::new(
                crate::error::ConfigResult::InvalidArgument,
                crate::error::last_errno(),
            ));
        }
        Ok(Self { handle, _cb: None })
    }

    /// Blocking receive of a monitor event.
    pub fn recv(&self) -> Result<MonitorEvent, RecvError> {
        let mut raw = MaybeUninit::<ffi::zlink_socket_monitor_event_t>::uninit();
        check_recv_rc(unsafe { ffi::zlink_socket_monitor_recv(self.handle, raw.as_mut_ptr(), 0) })?;
        let val = unsafe { raw.assume_init() };
        Ok(MonitorEvent::from_raw(&val))
    }

    /// Read the current state snapshot.
    pub fn snapshot(&self) -> Result<MonitorSnapshot, ConfigError> {
        let mut raw = MaybeUninit::<ffi::zlink_monitor_snapshot_t>::uninit();
        check_config_rc(unsafe { ffi::zlink_monitor_snapshot(self.handle, raw.as_mut_ptr()) })?;
        let val = unsafe { raw.assume_init() };
        Ok(MonitorSnapshot::from_raw(&val))
    }

    /// Install a callback handler for monitor events.
    pub fn on_event<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn(&MonitorEvent) + Send + 'static,
    {
        let (cb, userdata) = super::socket::CallbackBox::new(handler);

        unsafe extern "C" fn trampoline<F: Fn(&MonitorEvent) + Send + 'static>(
            event: *const ffi::zlink_monitor_event_t,
            userdata: *mut c_void,
        ) {
            let handler = unsafe { &*(userdata as *const F) };
            let ev = MonitorEvent::from_raw(unsafe { &*event });
            handler(&ev);
        }

        let rc =
            unsafe { ffi::zlink_socket_monitor_handler(self.handle, trampoline::<F>, userdata) };
        if rc != 0 {
            drop(cb);
            return Err(check_handler_rc(rc).unwrap_err());
        }
        self._cb = Some(cb);
        Ok(())
    }

    pub fn ignore_handler() -> fn(&MonitorEvent) {
        ignore_monitor_event
    }

    #[allow(dead_code)]
    pub(crate) fn raw(&self) -> *mut c_void {
        self.handle
    }

    pub fn close(&mut self) -> Result<(), CloseError> {
        if self.handle.is_null() {
            return Ok(());
        }
        let mut h = self.handle;
        check_close_rc(unsafe { ffi::zlink_monitor_close(&mut h) })?;
        self.handle = std::ptr::null_mut();
        self._cb = None;
        Ok(())
    }
}

impl Drop for SocketMonitor {
    fn drop(&mut self) {
        if !self.handle.is_null() {
            unsafe {
                let mut h = self.handle;
                ffi::zlink_monitor_close(&mut h);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// ServiceEvent – typed service monitor event
// ---------------------------------------------------------------------------

/// A discovery service monitor event.
#[derive(Debug, Clone)]
pub struct ServiceEvent {
    pub service_kind: ServiceKind,
    pub event_type: ServiceEventType,
    pub status: u32,
    pub error_code: u32,
    pub value: u64,
    pub detail_flags: u32,
    pub service_name: String,
    pub endpoint: String,
    pub routing_id: Option<RoutingId>,
    pub subject: String,
    pub subject_kind: SubjectKind,
}

/// Typed discovery service monitor event kind set.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ServiceEventType(u32);

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SubjectKind {
    None,
    Topic,
    Pattern,
}

impl ServiceEventType {
    pub fn bits(self) -> u32 {
        self.0
    }

    pub fn contains(self, mask: ServiceMonitorEventMask) -> bool {
        self.0 & mask.bits() != 0
    }

    pub fn is_discovery_error(self) -> bool {
        self.contains(SERVICE_MONITOR_EVENT_DISCOVERY_ERROR)
    }

    pub fn is_discovery_service_up(self) -> bool {
        self.contains(SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_UP)
    }

    pub fn is_discovery_service_down(self) -> bool {
        self.contains(SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_DOWN)
    }

    pub fn is_discovery_providers_changed(self) -> bool {
        self.contains(SERVICE_MONITOR_EVENT_DISCOVERY_PROVIDERS_CHANGED)
    }

    pub fn is_discovery_closed(self) -> bool {
        self.contains(SERVICE_MONITOR_EVENT_DISCOVERY_CLOSED)
    }
}

impl ServiceEvent {
    fn from_raw(raw: &ffi::zlink_service_event_t) -> Self {
        Self {
            service_kind: ServiceKind::from_raw(raw.service_kind),
            event_type: ServiceEventType(raw.event_type),
            status: raw.status as u32,
            error_code: raw.error_code as u32,
            value: raw.value as u64,
            detail_flags: raw.detail_flags,
            service_name: cstr_array_to_string(&raw.service_name),
            endpoint: cstr_array_to_string(&raw.endpoint),
            routing_id: RoutingId::from_raw_optional(raw.routing_id),
            subject: cstr_array_to_string(&raw.subject),
            subject_kind: match raw.subject_kind {
                1 => SubjectKind::Topic,
                2 => SubjectKind::Pattern,
                _ => SubjectKind::None,
            },
        }
    }
}

// ---------------------------------------------------------------------------
// ServiceMonitor
// ---------------------------------------------------------------------------

/// Monitor handle for discovery service lifecycle events.
pub struct ServiceMonitor {
    handle: *mut c_void,
    _cb: Option<super::socket::CallbackBox>,
}

unsafe impl Send for ServiceMonitor {}

impl ServiceMonitor {
    /// Open a service monitor on a discovery handle for all events.
    pub fn open<'a>(target: impl Into<ServiceMonitorTarget<'a>>) -> Result<Self, ConfigError> {
        Self::open_with_events(target, ServiceMonitorEventMask::ALL)
    }

    /// Open a service monitor with an explicit typed event mask.
    pub(crate) fn open_with_events<'a>(
        target: impl Into<ServiceMonitorTarget<'a>>,
        events: ServiceMonitorEventMask,
    ) -> Result<Self, ConfigError> {
        let target = target.into();
        let opts = ffi::zlink_service_monitor_open_options_t {
            events: events.bits(),
        };
        let handle = unsafe { ffi::zlink_service_monitor_open(target.raw(), &opts) };
        if handle.is_null() {
            return Err(crate::error::ConfigError::new(
                crate::error::ConfigResult::InvalidArgument,
                crate::error::last_errno(),
            ));
        }
        Ok(Self { handle, _cb: None })
    }

    /// Blocking receive of a service event.
    pub fn recv(&self) -> Result<ServiceEvent, RecvError> {
        let mut raw = MaybeUninit::<ffi::zlink_service_monitor_event_t>::uninit();
        check_recv_rc(unsafe {
            ffi::zlink_service_monitor_recv(self.handle, raw.as_mut_ptr(), 0)
        })?;
        let val = unsafe { raw.assume_init() };
        Ok(ServiceEvent::from_raw(&val))
    }

    /// Read the current state snapshot.
    pub fn snapshot(&self) -> Result<MonitorSnapshot, ConfigError> {
        let mut raw = MaybeUninit::<ffi::zlink_monitor_snapshot_t>::uninit();
        check_config_rc(unsafe { ffi::zlink_monitor_snapshot(self.handle, raw.as_mut_ptr()) })?;
        let val = unsafe { raw.assume_init() };
        Ok(MonitorSnapshot::from_raw(&val))
    }

    /// Install a callback handler for service events.
    pub fn on_event<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn(&ServiceEvent) + Send + 'static,
    {
        let (cb, userdata) = super::socket::CallbackBox::new(handler);

        unsafe extern "C" fn trampoline<F: Fn(&ServiceEvent) + Send + 'static>(
            event: *const ffi::zlink_service_event_t,
            userdata: *mut c_void,
        ) {
            let handler = unsafe { &*(userdata as *const F) };
            let ev = ServiceEvent::from_raw(unsafe { &*event });
            handler(&ev);
        }

        let rc =
            unsafe { ffi::zlink_service_monitor_handler(self.handle, trampoline::<F>, userdata) };
        if rc != 0 {
            drop(cb);
            return Err(check_handler_rc(rc).unwrap_err());
        }
        self._cb = Some(cb);
        Ok(())
    }

    pub fn close(&mut self) -> Result<(), CloseError> {
        if self.handle.is_null() {
            return Ok(());
        }
        let mut h = self.handle;
        check_close_rc(unsafe { ffi::zlink_monitor_close(&mut h) })?;
        self.handle = std::ptr::null_mut();
        self._cb = None;
        Ok(())
    }
}

impl Drop for ServiceMonitor {
    fn drop(&mut self) {
        if !self.handle.is_null() {
            unsafe {
                let mut h = self.handle;
                ffi::zlink_monitor_close(&mut h);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

fn cstr_array_to_string(buf: &[i8]) -> String {
    unsafe {
        let ptr = buf.as_ptr();
        if *ptr == 0 {
            String::new()
        } else {
            CStr::from_ptr(ptr).to_string_lossy().into_owned()
        }
    }
}
