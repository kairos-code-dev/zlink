use std::any::Any;
use std::time::Duration;

use crate::actor_models::{ActorJoinEntrySpotResult, ActorJoinResult, ActorLookupResult};
use crate::error::{RequestError, SubmitError, ZlinkError};
use crate::flags::SendFlags;
use crate::message::Message;

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

/// A multipart send builder: add parts with [`message`](SendOp::message), then
/// [`submit`](SendOp::submit).
///
/// Submitting consumes the added [`Message`] parts: on a successful submit each
/// part's payload is moved into the transport and the managed value is left
/// empty, so a part must not be reused after a successful submit. The request,
/// reply, and actor builders in this module share this same consume-on-submit
/// ownership model.
///
/// The `State` type parameter is a typestate ([`Empty`] or [`Ready`]) that
/// statically tracks whether at least one part has been added.
pub struct SendOp<State> {
    pub(crate) inner: Box<dyn SendOpRuntime>,
    pub(crate) _state: std::marker::PhantomData<State>,
}

pub(crate) trait RequestOpRuntime: Any + Send {
    fn as_any_mut(&mut self) -> &mut dyn Any;
    fn into_any(self: Box<Self>) -> Box<dyn Any>;
}

/// A request builder: add parts, then submit and await a reply. Parts are
/// consumed on a successful submit (see [`SendOp`]).
pub struct RequestOp<State> {
    pub(crate) inner: Box<dyn RequestOpRuntime>,
    pub(crate) _state: std::marker::PhantomData<State>,
}

pub(crate) trait ReplyOpRuntime: Any + Send {
    fn as_any_mut(&mut self) -> &mut dyn Any;
    fn into_any(self: Box<Self>) -> Box<dyn Any>;
}

/// A reply builder: add parts, then submit. Parts are consumed on a successful
/// submit (see [`SendOp`]).
pub struct ReplyOp<State> {
    pub(crate) inner: Box<dyn ReplyOpRuntime>,
    pub(crate) _state: std::marker::PhantomData<State>,
}

/// Async Actor join builder. Payload accumulates via `.message(...)`.
pub struct ActorJoinOp<State> {
    pub(crate) inner: Box<dyn ActorJoinOpInnerRuntime>,
    pub(crate) _state: std::marker::PhantomData<State>,
}

/// Async Actor Entry Spot join builder.
pub struct ActorJoinEntrySpotOp<State> {
    pub(crate) inner: Box<dyn ActorJoinEntrySpotOpInnerRuntime>,
    pub(crate) _state: std::marker::PhantomData<State>,
}

/// Builder for replying to an Actor join admission request. 0-part submit is allowed.
pub struct ActorJoinReplyOp<State> {
    pub(crate) inner: Box<dyn ActorJoinReplyOpInnerRuntime>,
    pub(crate) _state: std::marker::PhantomData<State>,
}

pub(crate) trait ActorJoinOpInnerRuntime: Any + Send {
    fn as_any_mut(&mut self) -> &mut dyn Any;
    fn into_any(self: Box<Self>) -> Box<dyn Any>;
}

pub(crate) trait ActorJoinEntrySpotOpInnerRuntime: Any + Send {
    fn into_any(self: Box<Self>) -> Box<dyn Any>;
}

pub(crate) trait ActorJoinReplyOpInnerRuntime: Any + Send {
    fn into_any(self: Box<Self>) -> Box<dyn Any>;
}

pub(crate) trait ActorReplyOpInnerRuntime: Any + Send {
    fn into_any(self: Box<Self>) -> Box<dyn Any>;
}

pub(crate) trait ActorLookupOpInnerRuntime: Any + Send {
    fn into_any(self: Box<Self>) -> Box<dyn Any>;
}

/// Async Actor leave builder (payload-less).
pub struct ActorLeaveOp<State> {
    pub(crate) inner: Box<dyn ActorReplyOpInnerRuntime>,
    pub(crate) _state: std::marker::PhantomData<State>,
}

/// Async Actor destroy builder (payload-less).
pub struct ActorDestroyOp<State> {
    pub(crate) inner: Box<dyn ActorReplyOpInnerRuntime>,
    pub(crate) _state: std::marker::PhantomData<State>,
}

/// Async Actor bind builder (payload-less).
pub struct ActorBindOp<State> {
    pub(crate) inner: Box<dyn ActorReplyOpInnerRuntime>,
    pub(crate) _state: std::marker::PhantomData<State>,
}

