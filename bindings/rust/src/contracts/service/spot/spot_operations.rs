use std::any::Any;
use std::time::Duration;

use crate::actor_models::{ActorJoinEntrySpotResult, ActorJoinResult, ActorLookupResult};
use crate::error::{RequestError, SubmitError, ZlinkError};
use crate::flags::SendFlags;
use crate::message::Message;
use crate::{RoutingId, ffi};

/// Typestate marker: no message has been set yet.
pub struct Empty;

/// Typestate marker: at least one message part has been set.
pub struct Ready;

/// Typestate marker: flags have been set, only callback submit available.
pub struct CallbackReady;

pub(crate) trait SendOpRuntime: Any + Send {
    fn as_any_mut(&mut self) -> &mut dyn Any;
    fn into_any(self: Box<Self>) -> Box<dyn Any>;
}

pub struct SendOp<State> {
    pub(crate) inner: Box<dyn SendOpRuntime>,
    pub(crate) _state: std::marker::PhantomData<State>,
}

pub(crate) trait RequestOpRuntime: Any + Send {
    fn as_any_mut(&mut self) -> &mut dyn Any;
    fn into_any(self: Box<Self>) -> Box<dyn Any>;
}

pub struct RequestOp<State> {
    pub(crate) inner: Box<dyn RequestOpRuntime>,
    pub(crate) _state: std::marker::PhantomData<State>,
}

pub(crate) trait ReplyOpRuntime: Any + Send {
    fn as_any_mut(&mut self) -> &mut dyn Any;
    fn into_any(self: Box<Self>) -> Box<dyn Any>;
}

pub struct ReplyOp<State> {
    pub(crate) inner: Box<dyn ReplyOpRuntime>,
    pub(crate) _state: std::marker::PhantomData<State>,
}

/// Async Actor join builder. Payload accumulates via `.message(...)`.
pub struct ActorJoinOp<State> {
    pub(crate) node_handle: *mut std::ffi::c_void,
    pub(crate) spot_handle: *mut std::ffi::c_void,
    pub(crate) actor: ffi::zlink_actor_ref_t,
    pub(crate) dest_node_rid: RoutingId,
    pub(crate) dest_spot_rid: RoutingId,
    pub(crate) parts: Vec<Message>,
    pub(crate) flags: SendFlags,
    pub(crate) timeout: Duration,
    pub(crate) _state: std::marker::PhantomData<State>,
}

unsafe impl<S> Send for ActorJoinOp<S> {}

/// Async Actor Entry Spot join builder.
pub struct ActorJoinEntrySpotOp<State> {
    pub(crate) node_handle: *mut std::ffi::c_void,
    pub(crate) actor: ffi::zlink_actor_ref_t,
    pub(crate) dest_node_rid: RoutingId,
    pub(crate) timeout: Duration,
    pub(crate) _state: std::marker::PhantomData<State>,
}

unsafe impl<S> Send for ActorJoinEntrySpotOp<S> {}

/// Builder for replying to an Actor join admission request. 0-part submit is allowed.
pub struct ActorJoinReplyOp<State> {
    pub(crate) spot_handle: *mut std::ffi::c_void,
    pub(crate) info: ffi::zlink_actor_join_info_t,
    pub(crate) join_result_code: i32,
    pub(crate) parts: Vec<Message>,
    pub(crate) _state: std::marker::PhantomData<State>,
}

unsafe impl<S> Send for ActorJoinReplyOp<S> {}

/// Payload-less builder shared by leave / destroy / bind / unbind.
pub(crate) struct ActorReplyOpInner {
    pub(crate) handle: *mut std::ffi::c_void,
    pub(crate) kind: ActorReplyOpKind,
    pub(crate) timeout: Duration,
}

pub(crate) enum ActorReplyOpKind {
    Leave {
        actor: ffi::zlink_actor_ref_t,
        current_spot_rid: RoutingId,
    },
    Destroy {
        actor: ffi::zlink_actor_ref_t,
    },
    Bind {
        session_rid: RoutingId,
        actor: ffi::zlink_actor_ref_t,
    },
    Unbind {
        session_rid: RoutingId,
        actor_id: std::ffi::CString,
    },
}

/// Async Actor leave builder (payload-less).
pub struct ActorLeaveOp<State> {
    pub(crate) inner: ActorReplyOpInner,
    pub(crate) _state: std::marker::PhantomData<State>,
}

unsafe impl<S> Send for ActorLeaveOp<S> {}

/// Async Actor destroy builder (payload-less).
pub struct ActorDestroyOp<State> {
    pub(crate) inner: ActorReplyOpInner,
    pub(crate) _state: std::marker::PhantomData<State>,
}

unsafe impl<S> Send for ActorDestroyOp<S> {}

/// Async Actor bind builder (payload-less).
pub struct ActorBindOp<State> {
    pub(crate) inner: ActorReplyOpInner,
    pub(crate) _state: std::marker::PhantomData<State>,
}

unsafe impl<S> Send for ActorBindOp<S> {}

