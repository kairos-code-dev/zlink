use crate::ffi;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SendFlags(u32);

impl SendFlags {
    pub const NONE: Self = Self(0);
    pub const DONT_WAIT: Self = Self(ffi::ZLINK_DONTWAIT as u32);

    pub const fn bits(self) -> u32 {
        self.0
    }
}

impl Default for SendFlags {
    fn default() -> Self {
        Self::NONE
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RecvFlags(u32);

impl RecvFlags {
    pub const NONE: Self = Self(0);
    pub const DONT_WAIT: Self = Self(ffi::ZLINK_DONTWAIT as u32);

    pub const fn bits(self) -> u32 {
        self.0
    }
}

impl Default for RecvFlags {
    fn default() -> Self {
        Self::NONE
    }
}
