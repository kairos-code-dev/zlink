use std::ffi::{CStr, c_void};
use std::marker::PhantomData;
use std::mem::MaybeUninit;

/// Bitmask to subscribe to all socket monitor events.
pub const MONITOR_EVENT_ALL: u32 = ffi::ZLINK_SOCKET_MONITOR_EVENT_ALL;

/// Bitmask for connection-ready-changed monitor events.
pub const MONITOR_EVENT_CONNECTION_READY_CHANGED: u32 =
    ffi::ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY_CHANGED;

/// Bitmask for discovery ready-changed service monitor events.
pub const SERVICE_MONITOR_EVENT_DISCOVERY_READY_CHANGED: u32 =
    ffi::ZLINK_DISCOVERY_MONITOR_EVENT_READY_CHANGED;

/// Bitmask for discovery error service monitor events.
pub const SERVICE_MONITOR_EVENT_DISCOVERY_ERROR: u32 = ffi::ZLINK_DISCOVERY_MONITOR_EVENT_ERROR;

/// Bitmask for discovery service-up monitor events.
pub const SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_UP: u32 =
    ffi::ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_UP;

/// Bitmask for discovery service-down monitor events.
pub const SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_DOWN: u32 =
    ffi::ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_DOWN;

/// Bitmask for discovery providers-changed monitor events.
pub const SERVICE_MONITOR_EVENT_DISCOVERY_PROVIDERS_CHANGED: u32 =
    ffi::ZLINK_DISCOVERY_MONITOR_EVENT_PROVIDERS_CHANGED;

/// Bitmask for discovery closed monitor events.
pub const SERVICE_MONITOR_EVENT_DISCOVERY_CLOSED: u32 = ffi::ZLINK_DISCOVERY_MONITOR_EVENT_CLOSED;

/// Bitmask for SPOT ready-changed service monitor events.
pub const SERVICE_MONITOR_EVENT_SPOT_READY_CHANGED: u32 =
    ffi::ZLINK_SPOT_MONITOR_EVENT_READY_CHANGED;

/// Bitmask for SPOT peer-up service monitor events.
pub const SERVICE_MONITOR_EVENT_SPOT_PEER_UP: u32 = ffi::ZLINK_SPOT_MONITOR_EVENT_PEER_UP;

/// Bitmask for SPOT peer-down service monitor events.
pub const SERVICE_MONITOR_EVENT_SPOT_PEER_DOWN: u32 = ffi::ZLINK_SPOT_MONITOR_EVENT_PEER_DOWN;

/// Bitmask for SPOT error service monitor events.
pub const SERVICE_MONITOR_EVENT_SPOT_ERROR: u32 = ffi::ZLINK_SPOT_MONITOR_EVENT_ERROR;

/// Bitmask for SPOT filter-applied service monitor events.
pub const SERVICE_MONITOR_EVENT_SPOT_FILTER_APPLIED: u32 =
    ffi::ZLINK_SPOT_MONITOR_EVENT_SUB_FILTER_APPLIED;

/// Bitmask for SPOT first-delivery-ready service monitor events.
pub const SERVICE_MONITOR_EVENT_SPOT_FIRST_DELIVERY_READY_CHANGED: u32 =
    ffi::ZLINK_SPOT_MONITOR_EVENT_PUB_FIRST_DELIVERY_READY_CHANGED;

/// Bitmask for SPOT queue-full service monitor events.
pub const SERVICE_MONITOR_EVENT_SPOT_PUB_QUEUE_FULL: u32 =
    ffi::ZLINK_SPOT_MONITOR_EVENT_PUB_QUEUE_FULL;

/// Bitmask for SPOT queue-drained service monitor events.
pub const SERVICE_MONITOR_EVENT_SPOT_PUB_QUEUE_DRAINED: u32 =
    ffi::ZLINK_SPOT_MONITOR_EVENT_PUB_QUEUE_DRAINED;

/// Bitmask for SPOT closed service monitor events.
pub const SERVICE_MONITOR_EVENT_SPOT_CLOSED: u32 = ffi::ZLINK_SPOT_MONITOR_EVENT_CLOSED;

