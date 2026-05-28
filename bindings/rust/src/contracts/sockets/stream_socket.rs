// SPDX-License-Identifier: MPL-2.0

use crate::socket_contracts::SocketRuntime;

/// STREAM socket, the raw transport-level socket with routing ids.
pub struct StreamSocket {
    pub(crate) inner: Box<dyn SocketRuntime>,
}

impl std::panic::UnwindSafe for StreamSocket {}
impl std::panic::RefUnwindSafe for StreamSocket {}
