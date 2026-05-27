use std::ffi::{CStr, c_void};
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

pub const MONITOR_EVENT_ALL: SocketMonitorEventMask = SocketMonitorEventMask::ALL;
pub const MONITOR_EVENT_CONNECTION_READY: SocketMonitorEventMask =
    SocketMonitorEventMask::CONNECTION_READY;

use crate::ctx::AutoHwmRecalcReason;
use crate::error::{
    CloseError, ConfigError, HandlerError, RecvError, check_close_rc, check_config_rc,
    check_handler_rc, check_recv_rc,
};
use crate::ffi;
use crate::message::RoutingId;
use crate::socket::{
    DealerSocket, PairSocket, PubSocket, RouterSocket, StreamSocket, SubSocket,
    XPubSocket, XSubSocket,
};

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
    pub auto_hwm_profile: u32,
    pub auto_hwm_role: u32,
    pub auto_hwm_policy_class: u32,
    pub auto_hwm_unit_budget_bytes: u64,
    pub auto_hwm_size_cap: u32,
    pub auto_hwm_socket_message_slots: u64,
    pub auto_hwm_effective_message_bytes: u64,
    pub auto_hwm_applied_sndhwm: i32,
    pub auto_hwm_applied_rcvhwm: i32,
    pub auto_hwm_effective_sndbuf: i32,
    pub auto_hwm_effective_rcvbuf: i32,
    pub auto_hwm_last_recalc_ms: u64,
    pub auto_hwm_last_recalc_reason: AutoHwmRecalcReason,
    pub auto_hwm_send_blocked_ratio_ppm: u32,
    pub auto_hwm_deferred_sndhwm: i32,
    pub auto_hwm_deferred_rcvhwm: i32,
}

impl MonitorSnapshot {
    pub(crate) fn from_raw(raw: &ffi::zlink_monitor_snapshot_t) -> Self {
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
            auto_hwm_profile: raw.auto_hwm_profile,
            auto_hwm_role: raw.auto_hwm_role,
            auto_hwm_policy_class: raw.auto_hwm_policy_class,
            auto_hwm_unit_budget_bytes: raw.auto_hwm_unit_budget_bytes,
            auto_hwm_size_cap: raw.auto_hwm_size_cap,
            auto_hwm_socket_message_slots: raw.auto_hwm_socket_message_slots,
            auto_hwm_effective_message_bytes: raw.auto_hwm_effective_message_bytes,
            auto_hwm_applied_sndhwm: raw.auto_hwm_applied_sndhwm,
            auto_hwm_applied_rcvhwm: raw.auto_hwm_applied_rcvhwm,
            auto_hwm_effective_sndbuf: raw.auto_hwm_effective_sndbuf,
            auto_hwm_effective_rcvbuf: raw.auto_hwm_effective_rcvbuf,
            auto_hwm_last_recalc_ms: raw.auto_hwm_last_recalc_ms,
            auto_hwm_last_recalc_reason: AutoHwmRecalcReason::from_raw(
                raw.auto_hwm_last_recalc_reason,
            ),
            auto_hwm_send_blocked_ratio_ppm: raw.auto_hwm_send_blocked_ratio_ppm,
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
        self.recv_with_flags(crate::flags::RecvFlags::NONE)
            .and_then(|opt| {
                opt.ok_or_else(|| RecvError::new(crate::error::RecvResult::NoData, libc::EAGAIN))
            })
    }

    /// Non-blocking receive of a monitor event. Returns `Ok(None)` when no event is available.
    pub fn recv_with_flags(
        &self,
        flags: crate::flags::RecvFlags,
    ) -> Result<Option<MonitorEvent>, RecvError> {
        use crate::error::RecvResult;
        let mut raw = MaybeUninit::<ffi::zlink_socket_monitor_event_t>::uninit();
        let rc =
            unsafe { ffi::zlink_socket_monitor_recv(self.handle, raw.as_mut_ptr(), flags.bits()) };
        if rc == RecvResult::NoData as i32 {
            return Ok(None);
        }
        check_recv_rc(rc)?;
        let val = unsafe { raw.assume_init() };
        Ok(Some(MonitorEvent::from_raw(&val)))
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
