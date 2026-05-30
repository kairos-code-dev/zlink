use std::any::Any;

use crate::error::{ConfigError, ConfigResult};

pub use crate::routing_id::RoutingId;

pub(crate) trait MessageRuntime: Any + Send {
    fn as_any_mut(&mut self) -> &mut dyn Any;
    fn as_bytes(&self) -> &[u8];
    fn data_mut(&mut self) -> &mut [u8];
    fn size(&self) -> usize;
    fn try_clone_box(&self) -> Result<Box<dyn MessageRuntime>, ConfigError>;
    fn get_property(&self, name: &str) -> Result<Option<String>, ConfigError>;
    fn ref_count(&self) -> i32;
}

/// Owned message frame.
///
/// `Message` is a safe public contract type. Native storage and FFI ownership
/// rules are handled by the private runtime implementation.
pub struct Message {
    pub(crate) inner: Box<dyn MessageRuntime>,
}

impl Message {
    /// Create an empty (zero-length) message.
    pub fn new() -> Result<Self, ConfigError> {
        crate::message_runtime::message_new()
    }

    /// Create a message of the given size filled with uninitialized bytes.
    pub fn with_size(size: usize) -> Result<Self, ConfigError> {
        crate::message_runtime::message_with_size(size)
    }

    /// Allocates a message with writable payload storage of `size` bytes; an
    /// alias for [`Message::with_size`].
    pub fn allocate(size: usize) -> Result<Self, ConfigError> {
        Self::with_size(size)
    }

    /// Create a message by copying the given byte source.
    pub fn try_from<T: AsRef<[u8]>>(data: T) -> Result<Self, ConfigError> {
        crate::message_runtime::message_from_slice(data.as_ref())
    }

    /// View the message payload as a byte slice.
    pub fn as_bytes(&self) -> &[u8] {
        self.inner.as_bytes()
    }

    pub(crate) fn data(&self) -> &[u8] {
        self.as_bytes()
    }

    /// View the message payload as a mutable byte slice.
    pub fn data_mut(&mut self) -> &mut [u8] {
        self.inner.data_mut()
    }

    /// Message size in bytes.
    pub fn size(&self) -> usize {
        self.inner.size()
    }

    /// Returns `true` if the message has zero-length payload.
    pub fn is_empty(&self) -> bool {
        self.size() == 0
    }

    /// Interpret the payload as a UTF-8 string.
    pub fn as_str(&self) -> Result<&str, std::str::Utf8Error> {
        std::str::from_utf8(self.data())
    }

    /// Returns a new `Vec<u8>` holding a copy of the payload.
    pub fn to_vec(&self) -> Vec<u8> {
        self.as_bytes().to_vec()
    }

    /// Copies the payload into `destination`, returning the number of bytes
    /// written. Returns an error when `destination` is smaller than the payload.
    pub fn copy_to(&self, destination: &mut [u8]) -> Result<usize, ConfigError> {
        let bytes = self.as_bytes();
        if destination.len() < bytes.len() {
            return Err(ConfigError::new(
                ConfigResult::InvalidArgument,
                libc::EINVAL,
            ));
        }
        destination[..bytes.len()].copy_from_slice(bytes);
        Ok(bytes.len())
    }

    /// Read a string property from the native message metadata.
    ///
    /// Returns `Ok(None)` when the property is absent.
    pub fn get_property(&self, name: &str) -> Result<Option<String>, ConfigError> {
        self.inner.get_property(name)
    }

    /// Return the native storage reference count for the message.
    ///
    /// This is a diagnostic helper only. It does not affect ownership or
    /// message lifetime.
    pub fn ref_count(&self) -> i32 {
        self.inner.ref_count()
    }

    /// Returns an independent copy of this message that owns its own payload.
    pub fn try_clone(&self) -> Result<Self, ConfigError> {
        Ok(Self {
            inner: self.inner.try_clone_box()?,
        })
    }
}

impl TryFrom<&[u8]> for Message {
    type Error = ConfigError;

    /// Creates a message holding an independent copy of the bytes.
    fn try_from(data: &[u8]) -> Result<Self, ConfigError> {
        crate::message_runtime::message_from_slice(data)
    }
}

impl TryFrom<Vec<u8>> for Message {
    type Error = ConfigError;

    /// Creates a message holding an independent copy of the bytes.
    fn try_from(v: Vec<u8>) -> Result<Self, ConfigError> {
        crate::message_runtime::message_from_slice(&v)
    }
}

impl TryFrom<&str> for Message {
    type Error = ConfigError;

    /// Creates a message holding the UTF-8 bytes of the string.
    fn try_from(value: &str) -> Result<Self, ConfigError> {
        crate::message_runtime::message_from_slice(value.as_bytes())
    }
}