/// Async Actor unbind builder (payload-less).
pub struct ActorUnbindOp<State> {
    pub(crate) inner: ActorReplyOpInner,
    pub(crate) _state: std::marker::PhantomData<State>,
}

unsafe impl<S> Send for ActorUnbindOp<S> {}

/// Async remote Actor lookup builder (payload-less).
pub struct ActorLookupOp<State> {
    pub(crate) node_handle: *mut std::ffi::c_void,
    pub(crate) target_node_rid: RoutingId,
    pub(crate) actor_id: std::ffi::CString,
    pub(crate) timeout: Duration,
    pub(crate) _state: std::marker::PhantomData<State>,
}

unsafe impl<S> Send for ActorLookupOp<S> {}

pub(crate) trait SendOpEmptyRuntime {
    fn message(self, message: Message) -> SendOp<Ready>;
}

pub(crate) trait SendOpReadyRuntime {
    fn message(self, message: Message) -> Self;
    fn flags(self, flags: SendFlags) -> Self;
    fn submit(self) -> Result<bool, SubmitError>;
}

pub(crate) trait RequestOpEmptyRuntime {
    fn message(self, message: Message) -> RequestOp<Ready>;
}

pub(crate) trait RequestOpReadyRuntime {
    fn message(self, message: Message) -> Self;
    fn timeout(self, timeout: Duration) -> Self;
    fn flags(self, flags: SendFlags) -> RequestOp<CallbackReady>;
    async fn submit_async(self) -> Result<Vec<Message>, ZlinkError>;
    fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static;
}

pub(crate) trait RequestOpCallbackReadyRuntime {
    fn message(self, message: Message) -> Self;
    fn timeout(self, timeout: Duration) -> Self;
    fn flags(self, flags: SendFlags) -> Self;
    fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static;
}

pub(crate) trait ReplyOpEmptyRuntime {
    fn message(self, message: Message) -> ReplyOp<Ready>;
}

pub(crate) trait ReplyOpReadyRuntime {
    fn message(self, message: Message) -> Self;
    fn flags(self, flags: SendFlags) -> Self;
    fn submit(self) -> Result<(), SubmitError>;
}

pub(crate) trait ActorJoinEntrySpotOpRuntime {
    fn timeout(self, timeout: Duration) -> Self;
    async fn submit_async(self) -> Result<ActorJoinEntrySpotResult, ZlinkError>;
    fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(ActorJoinEntrySpotResult) + Send + 'static;
}

pub(crate) trait ActorJoinOpEmptyRuntime {
    fn message(self, message: Message) -> ActorJoinOp<Ready>;
}

pub(crate) trait ActorJoinOpReadyRuntime {
    fn message(self, message: Message) -> Self;
    fn timeout(self, timeout: Duration) -> Self;
    fn flags(self, flags: SendFlags) -> Self;
    async fn submit_async(self) -> Result<(ActorJoinResult, Vec<Message>), ZlinkError>;
    fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(ActorJoinResult, Vec<Message>) + Send + 'static;
}

pub(crate) trait ActorJoinReplyOpRuntime {
    fn message(self, message: Message) -> ActorJoinReplyOp<Empty>;
    fn submit(self) -> Result<(), SubmitError>;
}

pub(crate) trait ActorReplyOpRuntime {
    async fn submit_async(self) -> Result<Vec<Message>, ZlinkError>;
    fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static;
}

pub(crate) trait ActorReplyOpTimeoutRuntime: ActorReplyOpRuntime {
    fn timeout(self, timeout: Duration) -> Self;
}

pub(crate) trait ActorLookupOpRuntime {
    fn timeout(self, timeout: Duration) -> Self;
    async fn submit_async(self) -> Result<ActorLookupResult, ZlinkError>;
    fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(ActorLookupResult) + Send + 'static;
}

impl SendOp<Empty> {
    pub fn message(self, message: Message) -> SendOp<Ready> {
        <Self as SendOpEmptyRuntime>::message(self, message)
    }
}

impl SendOp<Ready> {
    pub fn message(self, message: Message) -> Self {
        <Self as SendOpReadyRuntime>::message(self, message)
    }

    pub fn flags(self, flags: SendFlags) -> Self {
        <Self as SendOpReadyRuntime>::flags(self, flags)
    }

    /// Submit the send operation.
    pub fn submit(self) -> Result<bool, SubmitError> {
        <Self as SendOpReadyRuntime>::submit(self)
    }
}

impl RequestOp<Empty> {
    pub fn message(self, message: Message) -> RequestOp<Ready> {
        <Self as RequestOpEmptyRuntime>::message(self, message)
    }
}

impl RequestOp<Ready> {
    pub fn message(self, message: Message) -> Self {
        <Self as RequestOpReadyRuntime>::message(self, message)
    }

    pub fn timeout(self, timeout: Duration) -> Self {
        <Self as RequestOpReadyRuntime>::timeout(self, timeout)
    }

