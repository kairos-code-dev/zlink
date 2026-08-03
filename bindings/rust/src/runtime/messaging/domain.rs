use std::ffi::c_void;

use crate::domain::Received;
use crate::message::{Message, RoutingId};
use crate::messaging_operations::{Empty, ReplyOp, SendOp};
use crate::runtime_bridge::{ReceivedReplyRuntime, ReceivedSendRuntime};

enum ReplyContext {
    Router {
        handle: *mut c_void,
        routing_id: RoutingId,
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
            } => crate::operations::router_reply_op(*handle, *routing_id, *request_seq),
        }
    }
}

enum SendContext {
    Router {
        handle: *mut c_void,
        routing_id: RoutingId,
    },
}

unsafe impl Send for SendContext {}

impl ReceivedSendRuntime for SendContext {
    fn send_op(&self) -> SendOp<Empty> {
        match self {
            SendContext::Router { handle, routing_id } => {
                crate::operations::socket_send_to_op(*handle, *routing_id)
            }
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
            request_seq: None,
            parts,
            reply_context: None,
            send_context: Some(Box::new(SendContext::Router { handle, routing_id })),
        }
    }

    pub(crate) fn set_router_send_context(&mut self, handle: *mut c_void, routing_id: RoutingId) {
        self.send_context = Some(Box::new(SendContext::Router { handle, routing_id }));
    }

    pub(crate) fn with_router_reply_context(
        handle: *mut c_void,
        routing_id: RoutingId,
        request_seq: u64,
        parts: Vec<Message>,
    ) -> Self {
        Self {
            routing_id: Some(routing_id),
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
