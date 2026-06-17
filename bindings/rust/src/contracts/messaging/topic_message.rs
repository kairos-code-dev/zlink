// SPDX-License-Identifier: MPL-2.0

use crate::RecvError;
use crate::domain::recv_state_error;
use crate::error::CloseError;
use crate::message::{Message, RoutingId};

/// A received publish: its topic, source routing id, and message parts.
///
/// Owns its parts until the envelope is dropped or [`close`](TopicMessage::close)d.
pub struct TopicMessage {
    /// The source routing id, when the receive path provides one.
    pub routing_id: Option<RoutingId>,
    /// The topic the message was published under.
    pub topic: smol_str::SmolStr,
    /// The message parts, owned by this envelope.
    pub parts: Vec<Message>,
}

impl TopicMessage {
    /// Creates an empty reusable envelope; reuse it across `subscribe` calls.
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

    /// Returns `true` when this publish carries exactly one part.
    pub fn is_single_part(&self) -> bool {
        self.parts.len() == 1
    }

    /// Returns the topic this message was published under.
    pub fn topic(&self) -> &str {
        self.topic.as_str()
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
}