/// Async Actor unbind builder (payload-less).
pub struct ActorUnbindOp<State> {
    pub(crate) inner: Box<dyn ActorReplyOpInnerRuntime>,
    pub(crate) _state: std::marker::PhantomData<State>,
}

/// Async remote Actor lookup builder (payload-less).
pub struct ActorLookupOp<State> {
    pub(crate) inner: Box<dyn ActorLookupOpInnerRuntime>,
    pub(crate) _state: std::marker::PhantomData<State>,
}

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
    /// Adds the first message part, transitioning the builder to the ready
    /// state. The part is consumed on a successful submit (see [`SendOp`]).
    pub fn message(self, message: Message) -> SendOp<Ready> {
        <Self as SendOpEmptyRuntime>::message(self, message)
    }
}

impl SendOp<Ready> {
    /// Adds another message part. The part is consumed on a successful submit
    /// (see [`SendOp`]).
    pub fn message(self, message: Message) -> Self {
        <Self as SendOpReadyRuntime>::message(self, message)
    }

    /// Sets the send flags applied at submit time.
    pub fn flags(self, flags: SendFlags) -> Self {
        <Self as SendOpReadyRuntime>::flags(self, flags)
    }

    /// Submits the accumulated parts.
    ///
    /// Returns `Ok(true)` when the parts were queued, and `Ok(false)` only when
    /// [`SendFlags::DONT_WAIT`] is set and the send would have blocked
    /// (back-pressure). Other failures return an error.
    pub fn submit(self) -> Result<bool, SubmitError> {
        <Self as SendOpReadyRuntime>::submit(self)
    }
}

impl RequestOp<Empty> {
    /// Adds the first request part, transitioning the builder to the ready
    /// state. The part is consumed on a successful submit (see [`SendOp`]).
    pub fn message(self, message: Message) -> RequestOp<Ready> {
        <Self as RequestOpEmptyRuntime>::message(self, message)
    }
}

impl RequestOp<Ready> {
    /// Adds another request part. The part is consumed on a successful submit
    /// (see [`SendOp`]).
    pub fn message(self, message: Message) -> Self {
        <Self as RequestOpReadyRuntime>::message(self, message)
    }

    /// Sets how long the request waits for a reply before timing out.
    pub fn timeout(self, timeout: Duration) -> Self {
        <Self as RequestOpReadyRuntime>::timeout(self, timeout)
    }

    /// Sets the send flags and narrows the builder to callback submission;
    /// [`submit_async`](Self::submit_async) is no longer reachable afterward.
    pub fn flags(self, flags: SendFlags) -> RequestOp<CallbackReady> {
        <Self as RequestOpReadyRuntime>::flags(self, flags)
    }

    /// Submits the request and asynchronously returns the reply parts, which the
    /// caller owns.
    pub async fn submit_async(self) -> Result<Vec<Message>, ZlinkError> {
        <Self as RequestOpReadyRuntime>::submit_async(self).await
    }

    /// Submits the request; the reply (or error) is delivered later to
    /// `callback`, which owns the reply parts.
    pub fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        <Self as RequestOpReadyRuntime>::submit(self, callback)
    }
}

impl RequestOp<CallbackReady> {
    /// Adds another request part. The part is consumed on a successful submit
    /// (see [`SendOp`]).
    pub fn message(self, message: Message) -> Self {
        <Self as RequestOpCallbackReadyRuntime>::message(self, message)
    }

    /// Sets how long the request waits for a reply before timing out.
    pub fn timeout(self, timeout: Duration) -> Self {
        <Self as RequestOpCallbackReadyRuntime>::timeout(self, timeout)
    }

    /// Sets the send flags applied at submit time.
    pub fn flags(self, flags: SendFlags) -> Self {
        <Self as RequestOpCallbackReadyRuntime>::flags(self, flags)
    }

    /// Submits the request; the reply (or error) is delivered later to
    /// `callback`, which owns the reply parts.
    pub fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        <Self as RequestOpCallbackReadyRuntime>::submit(self, callback)
    }
}

impl ReplyOp<Empty> {
    /// Adds the first reply part, transitioning the builder to the ready state.
    /// The part is consumed on a successful submit (see [`SendOp`]).
    pub fn message(self, message: Message) -> ReplyOp<Ready> {
        <Self as ReplyOpEmptyRuntime>::message(self, message)
    }
}

impl ReplyOp<Ready> {
    /// Adds another reply part. The part is consumed on a successful submit
    /// (see [`SendOp`]).
    pub fn message(self, message: Message) -> Self {
        <Self as ReplyOpReadyRuntime>::message(self, message)
    }

