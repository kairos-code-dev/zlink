use crate::error::ZlinkError;
use crate::message::{Message, RoutingId};

// ---------------------------------------------------------------------------
// SendResult
// ---------------------------------------------------------------------------

/// Explicit outcome of a non-blocking send or publish attempt.
///
/// The binding never collapses these into a plain `bool`. Each variant maps
/// directly to the core `zlink_send_result_t` enum so that callers can
/// distinguish backpressure from readiness issues without errno heuristics.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SendResult {
    /// The message was accepted by the transport layer.
    Sent,
    /// The peer or queue is full (HWM reached). Transient condition.
    Backpressured,
    /// The socket is not in a state to send (e.g. no peers yet).
    NotReady,
}

#[allow(dead_code)]
impl SendResult {
    pub(crate) fn from_raw(raw: crate::ffi::zlink_send_result_t) -> Self {
        match raw {
            crate::ffi::zlink_send_result_t::ZLINK_SEND_RESULT_SENT => Self::Sent,
            crate::ffi::zlink_send_result_t::ZLINK_SEND_RESULT_BACKPRESSURED => Self::Backpressured,
            crate::ffi::zlink_send_result_t::ZLINK_SEND_RESULT_NOT_READY => Self::NotReady,
        }
    }

    /// Returns `true` if the message was successfully enqueued.
    pub fn is_sent(&self) -> bool {
        *self == Self::Sent
    }
}

// ---------------------------------------------------------------------------
// Received – multipart receive result
// ---------------------------------------------------------------------------

/// The result of a direct `recv` call on a data-plane socket.
///
/// Bundles the sender routing-id with the received multipart payload.
/// Ownership of every `Message` in `parts` belongs to the caller; each part
/// must eventually be dropped (which calls `zlink_msg_close`).
pub struct Received {
    routing_id: RoutingId,
    parts: Vec<Message>,
    request_seq: Option<u64>,
}

impl Received {
    pub(crate) fn new(routing_id: RoutingId, parts: Vec<Message>) -> Self {
        Self {
            routing_id,
            parts,
            request_seq: None,
        }
    }

    pub(crate) fn with_request_seq(
        routing_id: RoutingId,
        parts: Vec<Message>,
        request_seq: u64,
    ) -> Self {
        Self {
            routing_id,
            parts,
            request_seq: Some(request_seq),
        }
    }

    /// Sender routing-id attached to this delivery.
    pub fn routing_id(&self) -> &RoutingId {
        &self.routing_id
    }

    /// Borrow the multipart payload.
    pub fn parts(&self) -> &[Message] {
        &self.parts
    }

    /// Consume the result and return the individual parts.
    pub fn into_parts(self) -> Vec<Message> {
        self.parts
    }

    /// Request sequence attached by the request-reply API when this delivery
    /// represents an incoming request on a `RequestRouter`.
    pub fn request_seq(&self) -> Option<u64> {
        self.request_seq
    }

    /// Consume the result and return the single part, or error if the
    /// delivery does not contain exactly one frame.
    pub fn single_part(self) -> Result<Message, ZlinkError> {
        if self.parts.len() != 1 {
            return Err(ZlinkError::state(format!(
                "expected 1 part, got {}",
                self.parts.len()
            )));
        }
        Ok(self.parts.into_iter().next().unwrap())
    }
}

// ---------------------------------------------------------------------------
// TopicMessage – pub/sub receive result
// ---------------------------------------------------------------------------

/// The result of a topic-aware `subscribe` (blocking receive) call.
///
/// Contains the topic, sender routing-id, and the multipart payload.
pub struct TopicMessage {
    routing_id: RoutingId,
    topic: String,
    parts: Vec<Message>,
}

impl TopicMessage {
    pub(crate) fn new(routing_id: RoutingId, topic: String, parts: Vec<Message>) -> Self {
        Self {
            routing_id,
            topic,
            parts,
        }
    }

    pub fn routing_id(&self) -> &RoutingId {
        &self.routing_id
    }

    pub fn topic(&self) -> &str {
        &self.topic
    }

    pub fn parts(&self) -> &[Message] {
        &self.parts
    }

    pub fn into_parts(self) -> Vec<Message> {
        self.parts
    }

    pub fn single_part(self) -> Result<Message, ZlinkError> {
        if self.parts.len() != 1 {
            return Err(ZlinkError::state(format!(
                "expected 1 part, got {}",
                self.parts.len()
            )));
        }
        Ok(self.parts.into_iter().next().unwrap())
    }
}

// ---------------------------------------------------------------------------
// SubscriptionEvent – XPUB subscription notification
// ---------------------------------------------------------------------------

/// A subscription or unsubscription event received on an XPUB socket.
pub struct SubscriptionEvent {
    routing_id: RoutingId,
    subscribed: bool,
    topic: String,
}

impl SubscriptionEvent {
    pub(crate) fn new(routing_id: RoutingId, subscribed: bool, topic: String) -> Self {
        Self {
            routing_id,
            subscribed,
            topic,
        }
    }

    pub fn routing_id(&self) -> &RoutingId {
        &self.routing_id
    }

    /// `true` if this is a subscribe event, `false` for unsubscribe.
    pub fn is_subscribed(&self) -> bool {
        self.subscribed
    }

    pub fn topic(&self) -> &str {
        &self.topic
    }
}
