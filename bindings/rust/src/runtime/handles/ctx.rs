use std::ffi::{CString, c_void};
use std::sync::Mutex;
use std::time::Duration;

use crate::error::{
    CloseError, ConfigError, ConfigResult, check_close_rc, check_config_rc, config_validation_error,
};
use crate::ffi;
use crate::socket::{
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

impl AutoHwmProfile {
    pub(crate) fn to_raw(self) -> i32 {
        match self {
            AutoHwmProfile::Compact => {
                ffi::zlink_auto_hwm_profile_t::ZLINK_AUTO_HWM_PROFILE_COMPACT as i32
            }
            AutoHwmProfile::LowLatency => {
                ffi::zlink_auto_hwm_profile_t::ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY as i32
            }
            AutoHwmProfile::Balanced => {
                ffi::zlink_auto_hwm_profile_t::ZLINK_AUTO_HWM_PROFILE_BALANCED as i32
            }
            AutoHwmProfile::Throughput => {
                ffi::zlink_auto_hwm_profile_t::ZLINK_AUTO_HWM_PROFILE_THROUGHPUT as i32
            }
        }
    }

    pub(crate) fn from_raw(raw: i32) -> Result<Self, ConfigError> {
        match raw {
            x if x == ffi::zlink_auto_hwm_profile_t::ZLINK_AUTO_HWM_PROFILE_COMPACT as i32 => {
                Ok(AutoHwmProfile::Compact)
            }
            x if x == ffi::zlink_auto_hwm_profile_t::ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY as i32 => {
                Ok(AutoHwmProfile::LowLatency)
            }
            x if x == ffi::zlink_auto_hwm_profile_t::ZLINK_AUTO_HWM_PROFILE_BALANCED as i32 => {
                Ok(AutoHwmProfile::Balanced)
            }
            x if x == ffi::zlink_auto_hwm_profile_t::ZLINK_AUTO_HWM_PROFILE_THROUGHPUT as i32 => {
                Ok(AutoHwmProfile::Throughput)
            }
            _ => Err(config_validation_error()),
        }
    }
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

impl AutoHwmRecalcReason {
    pub(crate) fn from_raw(raw: u32) -> Self {
        match raw {
            1 => AutoHwmRecalcReason::Initial,
            2 => AutoHwmRecalcReason::RoleChange,
            3 => AutoHwmRecalcReason::PolicyToggle,
            4 => AutoHwmRecalcReason::Refresh,
            5 => AutoHwmRecalcReason::DeferredShrink,
            _ => AutoHwmRecalcReason::None,
        }
    }
}

/// The zlink context – foundation for creating sockets.
///
/// A context manages I/O threads and internal infrastructure. Dropping a
/// `Context` calls `zlink_ctx_term`, which blocks until all sockets created
/// from it are closed.
pub struct Context {
    handle: *mut c_void,
    /// Cached thread name prefix (write-only in the C API; readable from cache).
    thread_name_prefix: Mutex<String>,
}

/// Typed facade over `zlink_ctx_*` options.
pub struct ContextOptions<'a> {
    context: &'a Context,
}

unsafe impl Send for Context {}
unsafe impl Sync for Context {}

impl Context {
    /// Create a new context with default settings.
    pub fn new() -> Result<Self, ConfigError> {
        let handle = unsafe { ffi::zlink_ctx_new() };
        if handle.is_null() {
            return Err(ConfigError::new(
                crate::error::ConfigResult::InvalidHandle,
                crate::error::last_errno(),
            ));
        }
        Ok(Self {
            handle,
            thread_name_prefix: Mutex::new(String::new()),
        })
    }

    /// Interrupt all blocking calls on sockets of this context with ETERM.
    /// `drop` / `zlink_ctx_term` must still be called for final cleanup.
    pub fn shutdown(&self) -> Result<(), CloseError> {
        check_close_rc(unsafe { ffi::zlink_ctx_shutdown(self.handle) })
    }

    /// Recalculate and apply auto HWM for the entire context immediately.
    ///
    /// If auto HWM is disabled, this is a no-op that returns success.
    pub fn recalculate_auto_hwm(&self) -> Result<(), ConfigError> {
        check_config_rc(unsafe { ffi::zlink_ctx_auto_hwm_recalculate(self.handle) })
    }

    /// Access typed context options.
    pub fn options(&self) -> ContextOptions<'_> {
        ContextOptions { context: self }
    }

    pub(crate) fn set_int_option(&self, option: i32, value: i32) -> Result<(), ConfigError> {
        check_config_rc(unsafe { ffi::zlink_ctx_set(self.handle, raw_option(option), value) })
    }

    pub(crate) fn set_data_option(
        &self,
        option: i32,
        data: *const c_void,
        len: usize,
    ) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_ctx_set_data(self.handle, raw_option(option), data, len)
        })
    }

    pub(crate) fn get_int_option(&self, option: i32) -> Result<i32, ConfigError> {
        let mut result = ffi::zlink_config_result_t::ZLINK_CONFIG_OK;
        let value = unsafe { ffi::zlink_ctx_get(self.handle, raw_option(option), &mut result) };
        if result != ffi::zlink_config_result_t::ZLINK_CONFIG_OK {
            let code = match result {
                ffi::zlink_config_result_t::ZLINK_CONFIG_INVALID_HANDLE => {
                    ConfigResult::InvalidHandle
                }
                ffi::zlink_config_result_t::ZLINK_CONFIG_NOT_SUPPORTED => {
                    ConfigResult::NotSupported
                }
                _ => ConfigResult::InvalidArgument,
            };
            Err(ConfigError::new(code, crate::error::last_errno()))
        } else {
            Ok(value)
        }
    }

    // -- Socket factories --------------------------------------------------

    pub(crate) fn raw(&self) -> *mut c_void {
        self.handle
    }

    /// Create a PAIR socket.
    pub fn pair_socket(&self) -> Result<PairSocket, ConfigError> {
        PairSocket::new(self)
    }

    /// Create a PUB socket.
    pub fn pub_socket(&self) -> Result<PubSocket, ConfigError> {
        PubSocket::new(self)
    }

    /// Create a SUB socket.
    pub fn sub_socket(&self) -> Result<SubSocket, ConfigError> {
        SubSocket::new(self)
    }

    /// Create a DEALER socket.
    pub fn dealer_socket(&self) -> Result<DealerSocket, ConfigError> {
        DealerSocket::new(self)
    }

    /// Create a ROUTER socket.
    pub fn router_socket(&self) -> Result<RouterSocket, ConfigError> {
        RouterSocket::new(self)
    }

    /// Create an XPUB socket.
    pub fn xpub_socket(&self) -> Result<XPubSocket, ConfigError> {
        XPubSocket::new(self)
    }

    /// Create an XSUB socket.
    pub fn xsub_socket(&self) -> Result<XSubSocket, ConfigError> {
        XSubSocket::new(self)
    }

    /// Create a STREAM socket.
    pub fn stream_socket(&self) -> Result<StreamSocket, ConfigError> {
        StreamSocket::new(self)
    }
}

