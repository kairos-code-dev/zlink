use std::any::Any;

pub(crate) trait PairSocketRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn as_any_mut(&mut self) -> &mut dyn Any;
}

pub(crate) trait SocketRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn as_any_mut(&mut self) -> &mut dyn Any;
}

pub use crate::message_socket_contracts::{DealerSocket, PairSocket};
pub use crate::pubsub_socket_contracts::{PubSocket, SubSocket, XPubSocket, XSubSocket};
pub use crate::routed_socket_contracts::RouterSocket;
pub use crate::stream_socket_contract::StreamSocket;
