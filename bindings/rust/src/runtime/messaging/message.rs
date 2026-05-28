use std::any::Any;
use std::ffi::{CStr, CString};
use std::mem::MaybeUninit;
use std::slice;

use crate::error::ConfigError;
use crate::ffi;
use crate::message::{Message, MessageRuntime};
use crate::native_errors::{check_config_rc, config_validation_error};

struct NativeMessage {
    inner: ffi::zlink_msg_t,
}

impl NativeMessage {
    fn new() -> Result<Self, ConfigError> {
        unsafe {
            let mut msg = MaybeUninit::<ffi::zlink_msg_t>::uninit();
            check_config_rc(ffi::zlink_msg_init(msg.as_mut_ptr()))?;
            Ok(Self {
                inner: msg.assume_init(),
            })
        }
    }

    fn with_size(size: usize) -> Result<Self, ConfigError> {
        unsafe {
            let mut msg = MaybeUninit::<ffi::zlink_msg_t>::uninit();
            check_config_rc(ffi::zlink_msg_init_size(msg.as_mut_ptr(), size))?;
            Ok(Self {
                inner: msg.assume_init(),
            })
        }
    }

    unsafe fn from_raw(raw: ffi::zlink_msg_t) -> Self {
        Self { inner: raw }
    }
}

impl MessageRuntime for NativeMessage {
    fn as_any_mut(&mut self) -> &mut dyn Any {
        self
    }

    fn as_bytes(&self) -> &[u8] {
        unsafe {
            let ptr = ffi::zlink_msg_data(
                &self.inner as *const ffi::zlink_msg_t as *mut ffi::zlink_msg_t,
            ) as *const u8;
            let len = ffi::zlink_msg_size(&self.inner);
            if ptr.is_null() || len == 0 {
                &[]
            } else {
                slice::from_raw_parts(ptr, len)
            }
        }
    }

    fn data_mut(&mut self) -> &mut [u8] {
        unsafe {
            let ptr = ffi::zlink_msg_data(&mut self.inner) as *mut u8;
            let len = ffi::zlink_msg_size(&self.inner);
            if ptr.is_null() || len == 0 {
                &mut []
            } else {
                slice::from_raw_parts_mut(ptr, len)
            }
        }
    }

    fn size(&self) -> usize {
        unsafe { ffi::zlink_msg_size(&self.inner) }
    }

    fn try_clone_box(&self) -> Result<Box<dyn MessageRuntime>, ConfigError> {
        unsafe {
            let mut msg = MaybeUninit::<ffi::zlink_msg_t>::uninit();
            check_config_rc(ffi::zlink_msg_init(msg.as_mut_ptr()))?;
            let mut inner = msg.assume_init();
            match check_config_rc(ffi::zlink_msg_copy(
                &mut inner,
                &self.inner as *const ffi::zlink_msg_t as *mut ffi::zlink_msg_t,
            )) {
                Ok(()) => Ok(Box::new(NativeMessage { inner })),
                Err(error) => {
                    let _ = ffi::zlink_msg_close(&mut inner);
                    Err(error)
                }
            }
        }
    }

    fn get_property(&self, name: &str) -> Result<Option<String>, ConfigError> {
        if name.is_empty() {
            return Err(config_validation_error());
        }
        let c_name = CString::new(name).map_err(|_| config_validation_error())?;
        let ptr = unsafe { ffi::zlink_msg_gets(&self.inner, c_name.as_ptr()) };
        if ptr.is_null() {
            return Ok(None);
        }
        let value = unsafe { CStr::from_ptr(ptr) }
            .to_string_lossy()
            .into_owned();
        Ok(Some(value))
    }

    fn ref_count(&self) -> i32 {
        let mut err = ffi::zlink_config_result_t::ZLINK_CONFIG_OK;
        unsafe { ffi::zlink_msg_refcnt(&self.inner, &mut err) }
    }
}

impl Drop for NativeMessage {
    fn drop(&mut self) {
        unsafe {
            ffi::zlink_msg_close(&mut self.inner);
        }
    }
}

impl Message {
    /// Create an empty (zero-length) message.
    pub fn new() -> Result<Self, ConfigError> {
        Ok(Self {
            inner: Box::new(NativeMessage::new()?),
        })
    }

    /// Create a message of the given size filled with uninitialized bytes.
    pub fn with_size(size: usize) -> Result<Self, ConfigError> {
        Ok(Self {
            inner: Box::new(NativeMessage::with_size(size)?),
        })
    }

    pub fn allocate(size: usize) -> Result<Self, ConfigError> {
        Self::with_size(size)
    }

    fn from_slice(data: &[u8]) -> Result<Self, ConfigError> {
        let mut msg = Self::with_size(data.len())?;
        unsafe {
            let dst = ffi::zlink_msg_data(msg.raw_mut()) as *mut u8;
            std::ptr::copy_nonoverlapping(data.as_ptr(), dst, data.len());
        }
        Ok(msg)
    }

    /// Create a message by copying the given byte source.
    pub fn try_from<T: AsRef<[u8]>>(data: T) -> Result<Self, ConfigError> {
        Self::from_slice(data.as_ref())
    }

    pub(crate) fn raw_mut(&mut self) -> &mut ffi::zlink_msg_t {
        &mut self
            .inner
            .as_any_mut()
            .downcast_mut::<NativeMessage>()
            .expect("zlink native message")
            .inner
    }

    /// Construct from a raw `zlink_msg_t` whose ownership is being transferred
    /// to Rust. The caller must not close the original.
    pub(crate) unsafe fn from_raw(raw: ffi::zlink_msg_t) -> Self {
        Self {
            inner: Box::new(unsafe { NativeMessage::from_raw(raw) }),
        }
    }

    pub(crate) fn close_now(&mut self) {
        unsafe {
            ffi::zlink_msg_close(self.raw_mut());
            let _ = ffi::zlink_msg_init(self.raw_mut());
        }
    }
}

impl TryFrom<&[u8]> for Message {
    type Error = ConfigError;

    fn try_from(data: &[u8]) -> Result<Self, ConfigError> {
        Self::from_slice(data)
    }
}

impl TryFrom<Vec<u8>> for Message {
    type Error = ConfigError;

    fn try_from(v: Vec<u8>) -> Result<Self, ConfigError> {
        Self::from_slice(&v)
    }
}

impl TryFrom<&str> for Message {
    type Error = ConfigError;

    fn try_from(value: &str) -> Result<Self, ConfigError> {
        Self::from_slice(value.as_bytes())
    }
}
