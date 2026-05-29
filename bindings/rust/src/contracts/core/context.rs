use std::any::Any;
use std::time::Duration;

use crate::error::{CloseError, ConfigError};
use crate::socket_contracts::{
    DealerSocket, PairSocket, PubSocket, RouterSocket, StreamSocket, SubSocket, XPubSocket,
    XSubSocket,
};

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum AutoHwmProfile {
    Compact,
    LowLatency,
    Balanced,
    Throughput,
}

/// Recalculation trigger reported by the auto-HWM v2 monitor snapshot.
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum AutoHwmRecalcReason {
    None,
    Initial,
    RoleChange,
    PolicyToggle,
    Refresh,
    DeferredShrink,
}

pub(crate) trait ContextOptionRuntime {
    fn io_threads(&self) -> Result<i32, ConfigError>;
    fn set_io_threads(&self, threads: i32) -> Result<(), ConfigError>;
    fn max_sockets(&self) -> Result<i32, ConfigError>;
    fn set_max_sockets(&self, max: i32) -> Result<(), ConfigError>;
    fn socket_limit(&self) -> Result<i32, ConfigError>;
    fn thread_priority(&self) -> Result<i32, ConfigError>;
    fn set_thread_priority(&self, priority: i32) -> Result<(), ConfigError>;
    fn thread_scheduling_policy(&self) -> Result<i32, ConfigError>;
    fn set_thread_scheduling_policy(&self, policy: i32) -> Result<(), ConfigError>;
    fn max_message_size(&self) -> Result<i32, ConfigError>;
    fn set_max_message_size(&self, size: i32) -> Result<(), ConfigError>;
    fn msg_t_size(&self) -> Result<i32, ConfigError>;
    fn blocky(&self) -> Result<bool, ConfigError>;
    fn set_blocky(&self, blocky: bool) -> Result<(), ConfigError>;
    fn thread_name_prefix(&self) -> Result<String, ConfigError>;
    fn set_thread_name_prefix(&self, prefix: &str) -> Result<(), ConfigError>;
    fn auto_hwm_enabled(&self) -> Result<bool, ConfigError>;
    fn set_auto_hwm_enabled(&self, enabled: bool) -> Result<(), ConfigError>;
    fn auto_hwm_recalc_debounce(&self) -> Result<Duration, ConfigError>;
    fn set_auto_hwm_recalc_debounce(&self, value: Duration) -> Result<(), ConfigError>;
    fn auto_hwm_profile(&self) -> Result<AutoHwmProfile, ConfigError>;
    fn set_auto_hwm_profile(&self, profile: AutoHwmProfile) -> Result<(), ConfigError>;
    fn auto_hwm_msg_unit_bytes(&self) -> Result<i32, ConfigError>;
    fn set_auto_hwm_msg_unit_bytes(&self, bytes: i32) -> Result<(), ConfigError>;
    fn add_thread_affinity(&self, cpu: i32) -> Result<(), ConfigError>;
    fn remove_thread_affinity(&self, cpu: i32) -> Result<(), ConfigError>;
}

pub(crate) trait ContextRuntime: Any + ContextOptionRuntime + Send + Sync {
    fn as_any(&self) -> &dyn Any;
    fn shutdown(&self) -> Result<(), CloseError>;
    fn recalculate_auto_hwm(&self) -> Result<(), ConfigError>;
}

/// The zlink context, the foundation for creating sockets and services.
///
/// The public type owns context lifetime. Native context handles and option
/// marshalling stay inside the private runtime implementation.
pub struct Context {
    pub(crate) inner: Box<dyn ContextRuntime>,
}

impl Context {
    /// Create a new context with default settings.
    pub fn new() -> Result<Self, ConfigError> {
        crate::ctx::context_new()
    }

    /// Create a PAIR socket.
    pub fn pair_socket(&self) -> Result<PairSocket, ConfigError> {
        crate::ctx::context_pair_socket(self)
    }

    /// Create a PUB socket.
    pub fn pub_socket(&self) -> Result<PubSocket, ConfigError> {
        crate::ctx::context_pub_socket(self)
    }

    /// Create a SUB socket.
    pub fn sub_socket(&self) -> Result<SubSocket, ConfigError> {
        crate::ctx::context_sub_socket(self)
    }

    /// Create a DEALER socket.
    pub fn dealer_socket(&self) -> Result<DealerSocket, ConfigError> {
        crate::ctx::context_dealer_socket(self)
    }

    /// Create a ROUTER socket.
    pub fn router_socket(&self) -> Result<RouterSocket, ConfigError> {
        crate::ctx::context_router_socket(self)
    }

    /// Create an XPUB socket.
    pub fn xpub_socket(&self) -> Result<XPubSocket, ConfigError> {
        crate::ctx::context_xpub_socket(self)
    }

