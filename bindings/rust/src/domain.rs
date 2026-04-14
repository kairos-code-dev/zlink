use std::ffi::c_void;

use crate::error::{
    CloseError, RecvError, SubmitError, check_close_rc, check_submit_rc, recv_state_error,
    submit_state_error,
};
use crate::ffi;
use crate::flags::SendFlags;
use crate::message::{IntoMultipart, Message, RoutingId};
use crate::socket::prepare_send_parts;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SendResult {
    Sent,
    Backpressured,
    NotReady,
}

impl SendResult {
    pub(crate) fn from_raw(raw: crate::ffi::zlink_send_result_t) -> Self {
        match raw {
            crate::ffi::zlink_send_result_t::ZLINK_SEND_RESULT_SENT => Self::Sent,
            crate::ffi::zlink_send_result_t::ZLINK_SEND_RESULT_BACKPRESSURED => Self::Backpressured,
            crate::ffi::zlink_send_result_t::ZLINK_SEND_RESULT_NOT_READY => Self::NotReady,
        }
    }

    pub fn is_sent(&self) -> bool {
        matches!(self, Self::Sent)
    }
}

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
}

impl ReplyContext {
    fn reply_with_flags(&self, parts: Vec<Message>, _flags: SendFlags) -> Result<(), SubmitError> {
        let mut parts = parts.into_multipart();
        let mut native = prepare_send_parts(&mut parts).map_err(|_| submit_state_error())?;
        let rc = match self {
            Self::Router {
                handle,
                routing_id,
                request_seq,
            } => unsafe {
                ffi::zlink_router_reply(
                    *handle,
                    routing_id.as_raw(),
                    *request_seq,
                    native.as_mut_ptr(),
                    native.len(),
                )
            },
            Self::Spot {
                handle,
                node_rid,
                spot_rid,
                request_seq,
            } => unsafe {
                ffi::zlink_spot_reply_spot(
                    *handle,
                    node_rid.as_raw(),
                    spot_rid.as_raw(),
                    *request_seq,
                    native.as_mut_ptr(),
                    native.len(),
                )
            },
        };
        check_submit_rc(rc)
    }
}

pub struct Received {
    pub routing_id: Option<RoutingId>,
    pub spot_rid: Option<RoutingId>,
    pub request_seq: Option<u64>,
    pub parts: Vec<Message>,
    reply_context: Option<ReplyContext>,
}

impl Received {
    pub(crate) fn new(routing_id: Option<RoutingId>, parts: Vec<Message>) -> Self {
        Self {
            routing_id,
            spot_rid: None,
            request_seq: None,
            parts,
            reply_context: None,
        }
    }

    pub(crate) fn with_request_seq(
        routing_id: Option<RoutingId>,
        parts: Vec<Message>,
        request_seq: u64,
    ) -> Self {
        Self {
            routing_id,
            spot_rid: None,
            request_seq: Some(request_seq),
            parts,
            reply_context: None,
        }
    }

    pub(crate) fn with_router_reply_context(
        handle: *mut c_void,
        routing_id: RoutingId,
        request_seq: u64,
        parts: Vec<Message>,
    ) -> Self {
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

    pub fn reply(&self, parts: Vec<Message>) -> Result<(), SubmitError> {
        self.reply_with_flags(parts, SendFlags::NONE)
    }

    pub fn reply_with_flags(
        &self,
        parts: Vec<Message>,
        flags: SendFlags,
    ) -> Result<(), SubmitError> {
        self.reply_context
            .as_ref()
            .ok_or_else(submit_state_error)?
            .reply_with_flags(parts, flags)
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
