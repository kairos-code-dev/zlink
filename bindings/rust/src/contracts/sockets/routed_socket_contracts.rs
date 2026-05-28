// SPDX-License-Identifier: MPL-2.0

use crate::socket_contracts::SocketRuntime;

/// ROUTER socket, the asynchronous request/reply server-side socket.
pub struct RouterSocket {
    pub(crate) inner: Box<dyn SocketRuntime>,
}

impl std::panic::UnwindSafe for RouterSocket {}
impl std::panic::RefUnwindSafe for RouterSocket {}