    pub fn flags(self, flags: SendFlags) -> RequestOp<CallbackReady> {
        <Self as RequestOpReadyRuntime>::flags(self, flags)
    }

    pub async fn submit_async(self) -> Result<Vec<Message>, ZlinkError> {
        <Self as RequestOpReadyRuntime>::submit_async(self).await
    }

    pub fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        <Self as RequestOpReadyRuntime>::submit(self, callback)
    }
}

impl RequestOp<CallbackReady> {
    pub fn message(self, message: Message) -> Self {
        <Self as RequestOpCallbackReadyRuntime>::message(self, message)
    }

    pub fn timeout(self, timeout: Duration) -> Self {
        <Self as RequestOpCallbackReadyRuntime>::timeout(self, timeout)
    }

    pub fn flags(self, flags: SendFlags) -> Self {
        <Self as RequestOpCallbackReadyRuntime>::flags(self, flags)
    }

    pub fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        <Self as RequestOpCallbackReadyRuntime>::submit(self, callback)
    }
}

impl ReplyOp<Empty> {
    pub fn message(self, message: Message) -> ReplyOp<Ready> {
        <Self as ReplyOpEmptyRuntime>::message(self, message)
    }
}

impl ReplyOp<Ready> {
    pub fn message(self, message: Message) -> Self {
        <Self as ReplyOpReadyRuntime>::message(self, message)
    }

    pub fn flags(self, flags: SendFlags) -> Self {
        <Self as ReplyOpReadyRuntime>::flags(self, flags)
    }

    pub fn submit(self) -> Result<(), SubmitError> {
        <Self as ReplyOpReadyRuntime>::submit(self)
    }
}

impl ActorJoinEntrySpotOp<Empty> {
    pub fn timeout(self, timeout: Duration) -> Self {
        <Self as ActorJoinEntrySpotOpRuntime>::timeout(self, timeout)
    }

    pub async fn submit_async(self) -> Result<ActorJoinEntrySpotResult, ZlinkError> {
        <Self as ActorJoinEntrySpotOpRuntime>::submit_async(self).await
    }

    pub fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(ActorJoinEntrySpotResult) + Send + 'static,
    {
        <Self as ActorJoinEntrySpotOpRuntime>::submit(self, callback)
    }
}

impl ActorJoinOp<Empty> {
    pub fn message(self, message: Message) -> ActorJoinOp<Ready> {
        <Self as ActorJoinOpEmptyRuntime>::message(self, message)
    }
}

impl ActorJoinOp<Ready> {
    pub fn message(self, message: Message) -> Self {
        <Self as ActorJoinOpReadyRuntime>::message(self, message)
    }

    pub fn timeout(self, timeout: Duration) -> Self {
        <Self as ActorJoinOpReadyRuntime>::timeout(self, timeout)
    }

    pub fn flags(self, flags: SendFlags) -> Self {
        <Self as ActorJoinOpReadyRuntime>::flags(self, flags)
    }

    pub async fn submit_async(self) -> Result<(ActorJoinResult, Vec<Message>), ZlinkError> {
        <Self as ActorJoinOpReadyRuntime>::submit_async(self).await
    }

    pub fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(ActorJoinResult, Vec<Message>) + Send + 'static,
    {
        <Self as ActorJoinOpReadyRuntime>::submit(self, callback)
    }
}

impl ActorJoinReplyOp<Empty> {
    pub fn message(self, message: Message) -> ActorJoinReplyOp<Empty> {
        <Self as ActorJoinReplyOpRuntime>::message(self, message)
    }

    pub fn submit(self) -> Result<(), SubmitError> {
        <Self as ActorJoinReplyOpRuntime>::submit(self)
    }
}

macro_rules! impl_actor_reply_wrapper {
    ($ty:ident) => {
        impl $ty<Empty> {
            pub fn timeout(self, timeout: Duration) -> Self {
                <Self as ActorReplyOpTimeoutRuntime>::timeout(self, timeout)
            }

            pub async fn submit_async(self) -> Result<Vec<Message>, ZlinkError> {
                <Self as ActorReplyOpRuntime>::submit_async(self).await
            }

            pub fn submit<F>(self, callback: F) -> Result<(), SubmitError>
            where
                F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
            {
                <Self as ActorReplyOpRuntime>::submit(self, callback)
            }
        }
    };
}

impl_actor_reply_wrapper!(ActorLeaveOp);
impl_actor_reply_wrapper!(ActorDestroyOp);
impl_actor_reply_wrapper!(ActorBindOp);
impl_actor_reply_wrapper!(ActorUnbindOp);

impl ActorLookupOp<Empty> {
    pub fn timeout(self, timeout: Duration) -> Self {
        <Self as ActorLookupOpRuntime>::timeout(self, timeout)
    }

    pub async fn submit_async(self) -> Result<ActorLookupResult, ZlinkError> {
        <Self as ActorLookupOpRuntime>::submit_async(self).await
    }

    pub fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(ActorLookupResult) + Send + 'static,
    {
        <Self as ActorLookupOpRuntime>::submit(self, callback)
    }
}
