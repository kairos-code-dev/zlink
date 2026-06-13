use std::time::Duration;

use crate::actor_models::{ActorJoinEntrySpotResult, ActorJoinResult, ActorLookupResult};
use crate::error::{RequestError, SubmitError};
use crate::flags::SendFlags;
use crate::message::Message;
use crate::runtime_bridge::{
    ActorJoinEntrySpotOpContract, ActorJoinEntrySpotOpStorage, ActorJoinOpEmptyContract,
    ActorJoinOpReadyContract, ActorJoinOpStorage, ActorJoinReplyOpContract,
    ActorJoinReplyOpStorage, ActorLookupOpContract, ActorLookupOpStorage, ActorReplyOpContract,
    ActorReplyOpStorage, ActorReplyOpTimeoutContract, ReplyOpEmptyContract, ReplyOpReadyContract,
    ReplyOpStorage, RequestOpCallbackReadyContract, RequestOpEmptyContract, RequestOpReadyContract,
    RequestOpStorage, SendOpEmptyContract, SendOpReadyContract, SendOpStorage,
};

/// Typestate marker: no message has been set yet.
pub struct Empty;

/// Typestate marker: at least one message part has been set.
pub struct Ready;

/// Typestate marker: flags have been set, only callback submit available.
pub struct CallbackReady;

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
    pub(crate) inner: Box<dyn SendOpStorage>,
    pub(crate) _state: std::marker::PhantomData<State>,
}

/// A request builder: add parts, then submit and await a reply. Parts are
/// consumed on a successful submit (see [`SendOp`]).
pub struct RequestOp<State> {
    pub(crate) inner: Box<dyn RequestOpStorage>,
    pub(crate) _state: std::marker::PhantomData<State>,
}

/// A reply builder: add parts, then submit. Parts are consumed on a successful
/// submit (see [`SendOp`]).
pub struct ReplyOp<State> {
    pub(crate) inner: Box<dyn ReplyOpStorage>,
    pub(crate) _state: std::marker::PhantomData<State>,
}

/// Async Actor join builder. Payload accumulates via `.message(...)`.
pub struct ActorJoinOp<State> {
    pub(crate) inner: Box<dyn ActorJoinOpStorage>,
    pub(crate) _state: std::marker::PhantomData<State>,
}

/// Async Actor Entry Spot join builder.
pub struct ActorJoinEntrySpotOp<State> {
    pub(crate) inner: Box<dyn ActorJoinEntrySpotOpStorage>,
    pub(crate) _state: std::marker::PhantomData<State>,
}

/// Builder for replying to an Actor join admission request. 0-part submit is allowed.
pub struct ActorJoinReplyOp<State> {
    pub(crate) inner: Box<dyn ActorJoinReplyOpStorage>,
    pub(crate) _state: std::marker::PhantomData<State>,
}

/// Async Actor leave builder (payload-less).
pub struct ActorLeaveOp<State> {
    pub(crate) inner: Box<dyn ActorReplyOpStorage>,
    pub(crate) _state: std::marker::PhantomData<State>,
}

/// Async Actor destroy builder (payload-less).
pub struct ActorDestroyOp<State> {
    pub(crate) inner: Box<dyn ActorReplyOpStorage>,
    pub(crate) _state: std::marker::PhantomData<State>,
}

/// Async Actor bind builder (payload-less).
pub struct ActorBindOp<State> {
    pub(crate) inner: Box<dyn ActorReplyOpStorage>,
    pub(crate) _state: std::marker::PhantomData<State>,
}

/// Async Actor unbind builder (payload-less).
pub struct ActorUnbindOp<State> {
    pub(crate) inner: Box<dyn ActorReplyOpStorage>,
    pub(crate) _state: std::marker::PhantomData<State>,
}

/// Async remote Actor lookup builder (payload-less).
pub struct ActorLookupOp<State> {
    pub(crate) inner: Box<dyn ActorLookupOpStorage>,
    pub(crate) _state: std::marker::PhantomData<State>,
}

impl SendOp<Empty> {
    /// Adds the first message part, transitioning the builder to the ready
    /// state. The part is consumed on a successful submit (see [`SendOp`]).
    pub fn message(self, message: Message) -> SendOp<Ready> {
        <Self as SendOpEmptyContract>::message(self, message)
    }
}

impl SendOp<Ready> {
    /// Adds another message part. The part is consumed on a successful submit
    /// (see [`SendOp`]).
    pub fn message(self, message: Message) -> Self {
        <Self as SendOpReadyContract>::message(self, message)
    }

    /// Sets the send flags applied at submit time.
    pub fn flags(self, flags: SendFlags) -> Self {
        <Self as SendOpReadyContract>::flags(self, flags)
    }

    /// Submits the accumulated parts.
    ///
    /// Returns `Ok(true)` when the parts were queued, and `Ok(false)` only when
    /// [`SendFlags::DONT_WAIT`] is set and the send would have blocked
    /// (back-pressure). Other failures return an error.
    pub fn submit(self) -> Result<bool, SubmitError> {
        <Self as SendOpReadyContract>::submit(self)
    }
}

impl RequestOp<Empty> {
    /// Adds the first request part, transitioning the builder to the ready
    /// state. The part is consumed on a successful submit (see [`SendOp`]).
    pub fn message(self, message: Message) -> RequestOp<Ready> {
        <Self as RequestOpEmptyContract>::message(self, message)
    }
}

impl RequestOp<Ready> {
    /// Adds another request part. The part is consumed on a successful submit
    /// (see [`SendOp`]).
    pub fn message(self, message: Message) -> Self {
        <Self as RequestOpReadyContract>::message(self, message)
    }

