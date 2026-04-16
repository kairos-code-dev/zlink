use std::ffi::{CStr, CString};
use std::mem::MaybeUninit;
use std::slice;

use crate::error::{ConfigError, check_config_rc, config_validation_error};
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
    pub fn new() -> Result<Self, ConfigError> {
        unsafe {
            let mut msg = MaybeUninit::<ffi::zlink_msg_t>::uninit();
            check_config_rc(ffi::zlink_msg_init(msg.as_mut_ptr()))?;
            Ok(Self {
                inner: msg.assume_init(),
            })
        }
    }

    /// Create a message of the given size filled with uninitialized bytes.
    pub fn with_size(size: usize) -> Result<Self, ConfigError> {
        unsafe {
            let mut msg = MaybeUninit::<ffi::zlink_msg_t>::uninit();
            check_config_rc(ffi::zlink_msg_init_size(msg.as_mut_ptr(), size))?;
            Ok(Self {
                inner: msg.assume_init(),
            })
        }
    }

    /// Create a message by copying the given byte slice.
    pub fn copy_from(data: &[u8]) -> Result<Self, ConfigError> {
        let mut msg = Self::with_size(data.len())?;
        unsafe {
            let dst = ffi::zlink_msg_data(&mut msg.inner) as *mut u8;
            std::ptr::copy_nonoverlapping(data.as_ptr(), dst, data.len());
        }
        Ok(msg)
    }

    pub fn copy_from_bytes(data: bytes::Bytes) -> Result<Self, ConfigError> {
        Self::copy_from(data.as_ref())
    }

    pub fn copy_from_bytes_mut(data: bytes::BytesMut) -> Result<Self, ConfigError> {
        Self::copy_from(data.as_ref())
    }

    pub(crate) fn from_bytes(data: &[u8]) -> Result<Self, ConfigError> {
        Self::copy_from(data)
    }

    /// View the message payload as a byte slice.
    pub fn as_bytes(&self) -> &[u8] {
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

    pub(crate) fn data(&self) -> &[u8] {
        self.as_bytes()
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
    pub fn size(&self) -> usize {
        unsafe { ffi::zlink_msg_size(&self.inner) }
    }

    /// Returns `true` if the message has zero-length payload.
    pub fn is_empty(&self) -> bool {
        self.size() == 0
    }

    /// Interpret the payload as a UTF-8 string.
    pub fn as_str(&self) -> Result<&str, std::str::Utf8Error> {
        std::str::from_utf8(self.data())
    }

    /// Read a string property from the native message metadata.
    ///
    /// Returns `Ok(None)` when the property is absent. The lookup name is
    /// validated eagerly so empty names and interior NUL bytes fail fast.
    pub fn get_property(&self, name: &str) -> Result<Option<String>, ConfigError> {
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

    pub(crate) fn close_now(&mut self) {
        unsafe {
            ffi::zlink_msg_close(&mut self.inner);
            let _ = ffi::zlink_msg_init(&mut self.inner);
        }
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
    type Error = ConfigError;
    fn try_from(data: &[u8]) -> Result<Self, ConfigError> {
        Self::copy_from(data)
    }
}

impl TryFrom<Vec<u8>> for Message {
    type Error = ConfigError;
    fn try_from(v: Vec<u8>) -> Result<Self, ConfigError> {
        Self::copy_from(&v)
    }
}

// ---------------------------------------------------------------------------
// RoutingId
// ---------------------------------------------------------------------------

/// A validated routing identifier (max 255 bytes).
///
/// The `data[255]` bound matches `zlink_routing_id_t`. Construction validates
/// the length; overflow is rejected immediately (fail-fast).
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
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
    pub fn from_bytes(data: &[u8]) -> Self {
        if data.is_empty() {
            panic!("routing id must not be empty");
        }
        if data.len() > Self::MAX_LEN {
            panic!(
                "routing id length {} exceeds maximum {}",
                data.len(),
                Self::MAX_LEN
            );
        }
        let mut raw = ffi::zlink_routing_id_t {
            size: data.len() as u8,
            data: [0u8; 255],
        };
        raw.data[..data.len()].copy_from_slice(data);
        Self { raw }
    }

    pub fn from_u32(value: u32) -> Self {
        Self::from_bytes(&value.to_le_bytes())
    }

    pub fn from_utf8(value: &str) -> Self {
        Self::from_bytes(value.as_bytes())
    }

    pub(crate) fn new(data: &[u8]) -> Result<Self, ConfigError> {
        if data.is_empty() || data.len() > Self::MAX_LEN {
            return Err(config_validation_error());
        }
        Ok(Self::from_bytes(data))
    }

    /// The routing-id bytes.
    pub fn as_bytes(&self) -> &[u8] {
        &self.raw.data[..self.raw.size as usize]
    }

    pub(crate) fn data(&self) -> &[u8] {
        self.as_bytes()
    }

    /// Byte length of the routing id.
    pub fn size(&self) -> usize {
        self.raw.size as usize
    }

    pub(crate) fn len(&self) -> usize {
        self.size()
    }

    /// Returns `true` if the routing id is empty (should not happen after construction).
    pub fn is_empty(&self) -> bool {
        self.raw.size == 0
    }

    pub fn to_hex(&self) -> String {
        let mut out = String::with_capacity(self.size() * 2);
        for byte in self.as_bytes() {
            use std::fmt::Write as _;
            let _ = write!(&mut out, "{byte:02x}");
        }
        out
    }

    pub fn to_u32(&self) -> Option<u32> {
        let bytes = self.as_bytes();
        if bytes.len() != 4 {
            return None;
        }
        Some(u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]))
    }

    pub fn to_public_string(&self) -> Result<String, std::string::FromUtf8Error> {
        String::from_utf8(self.as_bytes().to_vec())
    }

    /// Borrow the underlying FFI struct.
    pub(crate) fn as_raw(&self) -> &ffi::zlink_routing_id_t {
        &self.raw
    }

    /// Build from a raw FFI struct (coming from native recv).
    pub(crate) fn from_raw(raw: ffi::zlink_routing_id_t) -> Self {
        Self { raw }
    }

    pub(crate) fn from_raw_optional(raw: ffi::zlink_routing_id_t) -> Option<Self> {
        if raw.size == 0 {
            None
        } else {
            Some(Self { raw })
        }
    }
}

