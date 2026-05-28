use crate::error::ConfigError;
use crate::results::ConfigResult;

/// A validated routing identifier.
///
/// The 255 byte bound maps to the C routing-id contract, but this public value
/// does not expose the native mirror type.
#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq, Hash)]
pub struct RoutingId {
    pub(crate) size: u8,
    pub(crate) data: [u8; 255],
}

impl RoutingId {
    pub const MAX_LEN: usize = 255;

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
        let mut raw = Self {
            size: data.len() as u8,
            data: [0u8; 255],
        };
        raw.data[..data.len()].copy_from_slice(data);
        raw
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
            return Err(validation_error());
        }
        let mut data = Vec::with_capacity(value.len() / 2);
        let bytes = value.as_bytes();
        for pair in bytes.chunks_exact(2) {
            let high = hex_nibble(pair[0]).ok_or_else(validation_error)?;
            let low = hex_nibble(pair[1]).ok_or_else(validation_error)?;
            data.push((high << 4) | low);
        }
        Self::new(&data)
    }

    pub fn try_from_string(value: &str) -> Result<Self, ConfigError> {
        Self::new(value.as_bytes())
    }

    pub(crate) fn new(data: &[u8]) -> Result<Self, ConfigError> {
        if data.is_empty() || data.len() > Self::MAX_LEN {
            return Err(validation_error());
        }
        Ok(Self::from_bytes(data))
    }

    pub fn as_bytes(&self) -> &[u8] {
        &self.data[..self.size as usize]
    }

    pub(crate) fn data(&self) -> &[u8] {
        self.as_bytes()
    }

    pub fn size(&self) -> usize {
        self.size as usize
    }

    pub(crate) fn len(&self) -> usize {
        self.size()
    }

    pub fn is_empty(&self) -> bool {
        self.size == 0
    }

    pub fn to_hex(&self) -> String {
        let mut out = String::with_capacity(self.size() * 2);
        for byte in self.as_bytes() {
            use std::fmt::Write as _;
            let _ = write!(&mut out, "{byte:02x}");
        }
        out
    }
}

fn validation_error() -> ConfigError {
    ConfigError::new(ConfigResult::InvalidArgument, libc::EINVAL)
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