    /// Sets how long the request waits for a reply before timing out.
    pub fn timeout(self, timeout: Duration) -> Self {
        <Self as RequestOpReadyContract>::timeout(self, timeout)
    }

    /// Sets the send flags applied at submit time.
    pub fn flags(self, flags: SendFlags) -> RequestOp<CallbackReady> {
        <Self as RequestOpReadyContract>::flags(self, flags)
    }

    /// Submits the request; the reply (or error) is delivered later to
    /// `callback`, which owns the reply parts.
    pub fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        <Self as RequestOpReadyContract>::submit(self, callback)
    }
}

impl RequestOp<CallbackReady> {
    /// Adds another request part. The part is consumed on a successful submit
    /// (see [`SendOp`]).
    pub fn message(self, message: Message) -> Self {
        <Self as RequestOpCallbackReadyContract>::message(self, message)
    }

    /// Sets how long the request waits for a reply before timing out.
    pub fn timeout(self, timeout: Duration) -> Self {
        <Self as RequestOpCallbackReadyContract>::timeout(self, timeout)
    }

    /// Sets the send flags applied at submit time.
    pub fn flags(self, flags: SendFlags) -> Self {
        <Self as RequestOpCallbackReadyContract>::flags(self, flags)
    }

    /// Submits the request; the reply (or error) is delivered later to
    /// `callback`, which owns the reply parts.
    pub fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        <Self as RequestOpCallbackReadyContract>::submit(self, callback)
    }
}

impl ReplyOp<Empty> {
    /// Adds the first reply part, transitioning the builder to the ready state.
    /// The part is consumed on a successful submit (see [`SendOp`]).
    pub fn message(self, message: Message) -> ReplyOp<Ready> {
        <Self as ReplyOpEmptyContract>::message(self, message)
    }
}

impl ReplyOp<Ready> {
    /// Adds another reply part. The part is consumed on a successful submit
    /// (see [`SendOp`]).
    pub fn message(self, message: Message) -> Self {
        <Self as ReplyOpReadyContract>::message(self, message)
    }

    /// Sets the send flags applied at submit time.
    pub fn flags(self, flags: SendFlags) -> Self {
        <Self as ReplyOpReadyContract>::flags(self, flags)
    }

    /// Submits the accumulated reply parts.
    pub fn submit(self) -> Result<(), SubmitError> {
        <Self as ReplyOpReadyContract>::submit(self)
    }
}

impl ActorJoinEntrySpotOp<Empty> {
    /// Sets how long the operation waits for completion before timing out.
    pub fn timeout(self, timeout: Duration) -> Self {
        <Self as ActorJoinEntrySpotOpContract>::timeout(self, timeout)
    }

    /// Submits the operation; the result is delivered later to `callback`.
    pub fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(ActorJoinEntrySpotResult) + Send + 'static,
    {
        <Self as ActorJoinEntrySpotOpContract>::submit(self, callback)
    }
}

impl ActorJoinOp<Empty> {
    /// Adds the first message part, transitioning the builder to the ready
    /// state. The part is consumed on a successful submit (see [`SendOp`]).
    pub fn message(self, message: Message) -> ActorJoinOp<Ready> {
        <Self as ActorJoinOpEmptyContract>::message(self, message)
    }
}

impl ActorJoinOp<Ready> {
    /// Adds another message part. The part is consumed on a successful submit
    /// (see [`SendOp`]).
    pub fn message(self, message: Message) -> Self {
        <Self as ActorJoinOpReadyContract>::message(self, message)
    }

    /// Sets how long the operation waits for completion before timing out.
    pub fn timeout(self, timeout: Duration) -> Self {
        <Self as ActorJoinOpReadyContract>::timeout(self, timeout)
    }

    /// Sets the send flags applied at submit time.
    pub fn flags(self, flags: SendFlags) -> Self {
        <Self as ActorJoinOpReadyContract>::flags(self, flags)
    }

    /// Submits the join; the result and reply parts are delivered later to
    /// `callback`, which owns the parts.
    pub fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(ActorJoinResult, Vec<Message>) + Send + 'static,
    {
        <Self as ActorJoinOpReadyContract>::submit(self, callback)
    }
}

impl ActorJoinReplyOp<Empty> {
    /// Adds a message part to the reply. The part is consumed on a successful
    /// submit (see [`SendOp`]).
    pub fn message(self, message: Message) -> ActorJoinReplyOp<Empty> {
        <Self as ActorJoinReplyOpContract>::message(self, message)
    }

    /// Submits the actor-join reply; a zero-part reply is allowed.
    pub fn submit(self) -> Result<(), SubmitError> {
        <Self as ActorJoinReplyOpContract>::submit(self)
    }
}

macro_rules! impl_actor_reply_wrapper {
    ($ty:ident) => {
        impl $ty<Empty> {
            /// Sets how long the operation waits for completion before timing
            /// out.
            pub fn timeout(self, timeout: Duration) -> Self {
                <Self as ActorReplyOpTimeoutContract>::timeout(self, timeout)
            }

            /// Submits the operation; the reply (or error) is delivered later to
            /// `callback`, which owns the reply parts.
            pub fn submit<F>(self, callback: F) -> Result<(), SubmitError>
            where
                F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
            {
                <Self as ActorReplyOpContract>::submit(self, callback)
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
        <Self as ActorLookupOpContract>::timeout(self, timeout)
    }

    /// Submits the lookup; the result is delivered later to `callback`.
    pub fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(ActorLookupResult) + Send + 'static,
    {
        <Self as ActorLookupOpContract>::submit(self, callback)
    }
}
