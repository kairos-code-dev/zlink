use std::any::Any;
use std::time::Duration;

use crate::actor_models::ActorRef;
use crate::actor_received::ActorReceived;
use crate::error::{ConfigError, RecvError, RequestError};
use crate::flags::RecvFlags;
use crate::spot_operations::{ActorJoinOp, ActorLeaveOp};
use crate::spot_operations::{Empty, SendOp};
use crate::spot_resource::Spot;

pub(crate) trait ActorRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn as_any_mut(&mut self) -> &mut dyn Any;
}

/// Owned local Actor facade.
pub struct Actor {
    pub(crate) inner: Box<dyn ActorRuntime>,
}

pub(crate) trait ActorPublicRuntime {
    fn actor_ref(&self) -> Result<ActorRef, ConfigError>;
    fn close_with_timeout(&mut self, timeout: Duration) -> Result<(), RequestError>;
    fn close(&mut self) -> Result<(), RequestError>;
    fn join(&self, spot: &Spot) -> ActorJoinOp<Empty>;
    fn leave(&self, spot: &Spot) -> ActorLeaveOp<Empty>;
    fn recv(&self, out: &mut ActorReceived, flags: RecvFlags) -> Result<bool, RecvError>;
    fn send_bound_session_msg(&self) -> SendOp<Empty>;
    fn close_bound_session(&self, timeout: Duration) -> Result<(), RequestError>;
}

impl Actor {
    pub fn actor_ref(&self) -> Result<ActorRef, ConfigError> {
        <Self as ActorPublicRuntime>::actor_ref(self)
    }

    pub fn close_with_timeout(&mut self, timeout: Duration) -> Result<(), RequestError> {
        <Self as ActorPublicRuntime>::close_with_timeout(self, timeout)
    }

    pub fn close(&mut self) -> Result<(), RequestError> {
        <Self as ActorPublicRuntime>::close(self)
    }

    /// Async user-Spot join (operation builder).
    pub fn join(&self, spot: &Spot) -> ActorJoinOp<Empty> {
        <Self as ActorPublicRuntime>::join(self, spot)
    }

    /// Async leave to the same node's Entry Spot (operation builder).
    pub fn leave(&self, spot: &Spot) -> ActorLeaveOp<Empty> {
        <Self as ActorPublicRuntime>::leave(self, spot)
    }

    pub fn recv(&self, out: &mut ActorReceived, flags: RecvFlags) -> Result<bool, RecvError> {
        <Self as ActorPublicRuntime>::recv(self, out, flags)
    }

    /// Actor-to-session relay (operation builder).
    pub fn send_bound_session_msg(&self) -> SendOp<Empty> {
        <Self as ActorPublicRuntime>::send_bound_session_msg(self)
    }

    pub fn close_bound_session(&self, timeout: Duration) -> Result<(), RequestError> {
        <Self as ActorPublicRuntime>::close_bound_session(self, timeout)
    }
}
