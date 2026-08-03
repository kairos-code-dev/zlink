use std::any::Any;
use std::mem::MaybeUninit;
use std::slice;

use crate::error::ConfigError;
use crate::ffi;
use crate::message::Message;
use crate::native_errors::check_config_rc;
use crate::runtime_bridge::MessageRuntime;

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

pub(crate) fn message_new() -> Result<Message, ConfigError> {
    Ok(Message {
        inner: Box::new(NativeMessage::new()?),
    })
}

pub(crate) fn message_with_size(size: usize) -> Result<Message, ConfigError> {
    Ok(Message {
        inner: Box::new(NativeMessage::with_size(size)?),
    })
}

pub(crate) fn message_from_slice(data: &[u8]) -> Result<Message, ConfigError> {
    let mut msg = message_with_size(data.len())?;
    unsafe {
        let dst = ffi::zlink_msg_data(msg.raw_mut()) as *mut u8;
        std::ptr::copy_nonoverlapping(data.as_ptr(), dst, data.len());
    }
    Ok(msg)
}

impl Message {
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
