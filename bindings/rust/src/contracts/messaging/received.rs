use std::any::Any;

use crate::error::{CloseError, RecvError, RecvResult};
use crate::message::{Message, RoutingId};

pub(crate) trait ReceivedReplyRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
}

pub(crate) trait ReceivedSendRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
}

pub struct Received {
    pub routing_id: Option<RoutingId>,
    pub spot_rid: Option<RoutingId>,
    pub request_seq: Option<u64>,
    pub parts: Vec<Message>,
    pub(crate) reply_context: Option<Box<dyn ReceivedReplyRuntime>>,
    pub(crate) send_context: Option<Box<dyn ReceivedSendRuntime>>,
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

    pub fn into_parts(self) -> Vec<Message> {
        self.parts
    }

    pub fn close(mut self) -> Result<(), CloseError> {
        for part in &mut self.parts {
            part.close_now();
        }
        Ok(())
    }
}

pub struct TopicMessage {
    pub routing_id: Option<RoutingId>,
    pub topic: smol_str::SmolStr,
    pub parts: Vec<Message>,
}

impl TopicMessage {
    pub fn empty() -> Self {
        Self {
            routing_id: None,
            topic: smol_str::SmolStr::default(),
            parts: Vec::new(),
        }
    }

    pub(crate) fn new(
        routing_id: Option<RoutingId>,
        topic: smol_str::SmolStr,
        parts: Vec<Message>,
    ) -> Self {
        Self {
            routing_id,
            topic,
            parts,
        }
    }

    pub(crate) fn adopt_from(&mut self, mut source: TopicMessage) {
        for part in &mut self.parts {
            part.close_now();
        }
        self.routing_id = source.routing_id.take();
        self.topic = std::mem::take(&mut source.topic);
        self.parts = std::mem::take(&mut source.parts);
    }

    pub fn is_single_part(&self) -> bool {
        self.parts.len() == 1
    }

    pub fn topic(&self) -> &str {
        self.topic.as_str()
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
        Ok(())
    }
}

fn recv_state_error() -> RecvError {
    RecvError::new(RecvResult::Busy, libc::EINVAL)
}