impl ContextOptions<'_> {
    pub fn io_threads(&self) -> Result<i32, ConfigError> {
        self.context
            .get_int_option(ffi::zlink_ctx_option_t::ZLINK_IO_THREADS as i32)
    }

    pub fn set_io_threads(&self, threads: i32) -> Result<(), ConfigError> {
        self.context
            .set_int_option(ffi::zlink_ctx_option_t::ZLINK_IO_THREADS as i32, threads)
    }

    pub fn max_sockets(&self) -> Result<i32, ConfigError> {
        self.context
            .get_int_option(ffi::zlink_ctx_option_t::ZLINK_MAX_SOCKETS as i32)
    }

    pub fn set_max_sockets(&self, max: i32) -> Result<(), ConfigError> {
        self.context
            .set_int_option(ffi::zlink_ctx_option_t::ZLINK_MAX_SOCKETS as i32, max)
    }

    pub fn socket_limit(&self) -> Result<i32, ConfigError> {
        self.context
            .get_int_option(ffi::zlink_ctx_option_t::ZLINK_SOCKET_LIMIT as i32)
    }

    pub fn thread_priority(&self) -> Result<i32, ConfigError> {
        self.context.get_int_option(3)
    }

    pub fn set_thread_priority(&self, priority: i32) -> Result<(), ConfigError> {
        self.context.set_int_option(3, priority)
    }

    pub fn thread_scheduling_policy(&self) -> Result<i32, ConfigError> {
        self.context
            .get_int_option(ffi::zlink_ctx_option_t::ZLINK_THREAD_SCHED_POLICY as i32)
    }

    pub fn set_thread_scheduling_policy(&self, policy: i32) -> Result<(), ConfigError> {
        self.context.set_int_option(
            ffi::zlink_ctx_option_t::ZLINK_THREAD_SCHED_POLICY as i32,
            policy,
        )
    }

    pub fn max_msg_size(&self) -> Result<i32, ConfigError> {
        self.context
            .get_int_option(ffi::zlink_ctx_option_t::ZLINK_MAX_MSGSZ as i32)
    }

    pub fn set_max_msg_size(&self, size: i32) -> Result<(), ConfigError> {
        self.context
            .set_int_option(ffi::zlink_ctx_option_t::ZLINK_MAX_MSGSZ as i32, size)
    }

    pub fn msg_t_size(&self) -> Result<i32, ConfigError> {
        self.context
            .get_int_option(ffi::zlink_ctx_option_t::ZLINK_MSG_T_SIZE as i32)
    }

    pub fn blocky(&self) -> Result<bool, ConfigError> {
        Ok(self
            .context
            .get_int_option(ffi::zlink_ctx_option_t::ZLINK_CTX_OPT_BLOCKY as i32)?
            != 0)
    }

    pub fn set_blocky(&self, blocky: bool) -> Result<(), ConfigError> {
        self.context.set_int_option(
            ffi::zlink_ctx_option_t::ZLINK_CTX_OPT_BLOCKY as i32,
            if blocky { 1 } else { 0 },
        )
    }

    /// Get the thread name prefix (returned from the cached value set via `set_thread_name_prefix`).
    pub fn thread_name_prefix(&self) -> Result<String, ConfigError> {
        Ok(self
            .context
            .thread_name_prefix
            .lock()
            .map_err(|_| config_validation_error())?
            .clone())
    }

    /// Set the thread name prefix (applied to I/O threads created by this context).
    pub fn set_thread_name_prefix(&self, prefix: &str) -> Result<(), ConfigError> {
        let c = CString::new(prefix).map_err(|_| config_validation_error())?;
        let bytes = c.as_bytes(); // excludes NUL
        self.context.set_data_option(
            ffi::zlink_ctx_option_t::ZLINK_THREAD_NAME_PREFIX as i32,
            bytes.as_ptr().cast(),
            bytes.len(),
        )?;
        *self
            .context
            .thread_name_prefix
            .lock()
            .map_err(|_| config_validation_error())? = prefix.to_owned();
        Ok(())
    }

    /// Get whether auto HWM is enabled.
    pub fn auto_hwm_enabled(&self) -> Result<bool, ConfigError> {
        Ok(self
            .context
            .get_int_option(ffi::zlink_ctx_option_t::ZLINK_CTX_OPT_AUTO_HWM_ENABLE as i32)?
            != 0)
    }

    /// Enable or disable auto HWM.
    pub fn set_auto_hwm_enabled(&self, enabled: bool) -> Result<(), ConfigError> {
        self.context.set_int_option(
            ffi::zlink_ctx_option_t::ZLINK_CTX_OPT_AUTO_HWM_ENABLE as i32,
            if enabled { 1 } else { 0 },
        )
    }

    /// Get the auto HWM recalculation debounce interval.
    pub fn auto_hwm_recalc_debounce(&self) -> Result<Duration, ConfigError> {
        let ms = self.context.get_int_option(
            ffi::zlink_ctx_option_t::ZLINK_CTX_OPT_AUTO_HWM_RECALC_DEBOUNCE_MS as i32,
        )?;
        Ok(Duration::from_millis(ms.max(0) as u64))
    }

    /// Set the auto HWM recalculation debounce interval.
    pub fn set_auto_hwm_recalc_debounce(&self, value: Duration) -> Result<(), ConfigError> {
        let ms = crate::ctx::duration_to_millis(value)?;
        self.context.set_int_option(
            ffi::zlink_ctx_option_t::ZLINK_CTX_OPT_AUTO_HWM_RECALC_DEBOUNCE_MS as i32,
            ms,
        )
    }

    pub fn auto_hwm_profile(&self) -> Result<AutoHwmProfile, ConfigError> {
        AutoHwmProfile::from_raw(
            self.context
                .get_int_option(ffi::zlink_ctx_option_t::ZLINK_CTX_OPT_AUTO_HWM_PROFILE as i32)?,
        )
    }

    pub fn set_auto_hwm_profile(&self, profile: AutoHwmProfile) -> Result<(), ConfigError> {
        self.context.set_int_option(
            ffi::zlink_ctx_option_t::ZLINK_CTX_OPT_AUTO_HWM_PROFILE as i32,
            profile.to_raw(),
        )
    }

    pub fn auto_hwm_msg_unit_bytes(&self) -> Result<i32, ConfigError> {
        self.context
            .get_int_option(ffi::zlink_ctx_option_t::ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES as i32)
    }

    pub fn set_auto_hwm_msg_unit_bytes(&self, bytes: i32) -> Result<(), ConfigError> {
        if bytes < 0 {
            return Err(config_validation_error());
        }
        self.context.set_int_option(
            ffi::zlink_ctx_option_t::ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES as i32,
            bytes,
        )
    }

    pub fn add_thread_affinity(&self, cpu: i32) -> Result<(), ConfigError> {
        self.context.set_int_option(
            ffi::zlink_ctx_option_t::ZLINK_THREAD_AFFINITY_CPU_ADD as i32,
            cpu,
        )
    }

    pub fn remove_thread_affinity(&self, cpu: i32) -> Result<(), ConfigError> {
        self.context.set_int_option(
            ffi::zlink_ctx_option_t::ZLINK_THREAD_AFFINITY_CPU_REMOVE as i32,
            cpu,
        )
    }
}