/// Bitmask for SPOT publisher-delivery-ready service monitor events.
pub const SERVICE_MONITOR_EVENT_SPOT_PUB_DELIVERY_READY_CHANGED: u32 =
    ffi::ZLINK_SPOT_MONITOR_EVENT_PUB_DELIVERY_READY_CHANGED;

/// Bitmask for SPOT subscriber-delivery-ready service monitor events.
pub const SERVICE_MONITOR_EVENT_SPOT_SUB_DELIVERY_READY_CHANGED: u32 =
    ffi::ZLINK_SPOT_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED;

/// Bitmask for SPOT subscription-ready-changed service monitor events.
pub const SERVICE_MONITOR_EVENT_SPOT_SUBSCRIPTION_READY_CHANGED: u32 =
    ffi::ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY_CHANGED;

use crate::error::{ZlinkError, check_rc};
use crate::ffi;
use crate::message::RoutingId;
use crate::service::ServiceKind;
use crate::service::{Discovery, Spot, SpotNode};
use crate::socket::*;

/// Typed monitor target for socket monitor observation.
pub struct MonitorTarget<'a> {
    handle: *mut c_void,
    _marker: PhantomData<&'a ()>,
}

impl<'a> MonitorTarget<'a> {
    fn raw(&self) -> *mut c_void {
        self.handle
    }
}

/// Typed service monitor target for discovery/spot observation.
pub struct ServiceMonitorTarget<'a> {
    handle: *mut c_void,
    _marker: PhantomData<&'a ()>,
}

impl<'a> ServiceMonitorTarget<'a> {
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

impl<'a> From<&'a SpotNode> for ServiceMonitorTarget<'a> {
    fn from(target: &'a SpotNode) -> Self {
        Self {
            handle: target.raw(),
            _marker: PhantomData,
        }
    }
}

impl<'a> From<&'a Spot> for ServiceMonitorTarget<'a> {
    fn from(target: &'a Spot) -> Self {
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
    pub event: u64,
    pub value: u64,
    pub routing_id: RoutingId,
    pub local_addr: String,
    pub remote_addr: String,
}

impl MonitorEvent {
    fn from_raw(raw: &ffi::zlink_monitor_event_t) -> Self {
        Self {
            event: raw.event,
            value: raw.value,
            routing_id: RoutingId::from_raw(raw.routing_id),
            local_addr: cstr_array_to_string(&raw.local_addr),
            remote_addr: cstr_array_to_string(&raw.remote_addr),
        }
    }

    pub fn is_connected(&self) -> bool {
        self.event & ffi::ZLINK_SOCKET_MONITOR_EVENT_CONNECTED as u64 != 0
    }

    pub fn is_disconnected(&self) -> bool {
        self.event & ffi::ZLINK_SOCKET_MONITOR_EVENT_DISCONNECTED as u64 != 0
    }

    pub fn is_listening(&self) -> bool {
        self.event & ffi::ZLINK_SOCKET_MONITOR_EVENT_LISTENING as u64 != 0
    }

    pub fn is_accepted(&self) -> bool {
        self.event & ffi::ZLINK_SOCKET_MONITOR_EVENT_ACCEPTED as u64 != 0
    }

    pub fn is_closed(&self) -> bool {
        self.event & ffi::ZLINK_SOCKET_MONITOR_EVENT_CLOSED as u64 != 0
    }

    pub fn is_connection_ready_changed(&self) -> bool {
        self.event & ffi::ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY_CHANGED as u64 != 0
    }
}

// ---------------------------------------------------------------------------
// MonitorSnapshot
// ---------------------------------------------------------------------------

/// A point-in-time snapshot of a monitored entity's state.
#[derive(Debug, Clone)]
pub struct MonitorSnapshot {
    pub state_flags: u32,
    pub detail_flags: u32,
    pub ready_count: u32,
    pub snd_pending_msgs: u64,
    pub rcv_pending_msgs: u64,
}

impl MonitorSnapshot {
    fn from_raw(raw: &ffi::zlink_monitor_snapshot_t) -> Self {
        Self {
            state_flags: raw.state_flags,
            detail_flags: raw.detail_flags,
            ready_count: raw.ready_count,
            snd_pending_msgs: raw.snd_pending_msgs,
            rcv_pending_msgs: raw.rcv_pending_msgs,
        }
    }

