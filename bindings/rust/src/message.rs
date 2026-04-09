use std::ffi::{CStr, CString};
use std::mem::MaybeUninit;
use std::slice;

use crate::error::{ZlinkError, check_rc};
use crate::ffi;

/// Owned message frame wrapping a native `zlink_msg_t`.
///
/// Implements RAII: dropping a `Message` releases its native storage via
/// `zlink_msg_close`. After a successful `send`, ownership transfers to the
/// native layer and drop is suppressed via `std::mem::forget`.
#[repr(transparent)]
pub struct Message {
    pub(crate) inner: ffi::zlink_msg_t,
}

// Message is logically a uniquely-owned buffer. Safe to send across threads.
unsafe impl Send for Message {}

impl Message {
    /// Create an empty (zero-length) message.
    pub fn new() -> Result<Self, ZlinkError> {
        unsafe {
            let mut msg = MaybeUninit::<ffi::zlink_msg_t>::uninit();
            check_rc(ffi::zlink_msg_init(msg.as_mut_ptr()))?;
            Ok(Self {
                inner: msg.assume_init(),
            })
        }
    }

    /// Create a message of the given size filled with uninitialized bytes.
    pub fn with_size(size: usize) -> Result<Self, ZlinkError> {
        unsafe {
            let mut msg = MaybeUninit::<ffi::zlink_msg_t>::uninit();
            check_rc(ffi::zlink_msg_init_size(msg.as_mut_ptr(), size))?;
            Ok(Self {
                inner: msg.assume_init(),
            })
        }
    }

    /// Create a message by copying the given byte slice.
    pub fn from_bytes(data: &[u8]) -> Result<Self, ZlinkError> {
        let mut msg = Self::with_size(data.len())?;
        unsafe {
            let dst = ffi::zlink_msg_data(&mut msg.inner) as *mut u8;
            std::ptr::copy_nonoverlapping(data.as_ptr(), dst, data.len());
        }
        Ok(msg)
    }

    /// View the message payload as a byte slice.
    pub fn data(&self) -> &[u8] {
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

    /// View the message payload as a mutable byte slice.
    pub fn data_mut(&mut self) -> &mut [u8] {
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

    /// Message size in bytes.
    pub fn len(&self) -> usize {
        unsafe { ffi::zlink_msg_size(&self.inner) }
    }

    /// Returns `true` if the message has zero-length payload.
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Interpret the payload as a UTF-8 string.
    pub fn as_str(&self) -> Result<&str, std::str::Utf8Error> {
        std::str::from_utf8(self.data())
    }

    /// Read a string property from the native message metadata.
    ///
    /// Returns `Ok(None)` when the property is absent. The lookup name is
    /// validated eagerly so empty names and interior NUL bytes fail fast.
    pub fn get_property(&self, name: &str) -> Result<Option<String>, ZlinkError> {
        if name.is_empty() {
            return Err(ZlinkError::validation("property name must not be empty"));
        }
        let c_name = CString::new(name)
            .map_err(|_| ZlinkError::validation("property name contains null byte"))?;
        let ptr = unsafe { ffi::zlink_msg_gets(&self.inner, c_name.as_ptr()) };
        if ptr.is_null() {
            return Ok(None);
        }
        let value = unsafe { CStr::from_ptr(ptr) }
            .to_string_lossy()
            .into_owned();
        Ok(Some(value))
    }

    // -- Request-Reply Envelope -----------------------------------------------

    /// Mark this message as a REQUEST with the given correlation ID.
    pub fn set_request(&mut self, correlation_id: u64) -> Result<(), ZlinkError> {
        check_rc(unsafe { ffi::zlink_msg_set_request(&mut self.inner, correlation_id) })
    }

    /// Mark this message as a REPLY with the given correlation ID.
    pub fn set_reply(&mut self, correlation_id: u64) -> Result<(), ZlinkError> {
        check_rc(unsafe { ffi::zlink_msg_set_reply(&mut self.inner, correlation_id) })
    }

    /// Retrieve the request-reply type and correlation ID.
    ///
    /// Returns `(msg_type, correlation_id)` where `msg_type` is 0 (DATA),
    /// 1 (REQUEST), or 2 (REPLY).
    pub fn request_info(&self) -> Result<(u8, u64), ZlinkError> {
        let mut msg_type: u8 = 0;
        let mut correlation_id: u64 = 0;
        check_rc(unsafe {
            ffi::zlink_msg_get_request_info(
                &self.inner,
                &mut msg_type,
                &mut correlation_id,
            )
        })?;
        Ok((msg_type, correlation_id))
    }

    // -- Per-Message Metadata -------------------------------------------------

    /// Set a metadata key-value pair on this message.
    ///
    /// Keys below `0x0100` are reserved and will return an error.
    pub fn set_metadata(&mut self, key: u16, value: &[u8]) -> Result<(), ZlinkError> {
        check_rc(unsafe {
            ffi::zlink_msg_set_metadata(
                &mut self.inner,
                key,
                if value.is_empty() {
                    std::ptr::null()
                } else {
                    value.as_ptr() as *const _
                },
                value.len(),
            )
        })
    }

    /// Retrieve a metadata value by key.
    ///
    /// Returns `None` if the key is absent.
    pub fn get_metadata(&self, key: u16) -> Option<&[u8]> {
        let mut size: usize = 0;
        let ptr = unsafe {
            ffi::zlink_msg_get_metadata(&self.inner, key, &mut size)
        };
        if ptr.is_null() {
            None
        } else {
            Some(unsafe { slice::from_raw_parts(ptr as *const u8, size) })
        }
    }

    /// Return the native storage reference count for the message.
    ///
    /// This is a diagnostic helper only. It does not affect ownership or
    /// message lifetime.
    pub fn ref_count(&self) -> i32 {
        unsafe { ffi::zlink_msg_refcnt(&self.inner) }
    }

    /// Construct from a raw `zlink_msg_t` whose ownership is being transferred
    /// to Rust. The caller must not close the original.
    pub(crate) unsafe fn from_raw(raw: ffi::zlink_msg_t) -> Self {
        Self { inner: raw }
    }
}

impl Drop for Message {
    fn drop(&mut self) {
        unsafe {
            ffi::zlink_msg_close(&mut self.inner);
        }
    }
}

impl TryFrom<&[u8]> for Message {
    type Error = ZlinkError;
    fn try_from(data: &[u8]) -> Result<Self, ZlinkError> {
        Self::from_bytes(data)
    }
}

impl TryFrom<&str> for Message {
    type Error = ZlinkError;
    fn try_from(s: &str) -> Result<Self, ZlinkError> {
        Self::from_bytes(s.as_bytes())
    }
}

impl TryFrom<Vec<u8>> for Message {
    type Error = ZlinkError;
    fn try_from(v: Vec<u8>) -> Result<Self, ZlinkError> {
        Self::from_bytes(&v)
    }
}

// ---------------------------------------------------------------------------
// RoutingId
// ---------------------------------------------------------------------------

/// A validated routing identifier (max 255 bytes).
///
/// The `data[255]` bound matches `zlink_routing_id_t`. Construction validates
/// the length; overflow is rejected immediately (fail-fast).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RoutingId {
    raw: ffi::zlink_routing_id_t,
}

impl RoutingId {
    /// Maximum data length (matches `zlink_routing_id_t.data[255]`).
    pub const MAX_LEN: usize = 255;