fn raw_option(value: i32) -> ffi::zlink_ctx_option_t {
    unsafe { std::mem::transmute(value) }
}

impl Drop for Context {
    fn drop(&mut self) {
        unsafe {
            ffi::zlink_ctx_term(self.handle);
        }
    }
}

// ---------------------------------------------------------------------------
// Version helper
// ---------------------------------------------------------------------------

/// Return the runtime library version as `(major, minor, patch)`.
pub fn version() -> (i32, i32, i32) {
    let (mut major, mut minor, mut patch) = (0i32, 0i32, 0i32);
    unsafe {
        ffi::zlink_version(&mut major, &mut minor, &mut patch);
    }
    (major, minor, patch)
}

/// Check whether the library supports a given capability (e.g. `"ipc"`, `"tls"`).
pub fn has(capability: &str) -> bool {
    let c = std::ffi::CString::new(capability).unwrap_or_default();
    unsafe { ffi::zlink_has(c.as_ptr()) != 0 }
}

// ---------------------------------------------------------------------------
// Duration → millis helper (with overflow check per Boundary Cost Policy)
// ---------------------------------------------------------------------------

pub(crate) fn duration_to_millis(d: Duration) -> Result<i32, ConfigError> {
    let ms = d.as_millis();
    if ms > i32::MAX as u128 {
        return Err(config_validation_error());
    }
    Ok(ms as i32)
}