    pub fn is_ready(&self) -> bool {
        self.state_flags & ffi::ZLINK_MONITOR_STATE_READY != 0
    }

    pub fn is_send_ready(&self) -> bool {
        self.state_flags & ffi::ZLINK_MONITOR_STATE_SEND_READY != 0
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
/// with the data plane. It supports both blocking `recv` and non-blocking
/// `try_recv` following the canonical naming policy.
pub struct SocketMonitor {
    handle: *mut c_void,
    _cb: Option<super::socket::CallbackBox>,
}

unsafe impl Send for SocketMonitor {}

impl SocketMonitor {
    /// Open a socket monitor for any socket type.
    ///
    /// `events` is a bitmask of `ZLINK_SOCKET_MONITOR_EVENT_*` constants.
    /// Use `MONITOR_EVENT_ALL` for all events.
    pub fn open<'a>(socket: impl Into<MonitorTarget<'a>>, events: u32) -> Result<Self, ZlinkError> {
        let target = socket.into();
        let opts = ffi::zlink_socket_monitor_open_options_t { events };
        let handle = unsafe { ffi::zlink_socket_monitor_open(target.raw(), &opts) };
        if handle.is_null() {
            return Err(ZlinkError::last());
        }
        Ok(Self { handle, _cb: None })
    }

    /// Blocking receive of a monitor event.
    pub fn recv(&self) -> Result<MonitorEvent, ZlinkError> {
        let mut raw = MaybeUninit::<ffi::zlink_socket_monitor_event_t>::uninit();
        check_rc(unsafe { ffi::zlink_socket_monitor_recv(self.handle, raw.as_mut_ptr(), 0) })?;
        let val = unsafe { raw.assume_init() };
        Ok(MonitorEvent::from_raw(&val))
    }

    /// Non-blocking receive. Returns `None` if no event is pending.
    pub fn try_recv(&self) -> Result<Option<MonitorEvent>, ZlinkError> {
        let mut raw = MaybeUninit::<ffi::zlink_socket_monitor_event_t>::uninit();
        let rc = unsafe {
            ffi::zlink_socket_monitor_recv(self.handle, raw.as_mut_ptr(), ffi::ZLINK_DONTWAIT)
        };
        if rc == -1 {
            let errno = unsafe { ffi::zlink_errno() };
            if errno == libc::EAGAIN {
                return Ok(None);
            }
            return Err(ZlinkError::last());
        }
        let val = unsafe { raw.assume_init() };
        Ok(Some(MonitorEvent::from_raw(&val)))
    }

    /// Read the current state snapshot.
    pub fn snapshot(&self) -> Result<MonitorSnapshot, ZlinkError> {
        let mut raw = MaybeUninit::<ffi::zlink_monitor_snapshot_t>::uninit();
        check_rc(unsafe { ffi::zlink_monitor_snapshot(self.handle, raw.as_mut_ptr()) })?;
        let val = unsafe { raw.assume_init() };
        Ok(MonitorSnapshot::from_raw(&val))
    }

    /// Install a callback handler for monitor events.
    pub fn on_event<F>(&mut self, handler: F) -> Result<(), ZlinkError>
    where
        F: Fn(MonitorEvent) + Send + 'static,
    {
        let (cb, userdata) = super::socket::CallbackBox::new(handler);

        unsafe extern "C" fn trampoline<F: Fn(MonitorEvent) + Send + 'static>(
            event: *const ffi::zlink_monitor_event_t,
            userdata: *mut c_void,
        ) {
            let handler = unsafe { &*(userdata as *const F) };
            let ev = MonitorEvent::from_raw(unsafe { &*event });
            handler(ev);
        }

        let rc =
            unsafe { ffi::zlink_socket_monitor_handler(self.handle, trampoline::<F>, userdata) };
        if rc == -1 {
            drop(cb);
            return Err(ZlinkError::last());
        }
        self._cb = Some(cb);
        Ok(())
    }

    #[allow(dead_code)]
    pub(crate) fn raw(&self) -> *mut c_void {
        self.handle
    }

