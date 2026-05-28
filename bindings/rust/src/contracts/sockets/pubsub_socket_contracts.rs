// SPDX-License-Identifier: MPL-2.0

use crate::socket_contracts::SocketRuntime;

macro_rules! define_pubsub_socket {
    ($name:ident, $doc:literal) => {
        #[doc = $doc]
        pub struct $name {
            pub(crate) inner: Box<dyn SocketRuntime>,
        }

        impl std::panic::UnwindSafe for $name {}
        impl std::panic::RefUnwindSafe for $name {}
    };
}

define_pubsub_socket!(PubSocket, "PUB socket, the topic publishing socket.");
define_pubsub_socket!(SubSocket, "SUB socket, the topic subscriber socket.");
define_pubsub_socket!(
    XPubSocket,
    "XPUB socket, the extended publisher socket with subscription events."
);
define_pubsub_socket!(XSubSocket, "XSUB socket, the extended subscriber socket.");
