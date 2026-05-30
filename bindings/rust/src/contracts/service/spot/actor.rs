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

/// A handle to a local actor: join/leave spots, receive its messages, and send
/// to its bound session. The caller owns it and releases it on drop.
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
    /// Returns this actor's reference.
    pub fn actor_ref(&self) -> Result<ActorRef, ConfigError> {
        <Self as ActorPublicRuntime>::actor_ref(self)
    }

    /// Closes the actor, waiting up to `timeout` for it to drain.
    pub fn close_with_timeout(&mut self, timeout: Duration) -> Result<(), RequestError> {
        <Self as ActorPublicRuntime>::close_with_timeout(self, timeout)
    }

    /// Closes the actor.
    pub fn close(&mut self) -> Result<(), RequestError> {
        <Self as ActorPublicRuntime>::close(self)
    }

    /// Begins joining the actor to `spot`; submit the returned operation to
    /// apply it.
    pub fn join(&self, spot: &Spot) -> ActorJoinOp<Empty> {
        <Self as ActorPublicRuntime>::join(self, spot)
    }

    /// Begins leaving `spot`; submit the returned operation to apply it.
    pub fn leave(&self, spot: &Spot) -> ActorLeaveOp<Empty> {
        <Self as ActorPublicRuntime>::leave(self, spot)
    }

    /// Receives the next message for this actor into caller-provided `out`
    /// storage. Returns `Ok(true)` on success and `Ok(false)` when
    /// [`RecvFlags::DONT_WAIT`] is set and none is available.
    pub fn recv(&self, out: &mut ActorReceived, flags: RecvFlags) -> Result<bool, RecvError> {
        <Self as ActorPublicRuntime>::recv(self, out, flags)
    }

    /// Begins a send to the actor's bound session; parts are consumed on a
    /// successful submit (see [`SendOp`]).
    pub fn send_bound_session_msg(&self) -> SendOp<Empty> {
        <Self as ActorPublicRuntime>::send_bound_session_msg(self)
    }

    /// Closes the actor's bound session, waiting up to `timeout` for it to
    /// drain.
    pub fn close_bound_session(&self, timeout: Duration) -> Result<(), RequestError> {
        <Self as ActorPublicRuntime>::close_bound_session(self, timeout)
    }
}
