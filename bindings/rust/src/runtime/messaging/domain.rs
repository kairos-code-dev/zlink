use std::ffi::c_void;

use crate::domain::Received;
use crate::runtime_bridge::{ReceivedReplyRuntime, ReceivedSendRuntime};
use crate::message::{Message, RoutingId};
use crate::spot_operations::Empty;
use crate::spot_operations::{ReplyOp, SendOp};

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

unsafe impl Send for ReplyContext {}

impl ReceivedReplyRuntime for ReplyContext {
    fn reply_op(&self) -> ReplyOp<Empty> {
        match self {
            ReplyContext::Router {
                handle,
                routing_id,
                request_seq,
            } => crate::service::router_reply_op(*handle, *routing_id, *request_seq),
            ReplyContext::Spot {
                handle,
                node_rid,
                spot_rid,
                request_seq,
            } => crate::service::spot_reply_to_spot_op(*handle, *node_rid, *spot_rid, *request_seq),
            ReplyContext::SpotRouter {
                handle,
                peer_rid,
                request_seq,
            } => crate::service::spot_reply_to_router_op(*handle, *peer_rid, *request_seq),
        }
    }
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

unsafe impl Send for SendContext {}

impl ReceivedSendRuntime for SendContext {
    fn send_op(&self) -> SendOp<Empty> {
        match self {
            SendContext::Router { handle, routing_id } => {
                crate::service::socket_send_to_op(*handle, *routing_id)
            }
            SendContext::RouterSpot {
                handle,
                node_rid,
                spot_rid,
            } => crate::service::router_send_to_spot_op(*handle, *node_rid, *spot_rid),
            SendContext::Spot {
                handle,
                node_rid,
                spot_rid,
            } => crate::service::spot_send_to_spot_op(*handle, *node_rid, *spot_rid),
        }
    }
}

impl Received {
    pub(crate) fn with_router_send_context(
        handle: *mut c_void,
        routing_id: RoutingId,
        parts: Vec<Message>,
    ) -> Self {
        Self {
            routing_id: Some(routing_id),
            spot_rid: None,
            request_seq: None,
            parts,
            reply_context: None,
            send_context: Some(Box::new(SendContext::Router { handle, routing_id })),
        }
    }

    pub(crate) fn set_router_send_context(&mut self, handle: *mut c_void, routing_id: RoutingId) {
        self.send_context = Some(Box::new(SendContext::Router { handle, routing_id }));
    }

    pub(crate) fn with_router_spot_send_context(
        handle: *mut c_void,
        node_rid: RoutingId,
        spot_rid: RoutingId,
        parts: Vec<Message>,
    ) -> Self {
        Self {
            routing_id: Some(node_rid),
            spot_rid: Some(spot_rid),
            request_seq: None,
            parts,
            reply_context: None,
            send_context: Some(Box::new(SendContext::RouterSpot {
                handle,
                node_rid,
                spot_rid,
            })),
        }
    }

    pub(crate) fn with_router_reply_context(
        handle: *mut c_void,
        routing_id: RoutingId,
        request_seq: u64,
        parts: Vec<Message>,
    ) -> Self {
        Self {
            routing_id: Some(routing_id),
            spot_rid: None,
            request_seq: Some(request_seq),
            parts,
            reply_context: Some(Box::new(ReplyContext::Router {
                handle,
                routing_id,
                request_seq,
            })),
            send_context: Some(Box::new(SendContext::Router { handle, routing_id })),
        }
    }

    pub(crate) fn with_spot_reply_context(
        handle: *mut c_void,
        node_rid: RoutingId,
        spot_rid: RoutingId,
        request_seq: u64,
        parts: Vec<Message>,
    ) -> Self {
        Self {
            routing_id: Some(node_rid),
            spot_rid: Some(spot_rid),
            request_seq: Some(request_seq),
            parts,
            reply_context: Some(Box::new(ReplyContext::Spot {
                handle,
                node_rid,
                spot_rid,
                request_seq,
            })),
            send_context: Some(Box::new(SendContext::RouterSpot {
                handle,
                node_rid,
                spot_rid,
            })),
        }
    }

    pub(crate) fn with_spot_router_reply_context(
        handle: *mut c_void,
        peer_rid: RoutingId,
        request_seq: u64,
        parts: Vec<Message>,
    ) -> Self {
        Self {
            routing_id: Some(peer_rid),
            spot_rid: None,
            request_seq: Some(request_seq),
            parts,
            reply_context: Some(Box::new(ReplyContext::SpotRouter {
                handle,
                peer_rid,
                request_seq,
            })),
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
            routing_id: Some(node_rid),
            spot_rid: Some(spot_rid),
            request_seq: None,
            parts,
            reply_context: None,
            send_context: Some(Box::new(SendContext::Spot {
                handle,
                node_rid,
                spot_rid,
            })),
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
            routing_id: Some(node_rid),
            spot_rid: Some(spot_rid),
            request_seq: Some(request_seq),
            parts,
            reply_context: Some(Box::new(ReplyContext::Spot {
                handle,
                node_rid,
                spot_rid,
                request_seq,
            })),
            send_context: Some(Box::new(SendContext::Spot {
                handle,
                node_rid,
                spot_rid,
            })),
        }
    }
}

pub(crate) fn received_reply(received: &Received) -> ReplyOp<Empty> {
    received
        .reply_context
        .as_ref()
        .expect("missing reply context")
        .reply_op()
}

pub(crate) fn received_send(received: &Received) -> SendOp<Empty> {
    received
        .send_context
        .as_ref()
        .expect("missing send context")
        .send_op()
}
