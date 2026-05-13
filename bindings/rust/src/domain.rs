use std::ffi::c_void;

use crate::error::{CloseError, RecvError, check_close_rc, recv_state_error};
use crate::message::{Message, RoutingId};
use crate::service::{Empty, ReplyOp, SendOp};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SendResult {
    Sent,
    Backpressured,
    NotReady,
}

impl SendResult {
    pub fn is_sent(&self) -> bool {
        matches!(self, Self::Sent)
    }
}

#[allow(clippy::large_enum_variant)]
enum ReplyContext {
    Router {
        handle: *mut c_void,
        routing_id: RoutingId,
        request_seq: u64,
    },
    Spot {
        handle: *mut c_void,
        node_rid: RoutingId,
        spot_rid: RoutingId,
        request_seq: u64,
    },
    SpotRouter {
        handle: *mut c_void,
        peer_rid: RoutingId,
        request_seq: u64,
    },
}

#[allow(clippy::large_enum_variant)]
enum SendContext {
    Router {
        handle: *mut c_void,
        routing_id: RoutingId,
    },
    RouterSpot {
        handle: *mut c_void,
        node_rid: RoutingId,
        spot_rid: RoutingId,
    },
    Spot {
        handle: *mut c_void,
        node_rid: RoutingId,
        spot_rid: RoutingId,
    },
}

pub struct Received {
    pub routing_id: Option<RoutingId>,
    pub spot_rid: Option<RoutingId>,
    pub request_seq: Option<u64>,
    pub parts: Vec<Message>,
    reply_context: Option<ReplyContext>,
    send_context: Option<SendContext>,
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
    /// each successful receive via [`Received::adopt_from`].
    ///
    /// See `doc/spec/bindings/README.md` "Canonical Recv: Caller-Provided
    /// Storage".
    pub fn empty() -> Self {
        Self {
            routing_id: None,
            spot_rid: None,
            request_seq: None,
            parts: Vec::new(),
            reply_context: None,
            send_context: None,
        }
    }

    /// Replace `self` with the contents of `source`, dropping any state
    /// currently held first. After this call `source` is left empty.
    pub(crate) fn adopt_from(&mut self, source: Received) {
        // Existing parts are dropped here, which closes any owned Messages
        // via Message::Drop semantics.
        self.routing_id = source.routing_id;
        self.spot_rid = source.spot_rid;
        self.request_seq = source.request_seq;
        self.parts = source.parts;
        self.reply_context = source.reply_context;
        self.send_context = source.send_context;
    }

    pub(crate) fn new(routing_id: Option<RoutingId>, parts: Vec<Message>) -> Self {
        Self {
            routing_id,
            spot_rid: None,
            request_seq: None,
            parts,
            reply_context: None,
            send_context: None,
        }
    }

    pub(crate) fn with_router_send_context(
        handle: *mut c_void,
        routing_id: RoutingId,
        parts: Vec<Message>,
    ) -> Self {
        Self {
            routing_id: Some(routing_id.clone()),
            spot_rid: None,
            request_seq: None,
            parts,
            reply_context: None,
            send_context: Some(SendContext::Router { handle, routing_id }),
        }
    }

    pub(crate) fn set_router_send_context(&mut self, handle: *mut c_void, routing_id: RoutingId) {
        self.send_context = Some(SendContext::Router { handle, routing_id });
    }

    pub(crate) fn with_router_spot_send_context(
        handle: *mut c_void,
        node_rid: RoutingId,
        spot_rid: RoutingId,
        parts: Vec<Message>,
    ) -> Self {
        Self {
            routing_id: Some(node_rid.clone()),
            spot_rid: Some(spot_rid.clone()),
            request_seq: None,
            parts,
            reply_context: None,
            send_context: Some(SendContext::RouterSpot {
                handle,
                node_rid,
                spot_rid,
            }),
        }
    }

    pub(crate) fn with_router_reply_context(
        handle: *mut c_void,
        routing_id: RoutingId,
        request_seq: u64,
        parts: Vec<Message>,
    ) -> Self {
        let send_routing_id = routing_id.clone();
        Self {
            routing_id: Some(routing_id.clone()),
            spot_rid: None,
            request_seq: Some(request_seq),
            parts,
            reply_context: Some(ReplyContext::Router {
                handle,
                routing_id,
                request_seq,
            }),
            send_context: Some(SendContext::Router {
                handle,
                routing_id: send_routing_id,
            }),
        }
    }

    pub(crate) fn with_spot_reply_context(
        handle: *mut c_void,
        node_rid: RoutingId,
        spot_rid: RoutingId,
        request_seq: u64,
        parts: Vec<Message>,
    ) -> Self {
        let send_node_rid = node_rid.clone();
        let send_spot_rid = spot_rid.clone();
        Self {
            routing_id: Some(node_rid.clone()),
            spot_rid: Some(spot_rid.clone()),
            request_seq: Some(request_seq),
            parts,
            reply_context: Some(ReplyContext::Spot {
                handle,
                node_rid,
                spot_rid,
                request_seq,
            }),
            send_context: Some(SendContext::RouterSpot {
                handle,
                node_rid: send_node_rid,
                spot_rid: send_spot_rid,
            }),
        }
    }

    pub(crate) fn with_spot_router_reply_context(
        handle: *mut c_void,
        peer_rid: RoutingId,
        request_seq: u64,
        parts: Vec<Message>,
    ) -> Self {
        Self {
            routing_id: Some(peer_rid.clone()),
            spot_rid: None,
            request_seq: Some(request_seq),
            parts,
            reply_context: Some(ReplyContext::SpotRouter {
                handle,
                peer_rid,
                request_seq,
            }),
            send_context: None,
        }
    }