    /// Create a `RoutingId` from a byte slice.
    ///
    /// # Errors
    /// Returns `ZlinkError` if `data` is empty or exceeds 255 bytes.
    pub fn new(data: &[u8]) -> Result<Self, ZlinkError> {
        if data.is_empty() {
            return Err(ZlinkError::validation("routing id must not be empty"));
        }
        if data.len() > Self::MAX_LEN {
            return Err(ZlinkError::validation(format!(
                "routing id length {} exceeds maximum {}",
                data.len(),
                Self::MAX_LEN,
            )));
        }
        let mut raw = ffi::zlink_routing_id_t {
            size: data.len() as u8,
            data: [0u8; 255],
        };
        raw.data[..data.len()].copy_from_slice(data);
        Ok(Self { raw })
    }

    /// The routing-id bytes.
    pub fn data(&self) -> &[u8] {
        &self.raw.data[..self.raw.size as usize]
    }

    /// Byte length of the routing id.
    pub fn len(&self) -> usize {
        self.raw.size as usize
    }

    /// Returns `true` if the routing id is empty (should not happen after construction).
    pub fn is_empty(&self) -> bool {
        self.raw.size == 0
    }

    /// Borrow the underlying FFI struct.
    pub(crate) fn as_raw(&self) -> &ffi::zlink_routing_id_t {
        &self.raw
    }

    /// Build from a raw FFI struct (coming from native recv).
    pub(crate) fn from_raw(raw: ffi::zlink_routing_id_t) -> Self {
        Self { raw }
    }
}

impl TryFrom<&[u8]> for RoutingId {
    type Error = ZlinkError;
    fn try_from(data: &[u8]) -> Result<Self, ZlinkError> {
        Self::new(data)
    }
}

impl TryFrom<&str> for RoutingId {
    type Error = ZlinkError;
    fn try_from(s: &str) -> Result<Self, ZlinkError> {
        Self::new(s.as_bytes())
    }
}

// ---------------------------------------------------------------------------
// IntoMultipart trait – lets callers pass a single Message or a Vec<Message>.
// ---------------------------------------------------------------------------

/// Conversion trait that allows `send`/`publish` methods to accept either a
/// single `Message` or a `Vec<Message>`.
pub trait IntoMultipart {
    fn into_parts(self) -> Vec<Message>;
}

impl IntoMultipart for Message {
    fn into_parts(self) -> Vec<Message> {
        vec![self]
    }
}

impl IntoMultipart for Vec<Message> {
    fn into_parts(self) -> Vec<Message> {
        self
    }
}