impl std::fmt::Display for RoutingId {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.to_hex())
    }
}

impl TryFrom<&[u8]> for RoutingId {
    type Error = ConfigError;
    fn try_from(data: &[u8]) -> Result<Self, ConfigError> {
        Self::new(data)
    }
}

// ---------------------------------------------------------------------------
// IntoMultipart trait – lets callers pass a single Message or a Vec<Message>.
// ---------------------------------------------------------------------------

/// Conversion trait that allows `send`/`publish` methods to accept either a
/// single `Message` or a `Vec<Message>`.
pub trait IntoMultipart {
    fn into_multipart(self) -> Vec<Message>;

    fn into_parts(self) -> Vec<Message>
    where
        Self: Sized,
    {
        self.into_multipart()
    }
}

impl IntoMultipart for Message {
    fn into_multipart(self) -> Vec<Message> {
        vec![self]
    }
}

impl IntoMultipart for Vec<Message> {
    fn into_multipart(self) -> Vec<Message> {
        self
    }
}

impl IntoMultipart for &[u8] {
    fn into_multipart(self) -> Vec<Message> {
        vec![Message::copy_from(self).expect("message copy failed")]
    }
}

impl IntoMultipart for Vec<u8> {
    fn into_multipart(self) -> Vec<Message> {
        vec![Message::copy_from(&self).expect("message copy failed")]
    }
}

impl IntoMultipart for bytes::Bytes {
    fn into_multipart(self) -> Vec<Message> {
        vec![Message::copy_from_bytes(self).expect("message copy failed")]
    }
}

impl IntoMultipart for bytes::BytesMut {
    fn into_multipart(self) -> Vec<Message> {
        vec![Message::copy_from_bytes_mut(self).expect("message copy failed")]
    }
}