    /// Create an XSUB socket.
    pub fn xsub_socket(&self) -> Result<XSubSocket, ConfigError> {
        crate::ctx::context_xsub_socket(self)
    }

    /// Create a STREAM socket.
    pub fn stream_socket(&self) -> Result<StreamSocket, ConfigError> {
        crate::ctx::context_stream_socket(self)
    }

    /// Interrupt all blocking calls on sockets of this context with ETERM.
    pub fn shutdown(&self) -> Result<(), CloseError> {
        self.inner.shutdown()
    }

    /// Recalculate and apply auto HWM for the entire context immediately.
    pub fn recalculate_auto_hwm(&self) -> Result<(), ConfigError> {
        self.inner.recalculate_auto_hwm()
    }

    /// Access typed context options.
    pub fn options(&self) -> ContextOptions<'_> {
        ContextOptions::new(self.inner.as_ref())
    }
}

/// Typed facade over context options.
pub struct ContextOptions<'a> {
    context: &'a dyn ContextRuntime,
}

impl<'a> ContextOptions<'a> {
    pub(crate) fn new(context: &'a dyn ContextRuntime) -> Self {
        Self { context }
    }

    pub fn io_threads(&self) -> Result<i32, ConfigError> {
        self.context.io_threads()
    }
    pub fn set_io_threads(&self, threads: i32) -> Result<(), ConfigError> {
        self.context.set_io_threads(threads)
    }
    pub fn max_sockets(&self) -> Result<i32, ConfigError> {
        self.context.max_sockets()
    }
    pub fn set_max_sockets(&self, max: i32) -> Result<(), ConfigError> {
        self.context.set_max_sockets(max)
    }
    pub fn socket_limit(&self) -> Result<i32, ConfigError> {
        self.context.socket_limit()
    }
    pub fn thread_priority(&self) -> Result<i32, ConfigError> {
        self.context.thread_priority()
    }
    pub fn set_thread_priority(&self, priority: i32) -> Result<(), ConfigError> {
        self.context.set_thread_priority(priority)
    }
    pub fn thread_scheduling_policy(&self) -> Result<i32, ConfigError> {
        self.context.thread_scheduling_policy()
    }
    pub fn set_thread_scheduling_policy(&self, policy: i32) -> Result<(), ConfigError> {
        self.context.set_thread_scheduling_policy(policy)
    }
    pub fn max_message_size(&self) -> Result<i32, ConfigError> {
        self.context.max_message_size()
    }
    pub fn set_max_message_size(&self, size: i32) -> Result<(), ConfigError> {
        self.context.set_max_message_size(size)
    }
    pub fn msg_t_size(&self) -> Result<i32, ConfigError> {
        self.context.msg_t_size()
    }
    pub fn blocky(&self) -> Result<bool, ConfigError> {
        self.context.blocky()
    }
    pub fn set_blocky(&self, blocky: bool) -> Result<(), ConfigError> {
        self.context.set_blocky(blocky)
    }
    pub fn thread_name_prefix(&self) -> Result<String, ConfigError> {
        self.context.thread_name_prefix()
    }
    pub fn set_thread_name_prefix(&self, prefix: &str) -> Result<(), ConfigError> {
        self.context.set_thread_name_prefix(prefix)
    }
    pub fn auto_hwm_enabled(&self) -> Result<bool, ConfigError> {
        self.context.auto_hwm_enabled()
    }
    pub fn set_auto_hwm_enabled(&self, enabled: bool) -> Result<(), ConfigError> {
        self.context.set_auto_hwm_enabled(enabled)
    }
    pub fn auto_hwm_recalc_debounce(&self) -> Result<Duration, ConfigError> {
        self.context.auto_hwm_recalc_debounce()
    }
    pub fn set_auto_hwm_recalc_debounce(&self, value: Duration) -> Result<(), ConfigError> {
        self.context.set_auto_hwm_recalc_debounce(value)
    }
    pub fn auto_hwm_profile(&self) -> Result<AutoHwmProfile, ConfigError> {
        self.context.auto_hwm_profile()
    }
    pub fn set_auto_hwm_profile(&self, profile: AutoHwmProfile) -> Result<(), ConfigError> {
        self.context.set_auto_hwm_profile(profile)
    }
    pub fn auto_hwm_msg_unit_bytes(&self) -> Result<i32, ConfigError> {
        self.context.auto_hwm_msg_unit_bytes()
    }
    pub fn set_auto_hwm_msg_unit_bytes(&self, bytes: i32) -> Result<(), ConfigError> {
        self.context.set_auto_hwm_msg_unit_bytes(bytes)
    }
    pub fn add_thread_affinity(&self, cpu: i32) -> Result<(), ConfigError> {
        self.context.add_thread_affinity(cpu)
    }
    pub fn remove_thread_affinity(&self, cpu: i32) -> Result<(), ConfigError> {
        self.context.remove_thread_affinity(cpu)
    }
}
