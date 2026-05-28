// SPDX-License-Identifier: MPL-2.0

use crate::RecvError;
use crate::domain::recv_state_error;
use crate::error::CloseError;
use crate::message::{Message, RoutingId};

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
