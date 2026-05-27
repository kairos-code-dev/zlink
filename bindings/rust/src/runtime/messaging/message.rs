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

    pub fn allocate(size: usize) -> Result<Self, ConfigError> {
        Self::with_size(size)
    }

    fn from_slice(data: &[u8]) -> Result<Self, ConfigError> {
        let mut msg = Self::with_size(data.len())?;
        unsafe {
            let dst = ffi::zlink_msg_data(&mut msg.inner) as *mut u8;
            std::ptr::copy_nonoverlapping(data.as_ptr(), dst, data.len());
        }
        Ok(msg)
    }

    /// Create a message by copying the given byte source.
    pub fn try_from<T: AsRef<[u8]>>(data: T) -> Result<Self, ConfigError> {
        Self::from_slice(data.as_ref())
    }

    pub fn try_clone(&self) -> Result<Self, ConfigError> {
        unsafe {
            let mut msg = MaybeUninit::<ffi::zlink_msg_t>::uninit();
            check_config_rc(ffi::zlink_msg_init(msg.as_mut_ptr()))?;
            let mut inner = msg.assume_init();
            match check_config_rc(ffi::zlink_msg_copy(
                &mut inner,
                &self.inner as *const ffi::zlink_msg_t as *mut ffi::zlink_msg_t,
            )) {
                Ok(()) => Ok(Self { inner }),
                Err(error) => {
                    let _ = ffi::zlink_msg_close(&mut inner);
                    Err(error)
                }
            }
        }
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

    pub fn to_vec(&self) -> Vec<u8> {
        self.as_bytes().to_vec()
    }

    pub fn copy_to(&self, destination: &mut [u8]) -> Result<usize, ConfigError> {
        let bytes = self.as_bytes();
        if destination.len() < bytes.len() {
            return Err(config_validation_error());
        }
        destination[..bytes.len()].copy_from_slice(bytes);
        Ok(bytes.len())
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
        let mut err = ffi::zlink_config_result_t::ZLINK_CONFIG_OK;
        unsafe { ffi::zlink_msg_refcnt(&self.inner, &mut err) }
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

// ---------------------------------------------------------------------------
// RoutingId
// ---------------------------------------------------------------------------

/// A validated routing identifier (max 255 bytes).
///
/// The `data[255]` bound matches `zlink_routing_id_t`. Construction validates
/// the length; overflow is rejected immediately (fail-fast). `Copy` because
/// the underlying FFI struct is plain bytes and is small enough that pass-by-
/// value beats the indirection of borrowing — callers in `service.rs` and the
/// router/stream sockets previously paid for a 256-byte memcpy via `clone()`
/// on every send/request path.
#[derive(Copy, Clone, Debug, PartialEq, Eq, Hash)]
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

    pub fn from_string(value: &str) -> Self {
        Self::from_bytes(value.as_bytes())
    }

    pub fn from_u32(value: u32) -> Self {
        Self::from_bytes(&value.to_be_bytes())
    }

    pub fn from_uuid_bytes(value: [u8; 16]) -> Self {
        Self::from_bytes(&value)
    }

    pub fn from_hex(value: &str) -> Self {
        Self::try_from_hex(value).expect("invalid routing id hex string")
    }

    pub fn try_from_hex(value: &str) -> Result<Self, ConfigError> {
        if value.is_empty() || value.len() % 2 != 0 || value.len() > Self::MAX_LEN * 2 {
            return Err(config_validation_error());
        }
        let mut data = Vec::with_capacity(value.len() / 2);
        let bytes = value.as_bytes();
        for pair in bytes.chunks_exact(2) {
            let high = hex_nibble(pair[0]).ok_or_else(config_validation_error)?;
            let low = hex_nibble(pair[1]).ok_or_else(config_validation_error)?;
            data.push((high << 4) | low);
        }
        Self::new(&data)
    }

    pub fn try_from_string(value: &str) -> Result<Self, ConfigError> {
        Self::new(value.as_bytes())
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

fn hex_nibble(value: u8) -> Option<u8> {
    match value {
        b'0'..=b'9' => Some(value - b'0'),
        b'a'..=b'f' => Some(value - b'a' + 10),
        b'A'..=b'F' => Some(value - b'A' + 10),
        _ => None,
    }
}

impl std::fmt::Display for RoutingId {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        if let Ok(text) = std::str::from_utf8(self.as_bytes()) {
            if !text.chars().any(char::is_control) {
                return f.write_str(text);
            }
        }
        if self.size() == 4 {
            let mut bytes = [0u8; 4];
            bytes.copy_from_slice(self.as_bytes());
            return write!(f, "{}", u32::from_be_bytes(bytes));
        }
        if self.size() == 16 {
            let bytes = self.as_bytes();
            return write!(
                f,
                "{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
                bytes[0],
                bytes[1],
                bytes[2],
                bytes[3],
                bytes[4],
                bytes[5],
                bytes[6],
                bytes[7],
                bytes[8],
                bytes[9],
                bytes[10],
                bytes[11],
                bytes[12],
                bytes[13],
                bytes[14],
                bytes[15]
            );
        }
        write!(f, "hex:{}", self.to_hex())
    }
}

impl TryFrom<&[u8]> for RoutingId {
    type Error = ConfigError;
    fn try_from(data: &[u8]) -> Result<Self, ConfigError> {
        Self::new(data)
    }
}

impl TryFrom<&str> for RoutingId {
    type Error = ConfigError;
    fn try_from(value: &str) -> Result<Self, ConfigError> {
        Self::try_from_string(value)
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
        vec![Message::try_from(self).expect("message copy failed")]
    }
}

impl IntoMultipart for Vec<u8> {
    fn into_multipart(self) -> Vec<Message> {
        vec![Message::try_from(self).expect("message copy failed")]
    }
}

impl IntoMultipart for bytes::Bytes {
    fn into_multipart(self) -> Vec<Message> {
        vec![Message::try_from(self).expect("message copy failed")]
    }
}

impl IntoMultipart for bytes::BytesMut {
    fn into_multipart(self) -> Vec<Message> {
        vec![Message::try_from(self).expect("message copy failed")]
    }
}