    pub fn close(&mut self) -> Result<(), ZlinkError> {
        if self.handle.is_null() {
            return Ok(());
        }
        let mut h = self.handle;
        check_rc(unsafe { ffi::zlink_monitor_close(&mut h) })?;
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

/// A service/discovery/spot monitor event.
#[derive(Debug, Clone)]
pub struct ServiceEvent {
    pub service_kind: ServiceKind,
    pub event_type: ServiceEventType,
    pub status: i32,
    pub error_code: i32,
    pub value: u32,
    pub detail_flags: u32,
    pub service_name: String,
    pub endpoint: String,
    pub routing_id: RoutingId,
    pub subject: String,
    pub subject_kind: u32,
}

/// Typed service monitor event kind set.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ServiceEventType(u32);

impl ServiceEventType {
    pub fn bits(self) -> u32 {
        self.0
    }

    pub fn contains(self, mask: u32) -> bool {
        self.0 & mask != 0
    }

    pub fn is_discovery_ready_changed(self) -> bool {
        self.contains(SERVICE_MONITOR_EVENT_DISCOVERY_READY_CHANGED)
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

    pub fn is_spot_ready_changed(self) -> bool {
        self.contains(SERVICE_MONITOR_EVENT_SPOT_READY_CHANGED)
    }

    pub fn is_spot_peer_up(self) -> bool {
        self.contains(SERVICE_MONITOR_EVENT_SPOT_PEER_UP)
    }

    pub fn is_spot_peer_down(self) -> bool {
        self.contains(SERVICE_MONITOR_EVENT_SPOT_PEER_DOWN)
    }

    pub fn is_spot_error(self) -> bool {
        self.contains(SERVICE_MONITOR_EVENT_SPOT_ERROR)
    }

    pub fn is_spot_filter_applied(self) -> bool {
        self.contains(SERVICE_MONITOR_EVENT_SPOT_FILTER_APPLIED)
    }

    pub fn is_spot_first_delivery_ready_changed(self) -> bool {
        self.contains(SERVICE_MONITOR_EVENT_SPOT_FIRST_DELIVERY_READY_CHANGED)
    }

    pub fn is_spot_pub_queue_full(self) -> bool {
        self.contains(SERVICE_MONITOR_EVENT_SPOT_PUB_QUEUE_FULL)
    }

    pub fn is_spot_pub_queue_drained(self) -> bool {
        self.contains(SERVICE_MONITOR_EVENT_SPOT_PUB_QUEUE_DRAINED)
    }

    pub fn is_spot_closed(self) -> bool {
        self.contains(SERVICE_MONITOR_EVENT_SPOT_CLOSED)
    }

    pub fn is_spot_pub_delivery_ready_changed(self) -> bool {
        self.contains(SERVICE_MONITOR_EVENT_SPOT_PUB_DELIVERY_READY_CHANGED)
    }

    pub fn is_spot_sub_delivery_ready_changed(self) -> bool {
        self.contains(SERVICE_MONITOR_EVENT_SPOT_SUB_DELIVERY_READY_CHANGED)
    }

    pub fn is_spot_subscription_ready_changed(self) -> bool {
        self.contains(SERVICE_MONITOR_EVENT_SPOT_SUBSCRIPTION_READY_CHANGED)
    }
}

impl ServiceEvent {
    fn from_raw(raw: &ffi::zlink_service_event_t) -> Self {
        Self {
            service_kind: ServiceKind::from_raw(raw.service_kind),
            event_type: ServiceEventType(raw.event_type),
            status: raw.status,
            error_code: raw.error_code,
            value: raw.value,
            detail_flags: raw.detail_flags,
            service_name: cstr_array_to_string(&raw.service_name),
            endpoint: cstr_array_to_string(&raw.endpoint),
            routing_id: RoutingId::from_raw(raw.routing_id),
            subject: cstr_array_to_string(&raw.subject),
            subject_kind: raw.subject_kind,
        }
    }

    pub fn is_spot_filter_applied(&self) -> bool {
        self.event_type.is_spot_filter_applied()
    }

    pub fn is_spot_subscription_ready_changed(&self) -> bool {
        self.event_type.is_spot_subscription_ready_changed()
    }

    pub fn is_spot_first_delivery_ready_changed(&self) -> bool {
        self.event_type.is_spot_first_delivery_ready_changed()
    }
}

// ---------------------------------------------------------------------------
// ServiceMonitor
// ---------------------------------------------------------------------------

/// Monitor handle for service/discovery/spot lifecycle events.
pub struct ServiceMonitor {
    handle: *mut c_void,
    _cb: Option<super::socket::CallbackBox>,
}

unsafe impl Send for ServiceMonitor {}

impl ServiceMonitor {
    /// Open a service monitor on a discovery, spot, or spot-node handle.
    pub fn open<'a>(
        target: impl Into<ServiceMonitorTarget<'a>>,
        events: u32,
    ) -> Result<Self, ZlinkError> {
        let target = target.into();
        let opts = ffi::zlink_service_monitor_open_options_t { events };
        let handle = unsafe { ffi::zlink_service_monitor_open(target.raw(), &opts) };
        if handle.is_null() {
            return Err(ZlinkError::last());
        }
        Ok(Self { handle, _cb: None })
    }

    /// Blocking receive of a service event.
    pub fn recv(&self) -> Result<ServiceEvent, ZlinkError> {
        loop {
            let mut raw = MaybeUninit::<ffi::zlink_service_monitor_event_t>::uninit();
            let rc = unsafe { ffi::zlink_service_monitor_recv(self.handle, raw.as_mut_ptr(), 0) };
            if rc == 0 {
                let val = unsafe { raw.assume_init() };
                return Ok(ServiceEvent::from_raw(&val));
            }

            let errno = unsafe { ffi::zlink_errno() };
            if errno != libc::EAGAIN {
                return Err(ZlinkError::last());
            }

            // The core service-monitor path can transiently surface EAGAIN
            // even on the blocking entrypoint. Keep the public Rust surface
            // blocking and only normalize that one case here.
            std::thread::yield_now();
        }
    }

    /// Non-blocking receive. Returns `None` if no event is pending.
    pub fn try_recv(&self) -> Result<Option<ServiceEvent>, ZlinkError> {
        let mut raw = MaybeUninit::<ffi::zlink_service_monitor_event_t>::uninit();
        let rc = unsafe {
            ffi::zlink_service_monitor_recv(self.handle, raw.as_mut_ptr(), ffi::ZLINK_DONTWAIT)
        };
        if rc == -1 {
            let errno = unsafe { ffi::zlink_errno() };
            if errno == libc::EAGAIN {
                return Ok(None);
            }
            return Err(ZlinkError::last());
        }
        let val = unsafe { raw.assume_init() };
        Ok(Some(ServiceEvent::from_raw(&val)))
    }

    /// Read the current state snapshot.
    pub fn snapshot(&self) -> Result<MonitorSnapshot, ZlinkError> {
        let mut raw = MaybeUninit::<ffi::zlink_monitor_snapshot_t>::uninit();
        check_rc(unsafe { ffi::zlink_monitor_snapshot(self.handle, raw.as_mut_ptr()) })?;
        let val = unsafe { raw.assume_init() };
        Ok(MonitorSnapshot::from_raw(&val))
    }

    /// Install a callback handler for service events.
    pub fn on_event<F>(&mut self, handler: F) -> Result<(), ZlinkError>
    where
        F: Fn(ServiceEvent) + Send + 'static,
    {
        let (cb, userdata) = super::socket::CallbackBox::new(handler);

        unsafe extern "C" fn trampoline<F: Fn(ServiceEvent) + Send + 'static>(
            event: *const ffi::zlink_service_event_t,
            userdata: *mut c_void,
        ) {
            let handler = unsafe { &*(userdata as *const F) };
            let ev = ServiceEvent::from_raw(unsafe { &*event });
            handler(ev);
        }

        let rc =
            unsafe { ffi::zlink_service_monitor_handler(self.handle, trampoline::<F>, userdata) };
        if rc == -1 {
            drop(cb);
            return Err(ZlinkError::last());
        }
        self._cb = Some(cb);
        Ok(())
    }

    pub fn close(&mut self) -> Result<(), ZlinkError> {
        if self.handle.is_null() {
            return Ok(());
        }
        let mut h = self.handle;
        check_rc(unsafe { ffi::zlink_monitor_close(&mut h) })?;
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