    /// Sets the send flags applied at submit time.
    pub fn flags(self, flags: SendFlags) -> Self {
        <Self as ReplyOpReadyRuntime>::flags(self, flags)
    }

    /// Submits the accumulated reply parts.
    pub fn submit(self) -> Result<(), SubmitError> {
        <Self as ReplyOpReadyRuntime>::submit(self)
    }
}

impl ActorJoinEntrySpotOp<Empty> {
    /// Sets how long the operation waits for completion before timing out.
    pub fn timeout(self, timeout: Duration) -> Self {
        <Self as ActorJoinEntrySpotOpRuntime>::timeout(self, timeout)
    }

    /// Submits the operation and asynchronously returns its result.
    pub async fn submit_async(self) -> Result<ActorJoinEntrySpotResult, ZlinkError> {
        <Self as ActorJoinEntrySpotOpRuntime>::submit_async(self).await
    }

    /// Submits the operation; the result is delivered later to `callback`.
    pub fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(ActorJoinEntrySpotResult) + Send + 'static,
    {
        <Self as ActorJoinEntrySpotOpRuntime>::submit(self, callback)
    }
}

impl ActorJoinOp<Empty> {
    /// Adds the first message part, transitioning the builder to the ready
    /// state. The part is consumed on a successful submit (see [`SendOp`]).
    pub fn message(self, message: Message) -> ActorJoinOp<Ready> {
        <Self as ActorJoinOpEmptyRuntime>::message(self, message)
    }
}

impl ActorJoinOp<Ready> {
    /// Adds another message part. The part is consumed on a successful submit
    /// (see [`SendOp`]).
    pub fn message(self, message: Message) -> Self {
        <Self as ActorJoinOpReadyRuntime>::message(self, message)
    }

    /// Sets how long the operation waits for completion before timing out.
    pub fn timeout(self, timeout: Duration) -> Self {
        <Self as ActorJoinOpReadyRuntime>::timeout(self, timeout)
    }

    /// Sets the send flags applied at submit time.
    pub fn flags(self, flags: SendFlags) -> Self {
        <Self as ActorJoinOpReadyRuntime>::flags(self, flags)
    }

    /// Submits the join and asynchronously returns its result and reply parts,
    /// which the caller owns.
    pub async fn submit_async(self) -> Result<(ActorJoinResult, Vec<Message>), ZlinkError> {
        <Self as ActorJoinOpReadyRuntime>::submit_async(self).await
    }

    /// Submits the join; the result and reply parts are delivered later to
    /// `callback`, which owns the parts.
    pub fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(ActorJoinResult, Vec<Message>) + Send + 'static,
    {
        <Self as ActorJoinOpReadyRuntime>::submit(self, callback)
    }
}

impl ActorJoinReplyOp<Empty> {
    /// Adds a message part to the reply. The part is consumed on a successful
    /// submit (see [`SendOp`]).
    pub fn message(self, message: Message) -> ActorJoinReplyOp<Empty> {
        <Self as ActorJoinReplyOpRuntime>::message(self, message)
    }

    /// Submits the actor-join reply; a zero-part reply is allowed.
    pub fn submit(self) -> Result<(), SubmitError> {
        <Self as ActorJoinReplyOpRuntime>::submit(self)
    }
}

macro_rules! impl_actor_reply_wrapper {
    ($ty:ident) => {
        impl $ty<Empty> {
            /// Sets how long the operation waits for completion before timing
            /// out.
            pub fn timeout(self, timeout: Duration) -> Self {
                <Self as ActorReplyOpTimeoutRuntime>::timeout(self, timeout)
            }

            /// Submits the operation and asynchronously returns the reply parts,
            /// which the caller owns.
            pub async fn submit_async(self) -> Result<Vec<Message>, ZlinkError> {
                <Self as ActorReplyOpRuntime>::submit_async(self).await
            }

            /// Submits the operation; the reply (or error) is delivered later to
            /// `callback`, which owns the reply parts.
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
    /// Sets how long the lookup waits for completion before timing out.
    pub fn timeout(self, timeout: Duration) -> Self {
        <Self as ActorLookupOpRuntime>::timeout(self, timeout)
    }

    /// Submits the lookup and asynchronously returns its result.
    pub async fn submit_async(self) -> Result<ActorLookupResult, ZlinkError> {
        <Self as ActorLookupOpRuntime>::submit_async(self).await
    }

    /// Submits the lookup; the result is delivered later to `callback`.
    pub fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(ActorLookupResult) + Send + 'static,
    {
        <Self as ActorLookupOpRuntime>::submit(self, callback)
    }
}
