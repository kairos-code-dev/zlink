use std::any::Any;
use std::ops::{BitOr, BitOrAssign};

use crate::core_context::AutoHwmRecalcReason;
use crate::error::{CloseError, ConfigError, HandlerError, RecvError};
use crate::flags::RecvFlags;
use crate::routing_id::RoutingId;

pub trait Monitorable: Any {
    fn as_any(&self) -> &dyn Any;
}

pub(crate) trait SocketMonitorRuntime: Send {
    fn recv(&self) -> Result<MonitorEvent, RecvError>;
    fn recv_with_flags(&self, flags: RecvFlags) -> Result<Option<MonitorEvent>, RecvError>;
    fn status(&self) -> Result<MonitorStatus, ConfigError>;
    fn on_event(&mut self, handler: Box<dyn Fn(&MonitorEvent) + Send>) -> Result<(), HandlerError>;
    fn close(&mut self) -> Result<(), CloseError>;
}

/// Typed bitmask for socket monitor subscriptions.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SocketMonitorEventMask(u32);

impl SocketMonitorEventMask {
    pub const ALL: Self = Self(0x7FFF);
    pub const CONNECTION_READY: Self = Self(0x1000);

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

/// A typed socket monitor event.
#[derive(Debug, Clone)]
pub struct MonitorEvent {
    pub event: MonitorEventType,
    pub value: u32,
    pub routing_id: Option<RoutingId>,
    pub local_addr: String,
    pub remote_addr: String,
}

impl MonitorEvent {
    pub fn is_connected(&self) -> bool {
        self.event.0 & 0x0001 != 0
    }

    pub fn is_disconnected(&self) -> bool {
        self.event.0 & 0x0200 != 0
    }

    pub fn is_listening(&self) -> bool {
        self.event.0 & 0x0008 != 0
    }

    pub fn is_accepted(&self) -> bool {
        self.event.0 & 0x0020 != 0
    }

    pub fn is_closed(&self) -> bool {
        self.event.0 & 0x0080 != 0
    }

    pub fn is_connection_ready(&self) -> bool {
        self.event.0 & 0x1000 != 0
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MonitorEventType(pub u64);

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MonitorSourceKind {
    Socket,
    SpotPub,
    SpotSub,
}

/// A point-in-time snapshot of a monitored entity's state.
#[derive(Debug, Clone)]
pub struct MonitorStatus {
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

fn ignore_monitor_event(_: &MonitorEvent) {}

/// Monitor handle for observing socket lifecycle and connection events.
///
/// The monitor is an independent observation plane that does not interfere
/// with the data plane.
pub struct SocketMonitor {
    pub(crate) inner: Box<dyn SocketMonitorRuntime>,
}

impl SocketMonitor {
    /// Open a socket monitor for all events.
    pub fn open(socket: &dyn Monitorable) -> Result<Self, ConfigError> {
        crate::monitor::socket_monitor_open(socket)
    }

    /// Blocking receive of a monitor event.
    pub fn recv(&self) -> Result<MonitorEvent, RecvError> {
        self.inner.recv()
    }

    /// Non-blocking receive of a monitor event. Returns `Ok(None)` when no event is available.
    pub fn recv_with_flags(&self, flags: RecvFlags) -> Result<Option<MonitorEvent>, RecvError> {
        self.inner.recv_with_flags(flags)
    }

    /// Read the current monitor status.
    pub fn status(&self) -> Result<MonitorStatus, ConfigError> {
        self.inner.status()
    }

    pub fn snapshot(&self) -> Result<MonitorStatus, ConfigError> {
        self.status()
    }

    /// Install a callback handler for monitor events.
    pub fn on_event<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn(&MonitorEvent) + Send + 'static,
    {
        self.inner.on_event(Box::new(handler))
    }

    pub fn ignore_handler() -> fn(&MonitorEvent) {
        ignore_monitor_event
    }

    pub fn close(&mut self) -> Result<(), CloseError> {
        self.inner.close()
    }
}

impl MonitorStatus {
    pub fn is_ready(&self) -> bool {
        crate::monitor::monitor_status_is_ready(self)
    }

    pub fn is_closed(&self) -> bool {
        crate::monitor::monitor_status_is_closed(self)
    }
}
