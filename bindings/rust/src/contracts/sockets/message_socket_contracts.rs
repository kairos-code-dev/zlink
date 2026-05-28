// SPDX-License-Identifier: MPL-2.0

use crate::socket_contracts::{PairSocketRuntime, SocketRuntime};

/// PAIR socket, a bidirectional one-to-one messaging socket.
pub struct PairSocket {
    pub(crate) inner: Box<dyn PairSocketRuntime>,
}

impl std::panic::UnwindSafe for PairSocket {}
impl std::panic::RefUnwindSafe for PairSocket {}

/// DEALER socket, the asynchronous request/reply client-side socket.
pub struct DealerSocket {
    pub(crate) inner: Box<dyn SocketRuntime>,
}

impl std::panic::UnwindSafe for DealerSocket {}
impl std::panic::RefUnwindSafe for DealerSocket {}
