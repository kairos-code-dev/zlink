use crate::error::{CloseError, RecvError, RecvResult};
use crate::message::{Message, RoutingId};
use crate::messaging_operations::{Empty, ReplyOp, SendOp};
use crate::runtime_bridge::{ReceivedReplyContext, ReceivedSendContext};

/// A received message envelope: its routing metadata and message parts.
///
/// Owns its parts until the envelope is dropped or
/// [`close`](Received::close)d. Reuse one instance across `recv` calls to
/// avoid a per-receive allocation.
pub struct Received {
    /// The source routing id, when the receive path provides one.
    pub routing_id: Option<RoutingId>,
    /// The request sequence, present when this envelope can be replied to.
    pub request_seq: Option<u64>,
    /// The message parts, owned by this envelope.
    pub parts: Vec<Message>,
    pub(crate) reply_context: Option<Box<dyn ReceivedReplyContext>>,
    pub(crate) send_context: Option<Box<dyn ReceivedSendContext>>,
}

impl Default for Received {
    fn default() -> Self {
        Self::empty()
    }
}

impl Received {
    /// Create an empty `Received` for caller-provided storage.
    ///
    /// Hand the same instance to socket `recv` across calls to avoid the
    /// per-recv allocation; the binding overwrites the internal state on
    /// each successful receive.
    pub fn empty() -> Self {
        Self {
            routing_id: None,
            request_seq: None,
            parts: Vec::new(),
            reply_context: None,
            send_context: None,
        }
    }

    /// Replace `self` with the contents of `source`, dropping any state
    /// currently held first. After this call `source` is left empty.
    pub(crate) fn adopt_from(&mut self, source: Received) {
        self.routing_id = source.routing_id;
        self.request_seq = source.request_seq;
        self.parts = source.parts;
        self.reply_context = source.reply_context;
        self.send_context = source.send_context;
    }

    pub(crate) fn new(routing_id: Option<RoutingId>, parts: Vec<Message>) -> Self {
        Self {
            routing_id,
            request_seq: None,
            parts,
            reply_context: None,
            send_context: None,
        }
    }

    /// Returns `true` when the envelope carries exactly one part.
    pub fn is_single_part(&self) -> bool {
        self.parts.len() == 1
    }

    /// Returns the source routing id, when present.
    pub fn routing_id(&self) -> Option<&RoutingId> {
        self.routing_id.as_ref()
    }

    /// Returns the request sequence, present when this envelope can be replied
    /// to.
    pub fn request_seq(&self) -> Option<u64> {
        self.request_seq
    }

    /// Returns the message parts, owned by this envelope.
    pub fn parts(&self) -> &[Message] {
        &self.parts
    }

    /// Returns the first part without transferring ownership; errors when the
    /// envelope has no parts.
    pub fn first_part(&self) -> Result<&Message, RecvError> {
        self.parts.first().ok_or_else(recv_state_error)
    }

    /// Consumes the envelope and returns its only part, transferring ownership;
    /// errors unless it holds exactly one part.
    pub fn single_part(self) -> Result<Message, RecvError> {
        self.single_part_or_error()
    }

    /// Consumes the envelope and returns its only part, transferring ownership;
    /// errors unless it holds exactly one part.
    pub fn single_part_or_error(self) -> Result<Message, RecvError> {
        if self.parts.len() != 1 {
            return Err(recv_state_error());
        }
        Ok(self.parts.into_iter().next().expect("single part"))
    }

    /// Consumes the envelope and returns ownership of all its parts.
    pub fn into_parts(self) -> Vec<Message> {
        self.parts
    }

    /// Closes every part, releasing their payloads.
    pub fn close(mut self) -> Result<(), CloseError> {
        for part in &mut self.parts {
            part.close_now();
        }
        Ok(())
    }

    /// Begins a reply to this request: add parts on the returned builder, then
    /// submit. Parts are consumed on a successful submit (see [`SendOp`]). Only
    /// valid for replyable envelopes (those with a request sequence).
    pub fn reply(&self) -> ReplyOp<Empty> {
        crate::received_operations::received_reply(self)
    }

    /// Begins a send addressed to this envelope's source route: add parts, then
    /// submit. Parts are consumed on a successful submit (see [`SendOp`]).
    pub fn send(&self) -> SendOp<Empty> {
        crate::received_operations::received_send(self)
    }
}

pub(crate) fn recv_state_error() -> RecvError {
    RecvError::new(RecvResult::Busy, libc::EINVAL)
}
