use std::any::Any;

pub(crate) trait PairSocketRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn as_any_mut(&mut self) -> &mut dyn Any;
}

/// PAIR socket, a bidirectional one-to-one messaging socket.
pub struct PairSocket {
    pub(crate) inner: Box<dyn PairSocketRuntime>,
}

impl std::panic::UnwindSafe for PairSocket {}
impl std::panic::RefUnwindSafe for PairSocket {}

pub(crate) trait SocketRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn as_any_mut(&mut self) -> &mut dyn Any;
}

macro_rules! define_socket {
    ($name:ident, $doc:literal) => {
        #[doc = $doc]
        pub struct $name {
            pub(crate) inner: Box<dyn SocketRuntime>,
        }

        impl std::panic::UnwindSafe for $name {}
        impl std::panic::RefUnwindSafe for $name {}
    };
}

define_socket!(
    DealerSocket,
    "DEALER socket, the asynchronous request/reply client-side socket."
);
define_socket!(PubSocket, "PUB socket, the topic publishing socket.");
define_socket!(SubSocket, "SUB socket, the topic subscriber socket.");
define_socket!(
    RouterSocket,
    "ROUTER socket, the asynchronous request/reply server-side socket."
);
define_socket!(
    StreamSocket,
    "STREAM socket, the raw transport-level socket with routing ids."
);
define_socket!(
    XPubSocket,
    "XPUB socket, the extended publisher socket with subscription events."
);
define_socket!(XSubSocket, "XSUB socket, the extended subscriber socket.");