    pub(crate) fn with_spot_send_context(
        handle: *mut c_void,
        node_rid: RoutingId,
        spot_rid: RoutingId,
        parts: Vec<Message>,
    ) -> Self {
        Self {
            routing_id: Some(node_rid.clone()),
            spot_rid: Some(spot_rid.clone()),
            request_seq: None,
            parts,
            reply_context: None,
            send_context: Some(SendContext::Spot {
                handle,
                node_rid,
                spot_rid,
            }),
        }
    }

    pub(crate) fn with_spot_reply_and_send_context(
        handle: *mut c_void,
        node_rid: RoutingId,
        spot_rid: RoutingId,
        request_seq: u64,
        parts: Vec<Message>,
    ) -> Self {
        Self {
            routing_id: Some(node_rid.clone()),
            spot_rid: Some(spot_rid.clone()),
            request_seq: Some(request_seq),
            parts,
            reply_context: Some(ReplyContext::Spot {
                handle,
                node_rid: node_rid.clone(),
                spot_rid: spot_rid.clone(),
                request_seq,
            }),
            send_context: Some(SendContext::Spot {
                handle,
                node_rid,
                spot_rid,
            }),
        }
    }

    pub fn is_single_part(&self) -> bool {
        self.parts.len() == 1
    }

    pub fn routing_id(&self) -> Option<&RoutingId> {
        self.routing_id.as_ref()
    }

    pub fn request_seq(&self) -> Option<u64> {
        self.request_seq
    }

    pub fn parts(&self) -> &[Message] {
        &self.parts
    }

    pub fn first_part(&self) -> Result<&Message, RecvError> {
        self.parts.first().ok_or_else(recv_state_error)
    }

    pub fn single_part(self) -> Result<Message, RecvError> {
        self.single_part_or_error()
    }

    pub fn single_part_or_error(self) -> Result<Message, RecvError> {
        if self.parts.len() != 1 {
            return Err(recv_state_error());
        }
        Ok(self.parts.into_iter().next().expect("single part"))
    }

    pub fn reply(&self) -> ReplyOp<Empty> {
        match self.reply_context.as_ref().expect("missing reply context") {
            ReplyContext::Router {
                handle,
                routing_id,
                request_seq,
            } => crate::service::router_reply_op(*handle, routing_id.clone(), *request_seq),
            ReplyContext::Spot {
                handle,
                node_rid,
                spot_rid,
                request_seq,
            } => crate::service::spot_reply_to_spot_op(
                *handle,
                node_rid.clone(),
                spot_rid.clone(),
                *request_seq,
            ),
            ReplyContext::SpotRouter {
                handle,
                peer_rid,
                request_seq,
            } => crate::service::spot_reply_to_router_op(*handle, peer_rid.clone(), *request_seq),
        }
    }

    pub fn send(&self) -> SendOp<Empty> {
        match self.send_context.as_ref().expect("missing send context") {
            SendContext::Router { handle, routing_id } => {
                crate::service::socket_send_to_op(*handle, routing_id.clone())
            }
            SendContext::RouterSpot {
                handle,
                node_rid,
                spot_rid,
            } => {
                crate::service::router_send_to_spot_op(*handle, node_rid.clone(), spot_rid.clone())
            }
            SendContext::Spot {
                handle,
                node_rid,
                spot_rid,
            } => crate::service::spot_send_to_spot_op(*handle, node_rid.clone(), spot_rid.clone()),
        }
    }

    pub fn into_parts(self) -> Vec<Message> {
        self.parts
    }

    pub fn close(mut self) -> Result<(), CloseError> {
        for part in &mut self.parts {
            part.close_now();
        }
        check_close_rc(0)
    }
}

pub struct TopicMessage {
    pub routing_id: Option<RoutingId>,
    pub topic: String,
    pub parts: Vec<Message>,
}

impl TopicMessage {
    pub(crate) fn new(routing_id: Option<RoutingId>, topic: String, parts: Vec<Message>) -> Self {
        Self {
            routing_id,
            topic,
            parts,
        }
    }

    pub fn is_single_part(&self) -> bool {
        self.parts.len() == 1
    }

    pub fn topic(&self) -> &str {
        &self.topic
    }

    pub fn parts(&self) -> &[Message] {
        &self.parts
    }

    pub fn first_part(&self) -> Result<&Message, RecvError> {
        self.parts.first().ok_or_else(recv_state_error)
    }

    pub fn single_part(self) -> Result<Message, RecvError> {
        self.single_part_or_error()
    }

    pub fn single_part_or_error(self) -> Result<Message, RecvError> {
        if self.parts.len() != 1 {
            return Err(recv_state_error());
        }
        Ok(self.parts.into_iter().next().expect("single part"))
    }

    pub fn into_parts(self) -> Vec<Message> {
        self.parts
    }

    pub fn close(mut self) -> Result<(), CloseError> {
        for part in &mut self.parts {
            part.close_now();
        }
        check_close_rc(0)
    }
}

pub struct SubscriptionEvent {
    pub routing_id: Option<RoutingId>,
    pub topic: String,
    pub subscribed: bool,
}

impl SubscriptionEvent {
    pub(crate) fn new(routing_id: Option<RoutingId>, subscribed: bool, topic: String) -> Self {
        Self {
            routing_id,
            topic,
            subscribed,
        }
    }
}
